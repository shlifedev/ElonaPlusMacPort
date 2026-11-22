# 2.335 한국어 DB 번역 진행 기록

- 담당 파일: `src/localization/ko-KR/original-database.json`, `original-races-classes.json`. 원본·VM·세이브를 변경하지 않는 독자 번역 자원이다.
- 입력: 비공개 `work/localization/2.335/static-pairs.json` 중 일본어 복원 소스 1..23800행에 등장하는 2,406개 고유 쌍. 작업 인덱스는 `work/localization/2.335/database-scope.json`이다.
- 문체·용어 기준: `src/localization/ko-KR/style-guide.md`, `glossary.tsv`. 일본어 DB 정의를 우선하고 영어는 대조한다. 옛 비공개 번역 메모리 문장을 가져오지 않는다.
- 전체 범위 및 정적 QA 완료: 2,406/2,406개 저장, `reviewed` 2,263개, `context_required` 143개. 원문 범위의 모든 항목을 읽고 번역/문맥 보류/기술 값 제외를 구분했다. 첫 패스(2,267/139) 이후 미확정 장소명 `陽牢の塔`를 포함한 의뢰 문장 4개도 함께 보류했다. **전체 게임 번역 완료나 실제 화면 번역률을 뜻하지 않는다.**
- 후속 이름 자원: 종족 76개·직업 12개, 총 88개 `reviewed`. 원본의 `racename` 및 `classname` 정의를 확인하고 용어집 표기를 적용했다. 기본 종족 11개·직업 12개에 대해 **목록 정의→`cnven` 변환→선택 목록 표시 경로**와 `ko-newgame-01/missing.tsv` 관측이 일치하는 대문자 표기 23개만 명시적인 `source_aliases`로 추가했다. 단순 문자열 관측이나 임의의 대소문자·부분 문자열 치환은 근거로 삼지 않는다.

## 판단 기록

- 기술 이름·효과·장비 인챈트 설명은 `skillname/skilldesc/skillencdesc`, 버프 이름·발생 문구는 `buffname/bufftxt` 원문 문맥을 확인한다.
- `begin/get/feel/start` 등 영어 동사 조각은 여러 일본어 문장과 대응하므로 전역 치환하지 않는다. 동적 주어·조사 결합이 필요한 버프 발생 문장은 번역안을 보관하되 `context_required`, `english_enabled:false`로 표시한다.
- 일본어에 없는 영어의 10배 공격력, 확정 사망, 특정 능력 한정 효과를 번역에 임의로 넣지 않는다. 예를 들어 저주 완료 피해는 일본어대로 9999이며, 능력 20％/시전 성공률 3％ 감소에서는 영어에만 있는 학습 감소를 추가하지 않는다.
- 일본어에 있고 영어에 빠진 게이지 소모·내성·DV·함정 설치 등의 효과를 보존한다. MP/HP/SP/PV/DV·수치·부호는 사라지지 않게 대조한다.
- `Speed`의 가속/속도, `Form Shift`의 태그 포스/형태 전환, 잘못 붙은 `Mana slash` 등 영어 의미 충돌은 별칭 비활성화로 처리한다.
- 빈 문자열 초기값은 번역 대상에서 제외한다. 종이 식신의 고유명 말장난 등 확정이 어려운 항목은 보류 사유를 기록한다.
- 다중 행 번역은 추후 화면별 줄바꿈 구현을 위해 `layout_status:needs-layout`로 표시한다. 현재 표시 계층의 정확한 문자열 치환으로 모든 DB 조각이 표시된다고 주장하지 않는다.
- `大嫌い`, `うざい`는 관계 단계 레이블과 대사가 다른 한국어 표현을 요구한다. 양쪽 항목의 일본어 전역 키를 비활성화했다. `ありがとう！/Thanks!`는 플레이어 생일 응답과 다른 화자의 감사 인사에 공통으로 쓰이므로 두 언어의 전역 키를 비활성화했다.
- 종족 `蜘蛛`와 몬스터 `まだら蜘蛛`의 영어 `spider`, 종족 `馬`와 탑승 대상 선택 레이블 `馬`도 전역 치환으로 구분할 수 없다. 각각의 충돌 키를 양쪽 자원에서 제외하고 문맥별 표시 경계를 기다린다.
- 이름의 일본어를 영어 식별자보다 우선한다. `dwarf`는 언덕의 민족, `mutant`는 카오스 셰이프, `catsister`는 수인이다. `駒`는 고성·캐릭터 카드의 체스 문맥을 확인해 체스 말로 옮겼다.
- 별칭 재검토 정정: 초기 24개 중 `God`는 종족 신이 아니라 캐릭터 시트의 신앙 레이블(297424/297427행)이었으므로 제거했다. 남은 23개는 기본 종족 목록(`val=0`, 433818~434128행), 직업 목록(439531~439553행), 표시 코드(종족 223502/223547행, 직업 223818/223820행)를 모두 대조했다. 각 항목의 `alias_evidence`에 개별 목록 정의 행과 표시 경로를 기록한다. `Fox`는 입력 스크립트가 지정한 사용자 종족명이며 이 자원의 번역/별칭에 포함하지 않았다.

