# ElonaPlus 2.335 Astra 6.2 재검수

2026-11-18에 커밋 `01754e3cbfe23f868d8b22708d21dea6221262ca`를 검수했다. Astra 6.2 병렬 작업자 세 명이 호스트·파일 처리, 한국어 표시, Windows x64를 각각 맡았다. Mac에서는 새 Release 빌드로 기존 플레이 경로와 사용자가 실행하는 한국어 런처의 옵션을 따로 검사했다.

## 수정 후 결과

사용자의 후속 요청에 따라 아래 검수에서 발견한 결함을 수정하고 더블클릭 런처의 Mac Release를 다시 빌드했다. 다음 수치는 수정 후의 측정이다. 뒤쪽의 재현 기록은 수정 전 커밋을 설명한다.

| 항목 | 수정과 재검증 |
| --- | --- |
| 전체 화면 시작 및 복원 | `2e4f93a`, 통합 `d6b39e6`. 호스트가 HSP의 `chgdisp` 요청을 SDL 데스크톱 전체 화면과 게임의 논리 해상도에 반영한다. 사용 중인 config의 바이트 그대로 복사한 프로필과 기본 한국어 런처 실행물로 1280×768, SDL 전체 화면 플래그 true, 801프레임, 관찰 오류 및 미지원 0건. 이 실행은 자동 입력용 숨긴 SDL 창으로 프레임 한도에서 종료했다. 보이는 Mac SDL 창에서도 전환과 복원을 별도 검사했다. |
| 세이브 가져오기 | `5a6fa3b`. 원본의 파일 종류를 유지하고 호출별 전용 임시 디렉터리에 복사·검증한 뒤 기존 슬롯을 덮어쓰지 않는 원자적 연산으로 공개한다. `表` 파일의 종류·내용·SHA, 기존 표식 파일 보존, 동시 가져오기를 검사했다. Mac·Windows 파일 계층 검사와 합성 세이브 회귀 통과. |
| 한국어 번역 | `5a6fa3b`. 공개 소스에 버전과 입력 판정값을 고정했다. 빈 작업공간에서 동일한 TSV와 두 보고서를 재생성했다. 단독 `猫`는 ‘고양이’로 표시하고 미등록 문장 `猫舌`의 글자 조각은 원문 픽셀을 유지한다. 기본 한국어 런처는 준비된 관리 AX의 해시를 확인해 문단 번역도 적용한다. |
| 검증 도구 | `44e524d`, `11abec5`. Mac 영어 플레이 8단계의 91개 화면·예상 입력 시점·복구된 VM 오류·저장 상태를 검사한다. 일본어 참조, 대체 표시, 한국어 각각에서 노른 도움말 6페이지의 지정 본문 영역을 OS별 기준과 비교한다. 화면 누락·교체와 복구된 VM 오류를 각각 주입했을 때 해당 검사가 실패했다. |
| Windows 실제 창 | `d6256c6`. Win32 창 표면으로 프레임을 표시하도록 기본값을 바꾸고 창 캡처를 타이틀과 비교한다. 언어 선택 화면이 남았던 이전 실행은 같은 판정에서 4.5%, 최신 실행은 97.9%가 일치했다. [Windows 검증 기록](WINDOWS_GUI.md) |
| Mac 런타임 의존성 | `b5a6a43`. Release가 고정된 SDL·SDL_ttf 공식 프레임워크를 로드한다. ARM64 Mach-O의 최소 버전은 호스트 13.0, 두 프레임워크 모두 11.0이다. 실제 macOS 13 장치에서의 실행은 별도 검증이 남는다. |

**최종 실행 증거**

