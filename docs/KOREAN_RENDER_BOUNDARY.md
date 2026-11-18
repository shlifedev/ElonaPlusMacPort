# 한국어 문단 표시: 재컴파일 관문과 관리 소스 변환

2026-11-18. 한국어 표시를 위해 CP932 게임 문자열이나 저장 형식을 바꾸지 않고, `gmes`가 문자열을 한 글자씩 자르기 **전**에 전체 원문을 네이티브 표시 계층에 전달한다. 이 문서는 번역문 작성, 관리 소스 생성, 실제 화면 적용을 구별한다.

**Windows 후속 관문:** `gui-native-ko-managed-03`에서 최신 MSVC 호스트/type48·5058키·기준/관리fallback/한국어 관리 AX의 신규게임·재로드·노른/세계지도9회 실행, 총41개 검사를 통과했다. 기준 저장154/154/162파일과 압축 해제 내용이 같다. Malgun의 가운데점 누락은 원문 글꼴의 글리프별fallback으로 수정했다. 이 검사는 headless이며 기존 외부 창 캡처 실패는 별도 미해결이다. 상세 명령·해시는 [WINDOWS_GUI.md](WINDOWS_GUI.md)에 있다.

## 관문 1: 현재 Mac 컴파일러로 전체 게임 재빌드

검증 입력은 `work/decompiled/2.335-reference/start.hsp`이며 원시 역컴파일 파일을 수정하지 않았다. 전체 소스와 컴파일러, 생성 AX의 SHA-256은 다음과 같다.

| 항목 | SHA-256 |
|---|---|
| 검증된 2.335 reference source | `c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55` |
| `build/native-macos-arm64-legacy-cp932-release/elona_hspcmp` | `53746f19b786c8cbb475e2d686308031e50c14e527ef5282ae4a80f29e1cf411` |
| 무수정 전체 소스의 native AX | `8575184fe1729c90736bc5f346759e6be06d06cf1cc8ff7d97456517cb8c9c5e` |
| 실행 비교에 사용한 Release host | `df654bed7610fb9c6dfb36607235be415572abbd7872afe4a8658f4a0718110d` |

컴파일 exit 0, 생성 AX 14,249,229 bytes. `work/localization/cp932-full-reference-01/compile.json`과 `compile.log`가 명령과 출력이다. 컴파일러의 미초기화 변수 경고는 보존했다. 이 검증은 이전 UTF-8 플랫폼 compiler probe와 다르게 현재 **legacy-CP932 프리셋**을 사용했다.

```sh
build/native-macos-arm64-legacy-cp932-release/elona_hspcmp \
  --compath=/Users/shlifedev/@src/ElonaPlus/.cache/upstream/OpenHSP/common/ \
  -o/Users/shlifedev/@src/ElonaPlus/work/localization/cp932-full-reference-01/start.ax \
  /Users/shlifedev/@src/ElonaPlus/work/localization/cp932-full-reference-01/start.hsp
```

### 정적 차이는 숨기지 않는다

기존 `tools/compare_ax_roundtrip.py`는 0x301/96바이트 AX만 받는다. 새 compiler의 0x360/112바이트 헤더를 직접 넣으면 명시적으로 거부한다. 비교용 사본에서 선택적 추가 섹션이 비어 있음을 확인하고 헤더 길이/섹션 위치만 매핑했다. 코드·데이터 본문은 그대로다. 이 사본은 **분석 전용이며 실행하지 않았다.** 기록은 `analysis-header-adapter.json`, `analysis-only-legacy-header.ax`다. 실제 게임 실행에는 변환하지 않은 native `start.ax`를 사용했다.

원본 2,805,612 토큰과 native 2,805,607 토큰은 1:1로 같지 않다. 첫 차이는 `onerror goto`의 선택적 `goto` 토큰 생략이다. 그 뒤 인덱스가 밀려 기존 비교기가 2,668,030개 차이를 보고하지만, 이를 266만 개의 의미 오류로 해석하면 안 된다. 다음 다섯 토큰을 정확히 식별한 뒤 원본→신규 토큰 번호를 매핑해 다시 비교했다.