## 수행한 QA와 재현

- 입력 ID 2,406개가 중복·누락 없이 유지되고 일본어·영어 원문 및 모든 원문 행 참조가 private 입력과 일치한다.
- 검토 완료 항목의 서식 표식(`@QM`, `%d/%s`, 색상 태그, 이스케이프 등) 불일치 0, 설명되지 않은 수치 불일치 0, 뜻하지 않은 가나 잔존 0을 확인했다. 한자 숫자→아라비아 숫자, `1日`→하루 같은 12개 표기 차이는 별도 근거를 남겼다.
- 현재 여섯 입력 자원 통합 scratch compiler 결과는 5,058개 키, 충돌 0이다. 이는 해당 입력 해시 시점의 수치이며 이후 다른 담당자의 번역 추가로 변한다. 컴파일된 provenance에 보류 항목이 포함되지 않음을 확인했다.
- private 재현·근거: `work/localization/2.335/database-qa.py`, `database-qa.json`, `database-qa-runtime.report.json`, `database-qa-runtime.sources.json`. 결과 JSON에는 입력 해시와 수치 표기 예외가 들어 있다. `db-batch-001.tsv`부터 `025.tsv`까지가 첫 패스 체크포인트다.

```sh
python3 scripts/compile_localization.py \
  src/localization/ko-KR/original-ui.json \
  src/localization/ko-KR/original-database.json \
  src/localization/ko-KR/original-races-classes.json \
  src/localization/ko-KR/original-tutorial.json \
  src/localization/ko-KR/original-controls.json \
  src/localization/ko-KR/community-reviewed.json \
  --output work/localization/2.335/database-qa-runtime.tsv
python3 work/localization/2.335/database-qa.py
```

## 남은 범위

보류 143개에는 동적 주어·조사 결합, 퀴즈의 일본어 철자 구분, 성별/대상의 문맥이 불분명한 선택지, 말장난 고유명, 장소명 미확정, 파일명·언어 코드·빈 문자열 같은 번역 제외 값이 함께 들어 있다. 보류 항목에 후보 번역이 있어도 런타임에 적용하지 않는다.

정렬된 상점 메뉴와 긴 일지 문단 260개는 `layout_status:needs-layout`이다. 의미·수치 검토를 마쳤어도 화면의 줄바꿈·잘림·읽기 순서는 별도 검증해야 한다. 추가 종족·직업 파일은 이름만 포함하며, 434k~440k행의 긴 종족·직업 설명은 이번 1..23800행 DB 범위에 포함되지 않았다. 후반 대사·아이템 DB 전체·동적 설명까지 번역했다고 보고하지 않는다.
