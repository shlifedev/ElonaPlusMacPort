# OpenHSP 패치

대상: `onitama/OpenHSP` 커밋 `3dbb8729dad53ee876fc681696c8fe542698860b`.
원본 라이선스는 해당 체크아웃의 `LICENSE`에 있다. 아래 패치는 원본 고지를 제거하지 않는다.

`0001-opt-in-legacy-compatibility.patch`는 POSIX `supio_linux.cpp`에 `ELONA_LEGACY_CP932` 조건부 경로를 추가한다. 빌드 옵션 기본값은 OFF이며 기존 UTF-8 경로를 유지한다. ON이면 CP932 리드 바이트 `81–9f`, `e0–fc` 다음 바이트를 문자열 검색·소문자화·분리·치환·트림에서 건너뛴다. 문자 중간의 `41`을 ASCII A로 소문자화하거나 `5c`를 경로 구분자로 오인하지 않게 한다.

문자 walker 수정 범위: `utf8strlen`, `strcase`, `strstr2`, `strchr2`, `strsp_get`, `strchr3`, `ReplaceStr`. Windows CP932 walker는 변경하지 않는다. 추가로 양 플랫폼의 `getpath`는 opt-in일 때 `src/legacy_logical_path.cpp`를 호출한다. 이 함수는 CP932 문자 경계를 보존하며 Windows 논리 경로만 분해하고 파일시스템에 접근하지 않는다. Windows의 기존 locale 의존 `mbstowcs` 경로도 피한다.

HSPUTF8 전역 플래그, 컴파일러 전체 문자 처리, 파일시스템 인코딩, UI 렌더링은 이 패치의 범위가 아니다. 이는 완성된 CP932 런타임 모드가 아닌 첫 호환 패치다.

`hsp3int.cpp`의 `randomize/rnd`에는 별도 `ELONA_LEGACY_RNG` opt-in 경로를 추가한다. `src/legacy_rng.cpp`가 Windows HSP 3.4 참조와 일치하는 32-bit LCG 상태와 15-bit 출력값을 구현한다. 호스트 C 라이브러리 RNG를 교체하며 exrand 플러그인에는 관여하지 않는다. `ELONA_LEGACY_CP932`와 독립적으로 선택한다.

`python3 tools/bootstrap_openhsp.py`가 고정 커밋을 체크아웃하고 패치를 적용한다. 다시 실행하면 체크아웃의 diff가 이 패치와 정확히 같은지 확인한다. 다른 수정이나 미추적 파일이 있으면 덮어쓰지 않고 실패한다. 캐시를 직접 고쳐 패치를 잃어버리지 말 것.

회귀: `tests/native/string_walkers.cpp`는 실제 upstream 함수를 호출한다. `tests/native/run_semantics.py --legacy-cp932`는 실제 HSP 컴파일러와 VM 경로에서 `、`(`81 41`), `ソ`(`83 5c`)를 검증한다.