- Mac 더블클릭 런처 Release에서 CTest 12/12(세이브 파일 계층 포함), 영어 플레이 8/8·화면 91/91, 전체 번역표 5,058키 글리프 검사 통과. `work/astra62-fixes/launcher-ctest.log`, `work/playability/astra62-launcher-final/summary.json`, `work/astra62-fixes/release-font.log`.
- Mac 한국어 런처와 실사용 config 복사본의 창 플래그와 렌더러 캡처는 `work/astra62-fixes/actual-launcher/`에 있다. 별도 선택 언어 실험에서는 일본어·영어 모두 캐릭터 생성과 집 저장 종료까지 확인했다. `work/review-ko-fix/fullscreen-language/{jp,en-final}/report.json`.
- Mac 및 Windows의 결정적 일본어 기반 게임에서는 OS별 참조·대체 표시·한국어 3가지로 신규 게임, 재로드, 북부 티리스를 각각 검사했다. 세이브 154/154/162파일의 압축 해제 내용과 본문 18개 모두 기대값과 일치했다. `work/astra62-fixes/launcher-norne/summary.json`, `work/windows/reference-build/gui-native-fix62-managed-final/verify.json`.
- 원격 Windows 기본값으로 실행한 창과 헤드리스 fixture 22개, 실제 창 타이틀, 신규 게임과 세이브 재로드가 통과했다. 예약 작업과 실행 프로세스는 정리됐다. `work/windows/reference-build/gui-native-fix62-final/verify.json`.
- Windows 최종 실행물로 헤드리스와 세션 1의 실제 창에서 전체 화면 전환·복원을 검사했다. 각각 `chgdisp` 7회, 종료 0, 원래 크기로 복원, 잔여 프로세스 0이었다. `work/windows/reference-build/gui-fullscreen-fix62/gui-fullscreen-1c32a41b05.json`.
- 조작 안내 창을 먼저 닫는 경우와 메인 창을 닫는 경우를 Mac 런처 Release에서 재검사했다. 대상 창에 따른 세 가지 종료 판정과 원본 VM 문자열 보존이 통과했다. `work/astra62-fixes/launcher-help-close/results.json`.

## 플레이어가 보고한 종료

| 실행 조건 | 관찰한 결과 |
| --- | --- |
| 사용자 세션 `play-q_h9nwc4` | 22프레임, 종료 코드 3, 첫 미지원 명령 `chgdisp`, 대화상자 0회 |
| 한국어 런처 옵션과 현재 config 복사본, 1280×768 전체 화면 | 동일하게 22프레임에 코드 3. 새로 빌드한 호스트에서도 재현 |
| 같은 config 복사본에서 `fullscreen.`만 0으로 변경 | 501프레임에 검사 한도로 종료. 대화상자 1회, 미지원 명령과 관찰된 HSP 오류 0회 |
| 한국어 런처 옵션과 새 프로필, 영어 선택 입력 스크립트 | Ari 생성 → 집 진입 → 이동 → 저장 종료. 4,358프레임, 코드 0, 세이브 154파일, 관찰 오류 0회 |

아래 세 재실행은 자동 입력용 헤드리스 호스트에서 수행했다. `scripts/play_game.py --language ko --dry-run`이 산출한 명령에 검수용 호스트·프로필·기록 경로와 결정적 입력을 사용했다. 기존 프로필에서는 config만 읽었으며 실험 파일은 `work/review-astra62/launcher-*`에 있다.

이보다 앞선 사용자 세션 `play-iadyml4x`는 캐릭터 생성 뒤 2,569프레임에서 게임 자체의 `end/0`으로 끝났다. 해당 로그에는 어떤 창을 닫았는지 정보가 없다. 과거에 조작 안내 창을 닫아 같은 현상을 재현하고 런처에 대응한 기록이 있지만, 사용자가 누른 팝업은 특정할 수 없다. [종료 조사](PLAY_EXIT_INVESTIGATION.md)

## 수정이 필요한 코드

