# 2.335 첫 집·노른 도움말 한국어 번역

2026-11-18 작성. 사용자 제공 ElonaPlus 2.335의 현재 일본어를 직접 번역하고 대응 영어와 대조했다. 기존 한글 패치의 긴 문장을 가져오지 않았으며, 이 파일을 통해 독립 번역이나 원본 게임에 새 프로젝트 라이선스를 지정하지 않는다.

## 완료 범위

| 구분 | 번역 결과 | 현재 런타임과의 관계 |
|---|---:|---|
| 첫 집·오프닝·로미아스 실습·라네이레 대화·작별 | 91개 항목, 원문 참조 102곳 | 85개 정적 항목과 6개 동적 템플릿으로 구별 |
| 노른 모험 안내 `data/exhelp.txt` | JP 18개 블록 전체, 48페이지, 본문 79행 | UTF-8 번역 작업본. CP932 VM에 파일 교체 방식으로 넣지 않음 |
| 노른 도움말 제목 | 1개 | static `lang` 표시 문자열 |
| 합계 | 171개 JSON 항목 | 번역 완료 범위이며, 화면 적용 완료 수치가 아님 |

산출물은 [original-tutorial.json](../localization/ko-KR/original-tutorial.json), 비공개 작업본 `work/localization/exhelp-ko.txt`, 원문 행별 번역표 `work/localization/home-ko.tsv`다. 입력별 해시와 수량은 `work/localization/tutorial-stats.json`에 있다. 원본 `elonaplus2.335/data/exhelp.txt`와 원시 HSP 소스는 수정하지 않았다.

## 조사한 문맥과 화자

- **라네이레:** HSP 373565–373690의 첫 집 대화 선택지, 깨어난 뒤 인사, 짐승으로 변한 왕자의 동화, 북부 티리스 소개, 사절의 목적. 초반 이후 다른 지역에서 만나는 분기는 이 범위에 포함하지 않았다.
- **로미아스와 라네이레의 집 튜토리얼:** 373797–374531. 튜토리얼 시작·거절, 먹기·파기·감정·장비·저주 해제·사격·상자·짐·집 관리, 재대화·분기 선택지, 라네이레가 없는 예외 분기, 작별·아이템 전달·시스템 로그까지 포함한다.
- **오프닝:** 418614–418874. `lomiaseaster` 분기의 동굴 조사·푸팃 농담, 의식 회복, JP/EN가 별도 `buff`에 있는 첫 대사와 플레이어 이름 삽입 문장까지 포함한다.
- **노른:** 초기 안내는 NPC 605의 대화 스위치가 아니라 HSP `label_9600`(603432 부근)이 `data/exhelp.txt`를 읽어 표시하는 모험 안내다. NPC 605의 393307 이후 대사는 후반 스토리이므로 초반 번역에 섞지 않았다. `%1`부터 `%18`까지는 모두 번역했다.

로미아스는 `자네`, `~하게/~네`로 능청스럽게 가르치고, 라네이레는 차분한 존대, 노른은 친근한 해체로 쓴다. 플레이어 선택지는 짧은 서술형, 시스템 로그는 평서형으로 구별한다. 캐릭터 생성 화면의 무인칭 안내 문체를 NPC 대사에 강제로 적용하지 않는다.

추가로 고정한 명사안은 **노른 / 푸팃 / 빈데일 숲 / 시에라 테르 / 자비 왕 / 자물쇠따개 / 주택 관리판 / 열광 상태**다. 푸팃은 기존 한국어 사전의 짧은 고유명 표기를 유지했다. 빈데일은 숲이므로 옛 사전의 `빈데일산`처럼 지형을 바꾸지 않았다. 기존 `glossary.tsv`는 이 작업에서 수정하지 않았다. 용어집 반영은 통합 담당이 조정한다.

## 원문 간 불일치와 보존 결정

