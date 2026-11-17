# 게임 진입 직후 종료 조사 — 2026-11-18

## 판정

사용자의 과거 실행 원인은 아직 확정할 수 없지만, 후속 조사에서 **첫 맵에 나타나는 초보자 조작 안내 창을 닫으면 게임 전체가 종료되는 경로를 실제 macOS 창으로 재현했다**. 사용자 설명과 최종 화면에 맞는 유력한 경로다. 당시 로그에는 닫힌 창 번호가 없으므로 그 클릭이 실제 사용자 사건이었다고 단정하지 않는다. 아래 최초 자동 검사와 후속 팝업 검사를 구분한다.

실제 사용자 세이브와 원본 폴더에는 쓰지 않았다. 모든 재현은 `work/game-run/exit-*`의 새 사용자 루트 또는 세이브 복사본에서 수행했다. 원본 ZIP/EXE는 실행 전후 SHA-256이 같았다. 이 문서의 게임 실행은 macOS ARM64에서 수행했으며, 새 진단 회귀의 Windows 실행은 아직 하지 않았다.

## 후속: 조작 안내 창 종료 재현과 네이티브 옵션

사용자가 "캐릭터 만들고 팝업/메시지를 누르니 꺼졌다"고 추가 설명한 뒤, 한국어 표시 감사 중 조작 안내 창의 경고문을 발견했다. 복원 소스 609517행의 `cfg_operate` 분기는 `screen 20,800,190`을 생성한다. 609523행에는 이 창을 닫으면 게임도 닫힌다는 영어/일본어 경고가 있다. 실제 사용자 설정을 읽어 `operate. "1"`임을 확인했으며 값을 변경하지 않았다.

이것은 `dialog` 명령으로 띄우는 OK 팝업이 아니라 **별도의 게임 창 20**이다. 모든 창의 닫기 이벤트를 게임의 전역 `onexit`로 보내는 원래 경로에서 공통 정리 후 `end`한다.

| 실제 검사 | 결과 | 근거 |
|---|---|---|
| 사용자 테스트 복사본 로드, 실제 조작 안내 창의 macOS 닫기 버튼 | 게임 전체가 종료. `window=20`, `source=window-close`, `close_requests=1`, `end_pc_words=273609`, exit 0/end, 프레임 776, 관찰 오류 0. 마지막 메인 화면은 첫 로미아스 대화 | `work/game-run/popup-operation-close-01/report.json`, `operation-help.png`, `screen0.bmp` |
| 수정 옵션으로 별도 Ari 테스트 프로필 로드, 실제 안내 창 닫기 | 프레임 983에 안내 창만 숨김. 메인 창 유지, 이동 `(12,17)→(12,18)`, 인벤토리 열기, 정상 저장·종료 프레임 1688. `hidden_help_close_requests=1`, 관찰 오류/미지원 호출 없음 | `work/game-run/popup-operation-fixed-01/report.json`, `help-before-close.png`, `inventory-after-move.png`, `saved.png` |
| 수정 후 저장을 새 프로세스에서 로드·재저장 | 이름/맵/좌표/레벨, 플레이어 인벤토리와 전체 인벤토리 369,600 bytes, 기술 273,600 bytes 유지. 로드에 따른 턴 +1 | `work/game-run/popup-operation-reload-01`, `work/modal-investigation/fixed-save-roundtrip.json` |

`--auxiliary-close-hide`는 **screen 20의 닫기만 숨기기로 바꾸는 opt-in 호스트 옵션**이다. 직접 호스트를 실행할 때 기본값은 꺼져 있어 원본 비교 의미가 유지된다. 메인 창 닫기와 애플리케이션 Quit는 기존 `onexit`로 전달한다. 원본 AX·VM 변수·사용자 설정 파일을 변경하지 않는다. 안내 창의 정확한 기존 경고 한 문장만 표시 단계에서 현재 동작에 맞게 바꾸며, 다른 창의 같은 문자열은 그대로 표시한다.

기능 회귀 `tests/graphics/run_operation_help_close.py`는 3개 경로를 검사했다: 옵션 없이 안내 창을 닫으면 기존 종료, 옵션을 켜고 안내 창을 닫으면 계속한 뒤 메인 창 닫기로 종료, 옵션을 켠 메인 창 직접 닫기는 기존 종료. 원본 VM 문자열 파일과 메인 창 BMP는 같고 안내 창 BMP만 바뀌는 것도 확인했다. 결과 `work/modal-investigation/help-close-fixture-01/results.json`.

