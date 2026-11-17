# 플러그인 대체 구현 (HMM 상태 비트 · exrand · z.hpi · hspda)

## 요약

ElonaPlus 2.335가 쓰는 원본 x86 플러그인 중 게임 상태·난수·저장에 직결되는 네 묶음을
이식 가능한 C++17 모듈로 구현했다. 원본 DLL의 ABI를 흉내 내거나 로드하지 않으며
Mac ARM64 / Windows x64 호스트는 타입이 있는 API만 호출한다.

| 모듈 | 파일 | 원본 대비 검증 수준 |
|---|---|---|
| HMMBITON/OFF/CHECK | `src/plugins/hmm_bits.h` | **x86 실행 비교** 71줄 일치(초기값 5 × 비트 13 + 반복) |
| (참고) HMM 오디오·입력 반환 계약 | GUI 소유 `src/app/dll_bindings_media.cpp` | 정적+x86 확인(아래 표), 구현 제안만 함 |
| exrand_randomize/rnd | `src/plugins/exrand.{h,cpp}` | **x86 실행 비교** 69줄·782개 값 일치(시드 8종·범위 10종·정밀도 판별 입력·미시드) |
| zOpen/zRead/zWrite/zClose | `src/plugins/zgzip.{h,cpp}` | **x86 실행 비교** 계약 58줄 일치 + 원본이 쓴 gzip 12개를 네이티브가 읽고, 네이티브가 쓴 7개를 원본이 읽음 |
| sortval(int)/sortnote/xnotesel/xnoteadd | `src/plugins/hspda.{h,cpp}` | **x86 실행 비교** 43줄 + sortnote 경계 40줄 + GUI xnote 12줄 일치. lstrcmpA ASCII 모델은 Windows 덤프 49,777쌍 일치 |

검증 플랫폼: macOS ARM64(Apple clang, 로컬)와 Windows 11 x64(MSVC 19.51, `home/windows`)에서
`elona_plugins_tests` 188,307개 검사가 모두 통과했다(실패 0, MSVC 경고 0).
원본 비교는 공식 HSP 3.4 x86 런타임(`hsp3cl.exe`, xnote는 `hsp3.exe`)에서 게임 동봉 DLL 사본으로 수행했다.
**게임 자체는 실행하지 않았다.** 이 결과는 플러그인 계약의 동등성이며 게임 플레이 검증이 아니다.

## 통합 방법 (루트 CMake 담당자용)

```cmake
include("${CMAKE_SOURCE_DIR}/cmake/ElonaPlugins.cmake")
elona_add_plugins(TESTS ${BUILD_TESTING})          # elona_plugins, elona_zlib, (elona_plugins_tests)
target_link_libraries(<host target> PRIVATE elona_plugins)
target_compile_definitions(<host target> PRIVATE ELONA_HAVE_PLUGINS)  # src/app/dll_bindings_plugins.cpp 사용 시
```

- `elona_plugins`: 정적 라이브러리. PUBLIC include 디렉터리는 `<repo>/src`이므로 `#include "plugins/zgzip.h"` 형태로 쓴다.
- `elona_zlib`: 고정 zlib 1.3.1(SHA-256 `9a93b2b7…df23`)의 핵심 C 파일만 빌드한다. 아카이브는
  `.cache/downloads/zlib-1.3.1.tar.gz`를 쓰고 없으면 해시 검사 다운로드한다. `-DELONA_PLUGINS_SYSTEM_ZLIB=ON`이면 `ZLIB::ZLIB`를 쓴다.
  Windows 스테이징 ZIP을 만드는 도구는 이 아카이브를 함께 넣어야 오프라인 빌드가 된다.
- CTest 항목 `plugins_unit`. 단독 빌드: `cmake -S tests/plugins -B build/plugins-macos -G Ninja && cmake --build build/plugins-macos && ctest --test-dir build/plugins-macos`.

## 반환값 계약 (이중 부호 반전 방지)

모든 함수는 원본 export가 EAX에 남긴 값을 그대로 돌려준다(`PluginResult::raw_return`, HMM은 `*_raw`).
legacy(`#func … $N`, subid -5/-6) 호출에서 `stat = -raw`, 양수 raw의 대기/오류 처리는 **호스트가 한 번만** 적용한다.
`PluginResult::stat`은 문서·테스트용 사본이다.

