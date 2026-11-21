# 개발·에이전트 안내

README는 사람이 처음 보는 짧은 소개만 둔다. 원본 준비, 복원, 빌드, 실행 옵션과 검증 범위는 이 문서에 둔다.
작업 규칙은 [AGENTS.md](../AGENTS.md), 현재 단계와 다음 작업은 [PLAN.md](PLAN.md)가 기준이다.

## 현재 상태

실험용 개발 빌드다. macOS ARM64와 Windows x64에서 자동 입력으로 신규 게임, 맵 이동, 저장 후 새 프로세스에서 불러오기를 확인했다. Mac에서는 1280×768 전체 화면 설정으로 시작했고, Windows에서는 실제 게임 창의 타이틀을 외부 캡처와 대조했다. Mac 음악과 효과음은 CoreAudio 장치 경로를 사용한다. 장시간 수동 플레이와 OS 입력기를 통한 입력은 추가 검증이 필요하다. [검수 및 수정 기록](ASTRA_62_REVIEW.md)

모바일·게임 밸런스 변경은 범위에서 제외한다. 한국어 번역은 사용자 추가 요청으로 진행 중이며, 원본 CP932 데이터와 분리된 UTF-8 표시 계층을 사용한다.

## 먼저 읽을 문서

1. [AGENTS.md](../AGENTS.md) — 목표, 보존 규칙, 구현·검증·협업 지침. Claude는 [CLAUDE.md](../CLAUDE.md)에서 같은 지침으로 진입한다.
2. [PLAN.md](PLAN.md) — 현재 단계, 완료 조건, 다음 작업.
3. [COMPATIBILITY.md](COMPATIBILITY.md) — 실제 2.335 소스에서 확인한 플랫폼 의존 지점.
4. [DECOMPILATION.md](DECOMPILATION.md), [WINDOWS_REFERENCE.md](WINDOWS_REFERENCE.md) — 복원과 원본 비교의 한계.

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

추출물은 `work/extracted/2.335/`, 원시 복원 소스는 `work/decompiled/2.335/`, 확인한 복원 오류를 보정한 참조 소스는 `work/decompiled/2.335-reference/`에 생성된다. raw 소스를 직접 편집하지 않는다. 상세 옵션과 입력 식별값은 [추출 문서](EXTRACTION.md), [복원 문서](DECOMPILATION.md), [Windows 참조 검증](WINDOWS_REFERENCE.md)에 있다.

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

`elona_hsp_probe --trusted-fixture FILE.ax`는 검토된 작은 테스트 전용이다. 게임 실행기나 임의 바이트코드를 안전하게 실행하는 샌드박스가 아니다. 지원하지 않는 외부 호출은 오류로 중단한다. [네이티브 검증 기록](NATIVE_RUNTIME.md), [구형 AX 변환](LEGACY_AX.md)을 참조한다.

별도 `elona_gui_host`는 CPU 픽셀 계층과 선택적 SDL3 창을 제공한다. 검증용 창 실행 결과와 실제 게임의 남은 조건은 [현재 체크포인트](PLAN.md)에 기록한다. 배포용 앱 패키지는 아직 만들지 않았다.

## Mac Release 빌드

최초 한 번 `python3 tools/bootstrap_macos_gui.py`로 공식 SDL3 및 SDL3_ttf 프레임워크를 프로젝트 캐시에 준비한다. 이후 아래 명령으로 더블클릭 런처가 사용하는 실행물을 빌드한다.

```sh
cmake --preset macos-arm64-legacy-cp932-release
cmake --build --preset macos-arm64-legacy-cp932-release
```

일본어 텍스트는 표시 가능하지만 게임 문자열은 원본 CP932이므로 한국어/이모지 입력 지원을 의미하지 않는다.

## 현재 작업공간에서 플레이

Mac에서는 루트의 `Play ElonaPlus.command`를 더블클릭하거나 `python3 tools/play_game.py`로 실행한다. 자동 입력이나 시간 제한이 없는 실제 플레이 모드다.

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
- 오류 조사에는 `--diagnose-errors`를 사용할 수 있다. 게임의 오류 복구 전에 첫 VM 오류에서 정지시키므로 일반 플레이의 기본값은 아니다.

## 한국어판

`Play ElonaPlus Korean.command`를 더블클릭하거나 `python3 tools/play_game.py --language ko`로 실행한다. 기존 세이브도 불러올 수 있다. 캐릭터 생성과 이명, 퀘스트, 장비·마법 외에 조합법과 재료, 동료 진화, 신앙 제단, 카드 1,165종의 제목과 설명을 한국어로 표시한다. F1 도움말과 게임 속 책 33권은 영어판과 일본어판의 설명과 구성을 각각 반영한다. 검수 번역만 사용하려면 `--reviewed-only`를 추가한다. 카드 설명 873종은 기본 모드에서 자동 초벌을 사용한다.

