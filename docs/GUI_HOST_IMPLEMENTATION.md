# 네이티브 GUI 호스트 구현 보고 (Claude 작업분)

작성: 2026-11-18. 소유 범위: `CMakeLists.txt`/`CMakePresets.json`(통합 부분), `src/cmake/ElonaGraphics.cmake`,
`src/graphics/*`, `src/app/*`, `src/platform/window*`·`keymap_vk*`·`audio_*`, `tests/graphics/*`, `tests/game/*`,
`scripts/run_game_trace.sh`. 다른 작업자 소유(`src/plugins/*`, `src/platform/encoding*`·`game_files*`)는 호출만 한다.

**이 보고는 "게임 전체 이식 완료"가 아니다.** 아래 표의 "실측"은 실제로 실행한 결과이고, 그 외는 정적 분석·근사다.

## 최신 통합 후속 (root)

- 최신 Mac Release CTest7/7(의미24개 + IME), Windows GUI `gui-native-03`의 headless/창11개씩·원본 신규게임/저장/로드·실제 타이틀 캡처 통과.
- Mac 실시간 시계/오디오와 스크립트 입력을 결합한 `root-realtime-load-01`에서 불러오기→인벤토리→저장종료 exit0. 음악 장치 출력 position913408 frames 확인. 일반 사용자 플레이는 `scripts/Play ElonaPlus.command`.
- 음악·IME·양 플랫폼 저장 데이터의 후속 근거는 [AUDIO_IMPLEMENTATION.md](AUDIO_IMPLEMENTATION.md), [IME_IMPLEMENTATION.md](IME_IMPLEMENTATION.md), [SAVE_COMPATIBILITY.md](SAVE_COMPATIBILITY.md), [WINDOWS_GUI.md](WINDOWS_GUI.md)에 있다.
- 아래 최초 Claude 체크포인트의 5/5·20개는 당시 검사 수이며 최신 검사 수와 구분한다.

## 1. 최초 작업 실측 결과 (macOS 27.2, Apple Silicon, arm64)

| 항목 | 결과 | 근거 / 재현 |
|---|---|---|
| 빌드 | Debug·Release·헤드리스 전용(SDL 없음) 모두 성공 | `cmake --preset macos-arm64-legacy-cp932-{debug,release}` / `-DELONA_GUI=OFF -DELONA_TEXT_TTF=OFF` |
| ctest | 3개 구성 모두 5/5 (string walkers, VM 의미 20/20, graphics_surface, gui_fixtures_headless, plugins_unit) | `ctest --preset macos-arm64-legacy-cp932-debug` |
| GUI fixture | 헤드리스·실제 SDL 창 각각 11/11 | `tests/graphics/run_gui_fixtures.py` (아래) |
| Windows HSP3.4 x86 대조 | pattern 3/3, colorkey 2/2, pixel 3/3, bitmap_export 1/1 **바이트 동일** | `--windows-reference work/windows/reference-build/gui-fixtures-02/cases` |
| 창 표시 검증 | Retina 2배 창 read-back을 screen0와 비교, 불일치 0 (보이는 Edit는 컨트롤 영역 안에서만 차이) | `results.json`의 `window_capture` |
| 원본 2.335 AX | 언어 선택 → 라이선스 → 타이틀(물결) → 신규 캐릭터 생성 전 단계 → 오프닝 → 맵 → 이동 → **저장·종료(exit 0)** → 재시작 → **불러오기** → 맵 복원 | `work/game-run/run27`(신규·저장), `run28`(불러오기) |
| 원본 보존 | 매 실행 전후 ZIP/EXE SHA-256 동일. 원본 폴더는 GameFileSystem 읽기 전용, 쓰기는 `work/game-run/<run>/user` | `original-before/after.sha256` |
| 결정성 | 같은 스크립트 두 번 실행 시 최종 화면 바이트 동일 | `det1`/`det2` |

실시간(사용자 조작) 창 실행은 root가 별도 preview로 운영하며, 이 보고서의 게임 결과는 **결정적 헤드리스 실행**이다.
두 모드는 같은 실행 파일·같은 명령 구현을 쓰지만 입력 원천(스크립트 vs 실제 키보드)과 시계(가상 vs 실시간)가 다르다.

### 재현 명령

