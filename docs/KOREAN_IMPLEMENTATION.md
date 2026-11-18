# 한국어 표시 구현과 현재 범위

사용자가 제공한 ElonaPlus 2.335의 영어판과 일본어판을 기준으로 한국어 표시문을 작성했다. 게임 로직과 세이브는 원본 CP932를 사용하고, 한국어 창은 UTF-8 사전을 읽는다.

## 남은 고정 문구와 모험 화면 (v13)

v12에서 한국어 표시 키가 없었던 일·영 원문 2,695쌍을 모두 대조했다. 새로 2,630쌍을 연결했다. 고정 원문 12,405쌍 중 12,340쌍(99.48%)이 적어도 한 언어에서 한국어 표시에 연결된다. 그중 검수·직접 교정은 8,337쌍, 자동 초벌은 4,003쌍이다. 사전에는 화면별 이름·조합도 들어 있어 표시 키 25,910개와 원문 쌍 개수는 따로 집계한다. 키 충돌은 0건이다. [집계와 입력 지문](../localization/ko-KR/coverage.json)

| 화면 | 바뀐 표시와 검증 |
|---|---|
| NPC 선택지와 모험 로그 | NPC 선택지 497곳은 같은 영어 대답이어도 원래 선택한 일본어 문장과 목록 위치로 구별한다. 목록을 버리고 새로 여는 경우까지 여섯 화면을 대조했다. 전투·탐험 로그는 원래 난수로 뽑은 대사에 맞춰 영어·일본어 양쪽을 표시한다. 서로 같은 영문을 쓰는 오의·운석 메시지, 연주·효과음을 포함해 348쌍의 양 언어 화면 696건을 픽셀 단위로 비교했다. 플레이어 상태 효과 71종도 완성된 문장 195건으로 확인했다. |
| 동료 진화 | 영문판에서 종명이 `EV`와 `aaaaaaa`로 뭉개지던 167개 진화 모드의 이전·이후 종 421개를 원본 분기와 대조했다. 7개 대표 로그를 네이티브 호스트에서 검사했다. 직접 지은 이름과 진화 직전에 원본이 덮어쓰는 단계도 구별한다. |
| 카드 덱 | 실제 선택할 수 있는 카드 1,165종의 제목·비용·희귀도와 본문이 기본 한국어 모드에서 표시된다. 설명 292종은 검수 전용 모드에서도 나오며, 873종에는 기존 자동 초벌을 사용한다. 전 카드의 원래 폭과 본문 높이를 검사했다. 복제 프로필의 덱 편집기에서는 거짓 예언자 제임, 섀도 시스터, 날벌레 황제의 제목과 본문을 직접 확인했다. 원문에서 메뚜기를 뜻하는 날벌레 황제의 설명도 교정했다. [덱 편집기 화면](../work/card-name-qa/editor-lore-final/verified.png) |
| 조합 | 조합법·재료·보유량, 선택한 아이템 이름, 성공률·소요 시간, 재료를 넣는 로그와 수량 선택을 번역했다. 영문으로 똑같은 `any bait`가 표시돼도 재료 ID로 낚시 미끼와 나무통을 구분한다. 복제한 베르니스 세이브로 조합 도구를 열어 재료 다섯 개를 선택했다. 수량 창까지 조작 안내를 포함한 17개 화면 영역과 추가 표시 8건을 검사했다. |
| 신앙 제단 | 무신앙과 여덟 신의 소개·제물·보너스·특수 능력, 귀의·개종·취소 선택지를 옮겼다. 신에 대한 피해 +40%, 주는 물리 피해 +20%, 받는 피해 -10% 등 원래 수치를 유지했다. 원본 제단 메뉴의 일·영 18개 화면에서 본문과 선택지가 겹치지 않았다. |

남은 65쌍에는 언어별 파일명과 모델 문자, 카드 번호 범위와 기호, 이름을 만들 때 붙이는 관사·전치사, 실행되지 않는 중복 카드 분기가 포함된다. 각 ID의 판단 근거를 기록했다. 게임 도중 조합되는 표현식 후보 4,785개, 외부 파일·이미지 속 글자는 위 고정 원문 통계 밖에 있다. [표기 유지 및 문맥 메모](../localization/ko-KR/original-fixed-adventure.json)

