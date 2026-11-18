# ElonaPlus 2.335를 C#과 .NET 10으로 이식할 때의 호환성

2026-11-18에 오프라인 게임 로직까지 C# 소스로 옮기는 경우를 조사했다. 현재 호스트는 원본 AX를 OpenHSP로 실행한다. 복원 HSP를 재컴파일한 결과는 원본과 정규화 토큰 2,805,612개, 분기 목적지, 함수 선언 722개가 일치한다. 이는 변환할 명령이 무엇인지 대조할 근거다. C#판의 플레이 동등성은 별도로 검증해야 한다. [원본과 복원 AX 비교](WINDOWS_REFERENCE.md)

**배포 목표에는 결정할 사항이 하나 있다.** 프로젝트의 최소 목표는 macOS 13 ARM64다. 조사 시점에 Microsoft가 .NET 10을 공식 지원하는 macOS는 15·26·27이고, Windows 11 x64는 지원 대상 버전이 있다. macOS 13에서의 실제 기동 가능성은 이번 검사 범위 밖이다. 런타임을 앱에 포함하거나 Native AOT로 게시해도 Microsoft의 OS 지원 범위가 확대되지는 않는다. [프로젝트 목표](PLAN.md) · [Microsoft .NET 10 OS 지원표, 2026-09-28 판](https://github.com/dotnet/core/blob/44927bc821d37f596e317c4316335632bd38c79b/release-notes/10.0/supported-os.md)

## 두 플랫폼에서 재현한 차이

동일한 C# 소스를 macOS 27.2 ARM64와 Windows 11 빌드 26200 x64에서 실행했다. 양쪽 모두 SDK 10.0.401, 런타임 10.0.12, Release/JIT였다. 산술·배열·문자열 길이·기본 난수의 HSP 기준값은 같은 입력으로 공식 HSP 3.4 x86 콘솔을 이번에 직접 실행해 얻었다. Win32 문자표와 게임 동봉 압축 플러그인은 기존 x86/Windows 참조 기록을 재사용했다.

| 입력 또는 동작 | HSP 또는 Windows 참조 결과 | .NET 10에서 직역한 결과 | 게임에 미칠 영향 |
|---|---|---|---|
| 정수 변수 1과 실수 변수 2.5를 더하고 10을 곱한 뒤 정수로 변환 | 정수가 왼쪽이면 30, 실수가 왼쪽이면 35 | 두 순서 모두 35 | 혼합형 수식을 그대로 옮기면 대미지·가격 같은 정수 계산의 중간값이 달라질 수 있다. |
| 2×3 배열에 같은 좌표로 값을 쓰고 연속 메모리 읽기 | `0,1,10,11,20,21` | `int[2,3]`은 `0,10,20,1,11,21` | 저장·메모리 복사·플러그인 호출은 인덱스 순서를 실제 바이트로 관찰한다. |
| `A表B` 길이 | CP932의 `41 95 5C 42`로 4바이트 | `string.Length`는 3 | 원문의 이름 제한, 캐럿 위치, 대화 줄바꿈이 어긋난다. `表`의 둘째 바이트도 백슬래시와 같은 `5C`다. |
| 시드 12345, 범위 32768, 기본 난수 첫 세 번 | `7584,19164,25795` | `new Random(12345).Next(32768)` 순서는 `2187,2298,25387` | 첫 추첨부터 갈라진다. 전투 재생에는 알고리즘과 호출 순서가 모두 필요하다. |
| 기존 비압축 입력 `plain.bin`을 압축 읽기 함수에 전달 | 동봉 `z.hpi`가 원문 40,000바이트를 반환 | `GZipStream.CopyTo`가 `InvalidDataException` 발생 | 정상 gzip을 읽을 수 있어도 구형 로더의 일반 파일·EOF·오류 처리는 별도 구현 대상이다. |

**테스트 세이브에서 배열 차이가 실제 아이템 개수에 영향을 줬다.** 자동 생성한 첫 집 세이브의 `inv.s1`은 압축 해제하면 369,600바이트다. 한 슬롯에 70개 필드가 연속하고 각 필드는 4바이트 little-endian 정수다. 같은 파일에서 플레이어용 200슬롯의 첫 필드를 검사하면 기존 검사기와 C#의 명시적 오프셋 방식은 점유 슬롯 **11개**를 읽는다. 같은 바이트를 `int[70,1320]`에 그대로 복사해 `[0, slot]`을 검사하면 **51개**다. 인벤토리 창을 열 수 있다는 사실만으로 잡아내기 어려운 오류다. [저장 레이아웃과 교환 결과](SAVE_COMPATIBILITY.md) · [독립 세이브 검사기](../tools/inspect_save_state.py)

**CP932 공급자를 등록해도 디코딩 규칙이 다르다.** `CodePagesEncodingProvider`와 엄격한 예외 폴백으로 Win32 참조표를 대조했다. Win32의 변환 오류·기본 문자 대체와 .NET의 변환 예외는 모두 거부로 집계했다. 1·2바이트 입력 33,024건 중 **1,442건**에서 두 결과가 달랐다. BMP 인코딩 63,488건에서는 차이가 0건이었다. 예를 들어 Win32는 `87 90`과 `81 E0`을 모두 `≒`(U+2252)로 읽는다. 두 OS의 .NET 10은 앞의 입력에서 예외를 내고 뒤의 것만 읽었다. 이 수치는 검사한 입력 조합에 관한 값이고, 게임 대사에서의 발생 빈도는 미측정이다. 세이브·VM 식별자·바이트 단위 문자열은 원본 바이트를 보존하고 한국어와 Unicode는 표시 경계에서 처리할 수 있다. [Win32 참조표와 옵션](PLATFORM_IMPLEMENTATION.md) · [Microsoft 인코딩 공급자](https://learn.microsoft.com/en-us/dotnet/api/system.text.codepagesencodingprovider?view=net-10.0)

게임은 기본 `rnd`와 별도로 `exrand.dll`도 쓴다. 기존 x86 참조에서 `exrand`는 범위 0에도 난수 하나를 소비했다. 시드 12345·범위 68,925,423 사례는 원본의 반올림을 거치면 64,074,182, 정확한 정수 곱으로 치환하면 64,074,181이다. 범위 0에 관한 이 관찰은 `exrand`의 동작이다. 두 발생기를 독립적으로 대조해야 한다. [원본 플러그인 측정](PLUGIN_IMPLEMENTATION.md) · [기본 난수 측정](NATIVE_RUNTIME.md) · [C# 수치 승격 규칙](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/expressions#12473-binary-numeric-promotions)

## 코드 변환과 플랫폼 경계

| 남은 문제 | 이 프로젝트에서 확인한 근거 | 이식할 때 확인할 결과 |
|---|---|---|
| 실행 위치와 호출 복귀 | 보정한 복원 소스에서 문자열·주석을 가리고 줄 첫 명령만 세면 `goto` 20,363곳, `gosub` 10,507곳, `await` 429곳이다. C#의 `goto`는 같은 함수 안의 유효한 레이블로만 갈 수 있다. | 메서드로 나누더라도 HSP의 복귀 위치·공유 변수·`stat`과 대기 중 이벤트 순서를 보존해야 한다. 실제로 안내 창 20의 닫기가 전역 `onexit`를 거쳐 게임 종료로 이어진 사례가 있다. [C# 언어 명세](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/statements#13104-the-goto-statement) · [창 종료 경로](PLAY_EXIT_INVESTIGATION.md) |
| 32비트 데이터와 64비트 호스트 | 복원 소스의 `varptr` 5곳은 파일 대화상자에 집중돼 있다. 원본은 19개 정수, 76바이트 구조체에 주소를 넣는다. 동봉 `hmm.dll`은 오디오 외에도 캐릭터·아이템 상태 비트에 쓰이며 관련 호출 후보가 958곳이다. | 해당 기능별로 반환값·출력 버퍼·취소·실패를 대조한다. 64비트 포인터를 원본 정수 필드에 넣을 수 없으므로 파일 선택과 핸들은 플랫폼 경계에서 변환한다. [ABI와 정적 호출 조사](COMPATIBILITY.md) · [Microsoft 네이티브 상호 운용 지침](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices) |
| 그래픽·입력·음악 | 현재 호스트는 VM에 화면·외부 함수 핸들러를 등록한다. SDL 창, 픽셀 버퍼, 키 반복, IME 조합·확정, 오디오 장치가 각자 경계를 가진다. | 기존 C++ 화면·음원 구현은 재사용 후보지만 VM 인수를 받는 호스트에는 C#에서 호출할 인터페이스가 필요하다. 실제 키 입력과 IME 확정, 보조 창 닫기, 음원 반복·정지를 양 OS에서 확인해야 한다. [GUI 호스트 구조](GUI_HOST_IMPLEMENTATION.md) · [IME 검증 범위](IME_IMPLEMENTATION.md) |
| 생성 코드의 유지보수와 배포 | 역컴파일 원시 출력은 618,613줄이다. 원래 주석과 파일 구성이 일부 소실됐고 `label_*` 형태가 남았다. 프로젝트는 원본과 그 추출물을 개발 소스와 분리한다. | 변환한 코드에서 원본 명령 위치와 첫 상태 차이를 추적할 수 있어야 한다. 빌드 시 C#을 생성할 수 있으며, Native AOT를 선택하면 실행 중 `Reflection.Emit` 및 동적 어셈블리 로딩에 제약이 생긴다. 이 조사에서는 JIT로 실행했다. [복원 결과](DECOMPILATION.md) · [Microsoft Native AOT 제약](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/#limitations-of-native-aot-deployment) |

## 동등성을 판정할 수 있는 범위

이식 실험의 첫 비교 대상으로는 동일한 테스트 세이브·시드·시계·입력에서 **전투 한 턴 → 아이템 획득 → 저장 → 새 프로세스에서 재로드**가 적합하다. 각 턴의 난수 호출, 좌표·HP·인벤토리·기술 배열을 비교하고, 저장은 압축을 푼 파일별 내용을 대조한다. 어느 시점에 처음 달라지는지 볼 수 있다. 기존 Mac/Windows 호스트는 특정 신규 게임 경로에서 154개 저장 파일의 압축 해제 내용이 전부 일치했고, 전투·전리품·재로드용 입력도 있다. [플레이 경로](PLAYABILITY.md) · [저장 대조](SAVE_COMPATIBILITY.md)

이 조사에서 실행한 것은 공식 HSP 콘솔과 .NET 10 비교 프로그램이다. 원본 x86 게임에서 만든 진행 세이브, 후반 이벤트, 장시간 게임 실행, 프레임 시간·GC, Native AOT 패키지와 macOS 13 실기는 다음 단계의 미검증 항목이다. 현재의 154파일 비교는 기존 네이티브 호스트 간의 결과다.

## 재현

저장소 루트에서 .NET SDK 10.0.401을 사용했다. 아래 `dotnet`은 이 Mac의 `/usr/local/share/dotnet/dotnet`에 해당한다. 인벤토리 인수를 생략하면 개인 데이터나 게임 없이 기본 실험을 실행할 수 있다.

```sh
dotnet run --file tools/reference/dotnet10/PortProbe.cs -c Release --artifacts-path build/dotnet10-port-probe -- tests/plugins/reference/gz_inputs/plain.bin work/game-run/run27/user/save/sav_Ari/inv.s1
dotnet run --file tools/reference/dotnet10/CompareCp932.cs -c Release --artifacts-path build/dotnet10-cp932-probe -- work/windows/reference-build/platform-cp932-02/out
python3 tools/inspect_save_state.py work/game-run/run27/user/save/sav_Ari
```

Windows에서는 같은 소스·입력의 SHA-256을 확인하고 전용 디렉터리 `reference-build/platform-dotnet10-port-01`에서 두 C# 파일을 실행했다. 공식 x86 HSP는 그 디렉터리 아래 별도 `hsp34`에 런타임과 ASCII 입력 `semantics.hsp`를 복사해 실행했다. 게임 DLL을 요구하지 않는 작은 산술·배열·문자열·난수 프로그램이다. 아래 명령의 `$case`는 새 검사 디렉터리, `$sdk`는 해시를 확인한 HSP 3.4 SDK, 두 도우미는 저장소 소스를 x86으로 빌드한 실행 파일이다. [컴파일·실행 도우미 사용법](WINDOWS_REFERENCE.md) · [HSP 입력](../tools/reference/dotnet10/semantics.hsp)

```powershell
& $compiler "$sdk\hspcmp.dll" "$case\semantics.hsp" "$case\semantics.ax" "$sdk\common" "$case\compile"
& $runner "$case\hsp3cl.exe" "$case\semantics.ax" "$case\run"
Get-Content "$case\run.stdout"
```

| 입력 또는 도구 | SHA-256 |
|---|---|
| 보정한 2.335 HSP 소스, 정적 집계 입력 | `c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55` |
| HSP 3.4 x86 실행물 복사본 | `29f90169fc3798bd876a14bfac7dd53d846f296b55278162b8d279b5e126308f` |
| 이번 HSP 참조 프로그램의 AX | `7a5d0813e55123162228af740f22b26ebcbc77b6ead714d6ea0ffa1b1642777d` |
| 테스트 인벤토리, 압축 해제 후 | `17ed594c281b2e05504cd290aa4a3f5b33d667cfbadf5641777244abe1a6bfe3` |
| Mac 10.0.12 `libcoreclr.dylib` | `3f0c1e99cb7808cf718d9be1c7c83c2f52011dcd4d1cf45170455ddce871bda3` |
| Windows 10.0.12 `coreclr.dll` | `128aee8c62a673d64739e585e3876b61133571cec83a46240a62581d5465639b` |

원시 출력은 `work/reports/dotnet10-port-01/{mac.txt,mac-encoding.txt,windows.txt}`에 있다. Windows 원본 문자표의 입력 범위·SHA-256은 [문자표 검증 기록](PLATFORM_IMPLEMENTATION.md)에 있다.
