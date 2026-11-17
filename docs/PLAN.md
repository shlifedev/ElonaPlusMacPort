# ElonaPlus Native — 개발 계획 및 인수인계

## 현재 상태 (2026-11-18)

**재개:** 사용자가 맥에서 정상 동작할 때까지 계속 진행하도록 지시했다. Claude 작업 이후 최신 요청은 **Codex 병렬 에이전트로 캐릭터 생성 뒤 종료 현상을 조사하고 한국어 번역을 병행**하는 것이다. 아래 체크포인트는 시간순으로 누적되므로 최신 관측을 우선한다. 전체 목표는 Mac/Windows 네이티브 오프라인 플레이와 원본 저장 데이터 보존이다.

### 최신 요청: 팝업 종료 조사·플레이 확대·한국어 번역

- 사용자는 “캐릭터를 만들고 팝업이 떠서 누르니 꺼졌다”고 설명했다. 최신 사용자 세션 `play-iadyml4x`는 첫 로미아스 대화에서 `end/0`이지만 구형 보고서에는 닫기 이벤트와 처리된 VM 오류 이력이 없어 이것만으로 원인을 확정할 수 없다.
- **동일 증상 재현:** 원본의 `cfg_operate` 조작 안내 창(screen 20, 800×190)을 실제 SDL 창 닫기로 닫으면 첫 대화 화면에서 게임 전체가 `end/0`으로 종료된다. 원본 HSP 609517–609546에 창과 경고가 있고 사용자 설정도 `operate.=1`이다. `popup-operation-close-01`은 `window=20 source=window-close onexit=1`, `end_pc_words=273609`, 관찰 오류0을 기록했다. 사용자가 실제 누른 창이 이것이라는 단정은 아니다.
- **네이티브 개선:** `--auxiliary-close-hide`는 screen 20의 창 닫기를 안내 창 숨기기로 처리한다. Python 플레이 런처가 이 옵션을 사용하며 직접 호스트 기본 동작은 참조 호환을 유지한다. 메인 창 닫기·게임 데이터·사용자 설정은 바꾸지 않는다. 새 안내 문구도 표시 계층에서만 바꾼다. 실제 Mac 창 닫기 후 본 게임 지속·이동·인벤토리·저장 종료를 확인했다. 상세/후속 로드 검사는 [PLAYABILITY.md](PLAYABILITY.md) 및 조사 기록을 따른다.
- **진단 보강:** 관찰된 HSP 오류와 PC, 닫기 출처/창 번호, 최근 입력 메타데이터(입력 본문 제외), 팝업 본문·결과와 빌드 식별을 기록한다. `--fail-on-hsp-error`는 명시적 진단용이며 기본 오류 복구 의미는 보존한다. [PLAY_EXIT_INVESTIGATION.md](PLAY_EXIT_INVESTIGATION.md).
- **확대 플레이:** 서로 다른 캐릭터, 집 튜토리얼·작별→북부 티리스→베르니스, 장비 변경·메뉴, 초원 전투·전리품·저장/새 프로세스 로드 8/8 통과. `work/playability/playcov-suite-02/summary.json`과 인벤토리/기술 바이트 비교가 근거다. 합성 입력에 의한 관측이며 장시간 수동 플레이 완료를 뜻하지 않는다.
- **한국어:** 구판 사전7303행은 재사용 조건 미확인이라 private 참고 자료로 유지한다. CC BY-SA 3.0 위키 번역을 현행 JP 문맥과 대조하고 독자 번역을 별도로 작성한다. 용어집223개, 번역 지침·출처 명세, CP932 원문→UTF-8 표시 전용 catalog가 있다. 초기 UI·DB·첫 집·노른 도움말 번역은 진행 중이다. 전체 한글 패치 완료가 아니다. `gmes`의 글자별 출력, 대화의 사전 줄바꿈, 동적 문자열은 문단 경계 구현이 필요하다. [KOREAN_SOURCES.md](KOREAN_SOURCES.md), [KOREAN_TUTORIAL.md](KOREAN_TUTORIAL.md).
- **새 검사:** `build/localization-check` CTest10/10, 한국어 표시 픽셀 변화와 원본 VM/입력·저장 문자열 유지, strict UTF-8 catalog 거부 조건 통과. 한국어 초기 catalog로 신규게임→이동→저장종료 `ko-newgame-01` exit0/4358프레임, 오류·미지원0. 이후 추가된 catalog compiler 검사도 단독 통과. 출시용 Release/Windows 검증 수치는 완료 후 아래에 갱신한다.

다음 인수인계 단위: popup 수정 후 Release/Windows 검사와 저장 재로드, 번역 키 충돌0 확인·실제 UI 검수, 관리 소스 변환을 통한 `gmes`/대화 문단 렌더 경계, 동적 문장·후반 콘텐츠 번역. 기존 원본/개인 세이브에 실험하지 않는다.

이 문서는 계획과 실행 결과를 구분한다. 루트 `AGENTS.md`가 공통 작업 규칙이며 `CLAUDE.md`는 Claude의 진입점이다.

