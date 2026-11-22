# 네이티브 런타임 가능성 검증

## 확인된 결과

이 문서는 작은 fixture용 `elona_hsp_probe`와 코어 호환성 검증 기록이다. 후속 GUI 호스트·게임 진행·파일 계층의 현재 상태는 [PLAN.md](PLAN.md)와 [PLATFORM_IMPLEMENTATION.md](PLATFORM_IMPLEMENTATION.md)를 참조한다.

OpenHSP의 실제 컴파일러와 VM 코어를 CMake로 빌드하여 macOS ARM64와 Windows x64 네이티브 프로세스에서 작은 HSP 프로그램을 실행했다. 초기 opt-in 빌드는 양 플랫폼에서 같은 20개 fixture와 문자열 함수 회귀를 통과했다. 2026-11-18의 note 호환성 수정 후 Mac 별도 빌드는 아래 4개를 더한 24개와 문자열 회귀를 통과했다. Wine, Rosetta, 원본 게임 DLL은 네이티브 실행물에서 사용하지 않았다.

### Windows note 줄바꿈 의미 (2026-11-18)

통합 게임의 config 재작성에서 Mac LF / Windows CRLF 차이를 발견했다. 작은 원본 HSP 3.4 x86 fixture로 `noteadd` 추가·미종결 문자열 뒤 추가·덮어쓰기/삭제·bare LF 읽기를 관측했다. Windows는 CRLF를 생성하고 bare LF는 줄 구분자가 아닌 데이터로 취급했다. 이전 Mac 빌드는 4개 모두 달랐고, `"tail"`에 `noteadd "next"`를 하면 `next`가 유실되는 문제도 재현됐다(CRSTR 길이 1과 내부 `nn+=2`의 불일치).

관리 패치의 `strnote.cpp` 조건을 바꿔 `ELONA_LEGACY_CP932` 빌드에서 Windows와 같은 두 규칙을 사용한다. 옵션이 꺼진 upstream 동작은 유지한다. 변경 전 별도 결과 `build/root-note-before/results.json`, 원본 관측 `work/windows/notes-reference-01.json`, 수정 후 `build/root-note-after/semantic-tests/results.json`, 비교 요약 `work/reports/notes-legacy-crlf.json`에 근거가 있다. 공식 x86 4개와 수정된 Mac 결과가 모두 일치했고 CTest `native_string_walkers`/`native_semantics`가 통과했다. 이 체크포인트에서 수정 후 Windows x64 24개 전체나 실제 게임 config 바이트 재비교까지 수행한 것은 아니다.

재현: `cmake --build build/root-note-after --target elona_hspcmp elona_hsp_probe elona_string_tests` 후 `ctest --test-dir build/root-note-after -R '^native_(string_walkers|semantics)$' --output-on-failure`. 새 빌드는 legacy CP932/RNG ON으로 구성한다. `bootstrap_openhsp.py`의 정확한 관리 패치 검증도 통과했다.

**이는 게임 실행 완료가 아니다.** 진단 호스트 `elona_hsp_probe`는 검토된 작은 fixture 전용이다. 원본 AX와 복원된 전체 게임 AX는 실행하지 않는다. 화면·입력·오디오·원본 플러그인·세이브는 구현하지 않았다. `mes`만 실제 stdout으로 출력하고, 미구현 외부 명령과 DLL 호출은 진단과 함께 실패한다. 호스트에 HSP 기본 파일 I/O가 남아 있으므로 임의 바이트코드용 샌드박스가 아니다.

