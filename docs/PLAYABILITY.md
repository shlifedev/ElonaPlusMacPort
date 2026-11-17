# 초기 구간 플레이 회귀 — 2026-11-18

macOS ARM64 Release 호스트에서 **신규 생성 → 집 밖 → 월드맵 → 베르니스 → 동료 영입 → 장비 변경**, 별도 야외 경로의 **전투·처치·줍기 → 저장 → 새 프로세스 로드**를 실행했다. 이 경로에서 첫 맵 진입 직후의 예기치 않은 종료는 재현되지 않았다. 사용자 실행에서 보고된 종료의 원인이 해결됐다는 뜻은 아니다.

이 검사는 결정적 시계·가상 키 입력·headless 표시를 사용한다. 실제 키보드/마우스/IME, 실시간 오디오, 장시간 플레이, Windows에서 아래 추가 경로를 실행한 증거는 아니다. 기존 Windows 기본 검증은 [WINDOWS_GUI.md](WINDOWS_GUI.md), 저장 교환은 [SAVE_COMPATIBILITY.md](SAVE_COMPATIBILITY.md)를 참조한다.

## 재현

원본 배포물과 정규화 AX, 빌드가 준비된 개발 환경에서 실행한다.

```sh
python3 tests/game/run_playability.py --prefix playability-new-check
```

- 기본 빌드: `build/native-macos-arm64-legacy-cp932-release`; 다른 빌드는 `--build`로 지정한다.
- 매번 새로운 prefix를 사용한다. 기존 결과/프로필은 덮어쓰지 않는다.
- 새 Ari 프로필을 처음부터 생성하고, 각 후속 시나리오는 이전 **테스트** 프로필을 복사한다. 실제 플레이어 폴더 `work/player/2.335/user`는 읽거나 변경하지 않는다.
- 원본 AX·세이브에 값을 주입하지 않는다. 입력은 게임의 정상 생성/이동/메뉴/전투/저장 경로만 이용한다. 저장 배열 검사는 읽기 전용이다.
- 산출물: `work/playability/<prefix>/summary.json`, 단계별 로그, 전투 저장 비교 `combat-roundtrip.json`; 화면/프로필은 `work/game-run/<prefix>-<stage>/`.
- 자동 판정: 종료 코드·이유, HSP 오류/미지원 호출 없음, 캡처 존재, 저장 캐릭터/맵/좌표, 복사 원본 프로필 불변, 실행물 불변, 원본 설치 전체 파일/ZIP 불변. 화면의 대사·전투·아이템 의미는 아래 증거를 직접 열어 별도로 확인했다. 픽셀 동등성이나 전체 게임 정확성을 자동 판정하는 도구는 아니다.

## 실제 확인한 범위

최종 회귀 prefix는 `playcov-suite-02`다. 각 화면 경로는 해당 `work/game-run/<prefix>-<stage>/cwd/shots/` 아래에 있다.

- 실행물 SHA-256: `f2a25cbadddfedd57bc93629cd69af526ac2ff396bc08ff9042a0ee9469904f8`.
- 정규화 AX SHA-256: `5c941a6728066a439f4397e21431b3b3fb579d49e397eb7425665d88ddddf4bc`.
- 원본 설치 1,511개 파일의 전후 해시 일치, ZIP 불변, 각 복사 원본 테스트 프로필 불변을 확인했다.
- 전체 결과: `work/playability/playcov-suite-02/summary.json`; 저장 비교: 같은 폴더의 `combat-roundtrip.json`.

| 단계 | 게임에서 관찰한 결과 | 저장/종료 증거 |
|---|---|---|
| `newgame` | Ari 생성 전 단계, 오프닝, 로미아스 첫 대화, Your Home에서 이동 | 맵 7, 좌표 (12,17), 4,358 waits 후 스크립트 저장 종료 |
| `departure` | 남쪽 경계 접근, 로미아스/라네이레의 작별 대화 5페이지, NPC 퇴장/선물, 집 떠나기 확인 | 자동 저장: 맵 7, (13,21). 1,851 waits에서 지정한 `max-waits` 종료 |
| `world` | 집/월드 튜토리얼 페이지 진행, North Tyris 진입 | 맵 4, (22,21), 2,508 waits 후 저장 종료; `north-tyris.bmp` |
| `vernis` | 월드에서 동쪽 4칸·남쪽 2칸 이동, 베르니스 진입, 마을 튜토리얼, 개 선택/동료 합류 | 맵 5, (26,1), 2,708 waits 후 저장 종료; `after-pet2.bmp` |
| `vernis` 메뉴 | 장비, 빠른 인벤토리, 캐릭터 정보, 일지 열기/닫기 | `equipment.bmp`, `inventory.bmp`, `character.bmp`, `journal.bmp` |
| `equipment` | 청동 투구를 해제하고 빈 머리 슬롯에서 다시 선택·착용. 로그 두 동작 확인, DV/PV 1/10 → 1/9 → 1/10 | 1,408 waits 후 저장 종료; `helmet-removed.bmp`, `helmet-restored.bmp` |
| `combat` | 집 서쪽 Grassland 진입. 날개 개구리에 공격/강타, 치명타·공포 상태, 상대 공격 회피, 처치 | `first-contact.bmp`, `bash.bmp`, `combat.bmp`, `kill-and-step.bmp` |
| `combat` 줍기 | 처치 후 아이템이 있는 칸으로 이동해 줍기. 실제 로그에 날개 개구리의 소변 획득 표시, 인벤토리 열기 | 맵 2, (16,11), 2,108 waits 후 저장 종료. 170개 저장 파일; `pickup.bmp`, `inventory-loot.bmp` |
| `reload` | 전투 저장을 복사한 새 프로세스에서 Grassland/인벤토리 로드, 이동 없이 재저장 | 맵 2, (16,11), 1,148 waits 후 저장 종료; 저장 이름/레벨/좌표/맵·인벤토리/기술 모두 유지 |
| `variant` | 별도 신규 Mira: 요정(Fairy, 표시 종족명 Sylph), 여성, 마법사. 오프닝·첫 대화·집 이동 | 맵 7, (11,13), 4,358 waits 후 저장 종료; `s2070-sheet.bmp`, `m4120-after-move.bmp` |

