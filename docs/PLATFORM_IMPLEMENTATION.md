# 플랫폼 파일·인코딩 계층과 Windows GUI 참조 기준선

작성: 2026-11-18, Claude Opus 5.5 (platform-files / Windows-reference 담당).
이 문서는 **구현·관측 결과**와 **남은 통합 작업**을 구분한다. 여기의 테스트 통과는
플랫폼 계층의 동작 근거이며, 원본 게임 AX 실행·신규 게임·저장/로드 검증이 아니다.

| 구분 | 의미 |
|---|---|
| compile | 빌드만 확인 |
| observed | 공식 HSP 3.4 x86(Windows) 또는 네이티브 빌드에서 실제 실행해 값을 확인 |
| runtime (게임) | 원본 2.335 AX를 이 계층으로 실행 — **아직 하지 않음** |

## 1. 소유 파일

| 경로 | 내용 |
|---|---|
| `src/platform/encoding.{h,cpp}` | CP932↔UTF-8 엄격 변환, NFC 정규화, Win32 wide 변환 |
| `src/platform/game_files.{h,cpp}` | 원본 불변 + 사용자 root overlay 파일시스템, tombstone, 외부 grant, 세이브 가져오기, SHA-256 |
| `src/platform/game_files_hsp.{h,cpp}` | HSP 코어 파일 이벤트 어댑터(코어 수정 없음) |
| `src/cmake/ElonaPlatform.cmake` | `elona_platform`, `elona_platform_hsp` target |
| `tests/platform/*` | 단위 테스트, 실제 VM E2E 호스트·fixture·runner(독립 CMake 프로젝트) |
| `scripts/reference/platform_*`, `gui_*`, `GuiFixtureProbe.cs` | Windows 참조 실행 도구 |

루트 `CMakeLists.txt`는 소유하지 않는다. 통합 측은 `include(src/cmake/ElonaPlatform.cmake)` 후
`target_link_libraries(<host> PRIVATE elona_platform elona_platform_hsp)`만 추가한다.

## 2. CP932 ↔ UTF-8 (`encoding.h`)

- API: `cp932_to_utf8`, `utf8_to_cp932` → `EncodingResult{error, offset}`. 잘못된 바이트·매핑 없음은
  실패와 입력 바이트 offset을 반환하며 `?`로 치환하지 않는다. NUL은 데이터로 보존한다.
- Windows 백엔드: Win32 `MultiByteToWideChar(932, MB_ERR_INVALID_CHARS)` /
  `WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS)`(문자 단위로 정확한 offset 보고).
- macOS 백엔드: `iconv("CP932")` + Windows 전용 단일 바이트 5개 보정
  (0x80↔U+0080, U+F8F0..U+F8F3→A0/FD/FE/FF). NFC는 CoreFoundation `CFStringNormalize`.
- **CoreFoundation `kCFStringEncodingDOSJapanese`는 텍스트 변환에 사용하지 않는다.**

### 근거: Windows 전체 표와 전수 비교 (observed)

`scripts/reference/platform_cp932_table.py`가 home/windows에서 Win32 API로 표를 덤프했다
(Windows 11 Pro 10.0.26200, 시스템 ACP 949). 첫 실행 `platform-cp932-01`은 C# `char[]` P/Invoke가
ANSI로 마샬링된 **무효 데이터**라 폐기했고, `ushort[]`로 고친 `platform-cp932-02`를 사용한다.

| 파일 | 항목 | SHA-256 |
|---|---|---|
| `cp932-decode.tsv` | 1바이트 256 + 리드 0x80–0xFF × trail 0x00–0xFF | `ee7cc690525f0d310ffa99f4055ddd2e597f0509d9056fd9b973d5aa1e2420e8` |
| `cp932-encode.tsv` | BMP 63,488 (서로게이트 제외) | `a81c57374475ddf51427608cefeaf959838ad8283dc96e743648f669a7946ad7` |

| 백엔드 (macOS 27.2 arm64) | decode 차이 | encode 차이 |
|---|---:|---:|
| iconv CP932 (보정 전) | 256 (0x80 단독과 0x80xx 쌍) | 5 (U+0080, U+F8F0–F8F3) |
| CoreFoundation DOSJapanese | 516 | 385 (NEC 선정 IBM 확장 우선 ED/EE행, U+00A2/U+2014 등 best-fit 매핑) |
| **최종 구현** | **0** | **0** (96,512 항목) |