| outcome | 의미 | 호스트 처리 |
|---|---|---|
| `ok` | 원본과 같은 결과 | raw 반환 |
| `hsp_dll_error` | 원본이 양수(대기 비트 없음)를 반환 → HSP가 DLL 오류. x86에서 `err=38` 관측 | raw 반환(호스트가 오류 발생) |
| `unsafe_input` | 원본이라면 잘못된 포인터·버퍼 초과·초기화 안 된 메모리 | 호출 위치 진단 후 정지 |
| `unsupported` | 원본은 받지만 이식판이 재현·검증하지 않은 입력 | 진단 후 정지 |
| `host_error` | 원본에 없는 호스트 실패 | 진단 후 정지 |

GUI 작업자의 `src/app/dll_bindings_plugins.cpp`를 검토했다. raw 반환, 비정상 outcome의 `DllRefusal`,
sortnote의 `code_setva`, xnote 버퍼 재할당 감지 연결이 이 계약과 맞다. 참고로 zOpen 쓰기 경로의 `not_found`를
즉시 거부하는데, 원본은 stat 1·핸들 0을 돌려주고 이후 zWrite에서 DLL 오류가 난다. 둘 다 명시적 정지이므로 결함으로 보지 않았다.

## 모듈별 근거와 동작

### HMMBITON / HMMBITOFF / HMMBITCHECK

- 정적: `hmm.dll` export `1000d6e0/1000d700/1000d720` → 내부 `10001000/10001020/10001040`. `shl reg, cl`(하위 5비트) 사용, 범위 검사 없음.
  ON/OFF EAX -1, CHECK 0/-1.
- 네이티브: `uint32_t(1) << (uint32_t(bit) & 31)`. 헤더 전용·실패 없음(호출 지점 946곳).
- x86: 초기값 0/1/-1/INT_MIN/0x12345678 × 비트 -33…64·INT_MAX·INT_MIN, 배열 양옆 sentinel, 반복 ON/OFF가 모두 일치(`tests/plugins/reference/hmm_bits.txt`).

### exrand

- 정적(원본 디스어셈블): MT19937 `init_genrand`(1812433253), mt19937ar-cok 재충전, 미시드 첫 호출은 5489로 시드.
  `exrand_rnd`: `fild u32 → fmul 2^-32 → fimul range → _ftol`. seed<0·range<0이면 EAX 3(상태·출력 불변).
  range 0은 0을 쓰고 난수 하나를 소비한다. `exrand_rnd2`(+base)는 export만 있고 2.335가 선언하지 않는다.
  `exrand_randomize_by_time`은 선언되지 않아 제공하지 않는다.
- 정밀도: 한 번의 53비트 반올림·정확한 정수곱·24비트 반올림이 서로 다른 입력(예: seed 12345, range 68925423 →
  53비트 64074182 / 정확 64074181 / 24비트 64074180)을 찾아 실행했다. 원본은 **64074182**로 53비트(=IEEE double)였다.
  네이티브는 double 곱셈 한 번으로 계산하며 (2^31+1)·(2^31−1) 같은 경계도 같은 규칙을 따른다.
- x86: 기존 seed 12345 열과 시드 0/1/12345/5489/1999/INT_MAX/20261117 × 범위 0…INT_MAX, 혼합 범위, range 0 소비,
  미시드 수열이 모두 일치. seed -1, range -5는 HSP `err=38`.
- 한계: 정밀도는 HSP 3.4 콘솔 런타임 프로세스에서 확인했다. 실제 게임 프로세스의 x87 제어 워드는 측정하지 않았다
  (동봉 플러그인에 Direct3D가 없어 53비트 기본값으로 **추론**).

### z.hpi (gzip 스트림)