| 문맥 | 현재 일본어 / 영어 차이 | 번역 처리 |
|---|---|---|
| exhelp 4 | 일본어는 ★와 왼쪽 숫자로 의뢰 난이도 설명. 영어는 $와 색상 설명 | 일본어의 별·숫자를 번역 |
| exhelp 7 | 일본어는 초반 여관 식비 부담과 야외 먹거리 설명. 영어는 여관 식사를 더 권장 | 일본어 안내를 보존 |
| exhelp 9 | 일본어는 수면 가능/필요 구별, 회복 중단, 침구 효과를 상세 설명 | 영어에서 생략된 중요 정보도 모두 번역 |
| exhelp 15 | 에테르병 20단계 설명 | 원문의 수치를 그대로 보존. 이 번역 작업에서 게임 로직 전체를 검증했다는 뜻은 아님 |
| exhelp 18 | 일본어는 3의 배수가 아닌 달의 15일~월말, 영어는 15~24일 | 일본어 기간과 4~5일 지속을 보존 |
| exhelp 18 태그 | `<emp1>`를 닫지 않고 다시 `<emp1>`가 나오는 원문 오자 | 요구된 원본 태그 보존을 우선. 자동으로 `<def>`로 수정하지 않음 |
| 라네이레의 북부 티리스 소개 | JP는 티리스 대륙 북쪽 지역. EN은 이르바 서반구의 대륙 | 일본어 지리를 번역, 해당 EN 전역 별칭 비활성화 |
| 로미아스의 북부 티리스 소개 | JP는 어느 나라의 지배도 받지 않음. EN은 팔미아 지배 | 일본어를 번역, 해당 EN 전역 별칭 비활성화 |
| 두 인물의 JP 설정 충돌 | 라네이레는 팔미아 통치, 로미아스는 무주권 지역으로 말함 | 원문 자체의 불일치로 기록. 번역자가 설정을 고치지 않음 |
| 집에서 마법 배우기 | 로미아스는 안전하게 마법을 배울 수 있다고 말함. 노른은 어려운 마법서의 위험을 경고 | 화자별 내용을 보존. 서로 다른 안내를 한 문장으로 합치지 않음 |
| 튜토리얼 종료 | EN은 단순 완료 메타 문구. JP는 앞으로의 모험에 대한 대사 | JP 대사를 번역, EN 전역 별칭 비활성화 |

## 데이터 규약

`source_ja`는 실제 일본어 문자열, `source`는 해당 영어 또는 빈 문자열이다. `source_ref`에는 파일·행을 두고 `speaker`, `origin`, `review_reason`을 모든 항목에 기록한다.

- `match_kind=exact`, `review_status=reviewed`: 실제 정적 문자열의 번역. `english_enabled=false`이면 일본어 키만 후보로 삼는다. 같은 영어에 여러 뜻이 있거나 현행 EN이 다른 내용을 말하는 경우를 포함한다.
- `match_kind=template`, `review_status=template_reviewed`: **6개 동적 문장**. `{player_name}`, `{player_title}`, `{speaker_name}`과 원래 HSP 표현식을 `placeholders`, `source_expression`에 보존한다. 현재 exact-match 런타임에 적용해서는 안 된다. 변수 치환·조사·문맥 처리를 지원하는 후속 경계가 필요하다.
- `match_kind=external_line`: 외부 도움말의 79행. `block_id`, `page`, `block_line`을 보존하며, 원문의 `<emp1>/<emp2>/<def>`를 순서까지 동일하게 유지한다. 영어 도움말은 행·페이지 경계와 내용이 다르므로 영어 줄을 임의로 1:1 대응시키지 않는다.

`work/localization/exhelp-ko.txt`의 블록 식별자는 `%1,KO` … `%18,KO`이며 각 블록은 `%END`로 닫는다. 본문 안의 빈 줄 위치가 JP와 같으므로 페이지 경계 48개가 유지된다. `KO`는 작업본의 언어 표시이며 게임의 `lang("JP", "EN")` 분기를 수정했다는 뜻이 아니다.

## 검증과 남은 통합

실행한 검사:

