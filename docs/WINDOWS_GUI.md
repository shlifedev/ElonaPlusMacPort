# Windows 네이티브 GUI(SDL3/SDL3_ttf) 검증 (2026-11-18)

**최신 상태.** Windows x64 네이티브 창의 타이틀과 신규 게임·저장/로드 검증을 통과했다. Windows 기본 표시 경로를 SDL 소프트웨어 렌더러와 Win32 창 표면으로 바꿨다. 게임은 각 프레임을 CPU DIB에 그리며, 창 크기를 바꾼 뒤에도 Win32가 현재 상태를 캡처한다.

`gui-native-fix62-final`에서 기본 설정 그대로 직접 띄운 게임 창의 PrintWindow 결과를 같은 실행의 타이틀 3프레임과 비교했다. 애니메이션 중에도 세 화면에서 같았던 415,724픽셀 중 406,961개가 일치했다(97.9%). 같은 검사로 이전 `gui-native-05`를 판정하면 18,796픽셀(4.5%)만 일치해 실패한다. 22개 GUI fixture 실행, 신규 게임, 재로드, 세션 정리·원본 무결성도 통과했다. `work/windows/reference-build/gui-native-fix62-final/verify.json`에서 판정을 확인할 수 있다.

한국어 관리 AX도 별도 Windows 재빌드 `gui-native-fix62-managed-final`에서 검증했다. 참조·일본어 대체 표시·한국어로 각각 신규 게임, 재로드, 북부 티리스까지 진행했고 노른 6페이지의 본문 18건을 대조했다.

### 이전 실행 기록
Claude 담당자가 사용량 한도로 중단한 뒤 root가 기존 실행 결과를 회수하고 검증 도구를 수정했다.
`gui-native-01`은 빌드와 headless 게임 실행을 마쳤으나 window-startup 작업 폴더를 두 번 만들면서
session 1 실행 전에 중단됐다. 이를 게임 실행 실패와 구분한다. 해당 실행의 잔여 프로세스/예약 작업은 없었다.

## 후속 진단·안내 창 수정: gui-native-04/05 부분 완료

2026-11-18 Codex 병렬 작업자가 팝업/종료 진단과 `--auxiliary-close-hide`를 포함한 C++ 소스를 Windows MSVC x64에서 다시 빌드했다. `04` 소스는 이후 root의 한국어 문단 helper/변환 AX 작업 전 스냅샷이다. `05`는 동시 소스 편집을 피하려고 같은 `04` 스냅샷을 명시적으로 재사용했다.

```sh
python3 scripts/reference/windows_gui_run.py --run gui-native-04
python3 scripts/reference/windows_gui_run.py --run gui-native-05 --snapshot-from gui-native-04
```

- 두 실행 모두 Release 빌드 성공, fixture 11개×headless/SDL 창의 22개 기대 결과 통과. x86 BMP 기준과 renderer readback 일치 유지.
- 원본 AX 신규게임→이동→저장·종료 exit0/4358프레임, 저장154파일. 복사 프로필의 새 프로세스 로드 exit124/max-waits/1101프레임. 미지원·HSP 오류 없음.
- 예약 작업 삭제, 잔여 프로세스0, 원본 ZIP과 private 원본 트리1530항목 불변을 각각 확인했다.
- **실제 타이틀 창의 외부 캡처 판정은 두 실행 모두 실패했다.** `04`는 15초 후 PrintWindow가 초기 언어 선택 화면(13색)을 반환했다. `05`는 자체 frame400 타이틀 screenshot 생성 marker를 관찰한 뒤 캡처했지만(`ready_file_observed=True`, 약1.9초) 같은 초기 화면이었다. 따라서 단순히 너무 일찍 캡처한 문제로 해결됐다고 할 수 없다. DC 캡처는 검정이었다.
- 동일 프로세스의 CPU frame400와 종료 시 SDL renderer readback에는 정상 타이틀이 있다. 캡처 API/합성 문제인지 실제 표시 갱신 문제인지 구분할 증거가 부족하다. 둘 다 `all_automated_checks_passed=false`를 그대로 보존했다. `session_and_integrity_ok=false`는 타이틀 캡처 조건도 포함한 판정이며 작업 정리나 해시 불변 검사가 실패했다는 뜻은 아니다.
- 최신 사용자 지시가 화면/팝업 **조사만**으로 바뀌어 여기서 추가 Windows 표시 수정·실행은 보류했다. 실제 사용자 창과 설정은 건드리지 않았다.

