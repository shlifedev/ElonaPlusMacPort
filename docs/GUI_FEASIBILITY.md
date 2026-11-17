# GUI 재사용 가능성과 다음 실행 관문

## 결론과 조사 범위

OpenHSP의 VM·명령 해석·일부 화면 객체 및 입력 코드는 재사용할 수 있다. 그러나 **HSP3Dish를 연결하는 것만으로 ElonaPlus 2.335가 실행되지는 않는다.** 기존 일반 Dish의 화면 버퍼 전환과 픽셀 읽기에 미구현 부분이 있고, 게임은 이 동작을 캐릭터 합성·색상 처리·메시지 버퍼에 실제로 사용한다. 새 공통 픽셀 처리 계층과 문자열 입력 어댑터가 다음 필수 관문이다.

사용자가 어려운 문제가 발생하면 멈춰 보고하도록 요청했으므로 이번 작업은 이 관문을 문서화하고 대규모 GUI 구현을 시작하지 않았다. 창이 뜨는 결과나 미구현 함수의 성공 반환을 게임 호환성으로 보고하지 않는다.

조사 입력은 OpenHSP `3dbb8729dad53ee876fc681696c8fe542698860b`다. 안정화한 native checkout과 분리한 `.cache/upstream/OpenHSP-gui`에서 `src/hsp3dish`, `src/hsp3`, `src/plugins`를 읽었다. 원본 AX를 실행하지 않았다. 네 가지 독립 GUI fixture를 CP932 소스로 만들고 컴파일만 성공했으며 GUI 실행은 하지 않았다. 결과와 해시는 `work/reports/gui-feasibility-2.335/report.json`에 있다.

## 기존 구현에서 확인한 관문

아래 소스 경로는 모두 위 고정 커밋 기준이다. 실제 실행 결과가 아니라 소스에서 확인한 동작이다.

| 영역 | 코드 근거 | 게임에서 필요한 동작 / 판정 |
|---|---|---|
| 픽셀 읽기 | `src/hsp3dish/hsp3gr_dish.cpp:536`의 pget handler는 `#if 0` 안에 있다. `hspwnd_dish.cpp:827`의 `Bmscr::Pget`도 0만 반환한다. | raw source에서 pget 22개 후보. 캐릭터 합성 중 특정 픽셀의 RGB를 gmode 색상 키로 사용한다. 미구현을 실제 픽셀 읽기로 바꿔야 한다. |
| 일반 Dish의 화면 전환 | `src/hsp3dish/emscripten/hgiox.cpp:538`의 `hgio_gsel`은 render_end 후 0을 반환하며 그리기 대상 변경 코드가 없다. 함수 주석도 미구현이라고 명시한다. 이 파일은 Linux 일반 Dish makefile에서도 사용한다. | buffer에 그린 뒤 메인 화면으로 복사하고 메인 화면을 다시 buffer로 저장하는 동작이 필요하다. |
| 일반 Dish의 buffer | 같은 파일 `:547`의 `hgio_buffer`는 빈 texture를 만들지만 렌더 타깃을 연결하지 않는다. `hspwnd_dish.cpp:177`은 option 32로 지정한 OFFSCREEN일 때만 이 함수를 호출한다. | 원본은 option 없는 `buffer 4`, `buffer 8` 등을 만든 뒤 직접 그린다. 원본 명령을 그대로 넘기는 것으로 충분하지 않다. |
| BMP 저장 | `hspwnd_dish.cpp:935`의 `BmpSave`는 파일을 쓰지 않고 0을 반환한다. | 참조 픽셀 검증에서도 종료 코드만 확인하면 거짓 통과한다. 파일 존재와 실제 픽셀 내용까지 확인해야 한다. |
| HGIMG4/DishGP의 다른 경로 | `src/hsp3dish/win32gp/hgiox.cpp:532`는 실제 gsel 대상을 설정하고 `:553`은 framebuffer를 만든다. | 이 경로는 일반 Dish보다 기능이 많지만 `gameplay::FrameBuffer`, gamehsp 및 3D/재질 계층을 함께 가져온다. 기본 buffer→OFFSCREEN 의미 변환과 pget·Win32 control 공백은 별도로 남는다. 전체를 가져오는 것이 곧 호환성을 해결하지 않는다. |
| Win32 입력 컨트롤 | Win32 GUI의 `hsp3gr_wingui.cpp:790`에는 listbox/combox, `:830`에는 mesbox가 있다. Dish의 해당 command switch에는 이 handler들이 없다. default는 unsupported 오류다. | mesbox 5개, objprm 29개, input 5개, button 8개 후보. 일반 input helper 하나로 다중행·선택·IME 동작을 대체할 수 없다. |
| Dish input 내부 | `hspwnd_obj.cpp:960`은 Hsp3ObjInput과 texmesPos 기반 편집기를 만든다. `hspwnd_edit.cpp`의 텍스트 편집 및 fontsystem은 UTF-8 전제를 가진다. | CP932 게임 문자열을 그대로 넣으면 안 된다. 편집 UI의 Unicode 상태와 게임의 CP932 bytes를 분리해야 한다. |
| 이벤트/IME | `src/hsp3dish/linux/hsp3dish.cpp:347`에 SDL_TEXTINPUT 처리와 여러 키 매핑이 있다. 이 파일에 SDL_TEXTEDITING 처리는 없다. | 문자 확정 이벤트는 참고할 수 있지만 조합 중 문자열·선택·취소·바이트 제한과 원본 onkey 동작은 별도 검증 대상이다. |
| 폰트 경계 | `src/hsp3dish/emscripten/fontsystem.cpp:556`은 TTF_RenderUTF8_Blended를 호출한다. | 내부 CP932 논리는 유지하고 표시 경계에서만 변환한다. 측정 폭·줄바꿈·커서 위치를 비교해야 한다. |