1. 18개 JP 블록과 KO 블록의 ID·본문 행 수·빈 줄 위치 일치.
2. 본문 79행의 강조 태그 목록이 순서까지 일치.
3. 선정한 첫 집 범위의 대사·선택지·로그 102곳을 빠짐없이 수록하거나 동일 문구로 묶었는지 대조.
4. 동적 문장 6개의 자리표시자 집합이 원본 변수와 일치.
5. UTF-8/NFC, JSON 구문, ID 중복, 원문 해시·행 참조 검사.
6. 기존 `original-ui.json`/`community-reviewed.json`의 적용 가능한 동일 키와 번역 충돌 여부 확인.

HSP는 도움말에서 줄을 읽은 뒤 `gmes`로 표시한다. 현재 표시 계층이 강조 태그 처리나 원문 줄바꿈 뒤 잘린 조각만 받는다면 긴 원문 줄의 exact key가 일치하지 않을 수 있다. 이 경우 번역 데이터가 있어도 화면은 미번역으로 남는다. **330px 본문 폭과 175px 창, 글자 폭·줄바꿈·페이지 높이는 실제 화면에서 별도 검증해야 한다.** 번역문을 CP932로 억지 인코딩하거나 raw HSP/AX 문자열을 교체하지 않는다.

후속 통합에서 `gmes` 전체 문자열 경계를 구현했다. **79행/48페이지 native fixture의 높이 초과 0**, 실제 JP 기반 게임의 **집 안내 2페이지·세계 지도 안내 4페이지** 한국어 표시를 확인했다. 신규 게임·재시작 로드·집 탈출을 원문과 대조한 6회 실행은 오류 없이 끝났고, 압축 해제 저장 내용은 단계별 154/154·154/154·162/162파일이 같았다. 세부 실행물·해시·화면·재현 명령과 남은 대화 경계는 [KOREAN_RENDER_BOUNDARY.md](KOREAN_RENDER_BOUNDARY.md)를 따른다. 번역된 171항목 전체가 화면에 적용됐다는 의미는 아니다.

첫 집·노른 외 전체 퀘스트, 후반 대사, 아이템 설명, 주문·기술 설명, 책, 생성되는 이름, 다른 외부 데이터는 이번 범위에 포함하지 않았다. 이 범위를 넘어선 게임 전체 한국어 번역 완료라고 보고하지 않는다.

## 표시 경계 조사와 구현 제안

이 절의 앞부분은 소스에서 확인한 사실이고, 뒤의 상세 ABI와 대화 경계는 **조사 당시의 설계 제안**이다. 이후 gmes의 8인수 명령·관리 소스 변환·원문 fallback 게임 비교와 노른 48페이지 native fixture를 구현·검증했다. 최신 구현 계약과 실제 결과는 [KOREAN_RENDER_BOUNDARY.md](KOREAN_RENDER_BOUNDARY.md)를 따른다. 대화 전체 문단과 동적 템플릿 경계는 아직 후속 범위다. 행 번호는 `work/decompiled/2.335-reference/start.hsp`의 SHA-256 `c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55`를 기준으로 한다.

### 현재 문자열이 잘리는 위치