| 검증 | 결과 |
|---|---|
| Mac 기본 빌드 | macOS 27.2 (26B123), AppleClang 21.0.0, Mach-O 64-bit arm64 컴파일러와 VM |
| Windows 기본 빌드 | MSVC 19.51, Windows 11 x64; 부모 작업자의 SSH 실행 기록 |
| 기본 의미 fixture | 양 플랫폼 10개 통과: int32, 배열, 제어 흐름, 사용자 함수, double, byte 연산, CP932 byte 보존, 잘못된 헤더/범위 거부, exec 거부 |
| Mac/Windows opt-in legacy 빌드 | 위 10개 + CP932 문자 경계 4개 + 논리 경로 3개 + 참조 난수 3개 = 20개 각각 통과 |
| 문자열 함수 회귀 | 양 플랫폼 opt-in: 실제 supio의 소문자화·검색·분리·치환·트림 경계 검사 통과; Mac 기본 빌드 UTF-8 경로 유지 확인 |
| 원본 2.335 AX | 직접 로드는 VM 생성 전에 거부. 별도 도구로 96→112-byte 헤더 정규화와 payload 불변 확인; 게임 실행은 하지 않음 |
| 공식 HSP 3.4 Windows x86 참조 | 원래 3.4 컴파일러가 만든 작은 AX 15개를 헤더 정규화하여 Mac ARM64/Windows x64 VM에서 실행. 두 플랫폼 모두 실제 x86 stdout과 15개 일치. 원본 게임에 내장된 VM과의 동등성은 별도 관문 |
| 원본 게임 플레이·저장 | 아직 미검증 |

기본 빌드와 opt-in 빌드를 구별한다. `ELONA_LEGACY_CP932=ON`은 POSIX 문자열 walker와 양 플랫폼의 논리 경로 분해를 CP932에 맞춘다. `HSPUTF8` 전체를 비활성화하지 않으며, 이 옵션만으로 모든 원본 문자열 의미가 보존되었다고 판단하지 않는다.

`ELONA_LEGACY_RNG=ON`은 호스트 libc 대신 Windows HSP 3.4 참조 벡터와 같은 15-bit RNG를 사용한다. `*-legacy-cp932-debug` preset은 두 opt-in 옵션을 함께 켠다. 각 옵션의 기본값은 OFF이며 독립적으로 선택할 수 있다.

## 재현

입력 OpenHSP: `https://github.com/onitama/OpenHSP.git` @ `3dbb8729dad53ee876fc681696c8fe542698860b`. 원본 라이선스는 `.cache/upstream/OpenHSP/LICENSE`에 보존된다. 프로젝트가 관리하는 패치는 `scripts/patches/openhsp/`에 있다.

```sh
python3 scripts/bootstrap_openhsp.py
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug -j 6
ctest --preset macos-arm64-debug

cmake --preset macos-arm64-legacy-cp932-debug
cmake --build --preset macos-arm64-legacy-cp932-debug -j 6
ctest --preset macos-arm64-legacy-cp932-debug
```

Windows는 Visual Studio x64 개발 환경(`vcvars64.bat`)에서 대응하는 `windows-x64-debug` 또는 `windows-x64-legacy-cp932-debug` preset을 사용한다. CMake/Ninja는 Visual Studio 번들 도구로 검증했다. Python이 없으면 CMake가 HSP 의미 테스트를 등록하지 않는다는 경고를 낸다. 이 경우 `elona_string_tests.exe`와 Mac에서 생성한 fixture AX를 실행하고 stdout bytes를 비교해야 한다. CTest 테스트 수만으로 Python fixture 검증까지 성공했다고 판단하지 말 것.

기본 fixture 결과는 `build/native-macos-arm64-debug/semantic-tests/results.json`, CP932 결과는 `build/native-macos-arm64-legacy-cp932-debug/semantic-tests/results.json`에 기록된다. 각 AX SHA-256, stdout hex, exit code를 포함한다. Windows 원격 결과는 `work/windows/` 아래 부모 작업자의 로그와 JSON을 확인한다. 생성물은 git에 포함하지 않는다.

최종 Windows 20-fixture 검증 스냅샷은 `19bb9c16737af38d`이며 `work/windows/native-semantics-results.json`이 실행물 해시와 개별 결과를 기록한다. Windows의 Python 실행 환경 대신 PowerShell에서 Mac 생성 AX를 실행하고 원시 stdout bytes를 비교했다. 빈 테스트 목록·중복 case·스냅샷 무결성을 검사하며, 거부 fixture도 stdout이 비어 있는지 확인한다.