- 정적: zlib 1.1.4 gzio의 `gzopen/gzread/gzwrite/gzclose` 래퍼. mode 0 → `"rb"+레벨문자`, 그 외 `"wb"+…`,
  레벨 문자 = 하위 바이트 + '0'. zOpen은 실패 시에도 핸들 변수에 0을 쓴다(stat 1). zRead는 음수를 0으로 바꾸고,
  zWrite는 `-inner`(NULL·읽기 핸들 → EAX 2 → DLL 오류), zClose는 내부값 0이면 stat 0, 아니면 1.
- 네이티브 구조: gzio 1.1.4 상태 기계(헤더 파싱, 비gzip 입력 투명 통과, CRC·ISIZE, 연결 멤버, 16 KiB 버퍼)를 C++로
  다시 작성하고 압축 자체는 현재 zlib raw deflate/inflate를 쓴다. 헤더 OS 바이트는 원본과 같은 0x0b.
- 핸들: 1부터 증가하는 int32 ID 레지스트리(재사용 없음, 호스트 포인터 아님). 0은 원본 NULL 분기
  (read stat 0, write DLL 오류, close stat 1). 미등록·닫힌 핸들은 `unsafe_input`.
- 버퍼: `count > buffer_capacity`, 음수 count는 원본에서 메모리 초과 → `unsafe_input`. count 0은 stat 0.
- 경로: 호스트가 해석한 파일시스템 경로만 받는다(CP932 변환은 파일 계층). Windows는 `_wfopen_s`로 유니코드 경로를 연다.
  디렉터리 경로는 MSVC처럼 열기 실패로 처리한다.
- x86 실행 비교(`gz_contract.txt` 58줄): 크기 0/1/4/16384/16385/100000, 4096·16385·100000 바이트 청크 읽기/쓰기,
  압축률 낮은 100000바이트(입력 버퍼 여러 번 채움), EOF 뒤 추가 읽기, 없는 파일·디렉터리, NULL 핸들,
  쓰기 핸들 읽기, count 0, CRC 오류(전체 길이 반환 후 0, close stat 1), 잘린 파일(부분 길이 후 0, close stat 1),
  비gzip 파일, 빈 파일, 연결 멤버, FEXTRA/FNAME/FCOMMENT/FHCRC 헤더, 잘못된 method, 레벨 문자 'f'/'h'/NUL/0x103.
  같은 스크립트를 네이티브로 재생해 줄 단위로 일치시켰다.
- 교차 읽기: 원본이 쓴 `orig_*`·`lv_*` 12개 파일을 네이티브가 정확히 복원했고, 네이티브가 쓴 7개 파일을 원본이 정확히 읽었다.
  압축 바이트 일치는 목표가 아니다.
- 한계: (1) 지원하지 않는 레벨 문자(MSVC fopen 모드로 들어가는 문자)는 `unsupported`. (2) zlib 1.1.4 raw inflate는
  스트림 끝 뒤 여분 바이트가 필요했으나 현재 zlib은 아니다. deflate 데이터는 완전하고 트레일러가 없는 손상 파일에서
  close의 음수 코드 종류가 다를 수 있지만 stat은 둘 다 1이다(비교 미실행). (3) 유니코드 경로는 네이티브(Mac/Win)에서만 검사했다.
  원격 Windows 로캘이 CP949라 원본 CP932 경로 비교는 하지 않았다. (4) `close_all()`은 프로세스 종료처럼 트레일러 없이 버린다.

### hspda (게임 동봉본, 2007-07-28 빌드)

게임 동봉 hspda.dll(SHA-256 `a1fcd341…eb08`)은 SDK 3.4판(`1033e651…8d09`)과 다르다. 선언도 xnotesel `$83`, xnoteadd `$6`이다.

- sortval: int 배열은 원본 퀵정렬(끝값 평균 피벗, int32 랩어라운드, Hoare 탐색)을 그대로 옮겼다. PVal `len[1]`개만 정렬(2차원은 첫 행).
  원본이 배열 밖을 탐색하게 되는 입력(피벗 오버플로)은 `unsafe_input`. double 배열은 `unsupported`(2.335는 int만 사용).
  문자열 변수·길이 0 → EAX -1(stat 1).
