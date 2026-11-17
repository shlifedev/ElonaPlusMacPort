# IME와 CP932 입력 통합 (2026-11-18)

macOS ARM64의 자동 검증을 완료했다. **실제 macOS/Windows 일본어 IME 후보 선택·확정 검증은 아직 하지 않았다.**
이 기록은 합성 SDL 이벤트와 HSP 입력 스크립트의 결과이며 사용자 키보드나 시스템 입력기 설정을 조작하지 않았다.

## 구현

- SDL `TEXT_EDITING`을 `InputEvent::TextEditing`으로 전달한다. `start`/`length`는 UTF-8 바이트가 아닌 Unicode 문자 수이며, 표시할 CP932 범위로 변환한다. 후보창은 OS가 담당하고 조합 문자열·선택 강조·밑줄·커서는 호스트의 Edit 오버레이에 그린다.
- 조합 문자열은 표시 전용이며 HSP 변수와 분리한다. `TEXT_INPUT` 확정 때만 NFC 정규화 후 엄격한 CP932 변환을 수행한다. 취소·Escape·포커스 상실·객체 삭제/재지정은 기존 확정 문자열을 보존한다.
- 변환할 수 없는 문자(예: 이모지)는 HSP 변수에 넣지 않고 입력창에 빨간 안내와 report 진단을 남긴다. `?` 대체를 하지 않는다. 조합 중의 미변환 문자는 `[CP932]`로 표시한다.
- 입력 한도는 CP932 바이트 예산으로 검사한다. 한 SDL 텍스트 이벤트는 한도를 초과하면 전체 거부하므로 선택 영역과 기존 문자열을 지우거나 DBCS 절반을 남기지 않는다. 원본 ANSI Edit의 모든 TCHAR/IME 한도 동작과의 동등성은 별도 Windows 실측이 필요하다.
- Left/Right/Backspace/Delete는 CP932 문자 경계를 따른다. CRLF도 한 단위로 움직인다. 커서 이동과 선택 변경은 화면을 갱신한다. 기본 선택 전체와 Ctrl+A(멀티라인 mesbox)를 표시한다. 마우스 드래그 선택·일반 편집기의 전체 동작 구현을 의미하지 않는다.
- 키 이벤트와 텍스트는 이벤트가 발생한 HSP 창의 포커스 객체에 전달한다. 조합 중의 키는 게임의 `getkey`/`onkey`/확정 문자열 편집으로 새지 않게 처리한다. OS별 조합 종료와 Return 이벤트 순서는 실제 입력기로 추가 검증해야 한다.
- Elona의 숨긴 keylog가 매 프레임 `objsel`을 호출해도 SDL 텍스트 입력을 재시작하지 않는다. `SDL_SetTextInputArea`에 Edit 커서 위치를 전달하며 letterbox/HiDPI 창 좌표로 변환한다.

SDL 계약 근거: [TextEditingEvent](https://wiki.libsdl.org/SDL3/SDL_TextEditingEvent),
[SetTextInputArea](https://wiki.libsdl.org/SDL3/SDL_SetTextInputArea),
[ClearComposition](https://wiki.libsdl.org/SDL3/SDL_ClearComposition). 실제 빌드 헤더는 설치된 SDL3 3.4.20이다.

## 검증과 재현

빌드: `build/ime-root-check/elona_gui_host` (RelWithDebInfo, legacy CP932/RNG, SDL3/SDL3_ttf ON).

```sh
python3 tests/graphics/run_ime_fixture.py \
  --compiler build/ime-root-check/elona_hspcmp --host build/ime-root-check/elona_gui_host \
  --common .cache/upstream/OpenHSP/common --work work/ime-example-headless
python3 tests/graphics/run_ime_fixture.py \
  --compiler build/ime-root-check/elona_hspcmp --host build/ime-root-check/elona_gui_host \
  --common .cache/upstream/OpenHSP/common --work work/ime-example-window --window
build/ime-root-check/elona_sdl_ime_tests
```

`--work`는 새 경로여야 한다. CTest는 `--unique`를 써 새 하위 폴더에 기록한다. 기본 `gui_ime_headless`는 데스크톱을 요구하지 않는다.
SDL 이벤트 경계 검사는 `ELONA_TEST_DESKTOP=ON`일 때 CTest에 등록되며, 직접 실행할 수도 있다. 자신의 숨긴 창만 만들고 닫는다.

| 검사 | 결과 / 증거 |
|---|---|
| HSP 조합/확정/취소 | 56 프레임 exit 0. `work/ime-root-check/checked-headless/results.json`, `checked-window/results.json` |
| 일본어 바이트 한도·커서·삭제 | `A日本`에서 Left/Delete/Backspace, `Z日本`에 초과 확정 거부 후 보존, Home 입력, focus lost 취소, Escape 취소 모두 HSP 내부 조건 확인 |
| 확정 문자 인코딩 | 분해형 `か` + combining dakuten → `が`(2 bytes), 이모지 거부 후 기존 문자열 보존 |
| 표시 | 숨긴 SDL renderer read-back에서 조합 선택 파란 픽셀과 오류 빨간 픽셀 확인. `checked-window/composition.bmp`, `rejected.bmp` |
| SDL 이벤트 경계 | `elona_sdl_ime_tests`: 15 checks, 0 failures. HSP 창 7 라우팅, Unicode 문자 범위, 확정/취소, 입력 영역, 활성/비활성 전환 |
| 기존 GUI 회귀 | 11/11, x86 기준 BMP/출력 9개 동일. `work/ime-root-check/regression/results.json` |
| CTest | `ctest --test-dir build/ime-root-check -R '^gui_ime_headless$' --output-on-failure` 통과 |

## 함께 고친 수명 및 검증 경계

- Host 소멸 시 `audio.reset()`을 먼저 실행해 음악 콜백이 참조하는 `MusicPlayer`보다 장치를 먼저 닫는다. 오디오 작업자의 스트림 수명 수정과 함께 사용한다.
- `HostOptions::scripted_realtime`은 입력·dialog만 스크립트로 처리하고 시계/RNG/오디오에는 실시간 경로를 유지한다. 사용자 입력은 결정적 모드처럼 무시한다. `--script-realtime` CLI는 root가 연결한다. 일반 사용자 모드의 모달 dialog 동작은 유지한다.
- 새 스크립트 이벤트: `compose <start> <length> <UTF-8 text>`(텍스트 생략 시 취소), `focus lost|gained`. 실제 OS IME 테스트의 대체라고 해석하지 않는다.