한국어 AX는 원본이 사용한 선택 번호, 이름과 대사를 보존한다. 고정 문구의 짧은 단어는 원래 게임 문구임을 확인한 표시 위치에서 조회하고, 플레이어 이름에는 이 조회를 적용하지 않는다. 양 언어 로그는 원본이 기록한 문자열과 줄 경계에 한국어를 그린다.

```sh
python3 tools/prepare_korean_game.py --output work/localization/managed-korean-static-v13 --compile
python3 tools/compile_localization.py --from-manifest localization/ko-KR/runtime.sources.json --output localization/ko-KR/runtime.tsv
python3 tools/korean_coverage.py --remaining work/localization/2.335/unbound-v13.json
host=build/native-macos-arm64-legacy-cp932-release/elona_gui_host
compiler=build/native-macos-arm64-legacy-cp932-release/elona_hspcmp
ko=localization/ko-KR/runtime.tsv
src=work/localization/managed-korean-static-v13
python3 tests/localization/run_fixed_messages.py --host "$host" --compiler "$compiler" --catalog "$ko" --ax-source "$src/start.hsp" --work work/localization/static-check
python3 tests/localization/run_card_names.py --host "$host" --compiler "$compiler" --catalog "$ko" --work work/localization/static-check
python3 tests/localization/run_altar.py --host "$host" --compiler "$compiler" --catalog "$ko" --work work/localization/static-check
python3 tests/localization/run_play_audit.py --run ko-static-check --ax "$src/start.ax" --catalog "$ko" --fallback localization/ko-KR/draft-display.tsv
python3 tests/localization/run_recipe_menu.py --profile work/ko-broad/ko-static-check/vernis/profile
```

최종 Mac 패키지에서 이명 후보 16개와 두 차례 재추첨한 후보 32개, 선택 후 캐릭터 시트와 기존 세이브의 이명을 다시 대조했다. F1 도움말 99항목도 게임 메뉴에서 열었다. 조합 도구는 베르니스 복제 세이브로 실제 창에서 열어 다섯 재료와 한국어 조작 안내가 보이는 수량 선택 화면을 캡처했다. [이명 선택 검증](../work/ko-alias-selection/tmp4307xv4o/summary.json) · [도움말 검증](../work/localization/remaining-v13/final-manual/tmpss3_fma6/summary.json) · [Mac 실제 게임 창](../work/localization/remaining-v13/packaged-window-final/recipe-window.png)

Mac의 신규 게임·출발·세계 지도·베르니스·장비·전투·다시 불러오기·마법사 생성 8개 경로에서 프레임 수와 압축 해제 세이브 154~170파일이 v12와 일치했다. 이 경로의 미번역 조회에 잡힌 연속 영문자 세 글자 이상의 문자열은 테스트 세이브 폴더명 `sav_Ari` 한 종류였다. Windows x64에서는 빌드·글꼴·게임 진행·저장 비교를 포함한 헤드리스 검사 42개를 통과했다. [Mac 경로별 결과](../work/ko-broad/ko-static-v13-package/summary.json) · [Windows 검사 결과](../work/windows/reference-build/gui-native-ko-static-v13-package/verify.json) · [v13 Mac 설치 이미지](../build/package/korean-2.335-v13/ElonaPlus-0.1.0-macos-arm64.dmg)

## 모험 중 표시와 실제 검증 (v12 기록)