| 원본 토큰 번호 | source 행 | 새 compiler가 생략한 구문 |
|---:|---:|---|
| 183 | 166 | `onerror goto`의 기본 `goto` |
| 124013 | 23737 | `onexit goto`의 기본 `goto` |
| 940558 | 232838 | `button goto "refresh"`의 기본 `goto` |
| 940586 | 232846 | `button goto "resize"`의 기본 `goto` |
| 940747 | 232893 | `button goto "del"`의 기본 `goto` |

정렬 후 나머지 **토큰 값/문자열 바이트 차이 0, 분기·라벨 대상 171,452쌍 차이 0, 라이브러리 선언 일치**를 확인했다. 추가·삭제를 임의로 허용한 실행물은 아니며, 삭제된 다섯 토큰이 모두 `(type=15, flags=0, value=0)`임을 기록했다. `work/localization/compare_current_compiler.py`와 `cp932-full-reference-01/aligned-compare.json`이 분석 입력과 결과다.

함수 선언에는 다음 **112개 차이**가 남는다. 공식 HSP 3.4 재컴파일처럼 선언까지 완전 동일하다고 표현하지 않는다.

- 72개 외부 `#func`: `target_or_flags=2`에서 `6`으로 바뀐다. 현재 `codegen.cpp`는 `#func`에 `STRUCTDAT_OT_STATEMENT | STRUCTDAT_OT_FUNCTION`을 지정한다. 원본은 statement만 허용했다. 실제 호출 토큰·인수 타입은 동일하다.
- 40개 함수: `var` 인수의 `sizeof(MPVarData)`가 Mac ARM64에서 16바이트이므로 선언 stack/다음 인수 offset이 원본 8바이트보다 커진다. 23개는 stack 크기만, 17개는 인수 offset도 다르다. 현재 VM은 내부 함수의 offset/size에 64비트 조정도 수행한다. 실행한 경로는 아래 대조를 통과했으나, 이 결과만으로 모든 var 함수 경로의 동등성을 주장하지 않는다.

### 신규 게임·저장·새 프로세스 불러오기

원본 normalized AX와 native 재컴파일 AX를 **같은 host, 같은 결정적 입력, 각각 새 private user root**로 실행했다. 이후 각각 동일한 물리적 user root를 새 프로세스로 열어 재저장했다. 개인 프로필이나 원본 저장 파일을 사용하지 않았다.

| 경로 | 원본 / 재컴파일 종료 | 프레임 | 비교 |
|---|---|---:|---|
| `newgame_english.script` | 둘 다 `end/0` | 둘 다 4358 | 압축 해제 세이브 154/154파일, 최종 BMP 동일 |
| `load_inspect_resave.script` | 둘 다 `end/0` | 둘 다 1158 | 압축 해제 세이브 154/154파일, 최종 BMP 동일 |

이름·좌표·레벨·맵·전체 저장 인벤토리·기술 필드가 같고 양 AX의 턴 차이는 0이다. 오류·미지원 호출 0, 원본 게임 트리 1511파일/ZIP과 사용 host 해시 불변을 확인했다. 실제 실행 기록은 `work/localization/cp932-full-reference-01/game-check/summary.json`, 재현 스크립트는 `work/localization/check_recompiled_game.py`다. 이미 있는 증거 폴더를 덮어쓰지 않으므로 재실행할 때는 새 디렉터리 이름을 사용한다.

이것은 **Mac 결정적 헤드리스의 두 경로에 대한 실행 관문 통과**다. 한국어 변환 AX 실행이나 전체 게임·모든 메뉴·Windows의 재컴파일 AX 검증을 대신하지 않는다.

## 관문 2: CP932 바이트를 유지하는 표시 훅 생성