upstream에 0 반환이나 no-op이 있다는 사실은 이를 우리 어댑터에서 허용한다는 뜻이 아니다. 검증되지 않은 기능은 호출명·명령 위치와 함께 실패하게 유지한다.

## 원본 게임에서 실제로 필요한 화면 의미

`work/decompiled/2.335/start.hsp`의 줄 23738–23760은 interface 이미지를 buffer 3에 로드하고 buffer 8을 만든 뒤 gcopy로 메시지 배경을 조립한다. 줄 23765 이후에는 item 이미지를 buffer 1에 로드하고 같은 버퍼에서 gzoom을 수행한다. 이는 단일 창에 texture를 출력하는 기능보다 넓은 요구다.

줄 24253–24264는 캐릭터 이미지를 합성하면서 pget으로 색상을 읽고 gmode 4의 색상 키·복사 및 gfdec2 효과를 사용한다. 이 단계에서 pget을 0으로 대신하면 화면만 다르게 보이는 수준을 넘어 서로 다른 색상 처리 경로가 된다. `gfini/gfdec2` 등 원본 DLL 효과도 같은 픽셀 저장소를 사용해야 한다. 호출 규약 및 반환 stat는 [플러그인 조사](PLUGIN_FINDINGS.md)를 따른다.

줄 309007–309075는 mesbox, objprm, objsel, aplsel/aplobj/apledit와 IME 상태를 함께 사용한다. 이름이나 검색 문자열을 입력하는 공통 경계로 묶어 구현해야 한다. Win32 컨트롤 핸들을 64-bit 게임 int로 흉내 내는 방식은 피한다. 오프라인 범위의 ActiveX/업로드 기능은 별도 비활성 처리 대상이며, 이 입력 UI 전체를 제거할 근거는 아니다.