공식 3.4에서 실제 실행한 AX를 그대로 사용하는 비교는 소스 재컴파일 검증과 별도다. `scripts/normalize_legacy_ax.py`가 16-byte 추가 헤더와 section offset만 조정하고 코드·데이터 payload를 유지한다. `scripts/compare_reference_native.py`는 플러그인 fixture를 제외한 15개를 실행하고 실제 x86 stdout과 비교했다. 결과는 `work/reference-native/fixtures-01/results.json`에 있다.

같은 normalized AX의 Windows x64 실행 비교도 스냅샷 `9f395dc06e965b17`에서 15개 통과했다. 결과는 `work/windows/reference-native-results.json`에 있다. GUI 확장 관문과 재사용 범위는 [GUI_FEASIBILITY.md](GUI_FEASIBILITY.md)를 참조한다.

```sh
python3 scripts/compare_reference_native.py \
  --reference work/windows/reference-build/fixtures-01 \
  --runtime build/native-macos-arm64-legacy-cp932-debug/elona_hsp_probe \
  --output work/reference-native/fixtures-01
```

별도 호환성 진단은 불일치가 남아 있으면 실패 코드 1을 반환한다. 현재 기본 Mac 빌드에서는 CP932·경로 3개가 실패하고 opt-in 빌드에서는 모두 통과한다. 난수 벡터는 비교를 위한 관찰값이다:

```sh
python3 tests/native/run_compatibility.py \
  --compiler build/native-macos-arm64-legacy-cp932-debug/elona_hspcmp \
  --runtime build/native-macos-arm64-legacy-cp932-debug/elona_hsp_probe \
  --common .cache/upstream/OpenHSP/common \
  --work build/native-macos-arm64-legacy-cp932-debug/compatibility-tests
```

## 발견하고 제한적으로 수정한 CP932 손상

CP932 소스 `mes getpath("、A.TXT",16)`는 문장부호 `、`의 바이트 `81 41`을 보존하고 ASCII만 소문자화해야 한다. 원래 POSIX 구현은 `81 61 61 2e 74 78 74`를 출력하여 문장부호의 둘째 바이트까지 변경했다. opt-in 패치 후 `81 41 61 2e 74 78 74`로 보존된다. `instr`도 CP932 글자 중간의 ASCII 바이트를 검색 결과로 반환하던 문제를 수정했다.

`、`의 둘째 바이트 0x41과 `ソ`의 둘째 바이트 0x5c를 검사한다. 패치는 동일한 문자 전진 규칙을 사용하는 함수에만 적용했으며 전체 원본 소스를 UTF-8로 변환하지 않았다. 원본 `talk_conv`와 `gmes`에도 CP932 리드 바이트 검사와 2-byte `strmid` 로직이 존재하므로 원본 논리는 그대로 보존해야 한다.

`src/legacy_logical_path.cpp`는 filesystem 접근 없이 Windows 논리 경로에서 파일명·확장자·디렉터리를 분리한다. `\\`와 `/`를 인식하되 CP932 문자 중간의 `5c`는 건너뛴다. 디렉터리의 점을 파일 확장자로 오인하지 않으며 `C:character.sav`도 처리한다. opt-in일 때 Windows도 같은 함수를 사용해 `mbstowcs`의 프로세스 locale에 의존하지 않는다. 원본에서 쓰는 플래그 조합을 우선 검증했으며 모든 Win32 path edge case를 구현한 범용 경로 라이브러리는 아니다.

## 남은 관문과 구체적 근거

