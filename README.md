# ElonaPlus Native

ElonaPlus **2.335**의 게임 규칙과 데이터를 보존하면서 **macOS ARM64 / Windows x64**에서 네이티브로 실행하기 위한 이식 프로젝트다. Custom-GX는 코드 복원과 유지보수 방식의 참고 자료이며 초기 기능 기준은 원본 2.335다.

**현재 실험용 개발 빌드다.** macOS ARM64와 Windows x64에서 자동 입력으로 신규 게임, 맵 이동, 저장 후 새 프로세스에서 불러오기를 확인했다. Mac에서는 실제 SDL 창과 CoreAudio 음악·효과음 출력을 확인했다. 최근 Windows 검증에서 내부 렌더러는 타이틀을 그렸지만 외부 창 캡처에는 앞선 언어 선택 화면이 남았다. 전체 화면 설정으로 시작하면 현재 호스트가 종료된다. 장시간 수동 플레이와 OS 입력기를 통한 입력은 추가 검증이 필요하다. [Astra 6.2 검수 기록](docs/ASTRA_62_REVIEW.md)

## 에이전트와 개발자 시작점

1. [AGENTS.md](AGENTS.md) — 목표, 보존 규칙, 구현·검증·협업 지침.
2. [docs/PLAN.md](docs/PLAN.md) — 현재 단계, 완료 조건, 다음 작업.
3. [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) — 실제 2.335 소스에서 확인한 플랫폼 의존 지점.
4. [docs/DECOMPILATION.md](docs/DECOMPILATION.md), [docs/WINDOWS_REFERENCE.md](docs/WINDOWS_REFERENCE.md) — 복원과 원본 비교의 한계.

Claude는 [CLAUDE.md](CLAUDE.md)에서 같은 지침으로 진입한다. 모바일·게임 밸런스 변경은 범위에서 제외한다. 한국어 번역은 사용자 추가 요청으로 진행 중이며, 원본 CP932 데이터와 분리된 UTF-8 표시 계층을 사용한다.

## 로컬 원본 준비와 코드 복원

원본 `elonaplus2.335/`와 `elonaplus2.335.zip`을 프로젝트 루트에 둔다. 원본·생성 소스·개인 세이브는 git에서 제외하며 원본 폴더를 수정하지 않는다. 도구는 고정한 2.335 SHA-256을 검사한다.

```sh
python3 tools/prepare_game.py --game-dir elonaplus2.335 --output work/extracted/2.335
python3 tools/normalize_legacy_ax.py work/extracted/2.335/start.ax --output work/normalized/2.335/start.ax
python3 tools/bootstrap_borscht.py
python3 tools/decompile_game.py
python3 tools/audit_compatibility.py
python3 tools/fix_decompiled_control_flow.py
python3 tools/restore_legacy_declarations.py
python3 -m unittest discover -s tests -v
```

추출물은 `work/extracted/2.335/`, 원시 복원 소스는 `work/decompiled/2.335/`, 확인한 복원 오류를 보정한 참조 소스는 `work/decompiled/2.335-reference/`에 생성된다. raw 소스를 직접 편집하지 않는다. 상세 옵션과 입력 식별값은 [추출 문서](docs/EXTRACTION.md), [복원 문서](docs/DECOMPILATION.md), [Windows 참조 검증](docs/WINDOWS_REFERENCE.md)에 있다.

## 네이티브 진단용 빌드

Python 3, Git, CMake, Ninja와 C++17 컴파일러를 사용한다. borscht 빌드에는 Rust/Cargo가 필요하다. Mac은 Apple Command Line Tools, Windows는 Visual Studio C++ 도구로 검증했다.

```sh
python3 tools/bootstrap_openhsp.py
python3 tools/bootstrap_audio.py
cmake --preset macos-arm64-legacy-cp932-debug
cmake --build --preset macos-arm64-legacy-cp932-debug
ctest --preset macos-arm64-legacy-cp932-debug
```

Windows 대응 preset은 `windows-x64-legacy-cp932-debug`다. 준비된 원격 PC는 아래 명령으로 별도 소스 스냅샷을 빌드하고 Mac과 결과를 비교한다.

```sh
python3 tools/windows_probe.py
python3 tools/windows_stage.py --legacy-cp932
```

