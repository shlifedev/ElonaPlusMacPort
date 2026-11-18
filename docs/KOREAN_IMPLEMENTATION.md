# 한국어 표시 구현과 현재 범위

기준은 사용자 제공 ElonaPlus 2.335다. 아직 전체 한글 패치나 배포판이 아니다. 번역된 데이터 수, 실제 화면에 적용되는 문자열 수, 플레이 검증 범위를 구분한다.

## 2026-11-18 영어 기반 플레이 보완

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

재생성:

```sh
python3 tools/compile_localization.py \
  localization/ko-KR/community-reviewed.json \
  localization/ko-KR/original-ui.json \
  localization/ko-KR/original-controls.json \
  localization/ko-KR/original-database.json \
  localization/ko-KR/original-races-classes.json \
  localization/ko-KR/original-tutorial.json \
  localization/ko-KR/original-dialogs.json \
  localization/ko-KR/original-creation-prompts.json \
  localization/ko-KR/original-english-help.json \
  localization/ko-KR/original-home-status.json \
  --output localization/ko-KR/runtime.tsv
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

한국어 런처는 준비된 `work/localization/managed-home-v2/start.ax`의 SHA-256을 확인하고 문단·HUD·로그 번역을 사용한다. 기존 작업공간에 v1 관리 AX만 있으면 문단 경로를 사용하고, 관리 AX가 없으면 정규화된 원본 AX와 고정 문구 번역으로 실행한다. [준비 명령](../README.md)

남은 작업은 NPC 대사의 사전 줄바꿈 경계, 동적 주어·조사·아이템 이름, 이름/문맥 키 분리, 아직 범위 밖인 본문·퀘스트·후반 콘텐츠, 모든 화면의 실제 배치 검수다. 한국어 이름/IME를 CP932 VM에 저장하는 기능은 구현하지 않았다.