도구: [prepare_korean_game.py](../tools/prepare_korean_game.py). 입력 SHA와 유일한 gmes 문맥을 확인하고 원본 바이트에 ASCII 두 구간만 삽입한다. 생성물은 새 `work/` 하위 디렉터리만 허용한다. 기존 출력, 심볼릭링크 경로, 원본 폴더로의 출력은 거부한다. 원본 소스의 혼합 CRLF/LF와 CP932 중복 매핑 바이트를 재인코딩하지 않는다.

선언은 다음과 같다.

```hsp
#regcmd 48
#cmd native_ko_gmes $00
```

`#deffunc gmes str gmes_prm0` 바로 다음에 다음 코드만 삽입한다.

```hsp
native_ko_gmes gmes_prm0, gmesx, gmesy, gmesw, gmestype, gmescol, gmescol(1), gmescol(2)
if ( stat >= 0 ) {
    gmesy += stat
    return 0
}
```

8개 인수는 **CP932 원문 문자열, x, y, 폭, 그림자 모드, R, G, B**다. 표시 표면은 호스트의 현재 선택 표면을 사용한다. 값 인수이므로 VM 변수를 네이티브 코드가 다시 쓰지 않는다.

- `stat < 0`: locale 비활성/카탈로그 없음 등의 원문 fallback. 원래 gmes 본문을 실행한다.
- `stat >= 0`: 네이티브가 실제 표시를 완료했을 때의 총 세로 증가량. gmes는 y만 그만큼 이동하고 `return 0`으로 끝난다. 지원하지 못한 태그/레이아웃을 성공으로 처리해서는 안 된다.
- 원본 gmes는 진입 시 항상 `font`를 호출하고, host의 font 성공은 stat 0이다. 현재 source의 gmes 호출 5곳(276037, 288638, 288749, 603485, 603693)은 반환 직후 stat를 분기 조건으로 사용하지 않는다. 그래도 높이가 stat에 남지 않도록 성공 반환은 명시적으로 0이다.
- 명령 내부에서 `refstr`, `strsize`, RNG, 게임 문자열, 대기/입력 상태를 바꾸지 않는다. 원본과 대응되는 폰트·색·커서의 표시 상태는 호스트가 정의·검증해야 한다.

```sh
python3 tools/prepare_korean_game.py \
  --output work/localization/managed-gmes-v1-01 --compile
python3 -m unittest discover -s tests/localization/source_transform -p 'test_*.py' -v
```

생성 manifest에는 입력/출력/컴파일러 해시, 삽입 바이트 위치·행 수·문맥 해시, ABI, 컴파일 명령과 종료 코드가 남는다. 컴파일을 요청하면 실제 토큰 스트림을 읽어 type48/$00 호출이 **정확히 한 개**인지 확인한다. 값 안의 우연한 바이트열을 opcode로 세지 않는다. 원본 AX와 무수정 재컴파일 AX에는 type48 토큰이 없음을 따로 확인했다.

안전성 검사 **8/8 통과**: 원문 바이트 불변, 알 수 없는 SHA, 없는/중복/변경 anchor, 기존 명령 선언, 출력 범위·덮어쓰기·심볼릭링크, 토큰 경계/잘린 입력을 확인했다. `prepare_korean_game.py`는 생성과 컴파일만 수행하며 실행·런처 변경·사용자 데이터 수정은 하지 않는다.

실제 생성물 `work/localization/managed-gmes-v1-01/start.ax`는 컴파일 exit 0, **2,805,631토큰 중 type48/$00 정확히 한 개**를 확인했다. 관리 소스 SHA는 `7299e08608e2e635ea916e2c9baef8b7c1979c35d7dce65fcdcbe5e2356275fb`, AX SHA는 `8aa19643c4824fa98ee175194864ecdfde8d4871c13a0d9f8ffa99cf9b99ad03`이다. 생성 manifest의 `executed=false`는 준비 도구 자체가 실행하지 않았다는 뜻이며, 다음 별도 실행 검증과 구별한다.

## 관문 3: 표시 훅이 있으나 번역을 끈 게임