| 우선순위 | 확인된 동작과 영향 | 수정 후 통과 조건 |
| --- | --- | --- |
| 높음 · 세이브 가져오기 | `src/platform/game_files.cpp:976`에서 CP932 경로의 마지막 **바이트**로 디렉터리를 판정한다. 합성 세이브의 일반 파일 `表`가 빈 디렉터리로 복사됐고, 함수는 성공을 반환했다. 파일 목록과 SHA 검증에서도 누락됐다. | 수집한 파일 종류를 복사 단계까지 유지한다. 가져온 파일의 종류·내용·목록을 입력 트리와 대조한다. |
| 높음 · 세이브 가져오기 | 같은 파일 955–959행에서 고정된 임시 경로를 먼저 재귀 삭제한다. 격리된 사용자 루트의 같은 이름 디렉터리에 미리 둔 파일이 사라졌다. 현재 관리 코드에서 이 API의 호출자는 테스트다. 두 결함의 영향 범위는 원본 세이브 가져오기 경로다. | 호출마다 새 임시 디렉터리를 배타적으로 생성하고 소유한 디렉터리만 정리한다. 기존 폴더 보존과 두 가져오기의 독립성을 검사한다. |
| 높음 · 번역표 재생성 | `scripts/compile_localization.py:26–28`은 Git에서 제외된 중의어 판정 파일이 없으면 빈 목록으로 진행한다. 관리되는 동일한 6개 JSON에서 5,058키 대신 5,082키가 생성됐다. 추가된 24키에는 마침표 하나를 긴 밸런스 설명으로, `Cancel`을 ‘아니요’로 바꾸는 매핑이 있다. 현재 커밋된 배포용 표에는 이 24키가 없다. | 판정 입력과 버전을 고정하고 누락 시 원인을 알린다. 깨끗한 체크아웃에서 배포용 TSV를 바이트 단위로 재현한다. |
| 중간 · 한국어 문장 표시 | `src/graphics/text_renderer_ttf.cpp:63–71`의 표시 단위 조회가 개별 글자에도 적용된다. 원본 `gmes`는 문장을 한 글자씩 `mes`에 넘긴다. 이를 재현한 `猫舌` 예제에서 첫 글자만 ‘고양이’로 확장되어 일본어 원래 글자 간격을 침범했다. 두 320×120 화면의 316픽셀이 달랐다. 기본 한국어 런처는 원본 정규화 AX를 쓴다. 관리 AX도 미등록 문단에서는 원래 출력 경로로 돌아간다. | 한 글자짜리 독립 항목과 문장 조각을 구별한다. 알려진 단독 항목의 번역과 미등록 문장 전체의 원문 보존을 함께 검사한다. |

세이브 두 건의 합성 입력은 `tests/platform/test_game_files.cpp`에 일반 파일 `表`와 가져오기 전에 만든 임시 경로 표식 파일을 추가한 것이다. 기존 176개 단언은 모두 통과했다. 관찰값은 `is_directory=1 staged_sentinel_exists=0 manifest_count=3`이었다. 소스는 `work/host-review-XDAE17/review.cpp`, 독립 재실행 출력은 `work/review-astra62/import.log`에 있다. 실제 세이브 손실을 관찰한 사례는 없다.

번역표 비교에는 `src/localization/ko-KR/runtime.sources.json`의 `inputs` 순서대로 파일을 전달하고 `--ambiguities`에 존재하지 않는 검수용 경로를 지정했다. 결과는 `work/review-ko-62/without.tsv`에 있다. 글자 분할 예제와 두 원본 크기 BMP는 같은 검수 폴더의 `tmpv43zumty/`에 있다. 이 예제는 표시 경로의 결함을 입증하며 게임 전체에서 영향을 받는 문구 수는 측정하지 않았다.

## 기존 자동 검사가 놓치는 범위

