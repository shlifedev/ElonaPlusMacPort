# 구형 AX 헤더 변환 및 교차 실행

## 구현한 범위

2.335 원본과 공식 Windows HSP 3.4의 작은 fixture는 96-byte 헤더를 사용한다. 고정한 최신 OpenHSP의 `HSPHED`는 112 bytes다. 구형 파일을 그대로 구조체로 읽으면 코드의 앞 16 bytes를 `pt_sr/max_sr/pt_exopt/max_exopt`로 잘못 해석할 수 있다.

`scripts/normalize_legacy_ax.py`는 이미 검사한 구형 레이아웃에만 다음 변환을 수행한다.

- 96번째 위치에 16개의 0 byte를 삽입해 새 두 옵션 영역을 없는 것으로 명시한다.
- 전체 파일 크기와 각 파일 절대 section offset을 16씩 늘린다. 미사용 0 offset은 유지한다.
- 코드·데이터·라벨·디버그·라이브러리·함수·매개변수 payload는 byte 단위로 그대로 유지한다. 상대 offset은 바꾸지 않는다.
- `0x301`, 코드 시작 96, 유효한 범위·테이블을 요구한다. 별도 runtime-name 옵션·확장 플러그인·현재 헤더는 이 도구의 범위 밖으로 거부한다.

`HSPHED.runtime`은 DS 상대 offset이 아닌 파일 절대 offset이다. 이번 입력에서는 0이며 runtime-name 옵션이 있는 다른 레이아웃을 추측해서 처리하지 않는다. 참조 소스는 OpenHSP `src/hsp3/hsp3struct.h`, `hsp3.cpp`, `src/hspcmp/codegen.cpp`다.

```sh
python3 scripts/normalize_legacy_ax.py work/extracted/2.335/start.ax \
  --output work/normalized/2.335/start.ax > work/normalize-report.json
python3 -m unittest discover -s tests -p test_normalize_ax.py -v
```

원본 AX 변환 후 payload 동일성과 8개의 유효성/경계 검사를 확인했다. **변환된 전체 게임을 실행하지 않았다.** 이 도구는 DLL ABI·포인터·명령 의미를 이식하지 않는다. 진단 호스트는 DLL 선언이 있는 게임 AX를 계속 거부한다.

## 실제 x86 기준과 비교

공식 HSP 3.4로 컴파일하고 Windows x86 `hsp3cl.exe`에서 실행한 작은 AX를 가져와 헤더만 변환했다. CP932/기본 난수 호환 옵션을 켠 Mac ARM64와 Windows x64 진단 VM에서 같은 AX를 실행했으며 **15개 모두 stdout·종료 코드가 일치하고 stderr가 없었다**. 비교에서 CRLF만 LF로 정규화했다. exrand DLL fixture는 구현되지 않은 외부 어댑터이므로 제외했다.

```sh
python3 scripts/compare_reference_native.py \
  --reference work/windows/reference-build/fixtures-01 \
  --runtime build/native-macos-arm64-legacy-cp932-debug/elona_hsp_probe \
  --output work/reference-native/fixtures-01
python3 scripts/windows_stage.py --legacy-cp932 \
  --fixtures-dir work/reference-native/fixtures-01
```

기록은 `work/reference-native/fixtures-01/results.json`과 `work/windows/reference-native-results.json`이다. Windows source snapshot은 `9f395dc06e965b17`이다. 입력/출력 AX·참조 런타임·네이티브 런타임 해시와 fixture별 실제 byte 출력을 기록했다.

이 첫 fixture 묶음의 `arrays`에는 HSP 3.4에 없는 `varsize`라는 이름이 있다. 공식 컴파일러는 미초기화 변수로 취급해 0을 출력했고, 같은 AX를 네이티브 VM에서 실행한 결과도 0이다. 이 항목을 `varsize` 함수의 버전 간 호환성 증거로 사용하면 안 된다. 원본 게임에서는 해당 함수 사용을 발견하지 않았다.

15개의 작은 프로그램 일치는 전체 게임·모든 명령·저장/로드의 동등성을 입증하지 않는다. 원본 게임의 DLL 선언과 Windows 포인터 경계는 별도로 구현하고 검증해야 한다.