```sh
cmake --preset macos-arm64-legacy-cp932-release && cmake --build --preset macos-arm64-legacy-cp932-release -j8
B=$PWD/build/native-macos-arm64-legacy-cp932-release
python3 tests/graphics/run_gui_fixtures.py --compiler $B/elona_hspcmp --host $B/elona_gui_host \
  --common $PWD/.cache/upstream/OpenHSP/common --work $B/gui-fixtures-window \
  --windows-reference $PWD/work/windows/reference-build/gui-fixtures-02/cases     # --headless 가능
# 원본 게임(정규화 AX 필요: scripts/normalize_legacy_ax.py → work/normalized/2.335/start.ax)
ELONA_BUILD=$B scripts/run_game_trace.sh run27 --deterministic --headless --max-waits 5000 \
  --script tests/game/newgame_english.script
REUSE_USER_FROM=run27 ELONA_BUILD=$B scripts/run_game_trace.sh run28 --deterministic --headless \
  --max-waits 1100 --script tests/game/load_continue.script
# 실시간 창(사용자 조작): $B/elona_gui_host --game work/normalized/2.335/start.ax --game-root elonaplus2.335 \
#   --user-root <비공개 dir> --work-dir <비공개 dir> --packed-dir work/extracted/2.335
```

스크린샷: `work/game-run/run27/cwd/shots/`(s2070-sheet 캐릭터 시트, m4120 이동 후, m4200 저장 프롬프트, m4330 저장 완료),
`work/game-run/run28/cwd/shots/l0400.bmp`(세이브 선택), `l1000.bmp`(복원된 맵), 이름 입력 창 오버레이 `work/game-run/run24/cwd/shots/s2285-name-window.bmp`.

## 2. 구조

- **픽셀 계층 `src/graphics/`** (SDL 비의존): 각 screen/buffer가 Win32 HSP3와 동일한 24bit BGR·bottom-up·4byte stride DIB를 소유.
  `hspwnd_win.cpp`의 Blt(gmode 1–7 정수식, 같은 버퍼 겹침 시 전방 스캔), BitBlt(gmode 0, 겹침 안전), Pget(범위 밖 CLR_INVALID),
  Boxfill(비정규화 사각형 무시), Cls, Print/strsp 줄 분리, HSV, RenderAlphaBitmap을 literal port. grotate/grect는 fcpoly를
  32bit 산술(uint32) 그대로 port(`poly_raster.cpp`). BMP 저장은 원본과 동일 바이트. 이미지 디코딩은 OpenHSP 동봉 stb_image.
- **텍스트**: SDL3_ttf + 시스템 글꼴(macOS Hiragino W3, Windows MS Gothic). CP932 셀 메트릭(반각 = 높이/2, 전각 = 높이)으로
  MS Gothic 배치를 재현. CP932→UTF-8은 UI 경계에서만(`ui_text_codec`, elona_platform 링크 시 그 모듈 사용).
- **호스트 `src/app/gui_host.cpp`**: OpenHSP 코어에 TYPE_EXTCMD/EXTSYSVAR/DLLFUNC/DLLCTRL을 등록하고 INTCMD(randomize)·
  INTFUNC(gettime)를 결정적 모드에서만 감싼다. msgfunc가 wait/await/stop·이벤트·onkey/onclick/onexit를 처리.
  미구현은 `ELONA-UNSUPPORTED <종류>: <이름>` 진단 후 **즉시 종료(exit 3)** — 게임의 onerror가 삼켜 다른 경로로 진행하는 것을 막는다.
- **창/입력 `src/platform/window_sdl.cpp`**: SDL3 창·렌더러(nearest, letterbox, HiDPI), 스캔코드→Windows VK, 텍스트 입력,
  메시지 박스, 렌더러 read-back 캡처. `window_headless.cpp`는 의존성 없음.
- **DLL 경계 `src/app/native_dll.h`**: handler는 x86 EAX 원값을 반환, legacy stat/await/DLL_ERROR 규약은 호스트 한 곳에서 적용.
  VarRef는 PVal/APTR/형/남은 바이트를 검사한 int32 view만 제공(포인터를 게임 int에 넣지 않음).