`elona_hsp_probe --trusted-fixture FILE.ax`는 검토된 작은 테스트 전용이다. 게임 실행기나 임의 바이트코드를 안전하게 실행하는 샌드박스가 아니다. 지원하지 않는 외부 호출은 오류로 중단한다. [네이티브 검증 기록](docs/NATIVE_RUNTIME.md), [구형 AX 변환](docs/LEGACY_AX.md)을 참조한다.

별도 `elona_gui_host`는 CPU 픽셀 계층과 선택적 SDL3 창을 제공한다. 검증용 창 실행 결과와 실제 게임의 남은 조건은 [현재 체크포인트](docs/PLAN.md)에 기록한다. 배포용 앱 패키지는 아직 만들지 않았다. 아래는 이 개발 작업공간에서 실행하는 방법이다.

## 현재 작업공간에서 플레이

Mac에서 준비된 Release 실행물을 사용하려면 루트의 **`Play ElonaPlus.command`**를 더블클릭한다.
터미널에서는 `python3 tools/play_game.py`로 실행한다. 자동 입력이나 시간 제한이 없는 실제 플레이 모드다.

- 기본 세이브/설정: `work/player/2.335/user/` (반복 실행해도 유지)
- 실행별 로그/종료 화면: `work/player/2.335/sessions/play-*/`
- 원본 `elonaplus2.335/`는 읽기 전용. 테스트 세이브나 이전 preview 프로필은 자동으로 덮어쓰거나 합치지 않는다.
- 기존 개발 프로필을 사용할 때: `python3 tools/play_game.py --profile <사용자폴더>`.
  이 런처로 같은 프로필을 동시에 여는 것은 잠금으로 막는다. 호스트를 직접 실행하면 이 런처 잠금은 적용되지 않는다.
- Windows는 같은 Python 런처에서 `--host <elona_gui_host.exe 경로>`를 지정할 수 있다.
  SDL DLL과 SoundFont를 포함한 빌드 환경이 필요하다. 독립 설치본은 아직 아니다.
- 네이티브 런처는 조작 안내 창(게임 창 번호 20)을 닫아도 본 게임을 계속한다.
  원본은 이 창의 닫기도 전체 종료로 처리한다. 직접 호스트를 실행해 이 개선을 사용할 때는
  `--auxiliary-close-hide`를 지정한다. 본 게임 창을 닫는 동작은 그대로다.

Mac Release를 다시 빌드할 때는 `cmake --preset macos-arm64-legacy-cp932-release`와
`cmake --build --preset macos-arm64-legacy-cp932-release`를 사용한다. 실제 창에는 SDL3/SDL3_ttf가 필요하다.
일본어 텍스트는 표시 가능하지만 게임 문자열은 원본 CP932이므로 한국어/이모지 입력 지원을 의미하지 않는다.

한국어 표시의 개발 중인 미리보기는 **`Play ElonaPlus Korean.command`** 또는
`python3 tools/play_game.py --language ko`로 실행한다. 아직 전체 한글 패치가 아니며,
검수한 고정 문구만 적용한다. 분할 출력되는 대사·튜토리얼, 동적 아이템 이름과 메시지에는
원문이 남아 있다. 이름 입력과 저장 데이터는 원본 형식을 유지한다.
[번역 출처](docs/KOREAN_SOURCES.md), [용어집](localization/ko-KR/glossary.tsv),
[번역 지침](localization/ko-KR/style-guide.md)을 함께 관리한다.
`--audit-translation`을 추가하면 해당 세션 폴더에 미번역 표시 문자열을 기록한다.
이는 로컬 진단 파일이며 자동 전송하지 않는다.

오류 조사에는 `python3 tools/play_game.py --diagnose-errors`를 사용할 수 있다.
이 옵션은 게임의 오류 복구 전에 첫 VM 오류에서 정지시키므로 일반 플레이의 기본값은 아니다.

## 배포물 구분

이 저장소는 개발 코드·패치·문서를 관리한다. 원본 게임·리소스·역컴파일한 전체 게임 소스는 자동으로 포함하지 않는다. 제3자 도구와 패치의 고지는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)에 보존한다. 아직 앱 패키지나 공개 릴리스를 만들지 않았다.