실제 클릭은 [orca-computer-use 스킬](/Users/shlifedev/.agents/skills/computer-use/SKILL.md)의 백그라운드 접근성 동작으로 **소유한 테스트 PID/창만** 대상으로 했다. 원인 재현용 PID 96790과 수정 검증 PID 98245는 모두 종료됐으며 사용자 게임 창에는 입력하지 않았다. 수정된 경로는 macOS에서 실측했고 Windows 실제 닫기 버튼을 누르는 추가 동작은 재검증하지는 않았다. Windows x64에서도 합성 창 닫기 fixture로 옵션 없이 frame 5 종료, 옵션 사용 시 안내 창을 숨기고 frame 10 메인 창 종료까지 계속, VM 원문/메인 창 픽셀 불변을 확인했다(`gui-native-04/localization-check/verify.json`).

## 후속: 시스템 팝업 계약과 기록 보완

- 실제 macOS SDL 모달 버튼을 OK → Yes → No → 경고 OK 순서로 눌렀다. HSP `stat`는 1/6/7/1, `onexit_count=0`, `close_requests=0`, 관찰 오류 0이었다. 기본 버튼 매핑이 게임 전체를 닫는 문제는 재현되지 않았다. `work/modal-investigation/fixture-01/modal-results.txt`, `report.json`, 버튼별 PNG가 근거다.
- 최신 사용자 세션에는 시작 시 이용약관 `dialog`만 기록됐다. 정상 신규 생성→첫 대화 경로에 별도 HSP 모달 팝업은 관찰되지 않았다. 게임 오류 처리부는 `error.txt`를 쓰고 외부 열기를 요청하며, 24210행의 `dialog` 분기는 비활성이다. `RUNMODE_EXITRUN`의 원인이 되는 HSP `run` 명령은 복원 게임 소스에 없다.
- macOS 진단 보고서의 `elona_gui_host` 충돌 6건을 확인했다. 모두 2026-11-18 00:00:25–00:00:52의 시작 단계 `dpm_ini→hsp3ext_getdir→unsupported→code_getdebug_line` null 접근이다. 이번 사용자 보고서 종료 시각은 08:00:46이며 첫 맵에 도달했다. 이전 시작 단계 충돌을 이번 사용자 종료의 원인으로 연결하지 않았다.
- 보고서에 `host_build`(컴파일 시각/아키텍처/진단 스키마), 원시 `vm_return_mode`, `dialog_count`, 최근 64개 `dialogs`를 추가했다. 팝업의 전체 본문/제목·mode·선택 결과·프레임·VM 위치·실제/자동 입력 여부·완료 여부·오류를 private report에 기록한다. 시작/종료는 stderr에도 남겨 실행 중 닫힌 위치를 좁힌다.
- `SDL_ShowMessageBox` 실패 시 실제 `SDL_GetError()`를 보존한다. headless에서 실제 팝업을 요청한 실패도 정확한 이유와 `completed=false`를 남기는 것을 검사했다. 반환값/게임 오류 처리 의미는 바꾸지 않았다.
- 기존 종료/복구 fixture 6/6과 새 모달 계약 회귀가 통과했다. `work/modal-investigation/exit-regression-01/results.json`, `contract-auto-03/results.json`, `no-interactive-ui/report.json`.
- 번역 카탈로그를 요청했는데 텍스트 렌더러 없이 빌드한 경우 exit 2와 명확한 진단으로 시작을 거부한다. SDL/TTF 없는 호스트의 기존 비번역 실행 의미는 유지한다. 실제 존재하는 runtime.tsv로 거부를 재확인하고 같은 no-TTF 호스트의 비번역 모달 fixture가 exit 0인 것도 확인했다: `work/modal-investigation/no-ttf-confirmation/{translation,plain}/stderr.log`.

## 과거 세션에서 확인한 사실

- `work/player/2.335/sessions/play-iadyml4x/report.json`: 프레임 2569, `exit_code=0`, `exit_reason=end`, 미지원 호출과 최종 HSP 오류 문자열은 비어 있었다.
- 최종 BMP를 확인하면 저장·종료 화면이 아니라 **Your Home의 첫 로미아스 대화**다. 검토용 PNG는 `work/reports/play-iadyml4x-final.png`다.
- stderr에 CP932로 표현할 수 없는 입력 거부 2회가 있으나, 이 메시지와 종료의 인과관계는 확인되지 않았다.
- 이 세션의 기존 보고서는 창 닫기 요청과 게임 `onerror`가 처리한 오류를 기록하지 않았다. 따라서 `end/0`만으로 해당 사건을 설명할 수 없다.
- 사용자 자동 세이브 복사본은 정상 로드되며, 최초 위치 `(18,11)`, 레벨 1, 턴 카운터 0 상태에서 첫 대화에 도달한다.