| 화면 | 원문에서 확인한 내용 | 확인한 결과 |
|---|---|---|
| F1 도움말 | 목차와 단축키 화면에 영어가 남았고, 영어판의 마지막 장비·잠금 동작에는 일본어도 붙었다. 두 설명서는 항목 구성과 일부 게임 설명이 다르다. | 실제 게임에서 영어판 49항목과 일본어판 50항목을 전부 열어 한글 글자 위치와 강조색을 대조했다. 원래 너비 510픽셀에서 본문 높이는 34~242픽셀로, 주어진 390픽셀 안에 들어갔다. 수정한 주제는 그대로 읽을 수 있다. |
| 책 | 책 제목이 같아도 영·일 판본의 저자와 쪽 구성, 원래 빈 쪽이 다를 수 있다. | 33권 66판본, 241쪽, 136개 펼침을 네이티브 책 창에서 검증했다. 수정한 본문 66건이 각각 원문으로 돌아갔다. 16번 책의 세로읽기 철자와 가나는 수수께끼를 풀 때 쓰인다. |
| 마을·후반 이야기 | 의뢰인 이름 뒤의 직업과 성별, 게시판 쪽수가 영어였다. 서로 다른 후반 대사에 같은 영문 임시 문구가 들어간 경우도 있었다. | 베르니스 두 번째 게시판과 고양이 의뢰, 다른 내용을 담은 노른의 유품 대사 두 장면과 보상 알림을 복제 세이브에서 확인했다. 지정 구간(원본 390,000~429,999행)의 같은 영문 임시 문구를 쓰는 서로 다른 일문 본문 488개 중 208개는 검수했고, 280개는 초벌이 연결됐다. |
| 장비·마법·전투 | 장비의 재질·관통률·명중·피해·내성·특수 효과, 마법과 능력의 창 제목·비용, 기술을 배우는 메시지에 영어가 남았다. | 청동 모닝스타의 성능 여섯 줄과 마법사 세이브의 마법·능력 목록을 열어 확인했다. 숫자·주사위·효과 등급을 보존했고, 전투·훈련·비용 345가지 표시를 기대 픽셀과 대조했다. |
| 채집·낚시 | 재료 이름과 수량·설명, 조리된 생선 이름과 획득 알림이 조합된 뒤 원문으로 표시됐다. | 게임에서 재료 59종 네 쪽과 ‘금붕어 초밥 (등급 9)’의 소지품 행과 상세 제목을 확인했다. 원본 어종 112종, 조리 가능한 어종 76종 × 9등급을 양 언어로 그린 1,592건에서 누락이 없었다. 실제 낚싯대를 던져 낚기까지의 과정은 이 검사에 포함하지 않았다. |
| 카드 덱 | 영문 설명이 비어 있는 카드가 있었다. 덱·생명력·마나 등은 숫자와 나눠 그려 단어가 남았다. | 카드 161종의 제목·설명 322개를 원본 창 폭으로 그렸다. 세 장을 넣은 테스트용 덱에서는 커서를 옮길 때 카드별 설명이 바뀌고 장수와 시작 수치가 보존됐다. |

검수 사전은 v11의 15,253키에서 18,132키로 늘었다. 고정 일·영 원문 12,405쌍 중 9,710쌍(78.27%)에 표시 키가 있다. 검수 사전 4,649쌍, 추가 직접 교정 998쌍, 추가 자동 초벌 4,063쌍이다. 미연결 원문은 3,069쌍에서 2,695쌍으로 줄었다. 파일에서 읽는 책·도움말, 동적 표현식 후보 4,785개와 이미지 속 글자는 이 집계 밖에 있다. [원본과 사전 해시·집계](../localization/ko-KR/coverage.json)

### 재현

영·일 도움말에서 같은 제목 아래에 사용자 수정 본문이 있을 수 있어 제목과 본문 전체의 CP932 지문을 주제마다 확인한다. 조회에는 줄끝만 정규화한다. 한국어 본문은 전용 설명서 창에서 줄바꿈과 강조를 적용한다. 단축키의 실제 값, 입력 명령과 기여자 표기는 사용자가 식별할 수 있도록 원래 철자를 보존한다.

```sh
cmake --build --preset macos-arm64-legacy-cp932-release
python3 tools/prepare_korean_game.py --output work/localization/managed-korean-adventure-v12 --compile
python3 tools/compile_localization.py --from-manifest localization/ko-KR/runtime.sources.json --output localization/ko-KR/runtime.tsv
ctest --test-dir build/native-macos-arm64-legacy-cp932-release --output-on-failure
ax=work/localization/managed-korean-adventure-v12/start.ax
bin=build/native-macos-arm64-legacy-cp932-release
ko=localization/ko-KR/runtime.tsv
python3 tests/localization/run_manual.py --ax "$ax"
for test in run_books run_quest_board run_card_details run_magic_training run_catalog_smoke; do
  python3 "tests/localization/$test.py" --host "$bin/elona_gui_host" --compiler "$bin/elona_hspcmp" --catalog "$ko" --work work/localization/adventure-check
done
python3 tests/localization/run_play_audit.py --run adventure-check --ax "$ax" --catalog "$ko" --fallback localization/ko-KR/draft-display.tsv
python3 tests/localization/run_town_quest.py --profile work/ko-broad/adventure-check/vernis/profile --ax "$ax" --catalog "$ko"
python3 tools/reference/windows_managed_check.py --run gui-native-ko-adventure-check --managed-ax "$ax" --catalog "$ko" --fallback localization/ko-KR/draft-display.tsv
```

