# 네이티브 세이브 연속성·Mac/Windows 교환 검증

2026-11-18. 대상은 **원본 2.335 AX를 실행하는 Mac ARM64·Windows x64 네이티브 호스트**다.
이 문서는 결정적 headless 실행 결과이며, 원본 Windows x86 실행물에서 직접 만든 세이브 가져오기나 장시간 플레이 전체를 검증했다는 뜻은 아니다.

## 확인한 결과

기존 Mac 테스트 캐릭터 `Ari`(run27)와 Windows `gui-native-02`의 신규 캐릭터를 사용했다.
모든 실험은 테스트 프로필을 새 작업 폴더로 복사한 뒤 실행했고, 원본 게임·출발 프로필은 수정하지 않았다.

| 경로 | 실행 증거 | 결과 |
|---|---|---|
| Mac 저장 → Mac 불러오기 → 인벤토리 → 재저장 | `work/game-run/save-state-mac-01/` | exit 0/end, 1,158 wait 프레임 |
| Windows 저장 → Mac 불러오기 → 인벤토리 → 재저장 | `work/game-run/save-win-to-mac-01/` | exit 0/end, 1,158 프레임, 8개 상태 불변 조건 통과 |
| Mac 저장 → Windows 불러오기 → 인벤토리 → 재저장 | `work/windows/reference-build/platform-save-cross-01/` | exit 0/end, 1,158 프레임, 8개 상태 불변 조건 통과 |
| Mac 저장·종료 후 **같은 물리적 user root**를 새 프로세스에서 다시 열기 | `work/game-run/save-state-reopen-01/restart/` | exit 0/end, 1,158 프레임, 동일 user root 및 8개 조건 통과 |

각 호스트 report의 `first_unsupported`와 `hsp_error`는 비어 있다. 인벤토리 화면에서 11개 항목과 이름·수량·무게 표시를 직접 확인했다.
Mac/Windows 글꼴은 다르므로 화면 픽셀을 같다고 판정하지 않았다.

보존된 내용:

- 이름 `Ari`, 헤더 `Ari Lv:1 Your Home`, 레벨 1, 맵 식별자 `gdata(20)=7`, `gdata(22)=1`.
- 플레이어 좌표 `cdata(1,0)=12`, `cdata(2,0)=17`.
- 플레이어 인벤토리 200개 슬롯의 모든 필드, 그리고 저장되는 전체 인벤토리 1,320개 슬롯 **369,600 bytes**가 동일하다.
- 저장되는 전체 기술 배열 `sdata.s1`의 **273,600 bytes**가 동일하다.

**완전히 정지된 상태를 그대로 재저장한 것은 아니다.** 첫 복원·재저장에서 플레이어 턴 카운터 `cdata(26,0)`는 33→34, 게임 내 시각은 517/10/12 01:21:33→01:21:54로 진행했다.
Mac→Mac·Windows→Mac·Mac→Windows 모두 동일한 변화다. `cdata.s1` 변경 인덱스는 6·10·26, `gdata.s1`은 15·181·597·599·600·601·805이며 JSON에 전 값을 보존했다.
일부 값의 의미를 추측해 무시하지 않는다. 실제 원본 게임 로직의 턴 처리 경로에서 카운터 증가와 게임 시각 진행이 확인되지만, 이 실험 자체는 원본 x86 로드 시의 턴 소비까지 비교한 검사가 아니다.

## 양 플랫폼 저장 바이트 대조

동일한 결정적 신규 게임 스크립트로 만들어진 Mac `run27`과 Windows `gui-native-02` 저장 폴더는 **154개 파일 모두 압축 해제 후 내용이 동일**했다.
동일한 Mac 출발 세이브를 각각 Mac와 Windows에서 불러와 인벤토리 확인 후 다시 저장한 결과도 **154개 파일 모두 압축 해제 후 동일**했다.
일반 텍스트 파일은 원시 바이트를 비교한다. 이 비교는 압축 파일 헤더/압축 스트림 바이트 동등성의 주장과 구분한다.

증거: `work/reports/save-cross-platform-parity.json`.

## 재현 도구

```sh
# 저장 배열 읽기 전용 검사
python3 scripts/inspect_save_state.py work/game-run/run27/user/save/sav_Ari

# 기존 실행 폴더 이름은 재사용할 수 없다.
python3 scripts/check_save_roundtrip.py \
  --profile work/game-run/run27/user \
  --run save-state-reopen-NEW --restart-same-profile

python3 scripts/check_save_roundtrip.py \
  --profile work/windows/reference-build/gui-native-02/evidence/game-newgame/user \
  --run save-win-to-mac-NEW

# home/windows의 검증 완료 호스트를 재사용하며 새 전용 폴더에서 headless 실행
python3 scripts/reference/save_roundtrip_windows.py \
  --profile work/game-run/run27/user \
  --run platform-save-cross-NEW --reference-run gui-native-02
```

`tests/game/load_inspect_resave.script`는 타이틀에서 Restore 선택 → 저장 슬롯 선택 → 맵 캡처 → X로 인벤토리 열기 → 닫기 → 저장·종료를 수행한다.
VM 상태나 세이브 바이트를 직접 변경하지 않는다. `inspect_save_state.py`는 읽기만 하며, 고정 길이·little-endian int32와 gzip 해제 길이를 검증한다.
레이아웃 근거는 비공개 복원 소스의 배열 선언(23825–23839), 플레이어 슬롯 범위(29823–29836), 저장/로드 길이와 zRead/zWrite(307178–307322), 좌표 표시(611895), 턴 카운터(610547), 시간 진행(609680–609735)다.

Windows 교환 검사는 기존 `gui-native-02/build/elona_gui_host.exe`를 사용했고 SHA-256은
`a69372a2dad41a0aa14869ffb8da332878a6e889e2d957340867ea08f238c200`이었다.
원본 ZIP을 새 전용 폴더에 CP932로 풀어 읽기 전용 게임 입력으로 사용했다. 원본 ZIP·private 게임 tree·전송 프로필 ZIP의 전후 해시가 같고, 시작한 PID 26148의 종료를 확인했다. 예약 작업이나 GUI 창은 만들지 않았다.
Mac 실행물 해시는 `save-win-to-mac-01/state-compare.json`과 `save-state-reopen-01/state-compare.json`에 기록돼 있다.

## 범위 제한

- 대상은 테스트 캐릭터 하나의 첫 맵이다. 진행된 퀘스트·동료·유저 NPC·변형 인벤토리·비ASCII 캐릭터 이름·손상 세이브는 별도 검사 대상이다.
- 원본 Windows x86 게임에서 직접 만든 기존 세이브 가져오기는 아직 이 검증에 포함되지 않는다. 저장 형식과 원본 DLL gzip 읽기/쓰기 비교가 있더라도 실제 세이브 수입 검사를 대체하지 않는다.
- 결정적 입력과 headless 화면 검증이다. 실시간 장시간 플레이, 실제 키보드/IME, 오디오 출력 검증은 다른 보고서의 범위를 따른다.
- 새로운 테스트 결과는 `work/`에만 저장했고 원본 배포물·개인 세이브를 공개 소스에 추가하지 않았다.