1. **구형 AX 로더와 실행 격리.** 원본 코드 시작은 offset 96, 최신 `HSPHED`는 112 bytes다. upstream에 그대로 주면 `pt_exopt/max_exopt`가 원본 코드 바이트를 읽을 수 있으므로 진단 호스트는 직접 로드를 거부한다. 별도 `normalize_legacy_ax.py`가 명시적인 헤더 확장·offset 이동을 구현했고 공식 3.4 fixture 15개로 실제 동작을 검증했다. 원본 게임 AX도 payload 불변을 확인했지만 실행하지 않았다. DLL/플러그인 초기화와 파일 I/O가 격리되기 전에 게임 전체를 실행하면 안 된다.
2. **실제 파일시스템 경계.** `getpath("C:\\game\\save\\character.sav",8)`의 basename 불일치는 opt-in 논리 경로 어댑터로 해결했다. 실제 파일 열기 경로에서 CP932→OS 인코딩 변환·대소문자·루트 접근 제한은 아직 없다. 논리 경로 분해 성공과 파일 I/O 호환을 구별해야 한다.
3. **난수의 남은 범위.** 기본 upstream은 C 라이브러리 `srand/rand`를 사용한다. Mac seed 12345, `rnd(32768)` 10개는 `28207,22016,20672,11128,21923,16663,20572,31306,13996,4663`으로 공식 HSP 3.4 x86 참조와 달랐다. `ELONA_LEGACY_RNG`는 uint32 상태를 `state = state * 214013 + 2531011`로 갱신하고 `(state >> 16) & 32767`을 반환한다. opt-in 결과 `7584,19164,25795,22125,5828,23405,27477,5413,29072,23404`는 참조와 일치한다. 추가로 seeds `0,1,-1,-2147483647`, ranges `1,2,7,100000,-7`의 각 5개 벡터도 실제 x86 결과와 일치했다. `rnd(0)` 참조는 15초 timeout으로 원인 미확정이며 동등성 통과가 아니다. `randomize`의 seed 생략 시 플랫폼 시간 원천은 아직 upstream 동작이고, exrand는 별도 플러그인이므로 이 벡터로 검증되지 않는다.
4. **포인터와 32-bit 게임 데이터.** `hsp3int.cpp`의 `varptr`는 `(int)(size_t)`로 주소를 32-bit에 넣고, `hsp3code.cpp`의 `dupptr`는 `code_geti()`의 int를 void*로 변환한다. 두 64-bit 플랫폼 모두 일반적으로 안전하지 않다. 원본의 `varptr` 5곳은 `OPENFILENAMEA` 76-byte 구조체를 int[19]로 만드는 파일 선택 경로다. 이를 플랫폼 파일 선택 어댑터로 대체해야 하며 HSP int를 전부 64-bit로 바꾸면 안 된다. 원본 raw 소스에서 `dupptr` 호출은 발견하지 않았다. `HSPLPTR`가 `long`인 정의는 확인되었지만 현재 hsp3 소스에서 사용처가 없어 이 경고의 직접 원인은 아니다.
5. **컴파일과 의미 동등성.** 복원 소스를 최신 컴파일러가 받았다는 사실은 원본과 같은 동작이라는 증거가 아니다. 원본/복원 AX 크기와 테이블에 차이가 있고, 별도 역컴파일 담당자의 보고서를 함께 확인해야 한다.
6. **운영체제·플러그인 경계.** 진단 호스트의 비구현 기능은 실패한다. 그래픽·입력·음원·압축·exrand·hspda·elona/water 동작을 구현하지 않았다. 기본 VM의 파일 명령이 남아 있으므로 원본 실행 전에 파일 I/O 정책 및 명령 추적을 만들어야 한다.

참조 fixture 주의: `varsize`는 HSP 3.5 이후 함수다. HSP 3.4에 같은 소스를 넣으면 미초기화 변수 경고 후 0을 읽으므로 이 값을 배열 크기 차이로 해석하지 않는다. 현재 VM의 `varsize(a)=24` 검사는 4-byte int × 6개를 확인하는 최신 VM 검사이며, 원본 게임의 raw 소스에는 `varsize` 호출이 없다.

다음 작업은 추가 원본 x86 기준 fixture 비교, 파일 I/O 어댑터와 명령 추적, 실제 필요한 플러그인 동작 구현이다. 원본을 무작정 실행하거나 반환값 0으로 플러그인을 대체하는 방식으로 진행하지 않는다.