| 단계 | 상태 | 현재 근거 / 다음 작업 |
|---|---|---|
| M0 문서·기준 고정 | 완료 | AGENTS.md, CLAUDE.md, 상세 계획·원본 제외·입력/도구 버전 고정 |
| M1 코드 복원 | 완료·정적 조건 충족 | 전체 raw 618,613줄 / 미식별 토큰 0. 복원 오류 보정 후 2,805,612개 정규화 토큰·분기와 722개 함수 선언 일치 |
| M2 Windows 기준 실행물 | 부분 완료 | 공식 HSP 3.4 x86 전체 재컴파일·작은 실행 비교·원본 언어 선택 화면 확인. 전체 플레이 비교 미완료 |
| M3 네이티브 실행 기반 | 부분 완료 | Mac 의미24개/Win 기존20개·문자열 검사, 공식 x86 AX15개 교차 실행, 양 OS 실제 SDL 창 확인 |
| M4 최소 플레이 구간 | 부분 검증 완료 | 양 OS 신규게임·저장/로드·세이브 교환/상태 비교, Mac 실시간 음악 포함 짧은 플레이 경로 통과. 장시간·실제 IME는 남음 |
| M5 오프라인 기능 완성 | 미시작 | 기능별 호환성·진행도별 검증 필요 |
| M6 배포 후보 | 미시작 | 양 플랫폼 실제 플레이·깨끗한 환경 검사 필요 |

**실험용 개발 빌드이며 전체 플레이 검증은 끝나지 않았다.** Mac 실제 창의 타이틀과 결정적 실행의 신규게임·이동·저장/재시작/로드를 확인했다. 실시간 장시간 플레이, 양 플랫폼 세이브 교환과 전체 게임의 동작 동등성은 추가 검증이 필요하다.

### 최신 통합 검증과 사용자 실행

Claude Opus 5.5 담당자들이 사용량 한도로 멈춘 뒤 root와 후속 Codex 작업자들이 남은 통합을 이어받았다. 실패/부분 완료 보고를 성공으로 바꾸지 않고 추가 실행으로 확인했다.

- **최신 Mac Release 빌드 CTest 7/7**, VM 의미 24개 포함. 근거 `work/root-final-build.log`. `Play ElonaPlus.command` / `tools/play_game.py`는 이 실행물을 실시간 입력·시간 제한 없이 시작한다. 기본 저장 폴더는 `work/player/2.335/user`, 실행 로그는 `sessions/play-*`다. 같은 프로필을 런처로 중복 실행하는 것을 OS 잠금으로 막는다.
- **Windows 실제 SDL 창 및 게임 경로:** `gui-native-02`와 최신 IME/오디오 수명 수정 후 `gui-native-03` 모두 fixture 11개×headless/창, 신규게임·저장·로드, 실제 session 1 타이틀 캡처 통과. 각 실행의 예약 작업 삭제·잔여 프로세스0·원본 트리/ZIP 불변 확인. Unicode 패치도 공유 소스에 적용돼 있다. [WINDOWS_GUI.md](WINDOWS_GUI.md).
- **음악:** MIDI83곡/MP3 1곡 양 OS 오프라인 검사 및 Mac CoreAudio 실제 출력 콜백 검사 통과. 반복·정지·재시작·WAV 동시 재생, Mac/Win 렌더 수치 대조, 고지 병합 완료. `root-realtime-load-01`은 원본 AX를 **실시간 시계/오디오 + 스크립트 입력**으로 불러오기→인벤토리→저장종료(exit0,1158프레임)했고 `music at exit ... position_frames=913408 ... output=device`를 기록했다. 키보드 실사용/장시간 청취 검사로 확대 해석하지 않는다. [AUDIO_IMPLEMENTATION.md](AUDIO_IMPLEMENTATION.md).
- **IME:** 조합·확정·취소·표시, 창별 전달, CP932 한도/삭제, 변환 실패 표시 구현. Mac 합성 SDL 15개 및 HSP headless/숨긴 창 검사 통과. 실제 OS 후보 선택과 원본 ANSI Edit 전체 동등성은 별도 검증 필요. [IME_IMPLEMENTATION.md](IME_IMPLEMENTATION.md).
- **저장 교환:** Mac→Windows, Windows→Mac, 같은 물리적 Mac user root 재시작 모두 성공. 좌표/레벨/맵, 전체 인벤토리369600 bytes/기술273600 bytes 유지. 동일 신규 저장 및 동일 입력 재저장 결과154/154파일 해제 내용 동일. 로드 시 턴+1·시각 진행은 양 OS 동일하게 관측했다. 원본 x86에서 직접 생성한 세이브 가져오기 실측은 아직 없다. [SAVE_COMPATIBILITY.md](SAVE_COMPATIBILITY.md).
- 기존 사용자 preview(PID55255)는 이전 빌드의 elona.dll 그림자 미지원으로 종료된 report가 남아 있었고 root가 종료하지 않았다. 해당 효과가 구현된 최신 Release를 런처로 다시 열어 **언어 선택 창을 확인하고 사용자에게 맡겼다**(PID80246, terminal `term_273963ca-9c69-4a6a-b092-ca54ffcab123`). 이 새 창에 자동 키 입력/종료를 하지 않는다. 증거 `work/reports/mac-player-latest-language.png`.

남은 범위는 실제 일본어 IME 수동 확인, 장시간 실시간 플레이와 후반 콘텐츠, 원본 x86 세이브 수입, Windows 장치 오디오, 일부 미도달 UI/플러그인 및 GDI 픽셀 대조, 배포용 의존성/앱 패키지다. 기본 플레이 가능과 전체 이식 완료를 구분한다.

### 저장·종료·불러오기 체크포인트