정적 행 시작 명령 집계는 buffer 19, gsel 1,012, gcopy 1,455, gzoom 116, grotate 236, gmode 1,163, redraw 575, picload 261, pget 22개다. 이는 실행 빈도가 아니며 조건문 뒤의 inline 명령을 빠뜨릴 수 있다. 집계 방식과 raw source SHA-256은 위 report에 기록했다.

## 재사용할 부분과 새로 구현할 부분

**유지 가능한 기반:** 현재 검증한 HSP core/컴파일러, legacy AX 헤더 정규화, CP932 문자열·논리 경로 및 기본 RNG 패치, 코드 위치 진단. Dish의 명령 인수 읽기, 일부 clipping/좌표 계산, Bmscr/HspWnd 객체 관리, 키 코드 매핑은 원본 Win32 GUI 구현과 대조하며 가져올 수 있다. 세 번째 코드의 고지와 라이선스는 유지한다.

**필수 공통 픽셀 계층:** 각 HSP buffer의 크기·색상·현재 위치·복사 모드와 실제 픽셀 내용을 소유한다. buffer↔buffer, screen↔buffer, 같은 buffer 안의 겹치는 복사, clipping, pget/pset, 색상 키, 확대/회전, gf 효과가 모두 같은 데이터를 보아야 한다. 초기에는 CPU 픽셀 저장소를 기준으로 정의하고 창에는 완성된 이미지를 표시하는 방식이 검증하기 쉽다. GPU 최적화는 동일한 픽셀 결과를 검증한 후 도입할 선택지다. 이것은 구현 제안이며 아직 성능이나 시각적 동등성을 검증한 결과가 아니다.

**필수 플랫폼 계층:** SDL3 창·이벤트·출력 연결, 사용자 데이터 경로와 읽기 전용 게임 리소스 접근, Unicode↔CP932 경계, IME 입력, 파일 선택, 오디오/압축 및 개별 플러그인 어댑터. 윈도우 내부 위젯을 공통 UI로 재현하는 쪽과 플랫폼 위젯을 쓰는 쪽은 입력 계약을 고정한 뒤 결정한다.

기존 Win32 GUI `BMSCR`는 HDC, pBit, HBITMAP, HPALETTE, HWND를 포함한다(`src/hsp3/win32gui/hspwnd_win.h:366`). Dish의 `BMSCR`는 이 필드를 제거하고 texture ID 등을 사용한다(`src/hsp3dish/hspwnd_dish.h:623`). 따라서 원본 x86 DLL에 동일한 이름의 구조체를 넘긴다고 ABI가 맞지 않는다. DLL을 로드하지 않고 관리하는 표면/객체 ID를 네이티브 어댑터가 해석해야 한다.

## macOS ARM64 / Windows x64의 빌드 판단

Linux Dish 실행부는 SDL2/SDL_image를 사용하고 makefile은 GL/EGL, SDL2_mixer/ttf, curl, gpiod, ffi까지 연결한다. 실행부가 Linux devctrl 및 일반 DLL/FFI 등록을 수행하므로 그대로 macOS host로 사용하는 것은 현재의 명시적 명령 제한을 잃는다. 필요한 창·입력 코드를 선택해서 이식해야 한다.

확인한 `src/hsp3dish/win32/hsp3dish.vcxproj`는 Win32 구성을 제공한다. 이 프로젝트를 이미 검증된 x64 GUI 빌드로 간주하지 않는다. vendored gameplay에는 `PlatformMacOSX.mm`과 `gameplay-main-macosx.mm`가 존재하고 Cocoa/NSOpenGLView 코드를 포함한다. 따라서 Mac 코드가 전혀 없다고 말할 수는 없지만, 이 코드가 현재 HSP3Dish host 및 ARM64 CMake 빌드와 통합되었다는 검증은 없다. 이번 조사는 GUI 빌드나 창 표시를 수행하지 않았다.

## 최소 재현과 다음 작업 계약