준비 명령은 새 출력 폴더를 사용한다. 기존 v12 입력이 있으면 그대로 검사를 실행할 수 있다. 도움말 검사 `work/localization/guide-v12-final/tmprtzgdcop/summary.json`은 배포 앱 안의 호스트와 AX로 얻었다. 별도 1,000프레임 비교에서 도움말의 단축키 화면과 세 쪽의 목차를 열었다. v11은 영어판과 일본어판 각각에서 미번역 88종을 기록했고, 같은 입력의 v12에서는 0종이었다(연속 영문자 3자 또는 일본어 문자가 있는 미조회 값 기준). 패키지 실제 창은 `work/localization/adventure-v12-release-window/manual-window.png`, 마을 대사는 `work/ko-late-story/norne-final/ocr.txt`, 카드 덱은 `work/card-game-coverage/editor-final/evidence.png`, 조리·채집물은 `work/fishing-expansion/game4/cwd/shots`에 있다.

실제 게임 8경로의 프레임과 세이브 154~170파일은 모두 v11과 일치했다. 이 경로에서 영문 세 글자 이상인 미조회 값은 저장 폴더 이름 `sav_Ari` 한 종류였다. Windows x64에서는 헤드리스로 검수 키와 초벌 키, 신규 생성·로드·출발·세계 지도 및 저장 대조 42개를 통과했다. [Mac 플레이 결과](../work/ko-broad/korean-adventure-v12-release/summary.json) · [Windows 결과](../work/windows/reference-build/gui-native-ko-adventure-v12-release/verify.json)

## 이명과 초반 플레이 검증 (v11 기록)

한국어 런처에서 **이명 선택 → 다시 굴리기 → 선택 → 캐릭터 시트**까지 생성된 이명이 한글로 표시된다. `Dangerous Tomorrow`는 ‘위험한 내일’, `Sword of Flame`은 ‘불꽃의 검’이다. 영·일 어휘를 명사·수식형으로 번역한 뒤 원본 조합 순서와 연결어에 맞춰 표시한다. 세이브의 CP932 이명과 난수 호출은 그대로다. 자유 입력 이름은 그대로 읽으며, 라네이레의 작별 대사는 원본에서 플레이어 이명을 부르는 두 자리에서만 이명을 번역한다.

| 검증 | 관측 결과 |
|---|---|
| 영·일 이명 사전 | `ndata-e.csv`, `ndata.csv`의 1~14열에 나오는 고유 어휘 2,729개. 실제 C++ 변환기로 조합 표본 7,714개에서 영문과 사전 구분자 노출 0건. |
| Mac 이명 화면·시트 | 첫 후보 16개와 재추첨 32개 모두 이명 사전만 뺀 대조 실행과 다른 글자를 그렸다. 후보 영역 바깥의 픽셀은 두 실행에서 같았다. 긴 후보로 커서를 옮겨도 선택 표시가 마지막 글자를 가리지 않는다. 두 번 다시 굴린 뒤 선택한 ‘사악한 신뢰’의 저장값은 `Wicked Trust`였다. 원본 AX가 만든 저장과 한국어 AX가 만든 저장 모두 새 프로세스로 불러와 시트 표시 및 저장 내용 154파일을 대조했다. |
| 생성·설정 | 모험 모드 6종의 설명 38줄과 모호한 캐릭터 이력은 일본어 원문과 대조했다. 생성·시트 이력 222개의 폭을 검사했다. 설정 7개 분야에서 영문 닫기 안내를 포함해 연속 영문자 3자가 남은 미번역 조회가 0건이었다. |
| Mac 실제 플레이 8경로 | 전사·마법사 생성, 첫 집, 세계 지도, 베르니스, 장비, 전투 및 재시작. 기본판과 검수 전용판 모두 v8 기준과 최종 프레임 및 압축 해제 세이브 154~170파일이 단계마다 같았다. 집 출발 시나리오는 예정된 1,851프레임 대기 상한, 나머지 7개는 정상 종료했다. 영문자 3자 이상인 미번역 조회는 77종·4,599회에서 저장 폴더명 `sav_Ari` 1종·600회로 줄었다. 사용자가 입력한 이름과 단축키는 계속 원래 표기로 나온다. |
| Windows x64 Release | 일본어 기반 새 게임·불러오기·세계 지도, 원본·무번역·한국어 저장 대조, 폰트와 원본 보존을 포함한 헤드리스 검사 42/42 통과. 이명 화면의 16개 일본어 기반 후보가 모두 한글인 것을 렌더 결과로 확인했다. |