새 작업공간에서는 위의 원본 복원·빌드 후 아래 명령으로 한국어 표시용 AX를 준비한다. 게임 원본과 세이브에는 CP932 문자열을 유지한다. [번역 범위와 재현 방법](KOREAN_IMPLEMENTATION.md)

```sh
python3 tools/prepare_korean_game.py --output work/localization/managed-korean-static-v14 --compile
```

[번역 출처](KOREAN_SOURCES.md), [용어집](../localization/ko-KR/glossary.tsv), [번역 지침](../localization/ko-KR/style-guide.md)을 함께 관리한다.
`--audit-translation`을 추가하면 해당 세션 폴더에 미번역 표시 문자열을 기록한다. 이는 로컬 진단 파일이며 자동 전송하지 않는다.

## CI/CD

GitHub Actions는 `main`과 `dev` 브랜치를 대상으로 한다. 원본 게임이 없는 환경이므로 저장소만으로 검증 가능한 범위만 실행한다. `tests/game` 실제 플레이 시나리오와 세이브 왕복은 포함하지 않는다.

- `.github/workflows/ci.yml` — push/PR(`main`, `dev`): Python 단위 테스트, macOS arm64 legacy-cp932 debug 빌드와 CTest, Windows x64 legacy-cp932 debug 빌드와 CTest(`tools/windows_build.ps1`과 같이 Python 기반 fixture 제외).
- `.github/workflows/release.yml` — `main` push: macOS `.dmg`와 Windows `.zip`을 만들어 `build-<번호>` release(Latest, 버전 `0.1.<번호>`)로 올린다. 자동 빌드 release는 최근 5개만 남긴다. 게임 코드 입력용 `game-data-2.335`는 prerelease로 둔다.

## 배포 패키지

`launcher/`는 Tauri 2 런처다. 게임 폴더 선택(elonaplus.exe SHA-256 확인), 언어 선택, 실행만 한다. 실행은 `tools/play_game.py`와 같은 인자로 `elona_gui_host`를 띄우고, 세이브·설정은 OS 데이터 폴더의 `ElonaPlus Native/`(`user/`, `sessions/play-*`, `launcher.cfg`)에 둔다. 같은 프로필을 두 번 여는 것은 `user/.native-player.lock` 잠금으로 막는다.

`tools/package_release.py`가 패키지를 만든다.

- mac: 호스트 rpath를 `@executable_path/../Frameworks`로 바꾸고 SDL 프레임워크와 `data/`를 `launcher/stage/`에 모은다. 그다음 Tauri로 `.app`(ad-hoc 서명)을 만들고 `hdiutil`로 `.dmg`를 만든다. ad-hoc 서명에서는 hardened runtime을 끈다. 켜면 라이브러리 검증 때문에 호스트가 번들된 SDL 프레임워크를 불러오지 못한다(`different Team IDs`).
- windows: `tauri build --no-bundle`로 만든 `ElonaPlus.exe`, `elona_gui_host.exe`, SDL DLL, `data/`를 `.zip`으로 묶는다. SDL VC 패키지는 `tools/reference/windows_gui_deps.py --extract .cache/windows-gui`로 준비하고, 호스트는 `windows-x64-legacy-cp932-release` preset으로 빌드한다.
- 두 플랫폼 모두 패키징 중에 번들된 호스트를 알 수 없는 옵션으로 한 번 실행한다. 사용법이 출력되고 exit 2로 끝나야 통과다. 라이브러리를 불러오지 못하면 패키징이 실패한다.

패키지에 넣는 게임 코드는 `packaging/game-data.json`의 파일 네 개(정규화 원본 AX, 한국어 AX, scene1/2.hsp)뿐이다. 그래픽·음악·효과음·맵은 넣지 않는다. CI에는 원본이 없으므로 이 파일들은 GitHub release `game-data-2.335`에 올려 두고 SHA-256으로 확인한다. 한국어 AX가 바뀌면 로컬에서 다시 만들고 다음 순서로 갱신한다.

```sh
cp work/localization/<새 AX 폴더>/start.ax /tmp/start-ko.ax
gh release upload game-data-2.335 /tmp/start-ko.ax --clobber
# packaging/game-data.json 해시와 tools/play_game.py의 MANAGED_*를 함께 갱신
```

로컬 확인: `python3 tools/package_release.py --platform mac --build-dir build/native-macos-arm64-legacy-cp932-release`(기본 입력은 로컬 `work/`). Developer ID 서명과 공증은 하지 않는다.

## 배포물 구분

이 저장소는 개발 코드·패치·문서를 관리한다. 원본 게임·리소스·역컴파일한 전체 게임 소스는 자동으로 포함하지 않는다. 제3자 도구와 패치의 고지는 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)에 보존한다. 한국어 이명 v11을 포함한 로컬 macOS 패키지는 `build/package/korean-alias-2.335/`에 있다. 이 버전의 공개 릴리스는 별도다.