전투 저장 재시작 비교는 플레이어 인벤토리 56,000 bytes뿐 아니라 **전체 저장 인벤토리 369,600 bytes와 기술 273,600 bytes의 해제된 바이트**가 같다. 이름/헤더/맵/좌표/레벨도 유지됐다. 이는 이 저장 경로에 대한 검사이며 다른 모든 세이브 형식/진행도를 보장하지 않는다.

8개 시나리오는 모두 빈 `hsp_error`/`first_unsupported`를 보고했다. 정상 저장 종료는 exit 0이다. `departure`의 exit 124는 호스트에 지정한 wait 한도 도달이며 충돌이나 게임의 임의 종료가 아니다. 사용자 실행에 대한 판단에서도 `end`/exit 0만으로 종료 원인을 단정하면 안 된다. 명시적 게임 종료와 창 닫기 경로를 구분하는 진단은 호스트 담당 작업에서 별도로 다룬다.

## 검사 중 발견한 입력/경로 주의점

- 집의 남쪽 바닥 경계로 걸어 나가야 한다. 바닥의 부러진 검 그림을 계단으로 오인해 `<`를 보내던 초기 탐색은 출구 검증이 아니었다.
- Enter는 대화 진행 외에 문맥 행동에도 사용된다. 튜토리얼 종료 뒤 Enter를 더 보내면 월드맵에서 집으로 다시 들어가거나 주변을 탐색한다. 탐색 초안의 반복 Enter를 실제 종료 문제로 오해하지 않았고, 최종 스크립트는 필요한 페이지 수만 진행한다.
- 테스트 `key` 이벤트의 Shift 처리는 영문자를 위한 것이다. `>` 같은 문장 부호는 `text >`로 공급한다. 이것은 자동 입력 규칙이며 실제 SDL 텍스트 입력 오류의 증거는 아니다.
- 빠른 인벤토리는 `X`, 장비는 `W`, 캐릭터는 `C`, 일지는 `J`로 열었다. 장비 화면의 첫 `A`는 투구를 즉시 해제한다. 빈 슬롯 선택 후 항목 선택까지 해야 재착용이 완료된다.
- 최초 통합 실행 `playcov-suite-01`은 요정의 최종 좌표를 전사와 같다고 가정한 검사 기대값 때문에 실패했다. 게임은 정상 저장 종료했고 요정의 실제 좌표는 (11,13)이었다. 별도 탐색과 회귀에서 동일하게 관측한 좌표로 검사 기대값만 고쳤다. 게임 동작/이동 입력은 바꾸지 않았다.

## 남은 검증

- 사용자가 겪은 첫 맵 직후 종료의 직접 원인: 이 결정적 경로에서는 미재현. 실제 이벤트와 종료 출처 기록으로 좁혀야 한다.
- 물리 키보드의 연속 입력/숫자패드/마우스/IME, 실시간으로 긴 대화/플레이, 앱 비활성화·복귀.
- 상점 거래, 퀘스트 수락/완료, 마법 사용, 동료 전투/명령, 던전·계단·월드 이동 중 랜덤 인카운터, 사망/부활, Act 진행 및 후반 콘텐츠.
- 다른 게임 모드/신앙/종족의 특수 슬롯과 다양한 저장 상태. 이번 신규 생성은 Yerles 전사와 Fairy 마법사 두 분기다.
- 이 추가 경로의 Windows 네이티브 재실행 및 원본 x86과의 게임 상태 대조.

관련 파일: `tests/game/run_playability.py`, `home_departure.script`, `home_to_world.script`, `world_to_vernis.script`, `equipment_roundtrip.script`, `field_combat_loot.script`, `reload_after_combat.script`, `newgame_fairy_wizard.script`. 모든 테스트 이름은 `tests/game/` 아래에 있다. 원본 게임 코드와 플레이어 데이터는 보고서/소스에 복사하지 않았다.