고정 일·영 문장 12,405쌍 중 9,336쌍(75.26%)에 한국어 표시 키가 있다. 검수 사전 4,050쌍, 추가 직접 교정 1,044쌍, 추가 자동 초벌 4,242쌍이다. 남은 3,069쌍, 동적 표현식 후보 4,785개와 외부 대화·책·그림 속 글자는 별도 검수 대상이다. 실게임 누락 건수는 위 입력에서 실제 조회한 값에 한정된다. [집계와 입력 해시](../localization/ko-KR/coverage.json)

Mac Release CTest 13/13, 관리 소스·번역 키 검사 17/17, 전체 검수 키 15,253개에 대한 폰트 렌더 검사도 통과했다. 원본 어휘 파일의 SHA-256은 영문 `524efbec365434a311ae8e295f89c717e1c516a9d3aea3c2ff352a8b0e403e75`, 일문 `e1b6fc96ab3dac574c6eeda230261a4564e5b1a119df8103389049f6c9d7c0f7`이다. 이명 번역의 `id`는 이 두 파일의 행·열을 가리킨다.

```sh
python3 tests/localization/check_alias_coverage.py --runner build/native-macos-arm64-legacy-cp932-release/elona_alias_tests
python3 tests/localization/run_alias_selection.py
python3 tests/localization/run_play_audit.py --run ko-recheck --ax work/localization/managed-alias-v11/start.ax --catalog localization/ko-KR/runtime.tsv --fallback localization/ko-KR/draft-display.tsv --reference work/ko-broad/final-default
python3 tools/reference/windows_managed_check.py --run gui-native-ko-recheck --catalog localization/ko-KR/runtime.tsv --fallback localization/ko-KR/draft-display.tsv
```

이명 회귀 검사는 새 프로필로 생성·재추첨·로드·커서 이동을 실행한다. 두 번 재추첨한 생성 경로는 4,701프레임 대기 상한에서 저장을 회수한다. Mac SDL 창 캡처는 `work/localization/alias-v11-package-final/alias-window.png`, 이명 비교는 `work/ko-alias-selection/tmpg4xs5053/summary.json`, 8경로 비교는 `work/ko-broad/alias-v11-final`과 `work/ko-broad/alias-v11-final-reviewed`, 설정 화면은 `work/localization/alias-v11-settings-final`에 있다. Windows 결과는 `work/windows/reference-build/gui-native-ko-alias-v11-02/verify.json`이다. 새 작업공간에서 8경로를 처음 실행할 때는 위 명령의 `--reference`를 생략할 수 있다.

## 2.335 한국어판의 표시 경계

현재 런처와 패키지가 사용하는 `managed-korean-static-v13/start.ax`의 SHA-256은 `4ea855ff380640df4f862a5e1e08186f0572d1da2fdf95e81be80028fde8ddc8`다. 검수 사전을 우선 조회하고 추가 정적 문구에는 별도의 한국어 초벌 사전을 사용한다. `--language ko --reviewed-only`로 검수 번역을 선택할 수 있다. [초벌 생성과 교정](KOREAN_STATIC_DRAFTS.md)