## 소스 관찰과 재현으로 확인한 보고서의 사각지대

읽기 전용 복원 소스 `work/decompiled/2.335-reference/start.hsp` 기준:

1. 23737행의 `onexit`는 공통 정리 루틴으로 이동한다. 이 루틴은 24067행에서 `end`한다. 기존 호스트는 등록된 `onexit`로 닫기 요청을 전달할 때 종료 원인을 기록하지 않았으므로, 실제 닫기 요청도 `end/0`으로 보고될 수 있었다.
2. 24068행부터의 오류 처리 루틴은 처리 중 오류가 반복되면 같은 공통 정리 루틴으로 이동한다. 그러면 최종 결과는 `end/0`일 수 있다. 원래 오류는 OpenHSP의 `onerror` 전달 전에 관찰해야 한다.
3. 정상 저장·종료도 같은 정리 루틴을 사용한다. 따라서 최종 `end` 위치만으로도 원인을 구분할 수 없다.

이는 추측만은 아니다. 작은 HSP fixture에서 나눗셈 오류 3회를 `onerror`로 받은 뒤 정상 코드 0으로 끝나는 상황을 재현했다. 또한 사용자 세이브 복사본을 첫 대화까지 로드한 후 합성 닫기 요청을 보내면 `exit_code=0`, `exit_reason=end`, `end_pc_words=273609`가 나왔다. 새 보고서는 이 경우 `close_requests=1`을 보존한다. **이 실험은 사용자가 실제로 닫기 요청을 보냈다는 증거가 아니다.**

## 구현

`src/app/gui_host.cpp/.h`에 다음 진단을 추가했다.

- `observed_hsp_error_count`: 원래 `onerror` 전달 직전에 관찰한 오류 수.
- `observed_hsp_errors`: 처음 32개 오류의 코드·디버그 행·VM 코드 위치·프레임. 이후 오류는 총수만 증가한다.
- `end_pc_words`: 실제 `end` 명령 위치. 원본 AX의 디버그 행 정보가 없을 때도 코드 위치를 남긴다.
- `close_requests`: 게임의 `onexit` 처리 여부와 무관한 닫기 요청 수. 로그에는 SDL 애플리케이션 Quit·개별 창 Close·합성 스크립트 중 이벤트 출처도 남긴다.
- `recent_input_events`: `--trace-commands`일 때 최근 64개 입력의 종류·키·프레임·텍스트 바이트 수. 이름과 IME 입력 내용은 저장하지 않는다.
- 입력 스크립트의 `close [window-id]`: 운영체제 닫기와 같은 호스트 이벤트 경로를 사용한다. 게임이 취소할 수 있으며 강제 종료하지 않는다.
- `--fail-on-hsp-error`: 진단용으로만 원래 `onerror`가 실행되기 전에 첫 오류에서 정지한다. **기본값은 꺼짐**이며 정상 복구를 포함한 원래 오류 처리 의미는 유지한다. 과거에 처리된 오류가 있었다는 이유만으로 나중의 정상 종료를 실패로 바꾸지 않는다.

VM 코어·원본 AX·게임 내용은 수정하지 않았다. `src/app/main.cpp`의 진단 플래그 연결은 통합 담당자가 추가했다.

## 검증 결과