호스트 담당의 type48 구현 후 `build/localization-check/elona_gui_host`(SHA `ad4cbf3ef544d0091976b924993a6ff6d7cc7d9d5dd56211ede08afda4cbfbda`)로 **무수정 native AX와 관리 변환 AX**를 비교했다. 번역 catalog는 전달하지 않았으므로 gmes 명령은 원문 fallback을 사용했다.

- 신규 게임·저장 종료: 양쪽 `end/0`, 4358프레임, 압축 해제 세이브 154/154파일과 최종 BMP 동일.
- 새 프로세스 불러오기·재저장: 양쪽 `end/0`, 1158프레임, 압축 해제 세이브 154/154파일과 최종 BMP 동일.
- 이름·좌표·맵·레벨·인벤토리·기술 불변, 턴 차이 0, 오류·미지원 호출 0. 원본 트리/ZIP과 사용 host 해시 불변.

증거는 `work/localization/managed-gmes-v1-01/game-check/summary.json`, 재현 절차는 `work/localization/check_managed_game.py`다. 여기의 `original` 경로 이름은 비교 도구의 기준 쪽 이름이며, 실제 AX는 **무수정 native 재컴파일본**임을 `argv.json`으로 확인할 수 있다. 원본 normalized AX와 native 재컴파일본 비교는 관문 1의 별도 결과다.

## 관문 4: 노른 18블록·48페이지의 한국어 레이아웃

`work/localization/check_tutorial_layout.py`는 번역 JSON에서 JP 도움말 79행을 읽어 private HSP fixture를 만든다. 게임과 같은 480×175 표면, x=120/y=55/폭=330의 본문과 기존 48페이지 경계를 사용해 type48/$00를 직접 호출했다. 호스트 SHA는 `f6f4d974d73c3cc4aba084c346c47261060128d04a042c2ca8eabd5bd49c3c90`이다.

결과는 **79/79 문단 매치, exit 0, 48/48페이지 높이 초과 없음**이다. 반환 높이를 누적한 최대 다음 y는 171(10블록 2페이지)이다. BMP의 실제 잉크 픽셀도 48개 모두 본문 폭 안에 있었으며 최대 x=448, y=166이었다. `b01-p01`, 가장 긴 `b10-p02`, 반복된 원문 강조 태그가 있는 `b18-p01` PNG를 열어 한글·태그·줄바꿈을 확인했다. 본문 축약이나 페이지 삭제 없이 통과했다.

증거: `work/localization/tutorial-layout-01/summary.json`, `pixel-bounds.json`, `user/user/layout.csv`와 48개 BMP. 이 fixture는 **실제 전체 게임의 도움말 창을 실행한 것이 아니며**, 현재 JP-only 도움말 번역이 EN 게임 모드에서도 적용된다는 뜻이 아니다. Windows의 다른 글꼴과 실제 게임 배경/초상화/키 입력도 별도 관문이다.

## 관문 5: 실제 일본어 기반 게임에서 한국어 도움말·저장 대조

공개 재현 도구 [run_game_gate.py](../tests/localization/source_transform/run_game_gate.py)는 기존 영어 신규 게임 스크립트에서 첫 언어 선택의 `DOWN` 한 번만 제거해 JP 기반 새 프로필 두 개를 만든다. 한쪽은 무수정 native reference AX, 다른 쪽은 관리 변환 AX와 고정 복사한 한국어 catalog를 사용한다. 각 프로필을 새 프로세스로 불러온 뒤 집을 나가 노른 안내를 읽고 북부 티리스에서 저장한다.

`work/localization/japanese-gate-final-01/summary.json`의 **6/6 실행 통과** 결과:

| 단계 | 양쪽 프레임·종료 | 원문/한국어 세이브 대조 |
|---|---|---|
| 타이틀→캐릭터 생성→집→이동→저장 | 4358, `end/0` | 154/154 압축 해제 파일 동일 |
| 새 프로세스 불러오기→인벤토리→재저장 | 1158, `end/0` | 154/154 동일 |
| 로미아스·라네이레 작별→노른 안내→북부 티리스→저장 | 3008, `end/0` | 162/162 동일 |