증거: `work/windows/reference-build/gui-native-{04,05}/verify.json`, `window-results/probe.log`, `window-results/window-startup-window0-pw.png`, `png/window-startup-f0400.png`, `png/window-startup-window.png`. `scripts/reference/WindowsGuiProbe.cs`의 ready-file marker, 실행기의 `--snapshot-from`이 재현을 지원한다.

## Windows 한국어 표시와 팝업 경계 fixture: 통과

위 전체 타이틀 캡처 실패와 별도로, `04` 실행물에 고정된 한국어 catalog를 주어 작은 전용 fixture를 실제 session1 창에서 확인했다.

```sh
python3 scripts/reference/windows_localization_check.py --run gui-native-04
```

- `localization-check/verify.json`의 `passed=true`. 실제 PrintWindow 이미지에서 **모험의 이정표**를 확인했고, 입력 오버레이는 원문 **Starting Menu**를 유지했다. VM/저장 원문 바이트 불변, 한국어 표시 픽셀 변화, renderer readback 오버레이 외 불일치0(오버레이758픽셀).
- 설치된 `C:\Windows\Fonts\malgun.ttf`를 직접 읽었고 복사·재배포하지 않았다. SHA-256 `7a183cf1c6c56b9609fcc16eda8b5229fbc11758a21e669ec00343239b02192f`.
- 호스트 SHA-256 `4a8c7e116298fe2835526d443333a24ad055330bc4841877a27e68b934e1934f`, catalog SHA-256 `cb917c2eb12e1f203584b34e31521596f479b2fc78fa5bb5934a372c29058e27`(3837키).
- 결정적 modal fixture의 1/6/7/1 결과와 안내 창 fixture의 기본 종료/옵션 적용 후 계속 실행→메인 창 닫기 분기를 확인했다. Windows의 실제 OS 모달 버튼을 수동 클릭한 검사는 아니다.
- 실제 창은 probe가 시작한 자기 프로세스만 캡처했고 일회성 작업 삭제·잔여0. 증거 `work/windows/reference-build/gui-native-04/localization-check/{verify.json,window-with-edit.png,window-results/korean-window-window0-pw.png}`.

이 검사는 **표시 catalog/VM 분리와 팝업 경계**에 대한 것이다. 이후 추가된 한국어 문단 helper/type48, 변환된 전체 AX, 최신 한국어 신규게임 전체 경로의 Windows 검증으로 확대 해석하지 않는다. 도구는 `scripts/reference/windows_localization_check.py`와 `.ps1`이다.

## 최신 관리 AX·문단 렌더러: gui-native-ko-managed-03 통과

2026-11-18 후속 Codex 검사. 최신 `native_ko_gmes` type48/$00와 `rich_text`를 포함한 Windows MSVC x64 호스트를 새 private 소스/빌드 디렉터리에서 만들었다. 기존 `gui-native-04/05`를 덮어쓰지 않았고, 그 실행의 외부 창 캡처 실패도 해결됐다고 기록하지 않는다. 이번 검사는 **Windows의 headless TTF/게임 버퍼 렌더링**이며 OS 창 표시·전체 화면·실제 IME 조작 검사가 아니다. 해상도/OS 팝업 동작이나 개인 설정은 바꾸지 않았다.

재현 도구는 `scripts/reference/windows_managed_check.py`, `.ps1`이다. 고정 catalog/AX와 새 실행 이름을 사용한다.

```sh
python3 scripts/reference/windows_managed_check.py \
  --run gui-native-ko-managed-03 \
  --catalog src/localization/ko-KR/runtime.tsv
```

