# ElonaPlus 2.335 raw 역컴파일

## 현재 결과

2026-11-17, macOS ARM64에서 고정된 borscht를 빌드해 2.335 바이트코드 전체의
raw HSP 소스 출력을 얻었다. 두 번의 독립 decode 출력은 바이트 단위로 같았다.

| 항목 | 관찰 결과 |
|---|---|
| 입력 | `work/extracted/2.335/start.ax` |
| 입력 크기 | 14,846,611 bytes |
| 입력 SHA-256 | `f1b1404060faa01dc1d3e46b9a58ba0c39cfd185afd33bdafc2287ee3e921d25` |
| borscht 커밋 | `03c8be3398a59d9df445fd5a0c7850b3f2188869` |
| 도구체인 | Rust/Cargo 1.100.0, macOS ARM64 |
| raw 소스 | `work/decompiled/2.335/start.hsp` |
| 출력 크기 / 줄 수 | 16,093,503 bytes / 618,613 lines |
| 출력 SHA-256 | `dce3e305f91e377fd669a045d9dce7cc197509d094852e56b67be312aa233725` |
| 인코딩 | encoding_rs SHIFT_JIS 출력, Python CP932 strict decode 통과 |
| 코드 영역 | 12,089,074 bytes 전부 토큰으로 집계 |
| 토큰 수 | 2,805,612 |
| 사전에서 식별하지 못한 토큰 | 0 |
| 의미 분석 DB | 적용하지 않음 |
| 새 컴파일러의 구문 검사 | ARM64 OpenHSP 3.7 컴파일 성공, 실행하지 않음 |
| Windows 공식 HSP 3.4 재빌드 | 성공; 복원 오류 보정 후 전체 정규화 토큰·선언 일치 |
| 복원 게임 플레이 호환 | 아직 검증하지 않음 |

이 결과는 소스 출력과 정적 어휘 분석 범위에 대한 증거다.
토큰 누락이 없다는 사실이 생성된 HSP의 의미가 원본과 같거나 게임이 정상 실행됨을
증명하지 않는다. 원래 변수 이름이 남아 있는 경우 활용되지만 원본 주석·매크로·파일
구조가 모두 복구된 것은 아니다.

## 재현

저장소 루트에서 Python 3.10+, Git, 기존 Rust/Cargo 도구체인을 사용한다.

```sh
python3 scripts/prepare_game.py --game-dir elonaplus2.335 --output work/extracted/2.335
python3 scripts/bootstrap_borscht.py
python3 scripts/decompile_game.py
python3 scripts/decompile_game.py --verify-repeat
python3 -m unittest discover -s tests -p test_decompile.py -v
```

`decompile_game.py`는 현재 2.335의 정확한 AX 해시만 허용한다. 이 제한은 unchecked
binary read가 있는 상위 디코더에 다른 입력을 전달하지 않기 위한 것이다.
원본 EXE나 DLL을 실행하지 않으며 기존 추출물을 수정하지 않는다.

일반 decode는 기존 출력 디렉터리를 덮어쓰지 않는다. `--verify-repeat`는 새 임시
디렉터리에서 다시 decode하고 원래 소스와 비교한다. 다르면 실패 기록을 보존한다.
별도의 실패 재현은 `--output work/decompiled/다른이름`으로 실행할 수 있다.

## 기록 위치

- `.cache/upstream/borscht-build.json`: 소스 커밋, 로컬 패치, Cargo.lock, 명령 사전,
  컴파일러 버전, 실행 파일 해시.
- `.cache/upstream/borscht-build.log`: Rust 빌드 경고와 오류.
- `work/decompiled/2.335/report.json`: 입력·출력 해시, 인코딩, 크기, 토큰 집계,
  반환 코드와 명시적 검증 상태.
- `work/decompiled/2.335/decode.log`: 실제 디코더 stdout/stderr. 로그에 나타나는
  `.run-*` 경로는 성공 뒤 최종 출력 폴더로 옮겨진 staging 경로다.

실패한 디코드의 출력이 존재하더라도 `status`가 `raw_decode_complete`가 아니면
완전한 결과로 취급하지 않는다. `compile_verified`와 `behavior_verified`는 별개다.