- GUI 첫 작업이 완료 보고됐다([GUI_HOST_IMPLEMENTATION.md](GUI_HOST_IMPLEMENTATION.md)). `run27`은 캐릭터 생성 후 좌/아래 이동, 저장·종료를 실행해 exit0/end(4358프레임)로 끝났고, `user/save/sav_Ari`에 파일 154개·총 871,753 bytes가 생겼다. root가 report와 저장 파일을 확인했다.
- `run28`은 `REUSE_USER_FROM=run27`로 **사용자 프로필을 새 run 폴더에 복사**하고 새 프로세스로 로드했다. 물리적으로 같은 user_root를 다시 연 검사는 아니다. root가 복원된 Your Home 맵 캡처(`work/reports/mac-original-loaded-run28.png`)를 확인했다. 종료는 exit124/max-waits(1101프레임)이며 양쪽 report의 first_unsupported/hsp_error는 비어 있다. 원본 ZIP/EXE 전후 해시도 동일하다.
- 이 결과는 결정적 headless 모드의 기본 플레이 경로 검증이다. 실시간 사용자 장시간 플레이, 전 인벤토리/좌표/턴 상태의 기계적 비교, Windows와의 세이브 교환을 모두 증명하지 않는다. 다음 통합 회귀에서 비교 가능한 상태와 경계를 더 확인한다.
- 후속 GUI 작업은 Windows Unicode 패치 적용, root note 수정 반영(24개 의미 검사), 음악 연결, IME와 이름 입력, 실시간 경로 및 저장/로드 회귀다. 이전 fixture 20/20·CTest5/5 보고는 note4개 추가 이전 체크포인트이며 서로 다른 검사 수를 혼동하지 않는다.

### 캐릭터 생성·첫 맵 진입 체크포인트

- GUI 담당의 Release `run26`은 원본 AX를 영어 선택 → 타이틀 → 캐릭터 설정/특성/외형 → 이름 `Ari` → packed `scene1.hsp` 오프닝 → 로미아스 대화 → `Your Home` 맵/HUD/미니맵/로그까지 실행했다. root가 맵 캡처(`work/reports/mac-original-home-run26.png`)와 report, arm64 실행물, 원본 ZIP/EXE 해시 전후 일치를 확인했다.
- 재현: `ELONA_BUILD=build/native-macos-arm64-legacy-cp932-release tools/run_game_trace.sh run26-new --deterministic --headless --max-waits 4300 --script tests/game/newgame_english.script`. 기존 run26은 덮어쓰지 않는다. 이 체크포인트 report는 frames=4301, exit124/max-waits이며 first_unsupported/hsp_error는 비어 있다.
- 이 결과는 자동 입력 기반이며 실제 키보드·IME의 전체 입력 검증이 아니다. 음악 구현은 별도 작업 중이고 현재 run26의 MIDI는 무음이다. 다음 필수 검증은 이동·인벤토리·저장 후 프로세스 재시작·같은 사용자 폴더에서 로드된 상태 비교다.
- Windows Unicode 수정안 `work/orchestration/windows-unicode-host.patch`는 독립 Windows snapshot에서 비ASCII/ASCII 10개, Mac에서 5개 파일 회귀를 통과했다. root가 코드 검토와 현재 소스의 `git apply --check`를 통과시켰다. 공유 GUI 파일의 동시 편집을 피하려고 GUI 담당자에게 적용을 맡겼으며 **공유 소스 적용 완료는 아직 확인하지 않았다**. VM 코어 수정 없이 native path로 AX 바이트를 읽어 `dpm_memfile`/`MEM:start.ax`로 전달한다. 이 snapshot은 note CRLF 수정 이전이었다.
- 플랫폼 담당 후속은 Windows SDL3/SDL3_ttf와 글꼴을 private 의존성으로 준비해 실제 native 창을 검증하는 것이다. 전역 설치·사용자 창 조작·기존 원본 수정은 하지 않는다.

### 타이틀 메뉴 체크포인트와 비교 근거 정정

- GUI 담당자의 `run12`: 정규화 원본 AX를 결정적 headless 모드에서 영어 선택 → 라이선스 안내 → Starting Menu와 물결 효과까지 실행했다. root가 report와 타이틀 캡처(`work/reports/mac-original-title-run12.png`)를 확인했다. 종료는 `max-waits`, exit 124, report frames=1601이고 `first_unsupported`/`hsp_error`는 비어 있다. 원본 ZIP/EXE의 전후 해시 파일도 일치한다.
- 이 실행은 자동 입력·라이선스 dialog 자동 OK를 사용했다. 실제 사용자 입력 전체 경로의 통과를 뜻하지 않는다. HMM 오디오/조이패드 backend 없음은 초기화 실패로 보고하고 MIDI/MP3는 무음과 진단을 남긴다. 동봉 HMM의 초기화 실패/미초기화 반환 계약 검증을 별도로 요청했으며 정상 오디오 완료로 취급하지 않는다.
- 사용자 두 번째 실행 요청으로 최신 빌드를 `work/game-run/user-preview-hojmp4fz`에서 실시간 실행해 언어 선택 화면을 확인했다. 이전 preview는 DMINIT 미지원으로 종료된 상태였다. 새 사용자 실행은 자동 조작하지 않는다.
- 플러그인 비교의 SDK DLL shadowing 발견: `plugins-claude-01`~`04` hspda 결과는 게임 DLL 기준으로 무효이며 `05`의 case-local 런타임 결과를 사용한다. 기본 GUI·파일 및 exrand/hmm/z 결과는 이 문제의 영향 대상이 아니다. 공용 실행기도 수정하고 Windows에서 기본 fixture 16개를 재실행했다. 상세와 기존 varsize 기대값 차이는 [WINDOWS_REFERENCE.md](WINDOWS_REFERENCE.md)를 참조한다.
- 계속할 작업: 캐릭터 생성 → 이동 → 저장/프로세스 재시작/로드, DPM scene 전달과 스트림 쓰기 연결, Windows 경로, 보이는 입력 컨트롤/IME와 음악 구현. 실제 게임 성공 여부는 해당 단계의 관측으로 판단한다.

### 플러그인 완료·파일 통합·note 수정 후속