완료된 이름은 재사용할 수 없으므로 재실행 때는 새 이름을 지정한다. 기본 관리 AX 입력은 `work/localization/managed-gmes-v1-01/start.ax`, 비교 입력은 `work/localization/cp932-full-reference-01/start.ax`다. 후자는 원문을 native로 재컴파일한 기준이며 원본 Windows EXE의 직접 실행이 아니다.

| 검사 | 실제 결과 |
|---|---|
| 최신 host/compiler/catalog 실행물 MSVC 빌드 | 성공, catalog/rich-text 단위 assertion 50개 통과. 전체 Windows CTest를 실행한 것은 아님 |
| 원문/한국어 표시 fixture | 화면 픽셀 변화, VM/저장 원문 `Starting Menu` 유지 |
| 문단 fixture | type48 두 번 호출, 강조·줄바꿈 높이·미번역 fallback·원문 저장 보존 통과 |
| 전체 고정 catalog | 5,058키 모두 실제 `mes` 호출, 조회 누락0, 미지원/관찰 오류0 |
| JP 신규게임→집→이동→저장 | 세 변형 모두 exit0, 4358프레임, 저장154파일 내용 동일 |
| 새 프로세스 로드→인벤토리→재저장 | 세 변형 모두 exit0, 1158프레임, 저장154파일 내용 동일 |
| 작별→노른 안내→북부 티리스→저장 | 세 변형 모두 exit0, 3008프레임, 저장162파일 내용 동일, 맵4/좌표(22,21) |
| 게임 문단 경계 도달 | 관리 AX 두 변형 모두 `native_ko_gmes=12`, 노른 집 안내2페이지+세계 지도 안내4페이지 캡처 |
| 보존·정리 | 원본 ZIP/private 원본 트리 불변, 검사 소유 잔여 프로세스0 |

세 변형은 **무수정 native 참조 AX / 번역 catalog 없는 관리 AX / 한국어 catalog 있는 관리 AX**다. 두 관리 변형을 참조와 비교했을 때 압축 해제한 모든 저장 파일 내용이 단계별로 일치했다. 이름·좌표·레벨·맵·전체 인벤토리·기술 필드가 동일하고 양쪽 턴 차이는 0이었다. 번역 없는 관리 AX는 최종 화면 BMP도 참조와 동일했다. 한국어는 표시만 달라졌다.

`evidence/managed-korean/world/cwd/shots/norne-home-1.png`, `norne-world-4.png`를 직접 열어 맑은 고딕 한국어 본문, 파란 강조, 기존 초상화/배경/다음 페이지 표시를 확인했다. 본문은 창 안에 표시됐고 주변 일부 원문 로그는 미번역 상태다. 이 장면 검수가 전체 UI 번역·레이아웃 완료를 의미하지 않는다.

검증 산출물은 `work/windows/reference-build/gui-native-ko-managed-03/verify.json`이며 **41/41 조건 true, `passed=true`**다. `evidence/summary.json`, `catalog-unit.log`, 각 실행의 `argv.json`/`report.json`/`save_snapshot`/`cwd/shots`가 세부 근거다.

| 고정 입력/출력 | SHA-256 |
|---|---|
| Windows host | `5912d086c87243cd8be2099ce0b5a86e25425f806e113012fad49bf648bb36af` |
| 5,058키 catalog | `dc65f3548b0bcbaabfc655c23c05c90e84650434ef55115a676f9c5d7a32b613` |
| 관리 AX | `8aa19643c4824fa98ee175194864ecdfde8d4871c13a0d9f8ffa99cf9b99ad03` |
| native 참조 AX | `8575184fe1729c90736bc5f346759e6be06d06cf1cc8ff7d97456517cb8c9c5e` |
| 소스 스냅샷 ZIP | `f72dc0c768b5c0714e4a2d728badafe43414d0864663aaf1abf62aac8e8da0d7` |

### 발견·수정된 Windows 글꼴 경계

앞선 `gui-native-ko-managed-02`의 전체 catalog smoke는 첫 `U+30FB` 일본어 가운데점에서 명시적 exit3을 냈다. 예를 들어 노동 에너지 상품의 한국어 번역문에 원문의 점선 구분자 `・・・・`가 남아 있었다. 단순 한글 fixture와 문단 fixture는 통과했으므로 전체 키 실제 렌더가 추가 결함을 발견한 것이다.