| 표시 영역 | 근거 |
|---|---|
| 캐릭터 생성 | 종족 11종·직업 12종, 일본어/영어 고유 설명 44개. 같은 영문 자리표시자를 쓰는 두 직업은 원본 클래스 ID로 구별한다. 실제 425px 폭 문단에서 최대 높이 82px로 다음 제목까지의 여유 104px 안에 들어갔다. 획득 기술·능력 보너스·숙련 무기도 처음 화면에서 읽을 수 있다. |
| 이명·설정·일지 | 이명 후보와 시트, 설정 항목과 현재 값, 일지 본문·소식·저장 헤더에 문맥별 표시를 적용한다. 첫 동료 합류에서는 개·고양이·불곰·소녀의 원래 종 ID를 읽어 종명만 번역한다. |
| 인벤토리·장비 | 낚시 결과 112개를 원래 종류와 물고기 ID로 구별한다. 원본에 이름이 있는 아이템 1,322종에 ID별 이름이 있다. 알려진 장비와 재료·수량·수치에 적용한다. 미감정 외관, 사용자가 지은 이름, 아직 해석하지 못한 조합은 원문을 표시한다. |
| 마을·퀘스트 | 도시 고유 대사 226행과 상점·교관·동료·주민 614행을 원본 파일과 대조했다. 게시판 88종의 제목과 본문에서 목표·수량·지명·보상을 분리했다. 팔미아·레시마스·길드 등 별도 의뢰와 이야기 419문구를 독자 번역했다. |
| 대화·도움말·책 | 내 집의 로미아스·라네이레, 양 판본의 프롤로그 5쪽, 마을 소문, 노른의 영문 도움말 18주제 46쪽(원문 182행), F1 설명서 전 항목과 서적 33권의 양 판본 241쪽. JP의 「길 잃은 병사를 위한 교범」과 EN의 「초보 모험가 안내서」는 저자와 내용이 달라 따로 번역했다. |
| 전투·성장 | 캐릭터 시트, 보유 특성·변이·에테르병, 각성 훈련, 상점, 전투·장비 로그에서 한국어를 표시한다. 전투 로그의 사용자 이름, 주사위, 턴, 수량은 그대로 전달한다. |

## 이전 영어 기반 플레이 보완

실사용 세션은 관리 AX와 한국어 카탈로그를 로드했고 기본 언어는 영어였다. 마지막 집 화면에서 날씨, 가구·가보 가치와 집 순위, 저장 확인 질문이 영어로 남아 있음을 확인했다.

- 보완한 Mac Release에서 같은 영어 선택·캐릭터 생성·집 이동·저장 입력을 4,858프레임까지 재생했다. 번역표 조회에 실패한 문자열 중 연속 영문자 3자 이상을 포함한 고유 문자열은 340종에서 235종으로 줄었다. 반복 그리기를 포함한 해당 조회 횟수는 30,906회에서 9,686회로 줄었다. 검사 대상에는 잘린 문장·버튼·표제도 포함된다. 이 수치로 게임 전체의 번역률을 계산할 수는 없다. 양쪽 모두 종료 코드 0, 관찰 오류 0, 압축 해제한 세이브 154파일이 동일했다. 기록은 `work/korean-coverage-followup/final-comparison.json`에 있다.
- 캐릭터 경력과 선택지, 라네이레·로미아스의 칭호, 시작 고지문을 추가했다. 시작 고지문은 영문본과 일본어본의 내용 차이와 문단 구분을 보존한다. 게임에서 조각 단위로 그리던 시간·날씨·레벨·기력과 첫 집의 확정 로그를 해당 표시 위치에서 번역한다. 가구·가보 수치, 등급, 시각을 보존한다. `work/korean-coverage-followup/home-final-logs/home3175.png`
- 영문판 노른의 집 4페이지, 세계 지도 3페이지는 29개의 실제 본문 행을 따로 번역했다. 새 프로필에서 일곱 페이지가 해당 한글 기준과 일치했고 비교 실행의 세이브 162파일이 일치했다. `work/localization/english-help-game-01/summary.json`.
- 최종 카탈로그 5,716키, 충돌 0건. Mac 실제 글리프 검사 5,716/5,716 및 Release CTest 12/12, Windows x64 재빌드와 한국어 렌더·저장 비교를 통과했다. Windows x64 헤드리스 검사 39개의 최종 기록은 `work/windows/reference-build/gui-native-ko-home-hud-02/verify.json`이다. Mac 최종 재실행 `work/korean-coverage-followup/postcheck`에서도 동일한 미번역 목록과 세이브를 확인했다. 소스에 번역된 긴 NPC 대사도 게임이 먼저 줄을 나누면 원문이 남을 수 있다.