이 비교는 `tests/platform/test_encoding.cpp`의 full-table 검사로 고정했다(`-DELONA_CP932_TABLE_DIR=`).
Windows x64에서는 같은 검사가 Win32 백엔드 자체를 검증한다(아래 6절).

주의: 표는 Win32의 cp932 표이며 시스템 ACP와 무관하다. Linux glibc iconv는 검증하지 않았다(대상 아님).

## 3. 파일시스템 정책 (`game_files.h`)

정적 근거(보정 소스 `work/decompiled/2.335-reference/start.hsp`): 게임은 줄 160의
`exedir = dirinfo(1) + "\\"` 뒤 **모든 경로를 `exedir + "..."` 절대형**으로 만든다.
`exedir + "\\graphic\\"` 같은 이중 구분자 56+47곳, 최초 실행 시 `original\` → 루트/`user\` `bcopy`
(줄 23689–23709), `dirlist` mode 0/1/5, `noteload "scene1.hsp"`(DPM 내장, 줄 597978)가 있다.

- **가상 루트**: 기본 `C:\ElonaPlus`. 호스트의 `dirinfo(0)`/`dirinfo(1)`이 이 문자열을 반환해야 한다.
- **경로 해석**: `\`와 `/`는 1바이트 위치에서만 구분자이며 CP932 trail 0x5C(예: `表`=95 5C)는 데이터다.
  연속 구분자 축약, `.` 무시, `..` 처리 후 루트 이탈은 `outside_root`. 다른 드라이브/UNC/선행 `\`는 거부.
  각 component의 끝 점·공백은 Win32와 같이 제거한다(`f1.bin.` = `f1.bin`, x86 관측). 제어 바이트,
  `<>:"|`, 와일드카드(목록 패턴 마지막 component 제외), 장치명(CON, COM1 …), 잘린 CP932,
  0xA0 같은 무효 CP932, 예약 메타 폴더 `.elonaplus`는 거부.
- **읽기 순서**: DPM 내장 멤버(디렉터리 없는 bare 이름만, 예: `scene1.hsp`) → 사용자 root →
  (tombstone이면 not_found) → 원본 root. ASCII 대소문자 무시 fallback이 있고 대소문자 구분 볼륨에서
  여러 후보가 있으면 `ambiguous_name`으로 실패한다. macOS NFD 파일명은 NFC로 비교한다.