- 플러그인 담당 완료 보고: Mac ARM64/Windows x64에서 188,307개 검사 통과. 원본 DLL 비교(run `plugins-claude-07`)의 실제 로드 경로·SHA-256 관측 8개를 root가 확인했다. SDK hspda 혼입 문제의 현재 기준은 `07`이다. 계약·범위·비ASCII 정렬 등 남은 제한은 [PLUGIN_IMPLEMENTATION.md](PLUGIN_IMPLEMENTATION.md)에 있다. zlib 원문 고지는 루트 문서와 `licenses/zlib.txt`에 병합했다.
- HMM 원본 비교에서 `DSRELEASE`는 초기화 여부와 무관하게 stat 1, `DSEND`/`DMEND`도 stat 1이다. DirectX INIT 실패 시 cfg_music/cfg_sound=2는 **기본 mm* 오디오 경로로 전환**을 뜻한다. 기존 GUI의 반환값 및 “오디오 끔” 주석 정정을 요청했다. 음악 담당 후속은 portable MIDI/MP3 렌더러 구현이다.
- 통합 파일 회귀에서 Mac 비ASCII game/user/work/AX 경로, 기본 파일 복사, COW/int32/CP932/tombstone/picload와 bmpsave/zOpen 스트림 쓰기가 통과했다. Windows full headless 호스트는 빌드·ASCII 파일 회귀 통과, 비ASCII 시작 경로와 폰트 없는 구성은 실패가 명시됐다. DPM 옵션은 그 관측 후 GUI에 추가됐으므로 현재 옵션을 사용한 재검증 대상으로 남겼다.
- 플랫폼 담당자는 독립 소스 스냅샷에서 Windows Unicode 인수/AX 읽기 최소 수정안을 작성·검증하고 root에게 패치로 제출한다. 공유 GUI/core 소스를 동시에 편집하지 않는다.
- root가 note 줄바꿈 차이를 공식 x86 4개 fixture로 확인해 `ELONA_LEGACY_CP932`에서 Windows CRLF/bare LF 데이터 규칙을 적용했다. 기존 Mac에서 미종결 문자열 뒤 추가 내용 유실까지 재현됐고, 별도 수정 빌드의 24개 의미 검사와 문자열 검사가 통과했다. 사용자 GUI 바이너리는 이 검사를 위해 교체하지 않았다. 세부 기록: [NATIVE_RUNTIME.md](NATIVE_RUNTIME.md).
- 사용자 세 번째 요청으로 기존 테스트 프로필을 사용해 최신 GUI를 실행했고 실제 타이틀 메뉴를 확인했다(`user-preview-q7x_v8_e`, 당시 PID 55255). 이 창에는 자동 입력/종료를 하지 않는다.

### 원본 게임 초기 화면 및 플랫폼 계층 체크포인트

- 사용자 직접 실행 요청으로 `--game work/normalized/2.335/start.ax`를 실시간 입력 모드로 실행했다. root가 실제 `ElonaPlus 2.335` 창의 일본어/영어 선택 화면을 확인했다. 증거: `work/reports/mac-original-language-preview.png`, 실행 정보 `work/game-run/latest-user-preview.json`. 사용자 실행 폴더는 `work/game-run/user-preview-711v_2uc`이며 원본과 분리했다. 창을 띄운 사실은 이후 플레이 성공을 뜻하지 않는다.
- 플랫폼 작업자가 `docs/PLATFORM_IMPLEMENTATION.md`로 완료 보고했다. Mac arm64 / Windows x64에서 CP932 전체표 96,512항목 불일치 0, 인코딩 76 checks, 파일 계층 173/174 checks, HSP 어댑터 71 checks와 실제 VM 파일 E2E를 통과했다. root는 보고서와 현재 연결 코드를 검토했으며 이 완료 보고에 대한 불필요한 전체 재실행은 하지 않았다.
- 파일 E2E는 int32 저장 바이트, CP932 파일명, 삭제 표식의 재시작 유지, 잘못된 쓰기와 chdir의 error 12, 원본 불변을 검증한다. 실제 게임 세이브 호환성 검증과는 별개다.
- GUI 현재 코드에 파일 이벤트와 가상 루트, 종류를 구분하는 resolver 오류가 연결돼 있다. 압축 스트림 쓰기의 부모 디렉터리 준비/삭제 표식 해제, DPM scene 전달, Windows wide 명령줄 경로 처리가 아직 통합 확인 대상이다.
- 플랫폼 담당자에게 통합 호스트의 비ASCII 경로/파일 회귀 및 Windows x64 headless 빌드를 후속 배정했다. GUI 소유 파일은 GUI 담당자가 수정한다. 사용자 직접 실행 창에는 자동 입력·종료를 하지 않는다.

이전 관문은 기존 HSP3Dish를 연결하는 것만으로 실제 2.335의 화면/입력이 성립하지 않는다는 점이었다. 게임은 여러 오프스크린 버퍼에 그리고 `pget`으로 픽셀을 읽으며 외부 효과 함수에 화면 구조를 전달한다. [GUI_FEASIBILITY.md](GUI_FEASIBILITY.md)에 당시 코드 근거가 있다. 현재는 별도 24-bit BGR CPU 픽셀 계층과 SDL3 호스트를 구현해 기본 관문을 통과했고, 실제 게임 호출 연결을 진행한다. 미구현 동작은 계속 명시적으로 실패시킨다.

### Mac GUI 첫 실행 체크포인트