## 소스 재빌드에 필요한 사항

- raw 소스는 CP932다. 편집기의 자동 UTF-8 변환을 사용하지 않는다.
- borscht는 `Dictionary.csv`를 현재 디렉터리에서 찾기 때문에 wrapper는 고정된
  upstream checkout을 작업 디렉터리로 사용한다.
- raw 소스 preamble에는 14개 라이브러리와 78개 외부 함수 선언이 나온다.
  이 선언은 런타임 의존성 목록이며, 컴파일 성공이 DLL의 동작까지 확인해 주지는 않는다.
- `exrand.dll`, `hspda.dll`, `hspext_ext.dll`, `elona.dll`, `z.hpi`, `hmm.dll`,
  `hspsock.dll`, `hspinet.dll`, `water.hpi` 및 Win32 API 의존성이 있다.
- 충돌하는 함수 이름은 raw 출력에서 `func_1` 등의 이름으로 바뀐다. 예를 들어
  `hspda`의 정렬 함수와 `elona.dll`의 회전 함수가 이에 해당한다.
- 원본 패키지의 `scene1.hsp`와 `scene2.hsp`는 별도 리소스다. EXE 재패키징 시 이
  리소스를 빠뜨리지 않는다. 역컴파일로 원래 `#packopt` 설정까지 복구되는 것은 아니다.
- 우선 원본 계열인 Windows x86 HSP SDK의 복원 빌드를 기준으로 삼는다.
  새 ARM64 OpenHSP 컴파일러의 구문 검사와 원본 재빌드 검증은 별도다.
- 버전별 erystia 분석 DB는 이 고정 커밋에서 2.15R까지다. 2.335에 구버전의
  변수·상수 의미 매핑을 무조건 적용하지 않는다.

## 테스트 및 한계

작은 독립 바이트코드 fixture 7개로 짧은/긴 토큰, if 추가 워드, DLL 함수 참조,
알 수 없는 명령의 위치, 잘린 입력, 잘못된 코드 영역을 검증했다.
실제 2.335 입력에서 raw decode와 반복 출력 일치도 확인했다.

추가로 `build/native-macos-arm64-debug/elona_hspcmp`를 사용해 CP932 raw 소스를
바이트 변경 없이 복사한 파일을 컴파일했다. 반환 코드 0과 `No error detected`를
확인했으며 결과 AX는 14,249,231 bytes다. 입력 복사본과 로그·명령·컴파일러 해시는
`work/decompiled/2.335-compile-probe/`에 있다.

```sh
build/native-macos-arm64-debug/elona_hspcmp \
  --compath=/Users/shlifedev/@src/ElonaPlus/.cache/upstream/OpenHSP/common/ \
  -o/Users/shlifedev/@src/ElonaPlus/work/decompiled/2.335-compile-probe/start.ax \
  /Users/shlifedev/@src/ElonaPlus/work/decompiled/2.335-compile-probe/start.hsp
```

이 컴파일러는 원본보다 새 버전이며 UTF-8 플랫폼 설정을 사용한다. 미초기화 변수
경고가 있고 코드·문자열·라벨 영역 크기가 원본과 다르다. 따라서 이것은 구문 수용
검증일 뿐, CP932 데이터 보존이나 원본과의 실행 의미 일치를 증명하지 않는다.
해당 AX는 실행하지 않았다.

Windows 공식 HSP 3.4 재빌드와 정적 비교는 이후 수행했다. raw 결과에는 연속 else
복원 오류, stop의 중복 goto, old DLL 선언 규약의 손실이 있었다. 원시 파일은 유지하고
`fix_decompiled_control_flow.py`, `restore_legacy_declarations.py`로 별도 소스를 생성했다.
최종 재컴파일의 2,805,612개 정규화 토큰과 함수 선언은 원본과 일치한다.
재현·해시·최소 실패 사례·작은 문자열/난수 실행 결과는
[WINDOWS_REFERENCE.md](WINDOWS_REFERENCE.md)에 있다.
세이브·복원 게임의 실제 진행과 전체 동작 동등성은 아직 검증하지 않았다.