- sortnote: 줄 수 = 1 + LF/CR/CRLF 개수. 끝이 줄바꿈이면 빈 항목이 생겨 CRLF가 하나 더 붙고, 빈 문자열은 `"\r\n"`이 된다.
  분할기는 "CR/LF 뒤 LF"를 한 번에 건너뛰므로 `\n\n`이 있으면 원본은 초기화되지 않은 항목을 정렬한다 → `unsafe_input`.
  버블 정렬(order 0: `lstrcmpA(next, cur) <= 0`이면 교환, 그 외 `>= 0`), 모든 항목 + CRLF 결합, 결과가 원래 블록보다 길면 `unsafe_input`.
- lstrcmpA: 출력 가능한 ASCII에 한해 Windows word sort를 모델링했다(`'`와 `-`는 1차 비교에서 무시, 대소문자 무시 후 소문자 우선,
  기호 순서 `" !\"#$%&()*,./:;?@[]^_\`{|}~\\+<=>"` 뒤 숫자·문자). x86 `lstrcmpA`(사용자 로캘 0x412) 덤프 49,777쌍과 일치했고,
  `CompareStringA(0x0411)` 덤프도 같은 결과였다. **비ASCII 바이트는 `unsupported`**다(Windows NLS 표 필요). 게임에서는
  사용자 NPC 파일 목록(`user\*.npc`) 정렬에만 쓰이며 동봉 배포에는 해당 파일이 없다.
- xnotesel/xnoteadd: 선택 시점의 버퍼 포인터를 저장하고, CR만 줄 구분(LF는 내용), 같은 줄이면 그 인덱스, 아니면 NUL 위치에 `"str\r\n"` 추가.
  원본의 크기 검사 없는 쓰기·4096바이트 스택 버퍼·항목 표 초과·재할당된 버퍼는 `unsafe_input`. 비문자열 선택은 stat 1.
- x86: 정렬 43줄, sortnote 경계 40줄(`hspda_sortnote_edges.txt`)이 일치. xnote는 GUI 런타임(`hsp3.exe`) 실행 12줄과
  C# 직접 호출 하네스(`XnoteHarness.cs`) 결과가 서로 같고 네이티브와도 같다.

### HMM 오디오·입력 초기화 계약 (GUI 작업자 요청으로 확인, 구현은 GUI 소유)

정적 분기와 x86 `hsp3.exe` 실행(run 07, `tests/plugins/reference/hmm_audio.txt`)이 일치했다. raw=EAX, stat=-raw.

| 함수 | 미초기화(INIT 호출 없음) | 초기화 후(이 Windows 세션) | 근거 |
|---|---|---|---|
| DSINIT / DMINIT / DIINIT | — | 성공 raw -1(stat 1). 실패 시 raw 0(stat 0) | 내부 bool `neg al; sbb` |
| DSSTOP ch | stat 0 (범위 밖·빈 슬롯도 0) | 빈 슬롯 stat 0 | 정적+x86 |
| DSRELEASE ch | **stat 1** (항상 raw -1) | stat 1 | 정적+x86 |
| DSSETVOLUME ch, vol | stat 0 | 빈 슬롯 stat 1, vol>100 stat 0 | 정적+x86 |
| CHECKPLAY ch | stat 0 | 재생 중 아님 stat 0 | 정적+x86 |
| DSPLAY / DMPLAY / DMSTOP | stat 0 | DMSTOP stat 1 | 정적+x86 |
| DIGETJOYNUM | stat 0 (bss 전역) | 조이스틱 없음 0 | 정적+x86 |
| DSEND / DMEND | stat 1 | stat 1 | 정적+x86 |
| DSGETMASTERVOLUME | 내부 -1 → raw +1 → **HSP `err=38`** | 100 | 정적+x86 |

Windows 11 세션 0에서도 세 INIT이 모두 성공했으므로 일반 Windows 원본은 DirectX 경로를 쓴다. INIT이 stat 0이면 게임은
`cfg_music/cfg_sound = 2`로 HSP 내장 `mmload/mmplay/mmstop` 경로로 전환한다(오디오 끔이 아님). 또 `snd` 루틴은 `cfg_sound`와 무관하게
`CHECKPLAY 7..12`로 빈 채널을 고른다. 백엔드가 없을 때 INIT stat 0은 원본의 "초기화 실패 환경"을 재현하는 것이며, 그 경우 mm* 명령이
미구현이면 성공 처리하지 말고 정지해야 한다. GUI 구현의 미초기화 `DSRELEASE` stat 0은 원본(stat 1)과 달라 수정을 제안했다.

