# Windows 네이티브 GUI(SDL3/SDL3_ttf) 검증 (2026-11-18)

**상태: 기본 GUI·신규게임·저장/로드 검증 완료, 장시간 실시간 플레이는 미검증.**
Claude 담당자가 사용량 한도로 중단한 뒤 root가 기존 실행 결과를 회수하고 검증 도구를 수정했다.
`gui-native-01`은 빌드와 headless 게임 실행을 마쳤으나 window-startup 작업 폴더를 두 번 만들면서
session 1 실행 전에 중단됐다. 이를 게임 실행 실패와 구분한다. 해당 실행의 잔여 프로세스/예약 작업은 없었다.

## 확인한 결과: gui-native-02

재현: `python3 tools/reference/windows_gui_run.py --run gui-native-<새이름>`.
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

Mac과 같은 버전(SDL3 3.4.20 / SDL3_ttf 3.2.2). 로컬 `.cache/downloads/windows-gui/`, 잠금 `tools/reference/windows_gui_deps.lock.json`.
고지: 각 패키지의 LICENSE.txt(zlib), SDL3_ttf에 포함된 FreeType/HarfBuzz/PlutoSVG/PlutoVG의 고정 소스 고지를 `licenses/`와 `THIRD_PARTY_NOTICES.md`에 보존했다. 배포 시 이 고지들도 포함해야 한다.

글꼴: 원격 Windows에 `C:\Windows\Fonts\msgothic.ttc`(MS Gothic)와 Yu Gothic이 설치되어 있어 호스트 기본 후보를 그대로 읽는다. 복사·재배포하지 않으므로 Noto CJK는 준비하지 않았다. `meiryo.ttc`는 없음, 일본어 보충 글꼴 기능은 NotPresent.

## 도구

- `tools/reference/windows_gui_deps.py`: 다운로드·SHA 검증·잠금.
- `tools/reference/windows_gui_run.py`: 비공개 소스 스냅샷(`work/orchestration/windows-gui/<run>/`; Unicode 패치는 이미 공유 소스에 있음 확인) → 전용 `reference-build/gui-native-NN` 업로드 → 원격 실행 → 회수 → 판정.
- `tools/reference/windows_gui_build.ps1`: Release/Ninja/MSVC, `ELONA_GUI=ON ELONA_TEXT_TTF=ON`, `CMAKE_PREFIX_PATH`는 run 폴더의 SDL 패키지만. session 0 headless: tests/graphics fixture 전부 + 원본 AX(언어→타이틀, newgame 저장, 프로필 복사 후 load). session 1: 일회성 Interactive/Limited 예약 작업으로 `WindowsGuiProbe.exe`가 직접 시작한 PID의 창만 PrintWindow/DC 캡처, finally에서 작업 삭제·잔여 프로세스 확인. 원본은 원격 ZIP 신규 추출 private 사본(해시 전후 비교).
- `tools/reference/windows_gui_verify.py`: fixture 기대값·x86 기준 BMP 바이트 비교·창 캡처 비교·게임 저장/로드 요약.

## 남은 검증

Windows 실제 오디오 장치 출력, 일본어 IME 수동 입력, 장시간 실시간 플레이와 후속 변경 회귀.
Mac/Windows 저장 데이터 교환 및 핵심 상태 비교는 별도 실행으로 진행한다.