- `tests/game/run_playability.py:93–95`는 종료 시점의 HSP 오류와 스크린샷 하나 이상의 존재를 검사한다. 신규 8회 실행의 개별 보고서에서는 처리 후 복구된 오류까지 세는 `observed_hsp_error_count`가 모두 0이었다. 이 값과 주요 단계별 기대 화면을 자동 판정에 넣으면 중간 실패를 식별할 수 있다.
- Windows의 `windows_gui_verify.py:193–209`는 외부 캡처의 고유 색상 수를 판정에 쓴다. 이전 `gui-native-05`의 외부 캡처는 800×600 타이틀과 **460,930/480,000픽셀**이 달랐다. 좌측 상단 640×480을 언어 선택 화면과 비교하면 **84/307,200픽셀**만 달랐다. 실제 표시가 멈췄는지, 캡처 API가 이전 화면을 반환했는지는 열려 있다. Windows 헤드리스 검사는 이 문제를 판정하지 않는다.
- `scripts/reference/windows_managed_check.py:103`은 노른 도움말 세 장의 파일 존재를 검사한다. 이번 산출물에서 여섯 장을 별도로 대조했지만 자동 판정만으로는 여섯 페이지의 위치·문구·줄바꿈까지 검수할 수 없다.
- Mac 새 호스트의 Mach-O 최소 버전은 13.0이다. 이 작업공간에서 동적 링크한 Homebrew SDL3와 SDL3_ttf의 Mach-O 최소 버전은 각각 27.2와 26.0이었다. 이 빌드를 macOS 13 호환 배포물로 인증할 근거가 없다. 배포 시 호스트와 동봉 라이브러리 전체의 대상 버전 및 해당 OS에서의 기동을 확인한다.
- README의 원본 준비 절차에 런처가 요구하는 정규화 AX 생성 명령을 추가했다. 별도 검수 폴더에서 새로 생성한 파일은 실제 런처 입력과 바이트가 일치했다.

## 다시 실행한 검사

| 환경 | 결과와 범위 |
| --- | --- |
| Mac ARM64 새 Release | CTest 11/11. Python 단위 검사 53/53, 관리 소스 변환 8/8. 한국어 5,058키의 실제 조회와 글리프 누락 0건 |
| Mac 실제 게임 AX, 결정적 헤드리스 | 8/8. 두 캐릭터 생성, 집, 북부 티리스, 베르니스, 장비, 초원 전투와 저장 후 새 프로세스 로드. 원본 트리·ZIP 불변. 출발 단계의 코드 124는 계획된 프레임 한도 |
| Windows x64 원격 재빌드, 헤드리스 | 41/41. 4개 fixture와 참조·관리 원문·관리 한국어 AX의 신규 게임/로드/월드 진입 9회. 단계별 세이브 154/154/162파일의 압축 해제 내용이 참조 실행과 모두 일치 |

기본 한국어 런처의 분할 문장과 별도 관리 AX의 문단 번역은 서로 다른 실행 경로다. 관리 AX의 노른 도움말 결과를 일반 더블클릭 실행의 번역 범위로 합산하지 않는다. 이번 검수에서 실시간 장시간 플레이, 실물 Windows 디스플레이, OS 입력기, 하위 macOS 기동을 새로 검사하지 않았다.

### 재현 명령

기존 준비 단계가 완료된 작업공간에서 실행한다. 반복 시 플레이 prefix, 원격 run 이름과 쓰기 대상은 새로운 이름을 사용한다.

```sh
cmake --preset macos-arm64-legacy-cp932-release -B build/review-astra62
cmake --build build/review-astra62 -j8
ctest --test-dir build/review-astra62 --output-on-failure
python3 -m unittest discover -s tests -v
python3 -m unittest discover -s tests/localization/source_transform -v
python3 tests/game/run_playability.py --build build/review-astra62 --prefix astra62-review
python3 tests/localization/run_catalog_smoke.py --compiler build/review-astra62/elona_hspcmp --host build/review-astra62/elona_gui_host --catalog src/localization/ko-KR/runtime.tsv --work work/review-astra62/font-audit
python3 scripts/reference/windows_managed_check.py --run gui-native-review62-20261118 --catalog src/localization/ko-KR/runtime.tsv
```

Mac 플레이 요약은 `work/playability/astra62-review/summary.json`, Windows 판정과 입력 해시는 `work/windows/reference-build/gui-native-review62-20261118/verify.json`에 있다. 원본 ZIP SHA-256은 `06b3fd7d523bcc75ff5c3805d9ae6d5a3056e179b815f27698e8923b1ea21391`, 기본 런처 AX는 `5c941a6728066a439f4397e21431b3b3fb579d49e397eb7425665d88ddddf4bc`, 한국어 TSV는 `dc65f3548b0bcbaabfc655c23c05c90e84650434ef55115a676f9c5d7a32b613`이다.