## 재현 절차

```sh
cmake -S tests/plugins -B build/plugins-macos -G Ninja && cmake --build build/plugins-macos
./build/plugins-macos/elona_plugins_tests --work build/plugins-macos/plugin-tests
# 원본 x86 비교(전용 reference-build/plugins-claude-* 폴더)
python3 tests/plugins/fixtures/plugin_fixtures.py stage --tests-bin build/plugins-macos/elona_plugins_tests
python3 tests/plugins/fixtures/plugin_fixtures.py upload
python3 tests/plugins/fixtures/plugin_fixtures.py run --run plugins-claude-NN
python3 tests/plugins/fixtures/plugin_fixtures.py fetch --run plugins-claude-NN
python3 tests/plugins/fixtures/plugin_fixtures.py collect --run plugins-claude-NN   # tests/plugins/reference 갱신
# Windows x64 네이티브 빌드·테스트(전용 build/plugins-claude-x64-NN)
python3 tests/plugins/fixtures/plugin_fixtures.py winbuild --run plugins-claude-x64-NN
```

`stage`는 원본 폴더의 DLL 네 개를 해시 확인 후 **복사**만 한다. 데이터는 모두 합성 패턴이다.
원격 러너(`RunPluginFixtures.ps1`)는 공유 `Hsp34Compile.cs`/`Hsp34Run.cs`를 읽기 전용으로 재사용하고, DLL·SDK 해시를 확인하며,
기존 run 폴더를 덮어쓰지 않는다. 기대값 자료는 `tests/plugins/reference/`(약 0.95 MB, 관측 run `plugins-claude-07`)에 있다. 각 fixture는 플러그인 호출 뒤 `GetModuleHandleA`/`GetModuleFileNameA`로 **실제 로드된 모듈 경로**를 출력하고, 러너가 그 경로의 SHA-256을 계산하며, `collect`가 케이스 폴더 내부·고정 해시인지 확인한다(`loaded_modules.txt`).

## 진행 중 발견한 문제와 교훈

1. **SDK DLL 가로채기.** HSP 런타임을 SDK 폴더에서 실행하면 실행 파일 폴더가 현재 폴더보다 먼저 검색되어,
   SDK 루트에 같은 이름이 있는 hspda.dll(또한 hspinet/hspsock/hspext)이 게임 원본 대신 로드된다. run 01–04와
   임시 실험 adhoc-02~04의 hspda 관측은 SDK판 결과였다(ASCII 정렬, 끝 빈 항목 없음, `$83` 호출 시 접근 위반).
   run 05부터 런타임 exe를 케이스 폴더에 복사해 실행했고, 결과가 정적 해석과 일치했다. exrand/hmm/z.hpi는 SDK에 없어 영향이 없다.
2. HSP 예약어(`line`, `pos`)를 변수명으로 쓴 fixture 문법 오류, 모듈 내부 `#func` 참조(`#func global` 필요)를 수정했다.
3. 테스트 코드의 C++ 평가 순서 의존(`read`와 `hex(rb)`를 한 식에 둠)이 MSVC에서만 드러나 수정했다.

## 남은 일

- 게임 통합: GUI 작업자의 디스패처가 실제 원본 AX에서 이 바인딩을 호출하는 경로의 실행 확인(저장/로드 포함)은 통합 담당이다.
- 원본 2.335 세이브 파일(복사본)을 zRead로 읽어 배열 바이트가 맞는지는 아직 확인하지 않았다(개인 세이브 미사용 원칙상 별도 복사본 필요).
- lstrcmpA 비ASCII(CP932) 순서, double sortval, 게임 프로세스의 x87 정밀도는 미구현/미검증으로 남는다.
- 루트 `THIRD_PARTY_NOTICES.md`·`licenses/`에 zlib 고지 병합(`src/plugins/THIRD_PARTY.md` 참조).