| 경계 | 확인한 동작 | 번역 경계에 주는 제약 |
|---|---|---|
| `gmes` 정의 38644 부근 | 입력 전체 문자열에 `$end`를 붙인 뒤 `peek`/`strmid`로 CP932 1~2바이트를 읽어 한 글자씩 `mes`에 전달한다. 태그·색·폰트·그림자·줄바꿈도 여기서 처리한다. 마지막에 `gmesx`를 원래 x로 되돌리고 `gmesy`를 다음 줄로 옮긴다. | 일반 `mes` 훅에서는 완전한 문장을 얻을 수 없다. **gmes 진입 전 원문 전체**가 필요하다. 한 글자 치환 사전은 만들지 않는다. |
| `talk_conv` 정의 7951 부근 | CP932 문자열 변수 자체에 줄바꿈을 넣는다. 일본어는 바이트/문장부호, 영어는 단어 기준이다. 줄 수를 반환한다. | 이 함수를 전역으로 우회하거나 UTF-8을 넣으면 다른 호출부의 레이아웃·문자열 의미가 변한다. |
| 대화 공통 루틴 `label_7082`, 430321 | `talk_conv buff, 56-en*3`을 호출한 뒤 대화 창·초상화 등을 준비한다. | 이 호출 **직전**의 `buff`와 화자/문맥을 복사해야 정적 문장 키와 일치시킬 수 있다. |
| 대화 본문 431523–431537 | `notesel buff` 후 `noteget` 한 줄씩 x=`wx+150`, y=`wy+43+cnt*19`에서 `mes`로 그린다. | 본문 루프만 네이티브 문단 표시로 대체할 수 있다. 주변 초상화·선택지·`chatval` 처리와 분리한다. |
| 노른 도움말 `label_9600`, 603432 이후 | `exhelp.txt`에서 `%N,JP/EN` 블록을 찾고 빈 줄까지 페이지를 읽어 `gmes s`를 호출한다. 창은 480×175, 본문 x=`wx+120`, y=`wy+55`, 폭=330이다. | 줄 단위 gmes 경계로 번역은 찾을 수 있으나, 번역문이 길면 원래 페이지의 남은 높이가 부족할 수 있다. 최종 높이 검사는 페이지 단위로 해야 한다. |

호스트의 `mes` 명령은 `src/app/gui_host.cpp`에서 `Screen::print`로 전달된다. `src/graphics/text_renderer_ttf.cpp`의 `draw_display_line`은 CP932를 UI 경계에서 UTF-8로 바꾸고 exact catalog를 찾는다. 이 경계는 짧은 독립 UI 문자열에는 적합하지만, 이미 잘린 gmes 글자나 talk_conv 조각을 원문 문장으로 복구할 수 없다. 편집 필드용 `draw_line`/`measure_line`은 원문 의미를 유지해야 한다.

### 우선 적용할 gmes 경계

가장 작은 첫 단계는 해시가 고정된 관리 소스 변환으로 `gmes` 진입부에 **명시적 네이티브 표시 명령**을 추가하는 것이다. 개념상 계약은 다음과 같다. 아래 이름과 형식은 제안이며 실제 opcode를 배정한 상태가 아니다.

```text
ko_gmes_try(
  out_handled:int32-ref, out_next_y:int32-ref,
  original:CP932-string, context_id:int32,
  surface_id:int32, x:int32, y:int32, width:int32,
  base_color:int32[3], shadow_mode:int32, language:int32
)
```

번역이 없거나 locale이 꺼져 있으면 `handled=0`만 돌려 원래 gmes 본문을 그대로 실행한다. 번역이 있을 때만 네이티브가 UTF-8 문단의 태그·색·그림자·줄바꿈을 처리하고 실제 다음 y를 돌려준다. 관리 소스는 그 경우에 한해 `gmesx`를 원래 x로, `gmesy`를 반환 y로 설정하고 원래 글자 루프를 건너뛴다. 문자열과 저장 데이터는 계속 CP932이며 한국어 문자열은 호스트 안에만 존재한다.

실제 계약에는 `gmes` 종료 후 관찰 가능한 폰트·색·커서 상태도 포함해야 한다. 숨은 `stat`, `refstr`, `strsize`, 대기 상태를 임의로 변경하면 안 된다. 기존 generic DLL 호출은 반환값으로 `stat`를 바꾸므로, #func를 추가하기만 해서는 부작용 없는 명령이 되지 않는다. 로컬 OpenHSP의 `.cache/upstream/OpenHSP/common/hsp3dish.as`와 `dish_enhance.as`에 `#regcmd`/`#cmd`의 실제 사용 예가 있다. 호스트에 별도 명령 타입을 등록하고, 현재 타입 점유 목록을 조사한 뒤 빈 ID를 배정하는 방식이 적합하다. 64비트 포인터를 HSP 정수로 전달하지 않고, 필요한 핸들은 수명이 제한된 int32 ID로 관리한다.

