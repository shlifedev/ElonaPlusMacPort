# src/plugins 제3자 고지

이 파일은 플러그인 대체 모듈이 사용하거나 참고한 제3자 구성 요소의 고지다.
루트 `THIRD_PARTY_NOTICES.md`와 `licenses/`는 통합 담당자가 소유하므로, 아래 표의
행과 `licenses/zlib.txt`(아래 원문)를 그쪽에 병합해 주기를 요청한다.

| 구성 요소 | 고정 버전 | 사용 범위 | 고지 |
|---|---|---|---|
| [zlib](https://github.com/madler/zlib) | 1.3.1, `zlib-1.3.1.tar.gz` SHA-256 `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23` | `elona_zlib` 정적 라이브러리(raw deflate/inflate·crc32). gz* 파일은 빌드하지 않음 | zlib License (아래) |
| zlib 1.1.4 `gzio.c` | `.cache/upstream/zlib-1.1.4-gzio.c`, SHA-256 `32239181263cbe364ddc1daad7ff080b83d1c23a21ccabe6be534876bbc38bc6` | `zgzip.cpp`의 스트림 상태 기계를 원본 z.hpi 계약에 맞추어 C++로 다시 작성할 때 제어 흐름을 따름(변경된 소스) | zlib License (Copyright (C) 1995-2002 Jean-loup Gailly) |

원본 게임 DLL(hmm.dll, z.hpi, exrand.dll, hspda.dll)은 이 저장소에 포함하지 않으며
네이티브 빌드에 링크·로드하지 않는다. 행동 비교는 Windows x86 참조 환경에서만 수행했다.
`tests/plugins/reference/`의 gzip 파일과 텍스트는 합성 패턴 데이터로 원본 DLL을 실행해
얻은 관측값이며 게임 데이터나 세이브를 포함하지 않는다.

## zlib License (zlib 1.3.1 `LICENSE` 원문)

```
Copyright notice:

 (C) 1995-2022 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu
```