## 원본 데이터를 지키는 경계

- 게임 VM의 문자열·파일명·배열·세이브는 CP932를 유지한다. UTF-8 한국어는 렌더러로 전달되는 표시 복사본에만 사용한다.
- 일반 `mes`는 검수 catalog의 정확한 전체 키가 맞을 때만 번역한다. 대소문자 변환/부분 문자열 치환을 자동 적용하지 않는다. 실제 게임에서 확인한 별칭은 데이터에 별도로 기록하고 충돌 검사한다.
- 입력 컨트롤은 원본 문자열 렌더 경로를 사용한다. VM 변수 및 입력/저장 값 유지 검사는 `tests/localization/run_display.py`에 있다. 이 검사가 사용자 작성 이름과 모든 표시 문맥의 구분까지 증명하는 것은 아니다. 사전에 같은 문구가 있는 이름·별명 등의 표시에는 추가적인 문맥 경계가 필요하며, 현재 전역 exact lookup을 전체 현지화 완료로 취급하지 않는다.
- `gmes`는 원문 한 글자씩 `mes`를 호출하므로 일반 lookup으로 긴 도움말을 번역할 수 없다. [관리 소스 변환](KOREAN_RENDER_BOUNDARY.md)은 원문을 바꾸지 않고 함수 입구에 명시적 문단 표시 명령을 추가한다. 검수된 전체 문장이 없으면 원본 `gmes`를 그대로 실행한다. VM에 한글 바이트를 억지로 넣지 않는다.
- 문단 렌더러는 `<emp1>`, `<emp2>`, `<def>` 등 알려진 태그, 글자 폭·어절 줄바꿈과 그림자를 처리한다. 알 수 없는 제어를 조용히 버리지 않는다. 한국어로 바뀌는 것은 표시 픽셀과 해당 문단의 높이다.

## 데이터 및 도구

| 자원 | 범위 |
|---|---|
| `glossary.tsv`, `style-guide.md` | 세계관 명사·문체·화자·효과 설명 기준 |
| `community-reviewed.json` | 출처/변경/CC BY-SA 고지를 보존한 기존 위키 번역 검수 |
| `original-ui.json`, `original-controls.json` | 메뉴·캐릭터 생성·시트·조작 안내 독자 번역 |
| `original-database.json` | 초기 DB 범위 2,406항목; 검수2,263/문맥 보류143 |
| `original-races-classes.json` | 종족76/직업12의 이름. 긴 종족/직업 설명까지 번역한 것은 아님 |
| `original-alias-words.json`, `original-options.json`, `original-journal.json`, `original-fishing.json` | 이명 명사·수식형, 설정별 용어, 모험 일지, 낚시 아이템 |
| `original-creation-prompts.json`, `original-dialogs.json` | 캐릭터 생성, 고지문, 첫 집 인물 칭호·대화 선택지 |
| `original-english-help.json`, `original-home-status.json` | 영문판 노른의 초반 안내, 시간·날씨·집 등급·저장 로그 |
| `original-tutorial.json` | 첫 집91항목, 노른18블록79본문행과 제목1개. 동적 템플릿6개는 현재 자동 적용 보류 |
| `english-ambiguities.json` | 2.335 조사에서 중의성이 발견된 영어 표시 키의 UTF-8 SHA-256 목록. 원문과 대응 일본어 문장은 제외 |
| `runtime.tsv` | 검수한 고정 문구와 화면·문맥별 키를 수록한 생성물 |
| `runtime.report.json`, `runtime.sources.json` | 제외 이유·충돌·입력 해시·키별 원본 JSON/항목/라이선스 추적 |

전체 static `lang` 조사에는 12,405개 고유 쌍과 4,785개 동적 표현식 후보가 있다. 그 밖의 파일·게임 내 조립 문장도 있으므로 이 숫자를 게임 전체 번역 분모로 사용하지 않는다.

검수 번역은 추적 파일에 기록한 순서로, 초벌은 2.335 비공개 추출물과 재사용 가능한 키 목록으로 다시 생성한다.