본문 안의 `<emp1>`, `<emp2>`, `<def>`, `<title1>`, `<p>`, `<br>`, `<b>`, 색 지시·이스케이프를 원본 계약에 맞게 처리한다. 이번 도움말은 강조 세 종류만 쓰지만 gmes는 다른 화면에서도 쓰이므로 범용 구현이 아닌 첫 단계에서는 지원 대상 컨텍스트를 제한한다. 알 수 없는 태그나 높이 초과를 조용히 버리지 않는다. 원문 fallback 또는 명시적 진단으로 남기고, 화면에서 글자가 사라진 상태를 번역 성공으로 세지 않는다.

노른 안내의 첫 검증은 **18블록·48개 원문 페이지** 모두를 화면에 그려 실제 glyph bounds와 창 안쪽 경계를 검사하는 것이다. gmes 한 줄의 반환 y만 구현해도 문단 번역은 가능하지만, 페이지 높이 초과를 해결하지는 못한다. 필요한 후속 단계는 블록 ID·원문 페이지를 넘기는 페이지 렌더러다. 추가 표시 페이지를 만든다면 도움말 키 입력 루프 안에서만 처리하고 게임 턴·선택 결과를 진행시키지 않아야 한다. UTF-8 `exhelp-ko.txt`를 CP932 `noteload` 경로에 그대로 주입하지 않는다.

### 대화와 동적 문자열 경계

대화는 `talk_conv` 호출 전에 원래 `buff`와 화자/문맥을 네이티브 쪽에 캡처한다. 원본 `talk_conv`는 그대로 실행해 CP932 변수와 기존 반환 의미를 보존하고, 본문을 그리는 루프에서 번역이 있는 경우만 캡처한 전체 문단을 사용한다. 선택지 렌더링·화자 초상화·배경·`chatval`과 Esc 처리에는 손대지 않는다. 캡처한 문단의 수명은 해당 대화 호출에 한정하며, 중첩 호출과 재표시에서 이전 화자의 번역이 섞이지 않도록 문맥 ID/호출 수명을 검사한다.

일반 대화 창은 600×380이고 본문은 `wy+43`부터 시작한다. 선택지 위치는 `wy+wh-56-keyrange*19`이므로 실제 본문 최대 높이는 선택지 수에 따라 달라진다. 긴 한국어는 네이티브 측 측정으로 줄바꿈하고, 필요하면 대화 본문 내부 스크롤/추가 표시 페이지를 둬야 한다. 1480×280의 다른 `evochat` 모드는 별도로 검증한다. 번역문이 선택지 위를 덮는 상태를 허용하지 않는다.

6개 동적 대사는 완성된 이름을 정규식으로 찾아 바꾸지 않는다. 원본 대사 생성 지점의 ID와 `{player_name}`/`{player_title}`/`{speaker_name}`에 대응하는 실제 CP932 값을 정해진 타입으로 전달하고, 네이티브에서 UTF-8 포맷·조사를 처리한다. 플레이어가 입력한 이름은 번역하지 않는다. 지금의 exact display catalog로는 이 템플릿을 실행하지 않는다.

### 소스 재컴파일 근거와 추가 검증 관문

전체 원본 소스를 재컴파일할 수 있다는 기존 근거는 있다. [WINDOWS_REFERENCE.md](WINDOWS_REFERENCE.md)의 최종 복원본은 `fix_decompiled_control_flow.py`와 `restore_legacy_declarations.py`가 생성했으며, `tools/windows_hsp34.ps1` + `tools/reference/Hsp34Compile.cs`로 공식 HSP 3.4에서 컴파일했다. 결과 AX SHA-256은 `cec8809573ed8172033ac86d94709c40289b65398dad1fcc778c394108cf3b72`이다. `work/reports/ax-roundtrip-2.335/reference-final.json`에는 **2,805,612 정규화 토큰, 실제 분기/라벨 대상, 함수 선언 722개의 차이 0**이 기록돼 있다. 이는 기존 검증 기록을 확인한 것이며 이번 번역 조사에서 재실행한 결과는 아니다.

