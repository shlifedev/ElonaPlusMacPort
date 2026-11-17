# Windows HSP 3.4 참조 검증

## 범위와 현재 상태

2026-11-17, SSH `home/windows`의 Windows 11 Pro x64에서 공식 HSP 3.4의
**x86 컴파일러와 x86 콘솔 런타임**을 사용했다. 원시 2.335 소스의 전체 컴파일,
작은 독립 fixture의 실행, 역컴파일 과정의 제어 흐름 오류 재현까지 확인했다.
이 컴파일/fixture 검증에서는 게임 GUI·플레이·세이브를 실행하지 않았다.
별도로 원본의 언어 선택 화면을 관측한 결과는
[WINDOWS_ENVIRONMENT.md](WINDOWS_ENVIRONMENT.md)에 있다.

작업 경로는 `%USERPROFILE%\ElonaPlusNative-2.335\reference-build`다.
원본 게임 설치, 개인 파일, 시스템 설정은 변경하지 않았다. 원본 DLL은 이 전용
경로의 x86 참조 fixture에서만 사용했고 이식판 x64 프로세스에는 로드하지 않았다.

## 고정한 도구와 입력

| 대상 | SHA-256 |
|---|---|
| 공식 [HSP 3.4 SDK ZIP](https://www.onionsoft.net/hsp/file/hsp34a.zip) | `2b841c71bbe4efad0dce1bed88bdd72bfd4ebe5e5671689b4a5833c105b278cf` |
| SDK `hspcmp.dll` | `b79bccb9230a26ff9b61def7d6707c13529defedf8c16dcb9ea2f90a2e07b005` |
| SDK `hsp3cl.exe` | `29f90169fc3798bd876a14bfac7dd53d846f296b55278162b8d279b5e126308f` |
| SDK `common/hspdef.as` | `270e99939a413e266de83c78e23ecf9168e4521b449b2d76c51e215c54ad2366` |
| 원시 CP932 소스 | `dce3e305f91e377fd669a045d9dce7cc197509d094852e56b67be312aa233725` |
| 원본 배포물의 `exrand.dll` | `fb99c1443797646428c705740ae0a93aacfe9cd76e3341b0e24970fb061a0b23` |

ZIP은 로컬 `.cache/downloads/hsp34a.zip`과 원격 `inputs/hsp34a.zip`에 있다.
SDK의 CP932 파일명을 PowerShell `Expand-Archive`가 처리하지 못했다. 실패한
`reference-build/sdk`는 별도로 남겼으며 실제 사용한 경로는 `sdk-cp932/hsp34`다.
스크립트는 `.NET ZipFile.ExtractToDirectory(..., Encoding.GetEncoding(932))`로
추출하며 SDK 문서와 라이선스 파일을 함께 보존한다.

`tools/reference/Hsp34Compile.cs`는 SDK의 `doclib/hspcmp.txt`,
`common/hspcmp.as`와 실제 DLL exports에 맞춘 32비트 stdcall 래퍼다.
시스템 .NET Framework의 C# 컴파일러에 `/platform:x86`을 지정한다.
`hsc_ini`, `hsc_objname`, `hsc_compath`, `hsc_comp(0,0,0,0)`, 메시지 조회,
`hsc_bye`만 호출하며 `hsc3_run`을 호출하지 않는다.
소스·출력 경로는 CP932로 명시적으로 전달한다.

`hsc3_messize`는 텍스트 길이가 아닌 버퍼 용량을 반환한다. 현재 래퍼는
`hsc_getmes`의 첫 NUL까지만 CP932 로그로 보존한다. 초기 `baseline` 및
`fixtures-01` 로그는 용량까지 저장했으므로 첫 NUL 뒤의 데이터는 메시지가 아니다.
`fixtures-02`, `control-flow-fixed-01`부터 수정된 래퍼를 사용했다.

## 전체 소스 컴파일

원시 소스를 바이트 변경 없이 복사하여 공식 3.4로 컴파일한 결과:

| 항목 | 결과 |
|---|---|
| 컴파일 결과 | return 0, `No error detected` |
| 미초기화 변수 경고 | 690개; 경고가 사라진 것으로 처리하지 않음 |
| AX 파일 크기 | 14,823,722 bytes |
| AX SHA-256 | `6fee403a1674ef16e6e3ee2fd8a38de0632a30b432303547115709d170f80c1b` |
| 코드 / 문자열 영역 | 12,089,082 / 2,640,190 bytes |
| 변수 / 라벨 / 함수 수 | 2860 / 16010 / 722 |
| 로컬 결과 | `work/windows/reference-build/baseline/` |

컴파일 성공 뒤 정적 비교에서 실제 복원 오류가 드러났다. 따라서 이 원시 AX를
원본과 동등한 실행물로 사용하면 안 된다.

원본과 원시 재컴파일 AX의 문자열 토큰 287,524개(고유 44,126개)는 바이트값,
등장 횟수, 코드 내 등장 순서까지 같다. double 78개(고유 bit pattern 23개)와
전체 변수 ID 토큰 순서도 같다. 문자열 영역 크기가 작아졌다는 것만으로 문자열
누락이라고 판단하지 않았다. 근거는
`work/reports/ax-roundtrip-2.335/literal-comparison.json`과 `token-context.json`이다.

### 확인한 제어 흐름 오류와 수정

1. 원본 코드 상대 바이트 위치 `3507426`의 두 번째 `else`가 복원에서 빠졌다.
   HSP 3.4는 연속 `else` 구문을 받아들이며 각 opcode의 상대 점프를 실행한다.
   borscht는 두 번째 else 블록을 첫 else 안으로 합쳐 다른 경로에서 실행한다.
   해당 게임 구간은 raw 소스 213230–213278행의 sprite 18 준비 코드다.
2. raw 소스 232679행의 `goto *label_4600`은 `stop`을 컴파일할 때 자동 삽입되는
   goto와 중복됐다. 두 goto 모두 같은 stop을 가리키며 첫 무조건 분기 뒤의
   두 번째 goto는 통상 제어 흐름에서 도달하지 않는다.

실제 jump target은 **명령 시작 + 명령/추가 워드의 6 bytes + extra × 2**다.
borscht 내부의 `token_offset + extra` 값은 그대로 VM의 최종 점프 위치가 아니다.
첫 오류에서 `else@3507204, extra=111`은 `3507432`로,
`else@3507426, extra=710`은 `3508852`로 간다.

`tools/fix_decompiled_control_flow.py`는 정확한 raw 해시를 확인한 뒤 두 번째 else를
원래 순서에 다시 넣고 중복 goto를 제거한다. 원시 파일은 보존하며 출력은
`work/decompiled/2.335-hsp34/start.hsp`다. CP932 문자열을 재인코딩하지 않는다.

| 항목 | 제어 흐름 수정본 |
|---|---|
| 소스 SHA-256 | `a9a6e5cdc6f08a664fb581647f06181667b5943e86de57cd0dfef32567823669` |
| 공식 3.4 컴파일 | 성공, 실행하지 않음 |
| AX SHA-256 | `cdf66894f9290f5d0faf56a760c5e27ddeb1085a87121d6d9681e5968bfa7a1b` |
| AX bytes / 코드 bytes | 14,823,720 / 12,089,080 |
| 기록 | `work/windows/reference-build/control-flow-fixed-01/` |

게임 코드가 없는 작은 `double_else` fixture도 공식 3.4로 컴파일·실행했다.
원본은 `true → extra → false`, borscht 복원본은 `true → false → extra`,
두 번째 else를 복구한 본은 다시 `true → extra → false`를 출력했다.
`stop_ghost` fixture는 원본·복원본·수정본 모두 `done`을 출력했다.
이 fixture는 `if 0` 안의 stop을 실행하지 않는다. goto 중복의 도달불가능성은
opcode와 target을 확인한 정적 판단이다.
소스 생성 도구는 `tools/reference/prepare_control_flow_fixture.py`, 결과는
`work/windows/reference-build/fixtures-control-01`, `fixtures-control-02`다.

### 별도로 확인한 DLL 선언 오류

borscht의 모든 외부 선언을 현대식 타입 목록으로 재출력하면 원래 `STRUCTDAT.subid=-5`
(old DLL)였던 54개 선언이 `-3`으로 바뀐다. old DLL 반환값의 부호를 바꾸어 `stat`에
전달하는 원본 규약까지 달라질 수 있다. 이는 제어 흐름 수정과 별도로 고쳐야 한다.
`docs/PLUGIN_FINDINGS.md`와 `legacy-dll-declarations.json`에 세부 근거를 기록한다.
`tools/restore_legacy_declarations.py`는 원본 AX의 정확한 인수 타입으로부터 구형
숫자 flags 선언을 재생성한다. 제어 흐름 수정본에서 ASCII 선언 54줄만 바꾸며
전체 CP932 텍스트를 디코딩/재인코딩하지 않는다.

### 최종 복원본의 정적 검증

제어 흐름 및 old DLL 선언을 모두 보정한 소스를 공식 3.4로 다시 컴파일했다.

| 항목 | 최종 결과 |
|---|---|
| 소스 | `work/decompiled/2.335-reference/start.hsp` |
| 소스 SHA-256 | `c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55` |
| AX SHA-256 | `cec8809573ed8172033ac86d94709c40289b65398dad1fcc778c394108cf3b72` |
| AX bytes / 코드 bytes | 14,823,720 / 12,089,080 |
| 컴파일 / 경고 | return 0, 미초기화 변수 경고 690개 |
| 기록 | `work/windows/reference-build/reference-fixed-01/` |
| 정규화 토큰 수 | 원본·복원본 모두 2,805,612 |
| 토큰 값·실제 분기/라벨 대상·line/argument 플래그 차이 | 0 |
| 함수 선언 722개 차이 | 0; old DLL 54개 포함 |

`tools/compare_ax_roundtrip.py`는 문자열을 원시 바이트로, double을 64비트 패턴으로,
라벨/분기를 대상 토큰 번호로, 함수/매개변수 참조를 선언 이름과 owner 정체로
해석하여 비교한다. 외부 함수 subid, target/flags, 인수 타입·owner·offset,
stack bytes와 function flags도 비교한다. 선언 재배치에 따른 ID 변화는 그대로
오류로 세지 않는다. ordinary DLL의 사용하지 않는 clsid 필드와 디버그 정보,
소스 파일명, 참조하지 않는 문자열 바이트는 비교 대상에서 제외한다.

토큰 저장 폭 `0x80`과 단일 식 최적화 플래그 `0x10`은 별도 집계한다. 원본 대비
코드가 6 bytes 커진 이유는 문자열 참조 세 개의 데이터 offset이 65535 경계를
넘어 각각 16비트에서 32비트로 커졌기 때문이다. 토큰 37853의 offset은
65467→65539, 37929는 65501→65573, 37932는 65502→65574이며 실제 문자열은 같다.
`0x10` 차이를 원본 실행 검증으로 대체해 설명하지 않는다.

최종 비교 기록은 `work/reports/ax-roundtrip-2.335/reference-final.json`이다.
독립적인 old DLL 메타데이터 비교도 `final-legacy-comparison.json`에서 54/54 일치했다.
작은 branch target 변경, literal 변경, 저장 폭, 잘린 token 및 DS 경계를 검증하는
fixture 5개와 출력 보존 회귀 검사 4개를 추가했다.

이 결과는 M1의 **정적 복원 조건**에 대한 근거다. 실제 복원 게임은 실행하지 않았고,
전체 플레이·저장·DLL 동작 동등성을 확인한 것은 아니다.

## 작은 실행 의미 참조

### DLL 검색 경로 정정 (2026-11-18)

플러그인 담당자가 SDK 폴더에서 `hsp3cl.exe`/`hsp3.exe`를 직접 실행하면 작업 폴더의
게임 DLL보다 SDK EXE와 같은 폴더의 동명 DLL이 먼저 로드되는 문제를 발견했다.
`plugins-claude-01`~`04`의 **hspda 결과는 게임 동봉 DLL의 동작 근거로 사용하지 않는다**.
로그는 보존한다. `plugins-claude-05`부터 런타임을 case 폴더로 복사해 실행했으며,
그 실행을 기반으로 `tests/plugins/reference/hspda*`를 교체했다. 이후 `plugins-claude-07`에서는
`GetModuleHandleA`/`GetModuleFileNameA`로 실제 로드 경로를 얻고 해당 파일의 SHA-256을
검증했다. root가 report의 모듈 관측 8개를 확인했으며 현재 기준 자료는 `07`이다.

root가 고정 SDK ZIP의 이름 목록을 확인했다. SDK에는 `hspda.dll`, `hspinet.dll`,
`hspsock.dll`, `hspext.dll`이 있지만 `exrand.dll`, `hmm.dll`, `z.hpi`,
게임의 `hspext_ext.dll`, `elona.dll`, `water.hpi`와 같은 이름의 파일은 없다.
따라서 기존 exrand 비교는 이 shadowing 문제의 영향 대상이 아니다. 기본 GUI·파일
fixture에는 외부 플러그인 선언이 없으며 해당 SDK DLL 결과를 기준으로 삼지 않는다.

공용 `RunHsp34Fixtures.ps1`도 case마다 런타임을 복사하고 그 SHA-256을 확인하도록 고쳤다.
`-InputName fixture-input-<name>`으로 독립 입력 폴더를 선택할 수 있으며 report에 입력
폴더·manifest 해시·실행 경로·런타임 복사본 해시를 남긴다. 기본값은 기존 `fixture-input`이다.

수정 실행기 실측: `fixtures-local-runtime-01`은 당시 공용 입력 폴더에 있던 제어 흐름
fixture 4개를 컴파일/실행 exit 0으로 통과했다. 이어 독립 입력을 생성한
`fixtures-local-runtime-02`는 기본 16개를 모두 컴파일/실행 exit 0, stderr 빈 값으로
실행했다. 런타임 복사본 16개 해시가 고정 SDK와 일치했고 exrand의 아래 10개 난수도
동일했다. 기대 stdout 14개 중 13개가 일치하며, 나머지는 아래에 기록한 HSP 3.4의
`varsize` 미지원 차이(0 대 24)다. 증거: `work/windows/case-local-runtime-02.json`,
`work/reports/case-local-runtime-comparison.json`, `work/reports/plugin-reference-validity.json`.

`tools/reference/prepare_hsp34_fixtures.py`가 `tests/native`의 검토된 작은 사례를
CP932 파일로 복사한다. `Hsp34Run.cs`는 `hsp3cl.exe`의 stdout/stderr BaseStream을
원시 바이트로 저장한다. PowerShell의 문자 디코딩/재인코딩을 거치지 않는다.
각 프로세스는 15초 제한이 있으며 제한 초과 시 그 fixture 프로세스만 종료한다.

기본 16개 fixture 모두 컴파일과 실행 종료 코드 0이며 stderr는 비었다.
CRLF만 LF로 정규화하면 13개 일반 의미·CP932·Windows 경로 사례가 기대값과 같다.
난수 2개는 관찰 벡터를 얻는 사례다. 배열 사례의 `varsize(a)`는 별도 주의가 필요하다.

HSP 3.4에는 `varsize` 함수가 없다. 이 이름을 미초기화 변수로 처리했다는 컴파일
경고가 있으며 출력 0은 배열 메모리 크기가 아니다. 이 함수를 제공하는 새 VM의
24와 비교해 배열 규약 차이라고 결론 내리면 안 된다. 공통 부분의 `length=3`,
`length2=2`, `a(2,1)=42`와 사용자 함수 호출 결과 42는 정상이다.

`randomize 12345`, `rnd(32768)` 열 번의 공식 3.4 결과:

```text
7584, 19164, 25795, 22125, 5828, 23405, 27477, 5413, 29072, 23404
```

원본 `exrand.dll` 복사본에서 `exrand_randomize 12345,0,0,0`과
`exrand_rnd value,32768,0,0` 열 번의 결과:

```text
30461, 29168, 10366, 4283, 6026, 1302, 6703, 27080, 18603, 17435
```

확장 검사는 다음과 같다. 각 범위 검사 전에 seed를 12345로 다시 설정했다.

| 조건 | 첫 5개 출력 |
|---|---|
| seed 0, range 32768 | 38, 7719, 21238, 2437, 8855 |
| seed 1, range 32768 | 41, 18467, 6334, 26500, 19169 |
| seed -1, range 32768 | 35, 29739, 3374, 11141, 31308 |
| seed -2147483647, range 32768 | 41, 18467, 6334, 26500, 19169 |
| range 1 | 0, 0, 0, 0, 0 |
| range 2 | 0, 0, 1, 1, 0 |
| range 7 또는 -7 | 3, 5, 0, 5, 4 |
| range 100000 | 7584, 19164, 25795, 22125, 5828 |

`rnd(0)`은 stdout/stderr 없이 제한 15초를 넘겨 래퍼가 종료했다. 오류 창 또는
정지 원인은 확인하지 않았으며 특정 오류 코드가 나왔다고 기록하지 않는다.
기록은 `work/windows/reference-build/fixtures-01`, `fixtures-02`에 있다.
공식 AX를 새 네이티브 VM에서 실행한 별도 비교는 `docs/NATIVE_RUNTIME.md`를 본다.

## 재현 절차

로컬에서는 먼저 추출·decode와 SDK 다운로드 해시를 확인한다.

```sh
python3 tools/bootstrap_borscht.py
python3 tools/decompile_game.py --verify-repeat
python3 tools/fix_decompiled_control_flow.py
python3 tools/restore_legacy_declarations.py
python3 tools/reference/prepare_hsp34_fixtures.py --extended-rng
```

이미 생성된 출력은 덮어쓰지 않는다. 새 출력 경로를 지정하거나 기존 해시를 검증한다.
SSH의 `/` 포함 별칭 때문에 일반 scp는 실패했으므로 SFTP batch를 사용했다.
전송 전 `tools/windows_probe.py`의 `run_powershell`로 전용 `reference-build/input`을
만든 뒤 아래와 같이 전송한다. 파일명은 전용 ASCII 상대 경로로 고정했다.

```text
put .cache/downloads/hsp34a.zip ElonaPlusNative-2.335/inputs/hsp34a.zip
put work/decompiled/2.335/start.hsp ElonaPlusNative-2.335/reference-build/input/start.hsp
put work/decompiled/2.335-hsp34/start.hsp ElonaPlusNative-2.335/reference-build/input/start-control-flow-fixed.hsp
put work/decompiled/2.335-reference/start.hsp ElonaPlusNative-2.335/reference-build/input/start-reference-fixed.hsp
put tools/reference/Hsp34Compile.cs ElonaPlusNative-2.335/reference-build/Hsp34Compile.cs
put tools/reference/Hsp34Run.cs ElonaPlusNative-2.335/reference-build/Hsp34Run.cs
put tools/windows_hsp34.ps1 ElonaPlusNative-2.335/reference-build/windows_hsp34.ps1
put tools/reference/RunHsp34Fixtures.ps1 ElonaPlusNative-2.335/reference-build/RunHsp34Fixtures.ps1
```

위 텍스트를 `subprocess.run(['sftp','-o','BatchMode=yes','-b','-','home/windows'],
input=batch, text=True, ...)`에 전달한다. fixture 입력 폴더의 manifest/HSP/DLL은
원격 `reference-build/fixture-input`으로 전송한다. 실행할 각 입력 소스의 SHA를
manifest와 대조하며 허용된 원본 exrand 외의 DLL은 fixture 래퍼가 거부한다.

다음 PowerShell을 `run_powershell('home/windows', script, timeout=120)`에 전달한다.
실행 정책이나 시스템 설정을 바꾸지 않고 스크립트 내용을 현재 프로세스에서 실행한다.

```powershell
$root = Join-Path $env:USERPROFILE 'ElonaPlusNative-2.335\reference-build'
$compile = [scriptblock]::Create([IO.File]::ReadAllText((Join-Path $root 'windows_hsp34.ps1')))
& $compile -RunName baseline-new -SourceVariant raw
# 다음 호출은 별도 SSH/PowerShell 호출로 실행 가능
& $compile -RunName control-flow-fixed-new -SourceVariant control-flow-fixed
& $compile -RunName reference-fixed-new -SourceVariant reference-fixed
$fixtures = [scriptblock]::Create([IO.File]::ReadAllText((Join-Path $root 'RunHsp34Fixtures.ps1')))
& $fixtures -RunName fixtures-new
```

각 실행은 이미 존재하는 run 디렉터리를 거부한다. SFTP `get -R`로 run 폴더를
`work/windows/reference-build/`에 회수한다. report.json에는 원본/SDK/래퍼/AX 해시,
호출 인수, 종료 코드와 실행 범위를 기록하며 로그와 AX는 Git에서 제외한다.

```sh
python3 tools/compare_ax_roundtrip.py \
  work/extracted/2.335/start.ax \
  work/windows/reference-build/reference-fixed-01/start.ax \
  --output work/reports/ax-roundtrip-2.335/reference-final.json
python3 -m unittest discover -s tests -p test_ax_roundtrip.py -v
python3 -m unittest discover -s tests -p test_control_flow_transform.py -v
```

원시 decode 소스의 컴파일 가능성과 작은 fixture 일치는 전체 게임의 동등성,
플러그인 호환성, 저장 형식, GUI 호환성의 완료 조건을 대신하지 않는다.