`scripts/reference/WindowsFontGlyphProbe.cs`로 동일 SDL3_ttf의 `TTF_FontHasGlyph`를 사용해 번역 대상에 포함된 고유 코드포인트984개를 읽기 전용 검사했다. 설치된 맑은 고딕과 굴림은 각각 U+30FB 하나가 없었고, MS Gothic은 이 글자를 제공하지만 한글879개가 없었다. 글꼴은 읽기만 했고 복사/설치하지 않았다. 근거는 `gui-native-ko-managed-02/glyph-audit/`다.

root는 한국어 글꼴에 없는 글자를 기존 원문용 글꼴에서 찾는 **글자 단위 fallback**을 `src/graphics/text_renderer_ttf.cpp`에 추가했다. 두 글꼴 모두 없으면 여전히 명시적으로 실패하고, 문장 내용·논리 셀 폭·배율은 바꾸지 않는다. 번역의 구두점을 지우는 방식으로 실패를 피하지 않았다. 이 수정이 들어간 `03`에서 같은 5,058키가 모두 통과했다. 맑은 고딕 SHA는 위 한국어 fixture와 동일하다.

이전 실패 기록도 남겼다. `01`은 새 검증 스크립트의 PowerShell 보고서 변수 범위 오류로 첫 fixture 뒤 멈췄고 게임 결함이 아니었다. `02`는 위 글꼴 실패 외에 EN용 이동 좌표를 JP 프로필에 적용한 첫 경로가 실제 노른 안내에 도달하지 못해 `gmes=0`이었다. 이를 도움말 통과로 세지 않았다. `03`은 이미 Mac에서 검증된 `tests/localization/source_transform/japanese_home_to_world.script`를 사용하고 정확한 맵/좌표·12호출·캡처를 필수 조건으로 추가했다. 회수 ZIP의 Windows 경로 구분자도 검사 도구에서 정규화했으며, 원본 실패 ZIP은 보존했다.

## 확인한 결과: gui-native-02

재현: `python3 scripts/reference/windows_gui_run.py --run gui-native-<새이름>`.
음악 의존성 및 renderer 소스도 스냅샷에 포함한다. 기존 실행 폴더는 재사용하지 않는다.

- Release MSVC x64 + SDL3/SDL3_ttf 빌드 성공. Unicode 인수/AX 패치와 CRLF 코어 수정 포함.
- fixture 11개를 headless와 session 1 SDL 창에서 각각 검사해 22개 예상 결과 일치.
  `unsupported_command`는 의도한 `mci` 미지원 exit3이며 기능 구현 완료를 의미하지 않는다.
- 빠른 fixture들은 외부 PrintWindow 캡처 전에 종료됐다. 이 fixture의 창 픽셀 검사는 호스트의
  SDL renderer readback으로 수행했으며, Edit overlay 영역을 제외한 불일치 0이다.
  기본 픽셀/색상 키/nearest 확대/BMP 결과는 기존 공식 HSP3.4 x86 기준 파일과 일치했다.
- 원본 AX: 신규게임→이동→저장·종료는 exit0/end, 4358프레임, `sav_Ari` 파일 154개.
  프로필을 새 폴더로 복사해 새 프로세스에서 불러오기는 1101프레임/max-waits(exit124).
  둘 다 `first_unsupported`와 `hsp_error`가 비어 있다. 실제 저장 내용 비교는 별도 후속이다.
- session 1에서 직접 시작한 native host PID 16656의 SDL 창을 PrintWindow로 캡처했다.
  800×600, 색상 45,087종이며 root가 **Starting Menu** 화면을 직접 확인했다.
  이 실행은 결정적 입력이고 25초 제한으로 정상 중단됐다. 사용자 키보드를 조작하지 않았다.
- 예약 작업 삭제, 잔여 프로세스 0. 원본 ZIP SHA-256 및 private 원본 트리 1,530항목 해시 불변.
- 판정기는 fixture뿐 아니라 게임 예상 종료/저장 파일, 실제 창 캡처, 정리/원본 보존도 필수로 검사한다.