반면 [DECOMPILATION.md](DECOMPILATION.md)의 초기 Mac 전체 소스 probe는 UTF-8 설정의 컴파일러로 구문 수용만 확인했다. 그것을 현재 CP932 네이티브 전체 게임의 의미 일치 근거로 쓰면 안 된다. 현재 `macos-arm64-legacy-cp932-*` 프리셋의 `elona_hspcmp`는 존재하나, 이 표시 훅을 넣은 전체 게임을 빌드·실행한 기록은 아직 없다.

권장 생성 파이프라인은 다음과 같다.

1. 검증된 reference 소스의 전체 해시와 gmes/대화 구간 문맥 해시를 확인한다. 입력 원문을 편집하지 않는다.
2. ASCII hook 선언·호출만 삽입한 CP932 소스를 새 `work/localization/` 하위 디렉터리에 생성한다. 원문↔출력 행/토큰 매핑, 변경 구간, 입력·컴파일러·출력 해시를 기록한다. 단순 전역 문자열 치환은 하지 않는다.
3. 먼저 훅 없는 reference를 현재 legacy-CP932 컴파일러로 재컴파일하고 기준 AX와 비교한다. 전체 게임 의미 비교가 실패하면 그 차이를 해결하기 전 번역 훅을 진행하지 않는다.
4. 표시 훅을 넣은 opt-in AX를 컴파일한다. `tools/compare_ax_roundtrip.py`의 기존 1:1 토큰 비교는 의도된 삽입 때문에 바로 같아지지 않으므로, 명시된 변경 블록에 대한 원본↔신규 토큰 매핑을 추가해 **그 밖의 문자열 바이트·분기 실제 대상·DLL 선언**이 그대로인지 검사한다. 차이를 통째로 무시하지 않는다.
5. locale off와 catalog miss에서 원본 픽셀/입력/상태를 비교한다. locale on에서는 허용된 문자 영역만 달라지는지, 동일 seed의 신규 게임→행동→저장→불러오기 상태가 유지되는지 검사한다. 기존 원본 AX 실행 경로는 독립된 기준으로 유지한다.
6. 네이티브 전용 표시 opcode가 들어간 AX는 공식 원본 런타임에서 그대로 실행할 수 있다고 가정하지 않는다. 작은 명령 ABI fixture, Mac/Windows 네이티브 GUI, 실제 긴 대사·노른 모든 페이지의 경계와 입력을 별도로 검증한다.

## 통합 담당에게 전달하는 제3자 고지 문안

다음 문안은 `THIRD_PARTY_NOTICES` 통합용이며, 해당 루트 파일은 이 작업자가 수정하지 않았다.

> **Elona Wiki Korean translation contributions.** `localization/ko-KR/community-reviewed.json` and the records derived from it in generated localization catalogs contain adapted contributions from [Elona Wiki, 한글패치 모음](https://elona.fandom.com/ko/wiki/%ED%95%9C%EA%B8%80%ED%8C%A8%EC%B9%98_%EB%AA%A8%EC%9D%8C), by the wiki contributors, including the named contribution sections of 카파 and 노힐부득 and corrections attributed to Evildragon. These contributions and their adaptations are available under [Creative Commons Attribution-ShareAlike 3.0 Unported](https://creativecommons.org/licenses/by-sa/3.0/). They have been checked against ElonaPlus 2.335 and edited for terminology, meaning, and style; ambiguous entries are excluded from global matching. Per-record origins, source snapshots, and changes are documented in `source-manifest.json` and `community-reviewed.json`. This notice applies to those translation records, not to the host source code, independently authored translation records, or original game assets.

생성된 `runtime.tsv`에서 원문 출처 열이 사라진다면 생성 보고서/명세에서 해당 키의 출처를 추적할 수 있게 해야 한다. 독립 번역 파일 전체에 위키의 CC 라이선스를 임의로 붙이거나, 반대로 위키 파생 행의 라이선스를 제거하지 않는다.