- 실행물: `build/native-macos-arm64-legacy-cp932-debug/elona_gui_host`, root의 `file` 검사에서 Mach-O arm64 확인.
- 작업자 보고: 기존 Homebrew SDL3 3.4.20 / SDL3_ttf 3.2.2 사용. 해당 체크포인트 CTest 4/4 통과(문자열, VM 의미 20개, surface, headless GUI). SDL 없는 별도 빌드도 4/4 통과. 이후 통합 중 검사 수는 늘어날 수 있다.
- root가 `gui-fixtures-window/results.json`을 확인했다. 9개 시나리오가 각각 예상한 결과를 만족했다. 이 중 7개는 exit 0이고, 객체 `mesbox`와 오디오 `mmload`를 호출한 2개는 **예상한 미지원 실패** exit 3이다. 이를 9개 기능 구현 완료로 해석하면 안 된다.
- 7개 성공 시나리오의 실제 SDL renderer 캡처와 CPU 화면 픽셀 비교에서 오차 0. Windows HSP 3.4 x86 기준 파일 9개가 바이트 단위로 일치했다. 검증 범위는 기본 픽셀·색상 키·nearest zoom·Pget·BMP 저장이다.
- root가 `text_wait` 창 캡처를 열어 영문·일본어·한자 표시를 확인했다. MS Gothic 요청에 Hiragino를 사용하므로 Windows 글꼴 픽셀 동등성이나 IME 검증을 의미하지 않는다.
- 증거: `build/native-macos-arm64-legacy-cp932-debug/gui-fixtures-window/results.json`, 같은 폴더 `screens/`, 열람용 PNG `work/reports/mac-gui-text-window.png`.
- 다음 순서: 결정적 입력 최소 회귀 → 공유 플러그인/파일 어댑터 연결 → 별도 work/user 디렉터리에서 원본 AX 실행 → 최초 미지원 호출 해결 → 실제 언어 선택/타이틀 → 캐릭터 생성/이동 → 저장 후 프로세스 재시작/로드. 각 단계의 화면·로그·입력·파일 변경 증거를 남긴다.
- 원본 소스 정적 경로: `start.hsp:173`의 exrand 초기화 후 `label_0470`으로 이동한다. 타이머(`timeBeginPeriod`, `timeGetTime`)와 설정/기본 사용자 파일 I/O가 그래픽 리소스 로딩보다 앞선다. 이 경로는 정적 관찰이며 게임 실행 성공 증거는 아니다.
- 남은 GUI 기능에는 객체 입력/선택, 오디오, dialog, 일부 Windows API가 있다. line/circle·halftone 확대·회전·gradient는 Windows 기준 대조가 더 필요하다. 플러그인·파일 모듈은 구현 중이며 완료 보고 전까지 전체 계약을 검증 완료로 기록하지 않는다.

## 이번 실행의 확인 결과

- 원본 보존형 추출 도구·manifest·정적 외부 함수 조사와 전체 raw 역컴파일을 구현했다. 생성물은 `work/`, upstream은 `.cache/`에 분리했다.
- 원시 복원 소스를 공식 HSP 3.4 x86으로 컴파일했다. 단순 컴파일 성공 후에도 `else` 누락 및 54개 구형 DLL 호출 규약 소실을 발견해 별도 보정 과정을 추가했다. 최신 상태와 정확한 변환 명령은 [WINDOWS_REFERENCE.md](WINDOWS_REFERENCE.md)를 우선한다.
- 두 보정을 합친 최종 참조 AX를 원본과 정적으로 비교했다. 정규화 토큰 값·줄/인수 플래그·실제 분기/라벨 대상 2,805,612개와 함수 선언 722개에서 차이가 없었다. 저장 폭과 단일식 최적화 플래그는 별도 기록하며, 이 결과가 전체 게임 실행 동등성을 증명하지는 않는다.
- Mac ARM64/Windows x64 컴파일러·진단용 VM을 만들었다. CP932 문자 중간의 ASCII 바이트 손상, 논리 경로 분해, 기본 난수의 플랫폼 차이를 제한된 opt-in 패치로 고쳤다.
- 최신 20개 의미 fixture와 실제 C++ 문자열 회귀 검사를 양 플랫폼에서 통과했다. Windows 최신 기본 의미 검증 snapshot은 `19bb9c16737af38d`다.
- 공식 HSP 3.4의 AX 15개를 코드 변경 없이 헤더만 변환해 두 네이티브 VM에서 실행했고 실제 x86 stdout과 일치했다. DLL 사용 fixture는 제외했다. `varsize`는 3.4에 없는 함수라는 첫 fixture의 한계도 기록했다.
- 원본 GUI는 session 1에서 언어 선택 화면까지 확인했다. 키 입력·캐릭터 생성·플레이는 아직 하지 않았다. 관측한 원본 프로세스와 일회성 예약 작업은 정리했다.
- Windows 및 Mac 진단 프로그램은 실제 게임 실행기가 아니다. DLL·화면·입력·저장 미구현을 더미 성공으로 숨기지 않았다.
- 최종 통합 확인: Python 단위 검사 53개 통과, 추출·보정 출력 재실행 동일성 확인, root의 독립 AX 비교도 차이 0. 원본 ZIP/EXE SHA-256은 최초 값과 동일하며 원본·캐시·생성 게임 소스는 Git 제외 상태다. 현재 개발 파일은 작업 트리에 있고 커밋·공개·릴리스는 하지 않았다.

보고서: [EXTRACTION.md](EXTRACTION.md), [DECOMPILATION.md](DECOMPILATION.md), [COMPATIBILITY.md](COMPATIBILITY.md), [NATIVE_RUNTIME.md](NATIVE_RUNTIME.md), [LEGACY_AX.md](LEGACY_AX.md), [WINDOWS_ENVIRONMENT.md](WINDOWS_ENVIRONMENT.md), [PLUGIN_FINDINGS.md](PLUGIN_FINDINGS.md).

## 확정한 제품 목표