- **결정적 실행 도구**: `--deterministic`(가상 tick, 실제 키보드 무시), `--script`(프레임별 key/down/up/text/mouse/click/dialog/
  screenshot/capture/exit), `--max-waits`, `--timeout-ms`, `--screenshot`, `--window-capture`, `--report`(JSON: exit_reason,
  first_unsupported, notes, 명령 횟수), `--seed-tick`, `--virtual-epoch`.

## 3. 원본 DLL 대체 현황 (77개 선언 중 원본 경로에서 실제 도달한 것)

| DLL | 구현 | 근거 |
|---|---|---|
| exrand / hmm bits / z.hpi / hspda | plugin 작업자 순수 모듈을 바인딩 | `src/plugins/*`, x86 대조는 plugin 문서 |
| hmm.dll 오디오·조이패드 | DirectX 백엔드 없음 → DSINIT/DMINIT/DIINIT 실패값(stat 0). 원본은 이때 cfg_music/cfg_sound=2로 **HSP 내장 mm\* 경로로 전환**(끄는 것이 아님). 미초기화 DSSTOP/DSSETVOLUME/CHECKPLAY/DSPLAY/DMPLAY/DMSTOP raw 0, DSRELEASE/DSEND/DMEND raw -1, DSGETMASTERVOLUME raw +1(DLL 오류) | plugin 작업자 x86 run plugins-claude-07 |
| hspext_ext.dll gfini/gfdec/gfdec2/gfinc | 정적 분석 이식(unpadded `sx*3` 보폭 포함) | Ghidra 12.2, `dll_bindings_gfx.cpp` 주석의 주소 |
| water.hpi 6함수 | 높이장·굴절·조명 정수식 이식, setlight는 미호출(광원 0,0) | 〃 |
| elona.dll `_grotate`(func_3) | **회전 그림자**: 비검정 텍셀 위치를 채널당 0x50 감산, ScanLine은 끝행 h-1 | 〃 |
| winmm/kernel32/user32 일부 | timeGetTime·timeBegin/EndPeriod, CreateMutexA/CloseHandle/GetLastError, GetKeyboardState/keybd_event(NumLock 상태와 숫자패드 매핑) | 좁은 대체 |

## 4. 알려진 미구현·차이 (다음 작업)

1. **음악**: MIDI/MP3 백엔드와 별도 SDL 스트림 연결 완료. Mac 실제 장치 콜백·원본 게임 실시간 실행 통과. 결정적 모드는 null 출력이며 Windows 장치 출력과 장시간 청취는 남음.
2. **IME 조합**: 후속으로 SDL_TEXT_EDITING/표시/확정/취소/CP932 바이트 한도와 삭제 경계를 구현했다. Mac 합성 SDL 이벤트 15개와 HSP headless/숨긴 창 검사가 통과했다. 실제 macOS/Windows IME 후보 선택 및 원본 ANSI Edit의 전체 한도 동등성은 미검증이다. [IME_IMPLEMENTATION.md](IME_IMPLEMENTATION.md) 참조.
3. **button/listbox/combox/chkbox**, 객체 색(objmode 4), Tab 이동 미구현(도달 시 진단 정지).
4. **교차 프로세스 단일 인스턴스 mutex** 없음(노트로 표시), `exec`(홈페이지/텍스트 열기), file dialog(GetOpen/SaveFileNameA), sysinfo, chgdisp(전체화면).
5. GDI 근사: line/circle, gzoom halftone, gradf. fcpoly·elona 그림자는 x87 확장 정밀도 대신 long double 사용 — Windows 픽셀 대조 필요.
6. Windows: wide argv와 native-path AX 로딩 적용, x64 SDL/TTF 실제 창·신규게임/저장/로드 검증 완료. 장시간 실시간 플레이·실제 IME는 남음.
7. 진단 위치: 원본 AX에는 디버그 정보가 없어 `line=-1`. DLL은 이름으로, 명령은 명령명으로 식별.
8. 배포: Homebrew SDL3는 macOS 26+ 대상 빌드라 deployment target 13.0 경고. 배포 전 고정 SDL3 소스 빌드 필요.
9. `docs/THIRD_PARTY_NOTICES.md`와 `docs/licenses/`에 OpenHSP 그래픽 이식, stb_image, SDL/TTF·관련 정적 라이브러리, 음악 의존성 고지 병합 완료. 실제 배포물의 동적 라이브러리/최소 OS 검사는 별도 필요.
