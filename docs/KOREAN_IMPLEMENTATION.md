# 한국어 표시 구현과 현재 범위

기준은 사용자 제공 ElonaPlus 2.335다. 아직 전체 한글 패치나 배포판이 아니다. 번역된 데이터 수, 실제 화면에 적용되는 문자열 수, 플레이 검증 범위를 구분한다.

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
| `original-tutorial.json` | 첫 집91항목, 노른18블록79본문행과 제목1개. 동적 템플릿6개는 현재 자동 적용 보류 |
| `english-ambiguities.json` | 2.335 조사에서 중의성이 발견된 영어 표시 키의 UTF-8 SHA-256 목록. 원문과 대응 일본어 문장은 제외 |
| `runtime.tsv` | 검수 완료·비충돌 고정 표시 키만 수록한 생성물 |
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
  --output localization/ko-KR/runtime.tsv
```

`reviewed`만 후보로 사용하고 `context_required`/`template_reviewed`는 그대로 넣지 않는다. JP/EN 별칭을 개별로 비활성화할 수 있으며 충돌한 키는 제외한다. 수치·서식·강조 태그 순서도 점검한다. 옛 `ko.txt` 전체 사전은 공개 재사용 조건 미확인이라 런타임에 일괄 가져오지 않았다. [출처 조사](KOREAN_SOURCES.md)를 참조한다.

## 확인한 검사

- Mac 개발/Release CTest11/11. strict UTF-8/중복 키, 문맥·서식 제외, 실제 한글 픽셀, VM 원문/입력·저장 값, 문단 태그·폭·높이·미번역 fallback 포함.
- 실제 catalog의 모든 키를 CP932 HSP fixture로 컴파일해 렌더하는 `tests/localization/run_catalog_smoke.py`. 최종 5,058키, 충돌0 catalog를 최신 Release에서 전부 그려 lookup 실패0/글리프 오류0을 확인했다(`work/localization-font-audit-release-final.log`). 이것은 글꼴 검사이며 모든 UI 배치가 맞다는 뜻이 아니다.
- 초기 catalog와 후속 전체 catalog의 같은 신규게임 입력은 각각 exit0/4,358프레임, 미지원·오류0. 생성된 세이브154파일의 압축 해제 내용이 같았다. `work/game-run/ko-fullcatalog-newgame-01/save-display-parity.json`.
- 노른79본문행/48페이지를 원래 본문폭330, 창480×175로 렌더한 fixture: 높이 초과0, 최대 다음y171, 글자 픽셀 최대x448/y166. 실제 일본어 기반 게임에서도 집 안내2페이지·세계지도 안내4페이지의 한글·강조를 확인했다. 신규게임/로드/집탈출 대조6/6, 저장154/154·154/154·162/162파일 압축 해제 내용 동일이다. [문단 경계 기록](KOREAN_RENDER_BOUNDARY.md).
- Windows 실제 창의 기본 한글 표시/입력 값 보존은 별도 fixture에서 통과했다. 이후 `gui-native-ko-managed-03`에서는 최신 문단 ABI·5058키 렌더·기준/관리fallback/한국어 관리 AX의 신규게임/로드/노른/세계지도9회가 모두 통과했다(41개 검사). 저장154/154/162파일은 기준과 동일했다. 이 최신 전체 게임 검사는 headless이며 실제 OS 창 장시간 플레이를 뜻하지 않는다. [WINDOWS_GUI.md](WINDOWS_GUI.md).
- Windows Malgun/Gulim에 U+30FB(일본어 가운데점)가 없어 전체 catalog 검사가 처음 실패했다. 해당 글자만 기존 원문용 MS Gothic에서 가져오는 글리프별 fallback을 추가했고5058키 전체 검사로 해결을 확인했다. 두 글꼴 모두 없는 글자는 오류로 진단한다. 구두점을 삭제하거나 다른 기호로 바꾸지 않았으며 해상도/HiDPI 정책도 변경하지 않았다.

## 실행과 남은 일

`Play ElonaPlus Korean.command` 또는 `python3 tools/play_game.py --language ko`는 준비된 `work/localization/managed-gmes-v1-01/start.ax`의 SHA-256을 확인하고 문단 번역을 활성화한다. 관리 AX가 없는 작업공간에서는 표준 오류로 알리고 정규화된 원본 AX로 고정 문구를 번역한다. 해시가 다른 관리 AX는 실행 전에 오류로 보고한다. 준비 명령은 [문단 경계 문서](KOREAN_RENDER_BOUNDARY.md)에 있다. 두 AX 모두 미등록 문단의 글자 단위 출력은 원문으로 유지하며, 같은 글자가 독립 메뉴 항목으로 표시될 때는 사전을 적용한다. 일본어 원문 기반 노른 번역을 영어 도움말의 서로 다른 페이지에 임의 대응시키지 않는다.

남은 작업은 NPC 대사의 사전 줄바꿈 경계, 동적 주어·조사·아이템 이름, 이름/문맥 키 분리, 아직 범위 밖인 본문·퀘스트·후반 콘텐츠, 모든 화면의 실제 배치 검수다. 한국어 이름/IME를 CP932 VM에 저장하는 기능은 구현하지 않았다.