| 검사 | 실측 결과 | 근거 |
|---|---|---|
| 실제 VM 오류/종료 회귀 | 6/6 통과: 재귀 오류 후 `end 0`, 오류 복구 후 `end 7`, strict 모드 첫 오류 중지 2건, `onexit goto` 종료, `onexit gosub` 취소 후 계속 | `work/exit-diagnostics-test-04/results.json` |
| SDL 이벤트 경계 | 자체 숨김 창에서 합성 IME 및 창 Close/앱 Quit 출처 구별 20개 검사 통과 | `build/exit-investigation/elona_sdl_ime_tests` |
| 기존 그래픽 fixture | 11/11, Windows HSP 3.4 x86 BMP 9개 바이트 동일 | `build/exit-investigation/gui-fixtures-exit-02/results.json` |
| 합성 IME 편집 회귀 | 조합/확정/취소/CP932 거부 검사 통과 | `work/exit-investigation/ime-regression-01/results.json` |
| 사용자 세이브 복사본 로드 | 첫 대화에서 1501프레임 제한까지 오류·조기 종료 없음 | `work/game-run/exit-user-load-01` |
| 첫 대화 입력 변형 | A/Return/Escape/Space 반복 4건 모두 1001프레임 제한 도달, 오류·조기 종료 없음 | `work/game-run/exit-key-{a,return,escape,space}-01` |
| 새 게임 난수 시드 변형 | 8건 모두 캐릭터 생성→오프닝→첫 대화를 지나 3051프레임 제한 도달; strict 오류 0 | `work/exit-investigation/newgame-seeds.json` |
| 실시간 SDL 신규 게임 | 실제 시계·숨김 SDL 창·음악 장치 출력으로 생성→맵→이동→저장·종료, 4358프레임, 관찰 오류 0·닫기 요청 0 | `work/game-run/exit-realtime-newgame-01` |
| 첫 대화에서 닫기 요청 | 원래 `onexit`가 공통 정리 루틴에서 `end/0`; 새 보고서에는 요청 1건 보존 | `work/game-run/exit-user-close-01` |

실시간 SDL 검증은 합성 입력을 사용했으며, 실제 사용자의 키보드·OS IME 조작을 재현한 검사는 아니다. 실시간 실행은 진단 구현 중간 빌드라 오류 목록은 있지만 최종 총수 필드가 아직 없었다. 난수 시드 검사와 fixture는 최종 진단 동작으로 재검증했다. 프레임 제한의 종료 코드 124는 테스트 종료이지 게임 충돌이 아니다.

## 재현 명령

빌드 및 원본 자산 없이 실행하는 회귀:

```sh
cmake -S . -B build/exit-investigation -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DELONA_LEGACY_CP932=ON -DELONA_LEGACY_RNG=ON \
  -DELONA_GUI=ON -DELONA_TEXT_TTF=ON -DBUILD_TESTING=ON
cmake --build build/exit-investigation --target elona_gui_host elona_hspcmp -j 6
python3 tests/graphics/run_exit_diagnostics.py \
  --compiler build/exit-investigation/elona_hspcmp \
  --host build/exit-investigation/elona_gui_host \
  --common .cache/upstream/OpenHSP/common \
  --work build/exit-investigation/exit-diagnostics --unique
```

새 이름을 지정하는 원본 게임 진단 실행 예:

```sh
ELONA_BUILD=build/exit-investigation tools/run_game_trace.sh exit-check-next \
  --deterministic --headless --max-waits 3050 --timeout-ms 60000 \
  --script tests/game/newgame_english.script --fail-on-hsp-error
```

기존 실행 폴더는 덮어쓰지 않는다. 세이브를 사용할 때도 `REUSE_USER_FROM`으로 기존 진단 실행의 사용자 루트를 **복사**한다. 실제 플레이어 루트에 위 시험 스크립트를 실행하지 않는다.

입력 해시 기록: `work/exit-investigation/inputs.sha256.json`, 닫기 이벤트 출처를 추가한 최종 빌드는 `work/exit-investigation/inputs-final.sha256.json`. 원본 ZIP SHA-256 `06b3fd7d523bcc75ff5c3805d9ae6d5a3056e179b815f27698e8923b1ea21391`, 원본 EXE `7d372ac439788de5288cacf200d46d4352d036aa74ae5715f654aaeab56a1e4e`, 정규화 AX `5c941a6728066a439f4397e21431b3b3fb579d49e397eb7425665d88ddddf4bc`.

## 다음 판정 기준

같은 문제가 다시 발생하면 새 세션의 최종 화면·첫 관찰 오류·닫기 요청·입력 메타데이터를 함께 확인한다. 오류가 잡히면 해당 VM 위치의 최소 재현과 호환 계층 수정으로 이어간다. 닫기 요청이 잡히면 그 요청이 실제 창 조작인지 SDL/플랫폼 이벤트인지 별도로 확인한다. 어느 쪽도 없으면 저장·종료 및 공통 정리 루틴으로 향한 게임 분기 경로를 좁힌다. 현재 자료만으로 특정 원인을 고쳤다고 보고하지 않는다.