```sh
python3 tools/compile_localization.py --from-manifest localization/ko-KR/runtime.sources.json --output localization/ko-KR/runtime.tsv
python3 tools/build_draft_display.py --reviewed-output localization/ko-KR/reviewed-display.tsv
python3 tools/korean_coverage.py --output localization/ko-KR/coverage.json
```

`reviewed`만 후보로 사용하고 `context_required`/`template_reviewed`는 그대로 넣지 않는다. JP/EN 별칭을 개별로 비활성화할 수 있으며 충돌한 키는 제외한다. 수치·서식·강조 태그 순서도 점검한다. 옛 `ko.txt` 전체 사전은 공개 재사용 조건 미확인이라 런타임에 일괄 가져오지 않았다. [출처 조사](KOREAN_SOURCES.md)를 참조한다.

## 이전 검증 기록 (5,058키 기준)

- Mac 개발/Release CTest11/11. strict UTF-8/중복 키, 문맥·서식 제외, 실제 한글 픽셀, VM 원문/입력·저장 값, 문단 태그·폭·높이·미번역 fallback 포함.
- 실제 catalog의 모든 키를 CP932 HSP fixture로 컴파일해 렌더하는 `tests/localization/run_catalog_smoke.py`. 최종 5,058키, 충돌0 catalog를 최신 Release에서 전부 그려 lookup 실패0/글리프 오류0을 확인했다(`work/localization-font-audit-release-final.log`). 이것은 글꼴 검사이며 모든 UI 배치가 맞다는 뜻이 아니다.
- 초기 catalog와 후속 전체 catalog의 같은 신규게임 입력은 각각 exit0/4,358프레임, 미지원·오류0. 생성된 세이브154파일의 압축 해제 내용이 같았다. `work/game-run/ko-fullcatalog-newgame-01/save-display-parity.json`.
- 노른79본문행/48페이지를 원래 본문폭330, 창480×175로 렌더한 fixture: 높이 초과0, 최대 다음y171, 글자 픽셀 최대x448/y166. 실제 일본어 기반 게임에서도 집 안내2페이지·세계지도 안내4페이지의 한글·강조를 확인했다. 신규게임/로드/집탈출 대조6/6, 저장154/154·154/154·162/162파일 압축 해제 내용 동일이다. [문단 경계 기록](KOREAN_RENDER_BOUNDARY.md).
- Windows 실제 창의 기본 한글 표시/입력 값 보존은 별도 fixture에서 통과했다. 이후 `gui-native-ko-managed-03`에서는 최신 문단 ABI·5058키 렌더·기준/관리fallback/한국어 관리 AX의 신규게임/로드/노른/세계지도9회가 모두 통과했다(41개 검사). 저장154/154/162파일은 기준과 동일했다. 이 최신 전체 게임 검사는 headless이며 실제 OS 창 장시간 플레이를 뜻하지 않는다. [WINDOWS_GUI.md](WINDOWS_GUI.md).
- Windows Malgun/Gulim에 U+30FB(일본어 가운데점)가 없어 전체 catalog 검사가 처음 실패했다. 해당 글자만 기존 원문용 MS Gothic에서 가져오는 글리프별 fallback을 추가했고5058키 전체 검사로 해결을 확인했다. 두 글꼴 모두 없는 글자는 오류로 진단한다. 구두점을 삭제하거나 다른 기호로 바꾸지 않았으며 해상도/HiDPI 정책도 변경하지 않았다.

## 실행과 남은 일

한국어판은 현재 v13, v12, 이명을 지원하는 v11, v8, 책 표시를 지원하는 v7, 초기 v2/v1 순서로 준비된 AX를 선택한다. v3–v6은 일본어 기반 게임에서 소지품을 열 때 영어 분류명 배열을 잘못 읽는 문제가 있어 자동선택에서 제외했다. 설치된 AX가 없으면 정규화한 원본으로 실행하고 준비 명령을 알려 준다. [준비 명령](DEVELOPMENT.md)

후반 분기·무작위 조합·추가 서적·그래픽에 들어간 글자의 전체 검수와 한국어 이름 입력은 후속 범위다. 수치 적용이나 세이브를 바꾸지 않고 표시 경계에서 처리한다.