관리 소스는 [tests/gui/](../tests/gui/README.md)의 UTF-8 `.hsp` 네 개와 CP932 생성기다. 아래 명령으로 깨끗한 checkout에서도 재현한다. compiler는 [네이티브 빌드 절차](NATIVE_RUNTIME.md)에 따라 먼저 빌드한다.

```sh
python3 tests/gui/prepare_fixtures.py
for fixture in work/gui-fixtures/*.hsp; do
  build/native-macos-arm64-legacy-cp932-debug/elona_hspcmp -d \
    --compath=.cache/upstream/OpenHSP/common/ \
    -o"${fixture%.hsp}.ax" "$fixture" || exit 1
done
```

`work/reports/gui-feasibility-2.335/`에는 최초 조사 결과가 있고, 관리 소스에서도 같은 CP932 내용으로 생성·컴파일됨을 확인했다. **네 fixture 모두 executed=false**다. 기존 headless probe에서 실행하도록 만든 파일이 아니다.

| Fixture | 검증할 계약 |
|---|---|
| pixel_roundtrip.hsp | 16×16 buffer를 RGB(17,34,51)로 채우고 buffer→screen→buffer 복사. pget/ginfo가 각 RGB를 반환해야 한다. 불일치 시 서로 다른 end code. |
| color_key.hsp | 검정/빨강 source를 파랑 destination에 gmode 2로 복사. 검정은 투명, 빨강은 보존되어야 한다. |
| bitmap_export.hsp | 8×8 RGB(17,34,51) buffer의 BMP를 전용 scratch 폴더에 생성. 파일 존재, dimensions, decoded pixels까지 비교한다. |
| input_control.hsp | mesbox를 만들고 objprm으로 `あA`를 설정하고 focus한다. IME 조합/확정/취소, backspace, 선택, CP932 byte 제한을 별도 interactive 절차로 확인한다. |

첫 fixture의 핵심 명령은 다음과 같다. game asset 없이 재현 가능하다.

```hsp
screen 0,16,16
redraw 0
buffer 1,16,16
color 17,34,51
boxf 0,0,15,15
gsel 0
pos 0,0
gmode 0
gcopy 1,0,0,16,16
pget 0,0
if ginfo(16)!=17 : end 11
if ginfo(17)!=34 : end 12
if ginfo(18)!=51 : end 13
end
```

다음 작업자는 아래 순서와 완료 조건을 따른다.

1. 공식 x86 GUI 참조에서 독립 fixture를 실행하고 실제 exit, pixel dump, 입력 결과를 수집한다. 원본 게임/세이브와 분리된 scratch 경로를 사용한다. 현재 CPU-only VM fixture 20개 및 공식 AX 15개 비교를 유지한다.
2. 공통 Surface와 buffer 선택/복사/pget 계약부터 구현한다. 똑같은 fixture가 Mac ARM64와 Windows x64에서 원본 pixels와 일치해야 한다. 미구현 명령의 0 반환을 허용하지 않는다.
3. gmode/clip/overlap/zoom/rotation와 gf 효과를 작은 색상 패턴으로 확장하고 화면 캡처뿐 아니라 decoded pixel 값을 비교한다. Win32 BMSCR 메모리 배치를 이식판에 복제하지 않는다.
4. 별도 입력 adapter에서 CP932와 IME 계약을 검증한다. dialog·mesbox·메뉴 편집기 컨트롤 지원 범위와 오프라인 비활성 기능을 구별한다.
5. 파일 I/O 격리, 플러그인 반환·배열 변경 계약까지 준비된 뒤 원본 game AX의 제한된 실행 추적을 시작한다. 타이틀 표시를 전체 게임·세이브 호환 완료로 처리하지 않는다.

이 관문의 핵심은 소스 부족이 아니라, 기존 GUI backend가 원본의 픽셀 저장소와 Win32 입력/플러그인 계약을 충족하지 않는다는 점이다. 범위가 정해진 새 계층과 실제 x86 기준 검증이 필요하다.