- **쓰기**: 사용자 root에만, 쓰기 허용 접두어 `save\ user\ tmp\ map\ config.txt error.txt server.txt`
  (정적 스캔 근거: `save\`·`tmp\` 세이브, `user\` NPC/그래픽/음악, `map\` 맵 편집기 줄 232990,
  `server.txt` 온라인 경로 줄 204620, `error.txt` 줄 24208). 그 밖은 경로를 포함한 `not_writable`로 실패한다.
  원본에만 있는 부모 디렉터리(`user\music` 등)는 원본 철자로 사용자 root에 materialize한다.
  부모가 논리적으로 없으면 원본 HSP처럼 실패한다. 쓰기는 같은 폴더 임시 파일 + rename(Windows
  `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`, POSIX는 fsync 후 rename)이며 바이트를 재인코딩하지 않는다.
- **bsave offset**: copy-on-write 후 패치, EOF 너머는 0으로 채움(x86 관측과 일치).
- **delete**: 사용자 사본은 실제 삭제, 원본에 같은 파일이 있으면 `user_root/.elonaplus/deleted.txt`에
  tombstone(UTF-8 논리 경로) 기록 → 재시작 후에도 숨김. 다시 쓰면 tombstone 해제. **원본 파일은 삭제하지 않는다.**
- **mkdir**: 한 단계만, 부모는 논리적으로 존재해야 하며 이미 있으면(어느 root든) 실패.
- **dirlist**: 두 root 병합, ASCII 대소문자 무시 중복 제거(사용자 우선), tombstone 제외, 모드 비트는
  `supio_win.cpp dirlist`와 동일(bit0 디렉터리, bit1 숨김/시스템, bit2 반전). 정렬은 NTFS와 같은
  ASCII 대문자 UTF-16 순서 근사(비ASCII 이름의 NTFS 전체 upcase 표 순서는 근사). CP932로 표현할 수 없는
  파일명은 건너뛰고 개수를 보고한다. Windows 8.3 단축명 매칭은 재현하지 않는다.
- **symlink**: 두 root 내부의 symlink는 읽기·쓰기 모두 거부한다.
- **외부 파일(`_fdialog`)**: `refstr`이 원본에서 임의 경로였던 곳(줄 304944 등)은 플랫폼 파일 선택기가
  고른 파일을 `grant_external_file()`로 등록해 `C:\ElonaPlus\@external\<n>\<이름>` 가상 경로로만 접근한다.
- **스트리밍 쓰기(zOpen)**: `begin_stream_write()`가 정책 검사·부모 materialize 후 native 경로를 주고,
  스트림을 닫은 뒤 `end_stream_write()`가 tombstone을 해제한다.
- **세이브 가져오기**: `import_save(fs, 원본/save/<player>, slot)` — 원본은 읽기만, symlink·특수파일·
  CP932 불가 이름·깊은 중첩 거부, 같은 이름(대소문자 무시) 슬롯 덮어쓰기 거부, 임시 폴더에 복사 →
  파일별 바이트 비교 → rename. 파일별 size/SHA-256 보고. 원본 세이브 바이트·int32 배열은 그대로다.

`resolve_write()`는 정책 검사만 하며 부모 materialize/tombstone 해제를 하지 않는다. 실제 쓰기는
`write_file`/`copy_file`/`make_directory`/`begin_stream_write`를 사용한다.

## 4. HSP 파일 명령 연결 (`game_files_hsp.h`) — 코어 수정 없음

OpenHSP `hsp3code.cpp`의 `code_event()`는 `call_eventfunc(HSPEVENT_ENABLE_FILE, …)`로
**type id ≥ `HSP3_TYPE_USER`(18)** 이고 `option & HSPEVENT_ENABLE_FILE`인 타입의 `eventfunc`를 먼저
호출하고, 0이 아닌 반환이면 기본 fopen 경로로 내려가지 않는다. `TYPE_EXTCMD`(9)는 호출 대상이 아니다.

```cpp
// hsp->Reset() 이후, AX의 #regcmd 플러그인 타입(z.hpi/water.hpi, 고정 id 18+) 등록 "후"
HSP3TYPEINFO* info = code_gettypeinfo(-1);
elona::platform::hsp::install_file_events(info, &hsp->hspctx, files.get());
```

| 이벤트 | 처리 |
|---|---|
| FNAME | `sbStrCopy(&ctx->fnbuffer, name)` (코어와 동일), 1 반환 |
| FREAD (bload/noteload) | `read_file(offset,size)`, `strsize`=읽은 바이트 |
| FWRITE (bsave/notesave) | offset<0 원자 교체, ≥0 패치, `strsize`=쓴 바이트 |
| FEXIST | 크기 / 없음·디렉터리 −1 / 와일드카드 등 malformed는 −1 + 진단 기록 / 루트 이탈은 오류 |
| FDELETE, FMKDIR, FCOPY | overlay 정책 |
| FCHDIR | 항상 명시 거부(2.335 정적 사용 0, OS cwd 미변경) |
| FDIRLIST1/2 | CRLF 목록·`stat`=개수, sb 버퍼는 FDIRLIST2에서 해제 |

실패는 `throw HSPERR_FILE_IO`(코어와 같은 오류, HSP error 12)이며 상세는 `hsp::last_diagnostic()`.
설치 전 이벤트도 예외로 막아 OS 파일 코드로 빠지지 않는다.

**`dirinfo`**: `TYPE_EXTSYSVAR`(10) 코드 0x102의 reffunc(`hsp3gr_linux.cpp hsp3typeinit_cl_extfunc`).
id 0/1 → `virtual_root()`, `Reset()`이 id 5(dir_tv)도 조회한다. 예시: `tests/platform/hsp_file_host.cpp`.

**이 어댑터가 다루지 않는 파일 접근**(각 담당 어댑터가 `resolve_read/read_file` 등 사용):
picload/getpicsize(PICLOAD 이벤트·이미지 로더), mmload/오디오, z.hpi `zOpen`, hspext/gf 계열, memfile/chdpm.
플러그인의 typed 오류 규칙: `not_found`만 원본의 정상 “열기 실패” 반환으로, 나머지
(`invalid_path/outside_root/not_writable/encoding_error/ambiguous_name/io_error`)는 명시 중단.

## 5. 원본 HSP 3.4 x86 파일 명령 기준값 (observed)

`scripts/reference/platform_hsp34_files.py` → `work/windows/reference-build/platform-files-03`
(`hsp3cl.exe` SHA `29f90169…`, 각 case 전용 폴더). hsp3cl은 오류 시 콘솔 키 입력을 기다려 출력이
사라지므로 각 fixture에 `onerror` 처리기를 넣었다(`platform-files-01`은 전부 timeout, 기록 보존).

| 조건 | 공식 결과 | 이 계층 |
|---|---|---|
| `exist` 디렉터리 / 없음 | −1 / −1 | 동일 |
| bsave offset EOF 너머 | 크기 7, 간격 0 채움 | 동일 |
| bload offset EOF 너머 | strsize 0 | 동일 |
| notesave `"x"` | 1바이트(CRLF 추가 없음) | 동일(바이트 그대로) |
| dirlist 모드 0–7 | 0,2=전부 / 1,3=파일 / 4,6=0개 / 5,7=디렉터리 | 동일 비트 의미 |
| 정렬 | `alpha.TXT, Beta, d1, f1.bin, n.txt, noext, Zeta.txt, _u.txt` | 동일(ASCII 대문자 순) |
| `x.*`↔확장자 없는 `x`, `*.*` | 일치 / 전부 | 동일 |
| `d1\` 끝 구분자, 없는 폴더 | 0 / 0 | 동일 |
| 대소문자 다른 이름 | 디스크 철자로 1개 | 동일 |
| `f1.bin.` / `f1.bin ` | 7 (같은 파일) | **처음 거부 → 수정 후 동일** |
| `*.bin`에 exist | −1 | 동일(진단 기록) |
| delete 없음/디렉터리, mkdir 기존/중첩 없음, bsave offset 없는 파일/없는 폴더, bload 없음/디렉터리, bcopy 없음, noteload 없음, chdir 없음 | 모두 HSP error 12 | 모두 `HSPERR_FILE_IO` |

## 6. 검증 결과

### macOS 27.2 arm64 (observed)

```sh
cmake -S tests/platform -B build/platform-tests -G Ninja \
  -DELONA_CP932_TABLE_DIR=$PWD/work/windows/reference-build/platform-cp932-02/out
cmake --build build/platform-tests && ctest --test-dir build/platform-tests --output-on-failure
```

| 테스트 | 결과 |
|---|---|
| platform_encoding | 76 checks + 전체표 96,512 불일치 0 |
| platform_game_files | 173 checks (원본 tree SHA 전후 동일) |
| platform_game_files_hsp | 71 checks (어댑터를 code_event와 같은 순서로 구동) |
| platform_hsp_e2e | 실제 HSP VM(루트 legacy-CP932 preset과 같은 코어 소스·정의) + 어댑터: `file_ops` 출력 15줄 바이트 일치, int32 `0,-1,INT_MIN,0x12345678` 저장 바이트, `表.txt` 파일명, tombstone, mkdir, DPM, 이중 구분자, bsave offset; `data\` 쓰기·chdir는 HSP error 12; 원본 불변 |

### Windows 11 x64, MSVC 19.51 / Ninja (observed)

`python3 scripts/reference/platform_windows_x64.py --run platform-x64-NN` — 소스·고정 OpenHSP·Win32 표·
E2E 기대값(`run_hsp_e2e.py --export`)을 ZIP(과거 고정 timestamp)으로 전용 `reference-build/platform-x64-NN`에
보내 빌드한다. 원격 Python이 없어 E2E는 `platform_build_x64.ps1`이 같은 기대값으로 비교한다.

| run | 결과 |
|---|---|
| platform-x64-01 | 실패: `test_game_files_hsp`에 `/utf-8` 누락 → ACP 949로 upstream UTF-8 주석을 읽어 C1070 |
| platform-x64-02 | ctest 3/3 통과, E2E 실패: 테스트 호스트가 narrow `argv`(ACP 949)를 받아 `game root ゲーム`의 `ー`가 `?`로 손실 → `wmain`으로 수정 |
| **platform-x64-03** | ctest 3/3, `test_encoding` 76 checks + **Win32 전체표 96,512 불일치 0**(backend win32-codepage-932), game_files 174, adapter 71; VM E2E `file_ops`(exit 7, stdout 바이트 일치)·`file_denied`·`file_chdir`(HSP error 12 + 진단) 통과, 사용자 파일 5개 일치, 원본 tree 불변 |

**호스트 요구사항(실측 근거)**: Windows 호스트는 `--game-dir`/`--user-dir` 등 native 경로를 wide 명령줄
(`wmain`/`GetCommandLineW`)과 `std::filesystem::path`로 받아야 한다. narrow `argv`나 ANSI API는 시스템 ACP를
거쳐 비ASCII 경로를 잃는다.

## 7. Windows HSP 3.4 x86 GUI fixture 기준선 (observed)

도구: `scripts/reference/gui_run_windows.py`(스테이징·SFTP·회수), `gui_run_fixtures.ps1`(원격, 일회성
Interactive/Limited 예약 작업, `finally` 삭제, 명령줄에 run 경로가 있는 잔여 프로세스만 정리),
`GuiFixtureProbe.cs`(x86, 직접 시작한 PID의 창만 열거·캡처), `gui_verify.py`(BMP 디코딩 비교).
`hsp3.exe` SHA `2d55f188b65e67172adc7f24f4a457bb40fb846c971c1279a3bac5126b426ecf`. 세 실행 모두
`taskRemoved=true`, 잔여 프로세스 0.

| 항목 | 결과 (`gui-fixtures-01`, `-02`) |
|---|---|
| tests/gui 4개 | pixel_roundtrip, color_key, bitmap_export exit 0; input_control은 아래 |
| `fixture-buffer.bmp` | 8×8, 24bpp, bottom-up, 246 bytes, 64픽셀 모두 (17,34,51), SHA `44a3da81…` |
| pixel 변형 | buffer→screen→buffer 세 BMP 16×16 전부 (17,34,51) |
| color key (gmode 2) | screen: x<8 (255,0,0), x≥8 (0,0,255) — (0,0,0)만 투명 |
| pattern | buffer (x,y)=(17x,17y,8(x+y)); screen 48×48: gmode0, gmode2(검정만 투명), `gzoom 32,32,1,0,0,16,16,0` = 정확한 nearest 2×; 전체 6966-byte BMP 일치, pget 9점 일치 |
| 창 | class `hspwnd0`, 제목 `Hot Soup Processor ver.3.4a`, DPI 96. `screen 0,16,16`도 최소 폭 때문에 client 130×16 |

**창 캡처 관측**: `PrintWindow`(PW_CLIENTONLY|PW_RENDERFULLCONTENT, PW_CLIENTONLY)와 창 DC BitBlt는
모두 API 성공을 반환했다. `gui-diag-01`에서 원인을 분리했다: 최소 폭보다 작은 16×16 창에서
`redraw 0→1`을 사용하면 원본 런타임의 **창 표시 자체가 흰색**이고, redraw 없이 그리면 16×16=256픽셀이
(17,34,51), 160×64 창은 redraw를 써도 10,240픽셀 모두 일치했다. 즉 캡처 도구 문제가 아니라 원본의
작은 창 redraw 표시 동작이며, 픽셀 정답은 `bmpsave` BMP를 기준으로 한다.

**input_control (programmatic set/query만)**: mesbox → Edit 1개(style `0x50211044`, exstyle `0x200`,
id 16384), `objsel` 후 focus=Edit, `EM_GETLIMITTEXT`=32, 초기 sel 0,0. `objprm "あA"`는 ANSI 바이트
`82 A0 41`로 보존되지만 **원격 시스템 ACP가 949**라 UTF-16은 U+AD87 U+0041(화면 `궇A`).
`WM_SETTEXT(A)` `82 A0 41 42` 재조회 동일, `EM_SETSEL 1,2` → 1,2. 일본어 로캘 표시·**IME 조합/확정/취소,
backspace, 키보드 입력 바이트 제한은 미검증**이다(수동 실행 안 함, 전역 로캘 변경 안 함).

## 8. 통합 `elona_gui_host` 파일 경계 회귀 (2026-11-18, observed)

도구: `scripts/reference/host_regression.py`(Mac, fixture + 원본 AX 단계), `tests/platform/host/*.hsp`,
`scripts/reference/host_windows_x64.py` + `host_windows_x64.ps1`(루트 `windows_stage.py`와 별개의 스냅샷,
`reference-build/platform-host-NN`). 호스트 바이너리는 공유 빌드가 아닌 전용 `build/host-regression-macos`
(`ELONA_GUI=OFF`, legacy CP932/RNG)와 원격 전용 run 폴더에서 빌드했다. 사용자 live preview(PID 30060,
`work/game-run/user-preview-711v_2uc`)와 그 바이너리는 건드리지 않았다.

### src/app 연결 상태 (읽기 전용 검토, 00:2x 파일 기준)

| 항목 | 상태 |
|---|---|
| `elona_platform_hsp` 링크, Reset 후 `install_file_events(code_gettypeinfo(-1))`, `dirinfo(0/1)=virtual_root` | 연결됨 |
| resolver typed 결과(not_found ↔ rejected), picload/mmload `resolve_read` | 연결됨 |
| zOpen 쓰기·bmpsave의 부모 materialize/tombstone 해제 | 검토 시 누락 → GUI가 `begin/end_stream_write`로 반영(00:24–00:26), 회귀로 해결 확인 |
| DPM `scene1.hsp`/`scene2.hsp`(`packed_members`) | **누락**: `noteload "scene1.hsp"` → HSP error 12 |
| Windows wide 명령줄 | **누락**: `main(int,char**)`, `vm.SetFileName(ax.string())` narrow |

### fixture 결과 (non-ASCII game/user/work/AX 경로: `原本 ゲーム root`, `ユーザー データ`, `作業 cwd`, `フィクスチャ 表`)

| case | Mac arm64 (host-06) | Windows x64 non-ASCII (platform-host-03) | Windows x64 ASCII |
|---|---|---|---|
| host_files: original 기본값 bcopy(config.txt, user\export.txt), COW 부모(user\graphic), int32 bsave/bload, `表.txt`, 병합 dirlist, tombstone, mkdir, `graphic\表.bmp` picload pget=(12,34,56) | PASS (결과 파일 바이트 일치) | exit 2: `game_root is not a directory: …原本 ゲ?ム root` | PASS |
| host_bmpsave_cow (원본에만 있는 `user\graphic2`) | PASS (host-01은 gap: HSP error 12) | exit 2 (argv) | PASS |
| host_zopen_fresh (user root에 `user\` 없음) | PASS (host-01은 gap: zOpen 실패 후 zWrite에서 HSP error 38) | exit 2 (argv) | PASS |
| host_zopen_existing | PASS | exit 2 (argv) | PASS |
| host_dpm (`noteload "scene1.hsp"`) | **GAP**: HSP error 12 line 5 | exit 2 (argv) | **GAP**: HSP error 12 |
| 합성 game root tree / work dir | 불변 / 비어 있음 | 불변 | 불변 / 비어 있음 |

Windows non-ASCII 실패는 호스트가 .NET `CreateProcessW`로 받은 wide 명령줄을 narrow `argv`(ACP 949)로
변환하며 `ー`를 `?`로 잃는 경계다(플랫폼 계층 E2E의 `wmain` 수정과 같은 원인).

### 원본 AX 제한 실행 (headless, `--deterministic`, private 사본, 신규 게임·저장/로드 검증 아님)

| 실행 | 결과 |
|---|---|
| Mac host-02 (입력 없음, max-waits 400) | exit 124 max-waits, unsupported 없음 |
| Mac host-03 (startup 스크립트, 00:21 빌드) | 프레임 141 첫 unsupported `water.hpi!_water_getimage@16` |
| Mac host-04 (startup, 최신 src 재빌드) | 3000 프레임 unsupported 없음, 타이틀 Starting Menu 화면 |
| Mac host-05 (newgame 스크립트) | 3000 프레임 unsupported 없음, 캐릭터 생성 Feats and Traits 화면에서 스크립트 입력 종료(`max-waits`) |
| Windows platform-host-03 (startup, ZIP 신규 추출) | 프레임 141 첫 unsupported `mes: mes/print without a font backend (build without SDL3_ttf)` — Windows에 SDL3_ttf가 없어 `ELONA_TEXT_TTF=OFF`로 빌드한 구성 차이 |

모든 Mac 실행: 원본 `elonaplus2.335/` 1,530개 항목 tree SHA `a410538a…` 전후 동일, ZIP `06b3fd7d…` 동일,
non-ASCII private 사본(`原本 ゲーム 2.335`) 전후 동일, work dir에는 스크립트가 지정한 `shots/`만 생성.
Windows: 원격 ZIP 해시 동일, ZIP에서 새로 추출한 private 사본 1,530 항목 전후 동일. (`reference-game\run-001`은
2026-11-17 23:13 원본 EXE 관측 때 생성된 config.txt·user\*.txt가 이미 있어 깨끗한 설치가 아니므로
platform-host-02 결과는 비교에서 제외했다.)

최초 실행 생성 파일(사용자 root에만): `user\export.txt`, `user\lastwords.txt`, `user\lastwords-e.txt`,
`user\music\musiclist.txt`, `user\graphic\face1.bmp`는 Mac·Windows 모두 `original\` 기본값과 바이트 동일
(SHA 양 플랫폼 일치). 게임 자신의 `tmp\*.*` 정리 루틴이 원본 `tmp\null.txt`를 지워 Mac에서는 tombstone만
생기고 원본은 그대로였다.

**발견한 플랫폼 차이 (코어, 이 계층 밖)**: 게임이 언어 선택 후 다시 쓴 `config.txt`(소스 줄 604124 부근
`noteget`/`noteadd` 재작성)의 6번째 줄 `language.	"1"`이 Windows는 CRLF(3,534 bytes), Mac은 LF(3,533 bytes)다.
원인은 OpenHSP `src/hsp3/strnote.cpp:11-17`의 `CRSTR`가 `HSPMAC/HSPLINUX`에서 `"\n"`이기 때문이다.
원본 Windows 동작·세이브 공유를 위해 legacy CP932 빌드에서 `CRSTR "\r\n"`을 쓰는 작은 opt-in 코어 패치가
필요하다(코어 소유자 결정 사항, 이 작업에서는 수정하지 않음).

**Windows DLL shadowing 영향 없음**: 이 문서의 23개 Windows fixture(`tests/gui` 4, `gui_fixtures` 7,
`platform_fixtures` 12)는 `#uselib/#include/#func/#regcmd`가 없고 case 폴더에 게임 DLL을 복사하지 않는다.
SDK에서 로드하는 것은 해시 검사한 `hspcmp.dll`(컴파일러)뿐이다.

### Windows Unicode 경로 패치 (제출: `work/orchestration/windows-unicode-host.patch`, root 적용 대기)

공유 `src/app`·코어는 수정하지 않고 독립 스냅샷(`work/orchestration/windows-unicode-host/snapshot`, 00:36 KST)에서
최소 패치를 만들었다. 대상·기준/패치 후 해시와 근거는 `work/orchestration/windows-unicode-host.json`.

| 파일 | 변경 |
|---|---|
| `src/app/main.cpp` | Windows `wmain(int, wchar_t**)`. 경로 옵션은 끝까지 `std::filesystem::path`(UTF-16), 옵션 이름·숫자·SDL 글꼴 이름만 UTF-8 |
| `src/app/gui_host.cpp` | **native-path 로딩 경계**: AX를 `std::filesystem`으로 읽어 HSP 메모리 스트림(`dpm_memfile` + `SetFileName("MEM:start.ax")`, filepack `pack_fopen`의 `MEM:`)으로 VM에 전달. ANSI 임시 복사·코어 변경 없음. 진단 문자열과 fixture 모드 `dirinfo`/hsptv 문자열은 `path::string()`(ACP 변환, MSVC에서 예외·손실) 대신 UTF-8 |
| `src/graphics/image_io.cpp` | 오류 문자열 `path.u8string()` |

VM 내부 경로(`exedir + "..."`, CP932)는 GameFileSystem이 해석하고, OS 시작 인수는 native Unicode 경로로 남는다.
두 표현은 섞이지 않는다. `strnote.cpp`(root의 legacy CRLF 패치)는 포함하지 않았다.

| 검증 | 결과 |
|---|---|
| Windows x64 패치 전 (platform-host-03) | non-ASCII 5 case 모두 exit 2 `…原本 ゲ?ム root` |
| **Windows x64 패치 후 (platform-host-unicode-01)** | non-ASCII·ASCII 각 5 case, 10/10 통과(DPM `--packed-dir` 포함). 게임 단계: `start ゲーム.ax`, Unicode game/user/work/packed 폴더로 실행, 프레임 141 첫 unsupported는 SDL3_ttf 없는 빌드의 `mes`(구성 차이), ZIP 신규 추출 private 사본 1,530 항목·ZIP 불변, 기본 user 파일 5개 원본 기본값과 바이트 동일 |
| Mac arm64 패치 후 (unicode-mac-01) | 5/5 통과. newgame 스크립트(최신) 프레임 2680에서 첫 unsupported `elona.dll!_grotate@16`; 원본 tree·ZIP·private 사본 불변 |

빌드는 패치 전 `strnote.cpp`(`7fed03a1…`)로 했다. 제출 시점 공유 `gui_host.cpp`는 GUI 변경으로 기준과 다르지만
`git apply --check`가 hunk 5–7을 11줄 offset으로 적용한다.

## 9. 남은 일과 필요한 호스트 훅

1. (연결됨) dirinfo·install_file_events·resolver·stream write·`--packed-dir`. Windows `wmain`/Unicode AX는
   `work/orchestration/windows-unicode-host.patch`로 검증 완료, root 적용 대기.
2. DPM 내장 `scene1.hsp`/`scene2.hsp` 바이트를 `FileSystemConfig::packed_members`로 전달(추출기 산출물).
3. `_fdialog` 플랫폼 파일 선택기 → `grant_external_file`.
4. 원본 AX 제한 실행 시 `hsp::event_counts()`/`last_diagnostic()`로 실제 파일 접근 목록을 기록하고
   쓰기 허용 목록과 대조한다. 이 문서의 허용 목록은 정적 근거다.
5. 일본어 ACP 환경 또는 수동 IME 검증은 별도 승인·환경이 필요하다.
6. 비ASCII 이름의 NTFS 정렬 완전 일치, 8.3 단축명 매칭은 재현하지 않았다(게임 경로는 ASCII 위주).

## 10. 세이브 가져오기 재검수 (2026-11-18)

가져오기는 순회할 때 확인한 파일 종류와 경로를 복사 단계까지 유지한다. CP932에서 마지막 바이트가 0x5C인 일반 파일 `表`와 빈 폴더 `能`를 합성 입력에 포함했다. 검사에서는 입력·출력 트리의 종류와 SHA-256을 비교하고 반환된 파일 목록의 상대 경로·크기·해시도 원본과 대조한다.

각 호출은 사용자 save 폴더 아래에 새 임시 디렉터리를 배타적으로 만들고, 그 디렉터리만 정리한다. 마지막 이름 변경은 macOS의 `RENAME_EXCL`, Windows의 덮어쓰기 없는 `MoveFileExW`를 사용한다. 서로 다른 두 슬롯 동시 가져오기, 빈 세이브와 일반 세이브의 같은 슬롯 경쟁, 기존 `.elonaplus-import-tmp` 안의 표식 보존을 검사했다.

- Mac ARM64에서 `cmake --build build/platform-tests -j8` 후 `ctest --test-dir build/platform-tests --output-on-failure` 4/4 통과.
- Windows x64에서 `python3 scripts/reference/platform_windows_x64.py --run platform-astra-saves-20261118-b` 실행. CTest 3/3, 파일 계층 단언 189/189, VM 파일 작업 검사 통과. 실행 기록은 `work/windows/reference-build/platform-astra-saves-20261118-b/report.json`에 있다. 합성 입력만 사용했다.
