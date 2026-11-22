# 2.335 일본어 정적 표시문 초벌

정적 `lang(일본어, 영어)` 쌍을 대상으로 일본어 원문에서 한국어 표시 후보를 생성한다. 전문 용어집과 기존 검수 번역이 먼저 적용된다. 게임에 들어가는 자료는 한국어 문자열과 식별 해시다. 모델, 복원 스크립트, 플레이어 세이브는 패키지에 포함하지 않는다.

## 입력과 생성

`scripts/extract_localization.py`가 만든 12,405개 고유 쌍에서 조사했다. 원본 스크립트 SHA-256은 `c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55`다. `draft-static.tsv`의 첫 필드는 일본어, NUL, 영어를 이어 붙인 UTF-8 바이트의 SHA-256 앞 20자리다. 둘째 필드는 한국어다. `draft-static-edits.tsv`에는 문맥을 대조해 직접 작성하거나 고친 항목을 같은 형식으로 기록했다. 별도 검수자가 대조한 장문 73쌍을 담은 `draft-linguistic-edits.tsv`를 마지막으로 적용한다. 두 교정 파일 모두 정적 ID와 원문의 숫자를 확인한다.

개발 Mac의 MLX에서 Google TranslateGemma 12B를 4비트로 실행했다. 게임 텍스트는 로컬에서만 처리했다. Japanese→Korean 템플릿, greedy decoding, 행별 토큰 상한을 사용했다. 앞서 시험한 M2M100 418M은 개구리를 거북이로, 금화를 달러로 옮긴 사례가 있어 채택하지 않았다. [TranslateGemma 모델 카드](https://huggingface.co/google/translategemma-12b-it) · [MLX 변환본](https://huggingface.co/mlx-community/translategemma-12b-it-4bit)

TranslateGemma 가중치에는 Gemma 이용 조건이 적용된다. 이 조건의 1.1(e)와 3.3은 생성 결과와 모델 파생물의 구분, 출력에 대한 Google의 권리 주장을 설명한다. 원작 게임의 텍스트 권리는 원작자에게 있다. 기존 한국어 팬 번역은 모델 입력이나 번역문으로 사용하지 않았다. [Gemma 이용 조건](https://gemma.ai/resources/terms/)

LaBSE의 CLS 벡터에 배포된 Dense/Tanh 및 정규화를 적용하고 일본어·한국어 벡터의 코사인 유사도를 구했다. 한쪽이라도 LaBSE의 256토큰 입력 범위를 넘으면 문장 앞부분만으로 승인하지 않고 보류한다. 숫자 또는 영문 토큰 불일치, 인식되지 않은 제어 표기, 원문 문자 잔류, 용어집의 신명·종족·지명·세계관 용어 불일치 및 반복 출력을 별도로 검사한다. 7자 이하 원문은 0.78, 더 긴 원문은 0.72 이상을 후보로 삼는다. `draft-english-ids.txt`는 실제 원본 일본어와 영어의 유사도가 0.70 이상인 초벌 쌍만 담는다. 원본 영어가 전혀 다른 짧은 대사인 경우도 있어 한국어 출력과 별도로 검사한다. 이 값은 번역의 정확도 백분율이 아니다. 지칭 대상, 이름의 표기, 높임말과 부분 누락은 직접 대조가 필요하다. [LaBSE 모델 카드](https://huggingface.co/sentence-transformers/LaBSE)

## 재현

실행 환경은 macOS ARM64, Python 3.12, mlx 0.32.4, mlx-lm 0.32.2, transformers 5.23.0, torch 2.15.0이다. 먼저 모델 제공처에서 아래 리비전의 파일을 로컬에 준비한다. `hf download`에는 모델 식별자와 파일명만 전달한다.

```sh
uv venv --python 3.12 .cache/ko-model/venv
uv pip install --python .cache/ko-model/venv/bin/python mlx==0.32.4 mlx-lm==0.32.2 transformers==5.23.0 torch==2.15.0
.cache/ko-model/venv/bin/hf download mlx-community/translategemma-12b-it-4bit --revision f3dcfd54df14672fbcf0731086fb47a797a943ae --local-dir .cache/ko-model/translategemma
.cache/ko-model/venv/bin/hf download sentence-transformers/LaBSE --revision 836121a0533e5664b21c7aacc5d22951f2b8b25b --include config.json tokenizer.json tokenizer_config.json model.safetensors '2_Dense/*.safetensors' --local-dir .cache/ko-model/labse
python3 scripts/extract_localization.py
.cache/ko-model/venv/bin/python scripts/generate_korean_drafts.py
.cache/ko-model/venv/bin/python scripts/score_korean_drafts.py
.cache/ko-model/venv/bin/python scripts/score_korean_drafts.py --english
python3 scripts/generate_korean_drafts.py --export-only
python3 scripts/build_draft_display.py --reviewed-output src/localization/ko-KR/reviewed-display.tsv
python3 scripts/korean_coverage.py
```

| 가중치 | SHA-256 |
| --- | --- |
| TranslateGemma shard 1 | `bd64914bb159830648d444dec435236c2690214124761e78ece98d1ef1ee75af` |
| TranslateGemma shard 2 | `c3b207c1a3ebafc136664dba65b3f474f73191634428b8380204e647fc844b89` |
| LaBSE encoder | `77d8e1f2dbab6eb5d3c261ce9d3dbf1e3c69e02938c95f934f94f42c22dfa31f` |
| LaBSE Dense | `f866c945fa2147ce4940c8ead0b4b20af4217e0f4664bd35f41c2a0967c4fe77` |

중간 결과는 `work/ko-broad/`에 누적된다. 게임의 3인칭 종족·주민·신앙 설명과 1인칭 대사는 분리해서 교정했다. 일본어 문자열을 포함하는 다른 고유명도 문맥을 대조했다. 예를 들어 ‘폭풍의 룰위’는 이름 안에 ‘바람의 룰위’의 일본어 철자가 부분 일치하지만 서로 다른 형태다. 가중치나 템플릿을 교체하면 전용 캐시 경로를 새로 사용해야 한다. 표시 키 결합 도구는 영어의 공통 미번역 안내문, 모호한 키, 실제 인수가 필요한 표현을 보류한다. 동적 표현 4,785곳, 외부 텍스트 파일, 런타임 조합 문자열은 이 정적 쌍 개수에 들지 않는다.

## 검수에서 바꾼 내용

- 첫 모델은 ‘날개구리’를 거북이로 옮기고, 행성이 공격 대상이라는 대사에서 공격 주체와 대상을 뒤집었다. 두 문장과 ‘빠진 깃털’을 일본어·영어 쌍으로 확인해 직접 번역했다.
- 12B 초벌에서도 손윗누이가 친동생으로 바뀐 사례, 모험가를 경호원으로 고용하는 주체가 바뀐 사례, 때리겠다는 경고가 이미 부딪쳤다는 말로 바뀐 사례가 나왔다. 고용 주체, 친족 관계, 위협의 시제와 인칭을 원문에 맞췄다.
- 별도 검수에서 미끼를 낚아채고 잠수하는 주체, 에테르 항체를 건네는 방향, 사건의 냄새를 맡는 탐정의 대사 등을 고쳤다. 탐정 대사 끝의 여분 따옴표가 발견되어 자동 후보의 따옴표 균형과 인용 누락도 검사한다.
- 실제 원본의 영어 별칭 3개를 대조했다. ‘고마워!’를 뜻하는 일본어에 붙은 `Never!`는 0.416, ‘다음에 다시 온다’에 붙은 점괘 요청은 0.067, ‘체류 상태 변경’에 붙은 `Ally List`는 0.187이었다. 모두 영어 별칭에서 보류했다. 값은 일본어와 영어 사이의 LaBSE 코사인 유사도다.
- 룰위·이츠파로틀·주아 등 신명과 사도, 혼돈의 신과 네피아의 관계, 제옴과 레시마스의 내력에 해당하는 109쌍은 일본어에서 직접 번역해 기존 세계관 용어와 대조했다.
- 직접 번역한 행에는 게임 기제에 영향을 주는 동료 합성 시 영구 소실, 호위 실패, 무게 때문에 귀환·승선이 막히는 경우, 한도 초과로 신의 선물을 받지 못하는 경우, 과세 시점과 금액이 있다. 수동 교정은 기존 표시층 검수 항목 다음에 적용된다.


## 2026-11-18 최종 집계

- 고유 일본어 문자열 7,538개를 로컬에서 처리했다. 비어 있거나 토큰 제한에 걸린 출력 2개를 제외한 7,536개에서 LaBSE 의미 점수를 계산했다. 현재 검수표를 기준으로 남은 적격 미처리 원문은 0개다.
- 수동 소스는 직접 교정 1,071쌍과 독립 검수 73쌍, 합계 1,144쌍이다. 기존 검수표가 먼저 처리하는 33쌍을 제외한 1,111쌍과 기계 초벌 4,381쌍이 최종 `draft-static.tsv` 5,492쌍을 이룬다. 영어 측 원문 대응 검사에 통과한 쌍은 2,199개다.
- 표시 키 7,317개가 결합되었다. 그중 수동 교정만 묶은 부분집합은 1,398개다. `korean_coverage.py`에서 중복을 제거하면 12,405개 정적 쌍 중 9,309개(75.04%)에 한 언어 이상의 표시 키가 있다. 기존 검수 3,990쌍, 추가 직접 교정 1,054쌍, 추가 자동 초벌 4,265쌍이다. 3,096쌍은 표시 키가 없다. 기본 언어·게임 진행 단계·텍스트 출력 경로에 따라 실제로 보이는 범위는 달라진다.
- 동일한 비공개 입력 캐시에서 두 번 export하여 UTF-8 출력의 바이트 일치를 확인했다. `draft-static.tsv`의 SHA-256은 `5e9e45b8d79c0b5cbf114c44b0acbd1720357c1c61ff469b41132db3e0c8dc91`, `draft-english-ids.txt`는 `c5996b23f864f10a11eb1dc5296974f64f67cb17056a65d46e29f2215cc995f7`다. 숫자·기호·용어 보존과 따옴표 회귀 검사, 한국어 출력의 제어 문자 검사, 키의 원본 존재 여부, 잘못된 영어 별칭 3종의 제외를 확인했다.