- ElonaPlus 2.335를 기준으로 원본의 규칙·콘텐츠·진행 방식을 최대한 그대로 재현한다.
- macOS ARM64와 Windows x64의 네이티브 실행물로 배포한다. 네이티브 프로세스 안에 HSP VM을 내장할 수 있지만 Wine·Rosetta·원본 Windows DLL에 의존하지 않는다.
- Custom-GX는 역설계 및 유지보수 방식의 참고 자료다. Custom-GX 고유 기능·밸런스 옵션을 초기 버전에 통합하지 않는다.
- 기존 원본 2.335 세이브를 복사해서 가져오고, Mac/Windows 이식판 사이에서 공유한다. 원본 게임으로의 역방향 호환은 초기 필수 조건이 아니다.
- 온라인 기능은 비활성 상태를 명시한다. 첫 버전은 오프라인 싱글플레이를 완성한다.
- 모바일·Intel Mac·Linux·게임 규칙 개선은 후속 범위다. 한국어 번역은 2026-11-18 사용자 추가 요청으로 현재 범위에 포함한다.
- 사용자는 공개 프로젝트를 지향하고, 필요한 서브에이전트 생성과 `home/windows` PC의 검증 이용을 허용했다. 해결하기 어려운 문제가 생기면 근거를 남기고 멈춰 보고하도록 요청했다.
- 초기 OS 목표는 macOS 13 이상 ARM64 및 Windows 11 x64다. 실제 검사한 OS·환경은 결과 문서에 별도로 기록한다.
- 더 최신 원본이 나와도 작업 중 기준을 자동 변경하지 않는다. 2.335 기반 검증 체계를 먼저 완성한다.

## 고정한 입력과 참고 자료

| 대상 | 버전 / SHA-256 / 커밋 |
|---|---|
| 원본 ZIP | `elonaplus2.335.zip`, SHA-256 `06b3fd7d523bcc75ff5c3805d9ae6d5a3056e179b815f27698e8923b1ea21391` |
| 원본 EXE | `elonaplus2.335/elonaplus.exe`, 15,181,992 bytes, SHA-256 `7d372ac439788de5288cacf200d46d4352d036aa74ae5715f654aaeab56a1e4e` |
| 복호화된 AX | 14,846,611 bytes, SHA-256 `f1b1404060faa01dc1d3e46b9a58ba0c39cfd185afd33bdafc2287ee3e921d25` |
| 보정한 참조 소스 | `work/decompiled/2.335-reference/start.hsp`, SHA-256 `c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55` |
| 공식 HSP 3.4 재컴파일 AX | 14,823,720 bytes, SHA-256 `cec8809573ed8172033ac86d94709c40289b65398dad1fcc778c394108cf3b72` |
| borscht | `Ruin0x11/borscht` @ `03c8be3398a59d9df445fd5a0c7850b3f2188869` |
| OpenHSP | `onitama/OpenHSP` @ `3dbb8729dad53ee876fc681696c8fe542698860b` |
| Custom-GX 참고 | `JianmengYu/ElonaPlusCustom-GX` @ `aebe0d8b1941ec2b664bbad87659e54cc3b8eb35` (2.32 계열) |

관찰된 사실:

- EXE 및 동봉 DLL은 Windows x86/PE32다.
- PE 섹션 끝 159744에서 DPMX를 확인했다. 멤버는 `scene1.hsp`, `scene2.hsp`, `start.ax` 세 개다.
- `start.ax`는 파일 위치 335381에서 시작하며 암호화되어 있다. 메모리 내 복호화 후 헤더·전체 크기·섹션 범위가 일치했다.
- AX 코드 시작 위치는 96이며 헤더 version 값은 `0x301`이다. 이 값만으로 원본 SDK의 정확한 버전을 단정하지 않는다.
- AX 변수 개수 선언은 2860이다. 코드 영역 12,089,074 bytes, 데이터 영역 2,664,747 bytes다.
- 디버그·라이브러리·함수·매개변수 테이블이 있다. 테이블 선언은 실행 도달 여부를 증명하지 않는다.
- 원본 폴더에 WAV 209개, MIDI 83개, MP3 1개가 있다. 세이브 폴더에는 안내 파일만 있다.
- borscht의 버전별 의미 분석 DB는 2.15R까지다. 이를 2.335에 그대로 적용해 변수·라벨 이름을 확정하면 안 된다.
- 현재 OpenHSP는 비Windows 환경에서 HSPUTF8을 활성화한다. 원본의 CP932 의미 보존은 별도 검증 대상이다.

