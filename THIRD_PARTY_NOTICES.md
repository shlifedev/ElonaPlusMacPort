# Third-party notices

이 문서는 개발에 사용하는 구성 요소의 출처와 고지를 보존한다. 원본 게임이나 프로젝트 전체에 새 라이선스를 부여하는 문서가 아니다.

| 구성 요소 | 고정 버전 | 사용 범위 | 고지 |
|---|---|---|---|
| [OpenHSP](https://github.com/onitama/OpenHSP) | `3dbb8729dad53ee876fc681696c8fe542698860b` | 네이티브 컴파일러·VM 및 제한된 호환성 패치; `win32gui/hspwnd_win.cpp`의 Blt/RenderAlphaBitmap와 `fcpoly.cpp`를 변경 이식한 `src/graphics/hsp_screen.cpp`·`poly_raster.cpp` | [BSD 3-Clause](licenses/OpenHSP.txt) |
| [borscht](https://github.com/Ruin0x11/borscht) | `03c8be3398a59d9df445fd5a0c7850b3f2188869` | AX 추출/복원 형식 참고와 디코더 빌드 | [MIT](licenses/borscht.txt) |
| [zlib](https://github.com/madler/zlib) | 1.3.1, archive SHA-256 `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23` | `elona_zlib` 정적 라이브러리(raw deflate/inflate·crc32); gz* 소스는 빌드하지 않음 | [zlib License 원문](licenses/zlib.txt) |
| zlib 1.1.4 `gzio.c` | SHA-256 `32239181263cbe364ddc1daad7ff080b83d1c23a21ccabe6be534876bbc38bc6` | `zgzip.cpp`의 스트림 상태 기계를 C++로 변경 구현할 때 제어 흐름 참고 | Copyright (C) 1995–2002 Jean-loup Gailly, zlib License; [변경 구현 고지](src/plugins/THIRD_PARTY.md) |
| [stb_image](https://github.com/nothings/stb) | v2.23, 위 OpenHSP 고정 커밋의 `src/hsp3/win32gui/stb_image.h` | `src/graphics/image_io.cpp`의 이미지 디코더 | [MIT / Public Domain 원문](licenses/stb_image.txt) |
| [SDL3](https://github.com/libsdl-org/SDL) | 3.4.20 | 네이티브 창·키보드·오디오 장치; Windows VC archive 해시는 [잠금 파일](tools/reference/windows_gui_deps.lock.json) | [zlib License](licenses/SDL3.txt) |
| [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) | 3.2.2 | 네이티브 글꼴 렌더링; Windows VC archive 해시는 같은 잠금 파일 | [zlib License](licenses/SDL3_ttf.txt) |
| [FreeType](https://github.com/libsdl-org/freetype) | SDL_ttf 3.2.2의 submodule `9973564cfa63763a3e4ac67c09147899539b1e07` | Windows SDL3_ttf VC 구성의 글꼴 엔진 | [FreeType License](licenses/FreeType-FTL.txt), [선택 안내](licenses/FreeType-LICENSE.txt), [BDF](licenses/FreeType-BDF.txt)·[PCF](licenses/FreeType-PCF.txt) 고지 |
| [HarfBuzz](https://github.com/libsdl-org/harfbuzz) | SDL_ttf 3.2.2의 submodule `564bf9818a18709776856533829c0c04950773d6` | Windows SDL3_ttf VC 구성의 텍스트 조형 | [Old MIT](licenses/HarfBuzz.txt) |
| [PlutoSVG](https://github.com/libsdl-org/plutosvg) | SDL_ttf 3.2.2의 submodule `2983eb6919feea272d793bc386384e3f5b97b03c` | Windows SDL3_ttf VC 구성의 SVG 글리프 지원 | [MIT](licenses/PlutoSVG.txt) |
| [PlutoVG](https://github.com/libsdl-org/plutovg) | SDL_ttf 3.2.2의 submodule `3e6f922f453da1c9e7d1d7f66cac1d9724a18b47` | Windows SDL3_ttf VC 구성의 벡터 래스터화 | [MIT](licenses/PlutoVG.txt) |
| [TinySoundFont](https://github.com/schellingb/TinySoundFont) | `a3621a19c6504885cd9583e6e26c35bc954fc8ec` | MIDI 신시사이저 | [MIT](licenses/TinySoundFont.txt) |
| [dr_mp3](https://github.com/mackron/dr_libs) | v0.7.4, `aec2dd20667f54acfdb66031217df6d240f56c52` | MP3 디코더(minimp3 기반) | [Public Domain / MIT-0 및 minimp3 CC0](licenses/dr_mp3.txt) |
| [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS) | v2.0.3, `684543d5e5efaef08d02be50dcda8d552478fa60` | MIDI 음색용 SoundFont; 개발 캐시에 별도 보관 | [GeneralUser GS License v2.0](licenses/GeneralUser-GS.txt) |
| [ElonaPlusCustom-GX](https://github.com/JianmengYu/ElonaPlusCustom-GX) | `aebe0d8b1941ec2b664bbad87659e54cc3b8eb35` | 프로젝트 방식 참고; 게임 소스를 이 저장소에 복사하지 않음 | upstream 고지 참조 |
| HSP 3.4 SDK | `hsp34a.zip`, SHA-256 `2b841c71bbe4efad0dce1bed88bdd72bfd4ebe5e5671689b4a5833c105b278cf` | Windows x86 참조 컴파일·작은 fixture 실행 | SDK 내부 원본 고지 보존; 개발 캐시에만 저장 |
| ElonaPlus 2.335 | 입력 해시는 `docs/PLAN.md` 참조 | 로컬 원본 비교와 사용자 자산 | 원본 동봉 고지 유지; 배포물·추출물은 git 제외 |

`licenses/`의 원문은 고정한 upstream에서 그대로 복사하거나 헤더의 고지 블록을 추출했다. 추가한 파일의 출처·SHA-256은 [licenses/sources.json](licenses/sources.json), 음악 입력 전체 해시는 [tools/bootstrap_audio.py](tools/bootstrap_audio.py)에 있다. zlib 1.3.1은 위 해시의 archive에서 `LICENSE`를 추출했다.

Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved. FreeType에는 FTL을 선택한다. Windows SDL_ttf의 추가 고지는 공식 `release-3.2.2` 소스 트리의 고정 submodule에서 수집했다. 공식 VC 바이너리를 이 소스로 재현 빌드했다는 뜻은 아니다.

Mac ARM64 Release는 SDL 공식 DMG에서 macOS 프레임워크와 동봉 고지를 개발 캐시로 복사한다. DMG와 바이너리 SHA-256은 준비 도구에 고정했다. 다른 개발 구성은 시스템에 설치된 SDL을 사용할 수 있다. 앱 패키지 제작 시 실제 포함하는 구성 요소와 고지를 확인해야 한다. macOS/Windows 설치 글꼴은 시스템에서 읽는다. GeneralUser GS의 라이선스와 샘플 출처 원문도 보존한다. [Mac 준비 도구](tools/bootstrap_macos_gui.py), [SDL 3.4.20](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20), [SDL_ttf 3.2.2](https://github.com/libsdl-org/SDL_ttf/releases/tag/release-3.2.2)

## 한국어 커뮤니티 번역

`localization/ko-KR/community-reviewed.json`에는 [Elona Wiki 「한글패치 모음」](https://elona.fandom.com/ko/wiki/%ED%95%9C%EA%B8%80%ED%8C%A8%EC%B9%98_%EB%AA%A8%EC%9D%8C)의 번역을 2.335 일본어 문맥과 대조하여 수정한 항목이 있다. 원 페이지에 표시된 기여자는 카파, 노힐부득, Evildragon 및 Elona Wiki 기여자들이다. 해당 수정 번역은 **[CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/)**으로 제공한다. 원문과 비교해 용어·효과·문체를 교정했고, 달라진 문맥과 동음 키는 적용을 보류했다. 각 항목의 `origin`·`review_reason`에 출처와 변경 근거를 보존한다.

적용 근거는 [Fandom의 텍스트 라이선스 안내](https://community.fandom.com/wiki/Help:Licensing)와 한국어 Elona Wiki의 CC BY-SA 표시다. 직접 문서 이력 접근은 차단되어 고정 revision ID를 확보하지 못했으며, 검색 색인에서 관찰한 사본의 해시와 날짜를 [출처 명세](localization/ko-KR/source-manifest.json)에 기록했다. 실제 배포 시 번역 데이터와 이 출처·변경·라이선스 고지를 함께 제공한다. 생성된 `runtime.tsv` 중 해당 데이터에서 유래한 항목에도 같은 고지를 유지한다. 이 고지는 호스트 코드·게임 원본·별도 자산의 라이선스를 변경하지 않는다.

옛 한국어 패치 `ko.txt` 0.5.0은 공개 재사용 조건을 확인하지 못해 `.cache/`·`work/`의 개인 참고 자료로만 사용한다. 전체 사전이나 그 장문 번역을 공개 런타임에 편입하지 않았다. 독자 번역 자료는 `original-*.json`에 구분한다. 자세한 버전·출처·적용 범위는 [KOREAN_SOURCES.md](docs/KOREAN_SOURCES.md)에 있다.
