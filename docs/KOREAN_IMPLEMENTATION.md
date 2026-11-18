# 한국어 표시 구현과 현재 범위

기준은 사용자 제공 ElonaPlus 2.335다. 아직 전체 한글 패치나 배포판이 아니다. 번역된 데이터 수, 실제 화면에 적용되는 문자열 수, 플레이 검증 범위를 구분한다.

## 현재 번역량과 플레이 검증

원본 스크립트의 고정 일본어·영어 문장 12,405쌍 중 **9,309쌍(75.04%)**에 한국어 표시 키를 연결했다. 원문 위치를 추적하는 검수 사전에서 3,990쌍, 별도로 일본어와 대조하고 직접 고친 번역에서 1,054쌍, 자동 번역·기계 검사 후의 추가 초벌에서 4,265쌍을 계산했다. 한 쌍에 두 언어가 모두 등록되더라도 한 번만 센다. 미결합 3,096쌍, 실행 중 조립되는 표현식 후보 4,785개, 외부 책·대화 파일, 그림 속 글자는 별도로 관리한다. 특정 키가 있는 것만으로 모든 사용 화면에서 표시됐다고 계산하지 않는다. [집계와 입력 해시](../localization/ko-KR/coverage.json)

Mac Release에서 원래 영문 기반 입력으로 전사·마법사 생성, 첫 집, 세계 지도, 베르니스, 장비, 전투·전리품 획득, 재시작을 포함한 8개 경로를 재생했다. 연속 영문자 3자 이상이 포함된 고유 미번역 표시값은 437종에서 **77종**, 그리기 횟수는 46,320회에서 **4,599회**로 줄었다. 반복 출력과 저장 슬롯명 `sav_Ari`, 사용자 이름 `Ari` 등도 포함한 값이다. 기본판과 검수판은 이 8경로에서 동일한 누락 건수였다. 두 판본 모두 기준 실행과 각 단계의 최종 프레임 및 압축 해제한 세이브 전체(단계별 154~170파일)가 일치했다. 집 출발 단계는 입력 스크립트가 정한 1,851프레임 대기 상한으로, 나머지 7단계는 게임의 정상 종료로 끝났다. 개인 세이브는 테스트에 사용하지 않았다.

- Mac Release CTest 12/12, 표시 키·소스 변환 단위 검사 12/12. 검수 사전 11,465키, 전체 추가 표시 키 7,317개(직접 교정 부분집합 1,398개)를 각각 CP932로 다시 전달해 한국어 폰트로 렌더했다. macOS SDL3 창의 실제 렌더 표면에서 1,600×1,200 종족 선택 화면을 캡처했다. `work/ko-broad/final-default/summary.json`, `work/ko-broad/final-reviewed-only/summary.json`, `work/ko-broad/sdl-final`에 재현 근거가 있다.
- Windows x64 Release `gui-native-ko-broad-final01`에서 42/42 검사 통과. 원본/관리 원문/관리 한국어 각각 일본어 기반 신규 게임·소지품 확인 후 재저장·세계 지도 이동을 실행했다. 세이브 전체 154/154/162파일과 원본 불변을 대조했다. 실제 Windows 폰트와 게임 런타임을 사용한 headless 검사였다. 이전 Mac 결과와 동일하게 검수 키 11,465개·전체 추가 표시 키 7,317개를 모두 조회했다. `work/windows/reference-build/gui-native-ko-broad-final01/verify.json`에 결과가 있다.

새 작업 폴더로 검사를 재현할 수 있다. 아래 `--reference`는 이전 8경로 실행의 결과가 있을 때만 추가한다.

```sh
python3 tests/localization/run_play_audit.py --run ko-recheck --ax work/localization/managed-creation-v8/start.ax --catalog localization/ko-KR/runtime.tsv --fallback localization/ko-KR/draft-display.tsv --reference work/ko-broad/base
python3 tools/reference/windows_managed_check.py --run gui-native-ko-recheck --catalog localization/ko-KR/runtime.tsv --fallback localization/ko-KR/draft-display.tsv
```

## 2.335 한국어판의 표시 경계

현재 런처는 `managed-creation-v8`을 기본으로 선택한다. SHA-256은 `0584039d76e73535d07b7dd8157fba729a6204aa50e827ef27e9c0f5dac4ba17`이다. 검수 사전을 우선 조회하고 추가 정적 문구에는 별도의 한국어 초벌 사전을 사용한다. `--language ko --reviewed-only`로 검수 사전만 사용할 수 있다. [초벌 생성과 교정](KOREAN_STATIC_DRAFTS.md)

| 표시 영역 | 근거 |
|---|---|
| 캐릭터 생성 | 종족 11종·직업 12종, 일본어/영어 고유 설명 44개. 같은 영문 자리표시자를 쓰는 두 직업은 원본 클래스 ID로 구별한다. 실제 425px 폭 문단에서 최대 높이 82px로 다음 제목까지의 여유 104px 안에 들어갔다. 획득 기술·능력 보너스·숙련 무기도 처음 화면에서 읽을 수 있다. |
| 인벤토리·장비 | 원본에 이름이 있는 아이템 1,322종에 ID별 이름이 있다. 알려진 장비와 재료·수량·수치에 적용한다. 미감정 외관, 사용자가 지은 이름, 아직 해석하지 못한 조합은 원문을 표시한다. |
| 마을·퀘스트 | 도시 고유 대사 226행과 상점·교관·동료·주민 614행을 원본 파일과 대조했다. 게시판 88종의 제목과 본문에서 목표·수량·지명·보상을 분리했다. 팔미아·레시마스·길드 등 별도 의뢰와 이야기 419문구를 독자 번역했다. |
| 대화·도움말·책 | 내 집의 로미아스·라네이레, 양 판본의 프롤로그 5쪽, 마을 소문, 노른의 영문 도움말 18주제 46쪽(원문 182행), 초기 서적 4권의 양 판본 36쪽. JP의 「길 잃은 병사를 위한 교범」과 EN의 「초보 모험가 안내서」는 저자와 내용이 달라 따로 번역했다. |
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

한국어판은 v8, 책 표시를 지원하는 v7, 초기 v2/v1 순서로 준비된 AX를 선택한다. v3–v6은 일본어 기반 게임에서 소지품을 열 때 영어 분류명 배열을 잘못 읽는 문제가 있어 자동선택에서 제외했다. 설치된 AX가 없으면 정규화한 원본으로 실행하고 준비 명령을 알려 준다. [준비 명령](DEVELOPMENT.md)

후반 분기·무작위 조합·추가 서적·그래픽에 들어간 글자의 전체 검수와 한국어 이름 입력은 후속 범위다. 수치 적용이나 세이브를 바꾸지 않고 표시 경계에서 처리한다.