참고: [Custom-GX README](https://github.com/JianmengYu/ElonaPlusCustom-GX/blob/work/README.md), [borscht](https://github.com/Ruin0x11/borscht), [OpenHSP](https://github.com/onitama/OpenHSP), [SDL3](https://wiki.libsdl.org/SDL3/FrontPage).

## 구조와 인터페이스

### 공통 런타임

C++17 + CMake + OpenHSP VM 코어 + SDL3를 기본 경로로 선택한다. 게임을 새 언어·엔진으로 전면 재작성하지 않는다. 게임 로직과 플랫폼 기능을 분리하고 2.335에서 실제로 필요한 HSP 명령과 플러그인 동작을 구현한다.

초기에는 추출한 원본 바이트코드로 런타임 문제와 코드 복원 문제를 분리한다. 복원 소스에서 재빌드한 바이트코드가 동일한 검증을 통과하면 관리 대상 게임 실행물로 전환한다. 범용 Windows 에뮬레이터나 모든 HSP 프로그램 지원이 목표가 아니다.

### 초기 호환성 관문

1. 96-byte 구형 AX 헤더를 명시적으로 읽고, 최신 OpenHSP 구조체의 추가 필드를 원본 코드 영역에서 잘못 읽지 않게 한다.
2. 게임의 int32·실수·배열 직렬화와 64-bit 호스트 포인터/핸들을 분리한다. `peek/poke`, `memcpy`, 포인터 전달을 조사한다.
3. 내부 CP932 문자열 연산의 바이트 의미를 보존하고 UI·파일시스템 경계에서 변환한다. 플랫폼별 문자열 구현의 차이를 비교한다.
4. HSP 기본 난수와 exrand를 각각 고정 seed·호출 순서로 비교한다. 임의로 표준 난수 생성기로 교체하지 않는다.
5. 플러그인의 실제 반환값·오류·배열 변경을 구현한다. 미구현 동작은 호출명·위치와 함께 명확히 실패한다.

### 기능별 구현

| 기능 | 방향 |
|---|---|
| 그래픽 | HSP 버퍼·색상 키·복사·회전 의미를 구현한 뒤 SDL3로 표시. 먼저 정확성, 이후 최적화 |
| 입력 | 키 코드·반복·조합키·마우스·문자 입력/IME를 구분해 변환 |
| 글꼴 | SDL3_ttf + Noto Sans Mono CJK JP 기본 글꼴과 사용자 설정. 원본과 줄바꿈·잘림 비교 |
| 음원 | SDL3_mixer로 WAV/MP3, FluidSynth + 고정 GeneralUser GS SoundFont로 MIDI. 출처/고지 포함 |
| 압축·저장 | z.hpi의 실제 동작과 파일 형식을 확인 후 재현. 확장자만 보고 ZIP으로 가정하지 않음 |
| DLL 기능 | exrand/hspda/elona.dll/water.hpi 등 사용 동작을 공통 C++ 코드로 구현 |
| Windows API | 사용 동작에 해당하는 좁은 호환 함수로 대체. PE DLL 로드 금지 |
| 네트워크 | 초기 버전에서 비활성화, 성공 응답을 위조하지 않음 |

UI 글리프와 MIDI 음색의 완전 일치는 목표가 아니다. 문자·배치·음악 선택/반복/정지 등 기능적 결과와 알려진 시각·음색 차이를 구분한다.

### 데이터 접근과 저장

- 원본 설치 폴더를 지정해서 사용하는 배포 방식으로 시작한다. 원본은 읽기 전용이며 앱 번들 내부에 세이브를 쓰지 않는다.
- 설정·세이브·캐시·로그는 플랫폼별 사용자 데이터 폴더에 분리한다.
- 최소 실행 옵션: `--game-dir`, `--user-dir`, `--import-save`. GUI에도 폴더 선택/가져오기 경로를 제공한다.
- 논리 경로와 OS 경로를 분리해 공백·한글·일본어 루트 경로를 지원한다. 파일명 대소문자와 경로 구분자 차이를 검증한다.
- 기존 세이브는 별도 슬롯으로 복사한다. 원본 세이브에 쓰지 않는다.
- 원본 데이터 구조를 우선 보존하고 이식판 메타데이터는 별도로 둔다.
- 임시 저장 후 교체해 실패 시 이전 세이브를 유지한다. 플랫폼 간 폴더 복사로 이어서 플레이할 수 있어야 한다.

## 마일스톤과 완료 조건

### M0 — 작업 기준 및 문서

- 공통 지침·Claude 진입점·상세 계획·원본 제외 규칙 작성.
- 원본 해시와 의존성 커밋 고정. 분석 결과·생성 코드·개인 데이터를 구분.
- 완료: 다른 에이전트가 문서만으로 목표·현재 상태·다음 작업을 이해할 수 있음.

### M1 — 재현 가능한 추출 및 코드 복원

- PE/DPM 추출, AX 복호화·검증, 라이브러리/명령 목록과 manifest 생성.
- borscht 고정 버전을 빌드하고 raw decode를 수행한다. 의미 분석은 별도 단계로 둔다.
- 2.335용 매핑과 필요한 디코더 수정은 재현 가능한 패치로 남긴다.
- 완료: 입력과 출력이 식별 가능하고 전체 코드 영역의 해석 상태 및 미해석 명령이 명확함. 재실행 가능.
- raw decode 성공은 컴파일 가능·게임 동등성 검증을 의미하지 않는다.

### M2 — Windows 참조 실행물

- `home/windows`의 OS·도구·실행 세션을 조사하고 전용 작업 경로를 정한다.
- 복원 소스를 Windows x86 기준으로 재빌드하고 원본과 비교한다.
- 새 게임·이동·대화·전투·저장/로드를 원본과 비교한다.
- 완료: 복원 과정에서 발생한 알려진 동작 차이가 없고 테스트 기록이 있음.

### M3 — 네이티브 기반

- ARM64/x64 코어 및 작은 HSP 의미 테스트를 구축한다. 테스트에 성공한 코드만 통합한다.
- 구형 헤더·CP932·int32·배열·난수·핸들 관문을 검증한다.
- 완료: 양 플랫폼에서 같은 의미 테스트 통과. headless 성공을 GUI 게임 완료로 해석하지 않음.

### M4 — 최소 플레이 구간

- 타이틀 → 캐릭터 생성 → 시작 지역 → 이동/대화/인벤토리 → 저장 → 종료/재시작.
- 완료: 양 플랫폼 실기 플레이 및 세이브 왕복 교환 통과. M2·M3가 선행 조건.

### M5 — 오프라인 기능 확대

- 월드맵·마을·던전·전투·AI·펫·상점·퀘스트·성장·주요 시나리오를 기능표로 추적한다.
- 진행도별 세이브와 대표 경로로 비교한다. 테스트 자료가 없는 범위는 미검증으로 표시한다.
- 완료: 필요한 명령/플러그인 구현 누락이 없고 대표 게임 흐름이 원본과 일치함.

### M6 — 배포 후보

- ARM64 .app 및 x64 Windows 패키지, 원본 폴더 지정/설치 안내, 진단 로그와 고지 정리.
- 개발 도구 없는 환경에서 설치·실행·저장·재시작 검증.
- 완료: 양 플랫폼 실기 검증 및 최소 30분 플레이, 세이브 회귀 검사 통과.

## 검증 전략

| 범위 | 핵심 검사 |
|---|---|
| 원본 보존 | 작업 전후 ZIP/EXE 해시 동일 |
| 추출기 | 잘못된 입력·잘린 헤더·범위 초과·경로 탈출·중복/겹침·다른 버전 오류 |
| 실행 의미 | 산술·배열·분기·함수·CP932 문자열·난수 및 에러 동작 |
| UI | 투명색·버퍼·회전·Retina·해상도·키 반복·IME |
| 오디오 | 포맷별 재생·반복·정지·음량·장면 전환 |
| 저장 | 원본 가져오기·Mac→Windows→Mac·반복 저장·손상·중단 처리 |
| 게임 | 신규 게임·지역 이동·전투·펫·거래·퀘스트·주요 이벤트 |
| 패키지 | 공백/유니코드 경로·읽기 전용 원본·개발 도구 없는 설치 |

게임 상태는 능력치·아이템·위치·펫·퀘스트 플래그 등 의미 있는 값으로 비교한다. 시간·로그 같은 비결정적 값은 분리한다. CI는 원본 자산 없는 작은 테스트와 빌드를 담당하고 전체 데이터/GUI 검증은 실제 Mac·Windows에서 수행한다. 결과에는 OS·CPU·커밋·원본 해시·명령·실행 여부를 남긴다.

## 의사결정과 중단 기준

- 복원 Windows 빌드가 다르면 네이티브 호환 문제와 섞지 말고 복원부터 해결한다.
- OpenHSP 문제가 발생하면 최소 재현과 작고 검증 가능한 코어 패치를 우선한다.
- 원본 API 동작을 확인하지 못한 상태에서 더미 성공을 넣어 진행하지 않는다.
- 장기적인 엔진 전면 교체나 다수 플러그인 의미 재구성이 필요한 차단 요인이 확인되면 자료를 보존하고 사용자에게 보고한다.
- 버전별 업데이트는 별도 원본 보존 → 차이 분석 → 매핑/호환 계층 갱신 → 회귀 검증 순서다.

## 이번 작업의 담당 범위

- root: 공통 문서, Windows SSH 조사/검증, 통합 검토, 최종 인수인계.
- extractor: Python 추출/manifest 도구, 경계 검사 테스트, EXTRACTION.md.
- decompiler: borscht bootstrap/패치/raw decode, DECOMPILATION.md.
- native_runtime: OpenHSP bootstrap/패치/CMake/작은 네이티브 실행 테스트, NATIVE_RUNTIME.md.

이 담당 범위의 작업은 마무리됐으며, 다음 작업자는 인수인계 문서와 현재 파일 상태를 기준으로 새 소유 범위를 정한다.

## 다음 에이전트의 시작점

1. 이 문서 상단 상태와 각 기술 보고서의 실제 결과를 읽는다.
2. `git status --short`로 진행 중인 변경을 확인한다.
3. 보고서에 기록된 명령으로 기존 결과를 필요한 범위에서 재현한다.
4. 미검증 항목 또는 차단 요인부터 진행하며 이미 완료한 분석을 반복하지 않는다.
5. 턴 종료 시 이 문서 상단의 상태·근거·다음 작업을 갱신한다.

### 바로 이어서 할 구체적인 작업

1. [GUI_FEASIBILITY.md](GUI_FEASIBILITY.md)의 독립 그래픽 fixture 4개를 공식 x86 GUI 참조에서 실행한다. 현재 이 네 fixture는 컴파일만 검증했다. `WINDOWS_ENVIRONMENT.md`의 session 1 관측 방식과 전용 scratch 폴더를 재사용한다.
2. 각 HSP buffer가 실제 픽셀 데이터를 보유하는 공통 Surface를 구현한다. 먼저 16×16 RGB 패턴에서 buffer→screen→buffer 복사, clipping, pget, ginfo와 색상 키가 참조와 일치하는지 양 플랫폼에서 비교한다. CPU 픽셀 저장소 + SDL3 출력이 우선 검토할 구조다.
3. 병렬로 HMM 상태 비트·exrand·gzip 스트림을 작은 x86 fixture로 확정하고, 게임 int32와 호스트 포인터를 분리한 어댑터를 만든다. 정적 분석 근거는 `PLUGIN_FINDINGS.md`에 있다. DLL 반환값/오류와 배열 변경까지 비교한다.
4. CP932↔Unicode·IME·파일 선택·mesbox/apledit 경계를 구현한다. 원본 리소스 읽기와 사용자 상태 쓰기를 분리하고 원본 세이브 대신 복사본으로 검사한다.
5. 이 계층들이 준비된 후 제한된 전체 게임 실행 추적과 M2의 원본/복원 플레이 비교, M4의 신규 게임·저장/로드로 진행한다. 현재 VM의 내부 파일 I/O를 격리하지 않은 채 원본 AX를 실행하지 않는다.

재사용할 결과는 20개 네이티브 의미 fixture, 공식 AX 15개 비교, 원본/보정 AX 정적 비교 및 파일별 manifest다. 게임의 618,613줄을 새 언어로 전면 재작성하는 계획으로 바꾸지 않는다. 원본·캐시·생성 소스·개인 데이터를 Git에 넣거나 공개하지 않은 상태이며 공개 저장소 생성/배포는 별도 후속 작업이다.