증거: `work/windows/reference-build/gui-native-02/{report.json,verify.json}`,
`window-results/probe.log`, `window-results/window-startup-window0-pw.png`, `evidence/`, `png/`.
전체 자동 판정 `all_automated_checks_passed=true`를 root가 확인했다.
`gui-native-02`는 후속 IME/오디오 수명 수정 이전 체크포인트다. 이를 반영한 최신 `gui-native-03`도 동일 검증을 통과했다.
`gui-native-03/verify.json`: 전체 자동 판정 true, 신규/로드154파일, 실제 창 timeout25초, 잔여0, 원본 불변.
Windows에서 실제 일본어 후보 선택이나 장치 오디오를 검증했다는 뜻은 아니다.

## 고정 의존성 (전용 폴더만, 전역 설치·환경변수 변경 없음)

| 패키지 | 공식 URL | SHA-256 | 근거 |
|---|---|---|---|
| SDL3-devel-3.4.20-VC.zip | https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-devel-3.4.20-VC.zip | `1ec3009655ee37b1f73e52b3b120acc5afd8abaf9532f3303b1bf1dfe91b5969` | GitHub release asset digest |
| SDL3_ttf-devel-3.2.2-VC.zip | https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-devel-3.2.2-VC.zip | `67805c5babfc49ca0c56882dc9b8cabbcdd1e6f9edde10ddac91ddb38f3afb8c` | 최초 다운로드 시 고정(업스트림 digest·서명 없음; 서명은 소스 아카이브만) |

Mac과 같은 버전(SDL3 3.4.20 / SDL3_ttf 3.2.2). 로컬 `.cache/downloads/windows-gui/`, 잠금 `scripts/reference/windows_gui_deps.lock.json`.
고지: 각 패키지의 LICENSE.txt(zlib), SDL3_ttf에 포함된 FreeType/HarfBuzz/PlutoSVG/PlutoVG의 고정 소스 고지를 `docs/licenses/`와 `docs/THIRD_PARTY_NOTICES.md`에 보존했다. 배포 시 이 고지들도 포함해야 한다.

글꼴: 원격 Windows에 `C:\Windows\Fonts\msgothic.ttc`(MS Gothic)와 Yu Gothic이 설치되어 있어 호스트 기본 후보를 그대로 읽는다. 복사·재배포하지 않으므로 Noto CJK는 준비하지 않았다. `meiryo.ttc`는 없음, 일본어 보충 글꼴 기능은 NotPresent.

## 도구

- `scripts/reference/windows_gui_deps.py`: 다운로드·SHA 검증·잠금.
- `scripts/reference/windows_gui_run.py`: 비공개 소스 스냅샷(`work/orchestration/windows-gui/<run>/`; Unicode 패치는 이미 공유 소스에 있음 확인) → 전용 `reference-build/gui-native-NN` 업로드 → 원격 실행 → 회수 → 판정.
- `scripts/reference/windows_gui_build.ps1`: Release/Ninja/MSVC, `ELONA_GUI=ON ELONA_TEXT_TTF=ON`, `CMAKE_PREFIX_PATH`는 run 폴더의 SDL 패키지만. session 0 headless: tests/graphics fixture 전부 + 원본 AX(언어→타이틀, newgame 저장, 프로필 복사 후 load). session 1: 일회성 Interactive/Limited 예약 작업으로 `WindowsGuiProbe.exe`가 직접 시작한 PID의 창만 PrintWindow/DC 캡처, finally에서 작업 삭제·잔여 프로세스 확인. 원본은 원격 ZIP 신규 추출 private 사본(해시 전후 비교).
- `scripts/reference/windows_gui_verify.py`: fixture 기대값·x86 기준 BMP 바이트 비교·창 캡처 비교·게임 저장/로드 요약.

## 남은 검증

Windows 실제 오디오 장치 출력, 일본어 IME 수동 입력, 장시간 실시간 플레이와 후속 변경 회귀.
Mac/Windows 저장 데이터 교환 및 핵심 상태 비교는 별도 실행으로 진행한다.