세 단계 모두 이름·좌표·레벨·맵·전체 인벤토리·기술 필드 불변, 양쪽 턴 차이 0, 오류·미지원 0이다. 한국어 표시에 의해 화면은 달라졌다. 원본 트리/ZIP과 사용 host 해시는 불변이었다. 이 실행에서 사용한 catalog SHA-256은 `300ba300ee71dc733705857b131dc8d5373de26a1b8aba2eeb3e33d8010f2862`이며, 이후 추가되는 번역까지 검증됐다는 뜻은 아니다.

실제 게임의 노른 **집 안내 2페이지와 세계 지도 안내 4페이지**, 원문 문단 **12개**가 native gmes로 표시됐다. 실제 배경·초상화·강조·다음 페이지 표식을 포함한 `norne-home-1.png`, `norne-world-4.png`를 열어 확인했다. 각 BMP는 `korean/world/cwd/shots/` 아래 있으며, 최종 저장 위치는 북부 티리스(맵 4), 좌표 (22,21)이다.

```sh
python3 tests/localization/source_transform/run_game_gate.py \
  --host build/localization-check/elona_gui_host \
  --reference-ax work/localization/cp932-full-reference-01/start.ax \
  --managed-ax work/localization/managed-gmes-v1-01/start.ax \
  --catalog localization/ko-KR/runtime.tsv \
  --work work/localization/japanese-gate-final-01
```

새 실행에는 새 `--work` 이름이 필요하다. 이 도구는 재사용 가능한 실험용 경로이며 개인 세이브를 받지 않는다. 검증된 JP 프로필은 집의 (11,13)에서 시작하므로 기존 EN의 (12,17)용 탈출 스크립트를 그대로 쓰면 벽에 막혔다. 최종 [japanese_home_to_world.script](../tests/localization/source_transform/japanese_home_to_world.script)는 실제 맵을 읽기 전용으로 확인한 뒤 키 입력만 조정했다. 초기 실패 probe의 `gmes=0`은 도움말 통과로 세지 않는다. 검증용 맵·저장 배열이나 게임 규칙을 바꾸지는 않았다.

이번 관문은 Mac의 결정적 헤드리스 게임 표시다. 실제 키보드로 장시간 플레이하거나 Windows에서 이 관리 AX를 실행한 결과는 아니다. 긴 NPC 대사의 `talk_conv` 경계, 미번역 UI·로그, 동적 문장 등은 여전히 남는다.

## 다음 통합 관문

후속 검증을 마친 관리 AX는 한국어 런처가 SHA-256을 확인해 사용한다. 관리 AX가 없는 작업공간은 정규화된 원본 AX로 실행한다.

1. locale off 비교와 JP 기반 한국어 실제 게임의 저장·입력 경로는 관문 3/5에서 통과했다. 이후 번역/호스트 변경 시 같은 재현 도구로 필요한 회귀를 수행한다.
2. 짧은 문단 fixture에서 강조 태그·줄바꿈·그림자·반환 높이·다음 표시 위치와 `stat=0`을 확인한다.
3. 노른 18블록/48개 원문 페이지의 native fixture, 실제 게임의 초기 6페이지는 관문 4/5를 통과했다. 나머지 실제 게임 맥락과 Windows에서 같은 경계를 검사한다. gmes 한 줄 훅은 임의의 추가 번역에 대한 페이지 높이를 자동으로 보장하지 않는다.
4. 첫 집 대화는 `talk_conv` 이전 전체 `buff` 캡처와 본문 루프 경계가 별도로 필요하다. 이번 gmes 훅만으로 첫 집 91개 항목 전체가 표시된다고 보고하지 않는다.
5. 6개 동적 템플릿은 이름·호칭을 정해진 인수로 넘기는 후속 문맥 API가 필요하다. 완성된 이름에 정규식 치환을 걸지 않는다.

구체적인 소스 위치와 대화·페이지 설계는 [KOREAN_TUTORIAL.md](KOREAN_TUTORIAL.md)의 표시 경계 절을 따른다.
