# 저장 압축·상태 비트 플러그인 조사

## 결론과 증거 범위

조사한 `hmm.dll`의 상태 비트 함수 세 개와 `z.hpi`의 입출력 함수 네 개는
네이티브 어댑터로 구현할 수 있는 작은 계약으로 좁혀졌다. HMM의 나머지
오디오·입력 기능까지 해결했다는 뜻은 아니다. 이 조사에서는 DLL을 실행하지
않았으며 아래 결과는 원본 바이너리 명령, 공개 소스, 문서에서 도출했다.

별도로 **복원 소스가 54개 구형 DLL 선언을 일반 DLL 선언으로 바꾸는 오류**를
발견했다. 원본 `STRUCTDAT.subid=-5`가 재컴파일에서 `-3`으로 바뀐다. 이 차이는
`stat`의 부호 및 양수 반환 처리에 영향을 준다. 원본 동작 비교 전에 선언을
복원해야 한다.

| 입력 | SHA-256 |
|---|---|
| 원본 `hmm.dll` | `5eed0341f3399b833d56402bf4b9c67bae6469e099ee0b802e9ebee428550565` |
| 원본 `z.hpi` | `92aa68357593af607ae813b978298484003befac59cc5b4d66b31380210c9bee` |
| 공식 배포 `hmmdll.zip` | `de08ab73a1d341496db3ee47ff4ae242ac99f0e7d6586e4a3ce6d3dd7d07fc44` |
| zlib v1.1.4 `gzio.c` | `32239181263cbe364ddc1daad7ff080b83d1c23a21ccabe6be534876bbc38bc6` |

REA의 Hopper 제공자가 원본 해시를 확인한 전용 복사본을 분석했다. 주소는
해당 PE의 가상 주소다. 전체 정적 증거는 무시되는
`work/reports/plugins-hmm-evidence.json`, `plugins-z-evidence.json`에 보존했다.
두 번째 번들은 앞선 HMM 기록도 포함한다. 분석 세션은 닫았다.

## HMMBITON/OFF/CHECK

| 함수 | export / 내부 함수 주소 | 관찰한 연산 | 원본 HSP `stat` 계약 |
|---|---|---|---|
| HMMBITON | `1000d6e0` / `10001000` | 첫 인수가 가리키는 32비트 값에 `1 << bit` OR | 1 |
| HMMBITOFF | `1000d700` / `10001020` | 같은 값에 반전 마스크 AND | 1 |
| HMMBITCHECK | `1000d720` / `10001040` | 첫 인수 값과 마스크 AND | 설정됨 1, 해제됨 0 |

ON/OFF는 첫 인수를 쓰기 가능한 정수 주소로 받고 CHECK는 정수 값으로 받는다.
실제 export의 반환값은 ON/OFF `-1`, CHECK `0/-1`이다. 위 `stat` 값은
구형 HSP 호출 규약의 부호 변환을 적용한 결과이며 공식 HMM 설명과 일치한다.
이 세 함수의 관찰된 명령에서는 DirectX 초기화나 전역 상태를 읽지 않는다.

32비트 x86 `shl ..., cl`을 사용하므로 유효 시프트는 인수의 하위 5비트다.
관찰한 구현에는 비트 번호 범위 검사도 포인터 검사도 없다. 네이티브 구현은
`uint32_t(1) << (uint32_t(bit) & 31)` 같은 정의된 연산으로 동일 결과를 내야
하며, C/C++의 음수·32 이상 시프트나 부호 있는 최상위 비트 이동에 의존하면 안 된다.
VM 변수의 유효 주소·크기를 확인하는 책임은 어댑터 경계에 둔다.

Windows x86 비교가 필요한 최소 조합은 초기값 0/1/-1/최상위 비트,
bit -1/0/1/30/31/32/63, 반복 ON/OFF, 배열 원소와 양옆 sentinel이다.
반환 `stat`와 변경 후 32비트 값을 모두 비교한다. NULL이나 해제된 실제
포인터를 원본에 전달하는 검사는 필요하지 않다.

## z.hpi의 좁은 계약

이 DLL에는 zlib 1.1.4 deflate/inflate 식별 문자열이 있다. export가 부르는
함수는 `gzopen/gzread/gzwrite/gzclose`의 공개 `gzio.c`와 구조·인수·분기·gzip
헤더 처리에서 대응한다. **gzip 래퍼라는 강한 정적 근거**이며 빌드 전체가
공개 소스와 동일하다는 검증은 아니다.
[zlib v1.1.4 원문](https://github.com/madler/zlib/blob/v1.1.4/gzio.c)

| 함수 | export / 대응 내부 함수 | 관찰·도출한 계약 |
|---|---|---|
| zOpen | `10008200` / `10002a70` → `10002a90` | `(out_handle, path, mode, level)`; mode 0은 읽기, 그 외 쓰기. `r/w`, `b`, level 하위 바이트+`'0'`로 모드 문자열을 만든다. 결과 핸들을 출력 변수에 쓴다. 성공 stat 0, 실패 1. |
| zRead | `10008250` / `10003070` | `(buffer, handle, count, unused)`; 읽은 길이를 stat로 반환한다. 내부 음수 오류는 0으로 바뀌어 EOF와 구별되지 않는다. |
| zWrite | `10008270` / `10003340` | 같은 인수 배치. 정상적으로 소비한 바이트 수가 stat다. 내부 결과를 부호 반전해 export한다. |
| zClose | `10008290` / `10003800` | 첫 인수 핸들을 닫는다. 내부 결과 0이면 stat 0, 그 외 1로 정규화한다. 핸들은 값으로 받으므로 게임 변수를 자동으로 0으로 만들지 않는다. |

이 표의 HSP 반환 의미는 원본 AX의 legacy 선언과 OpenHSP 호출 처리에서
도출했다. 아직 원본 DLL 실행 결과로 확인한 것은 아니다.

`zWrite`의 NULL 핸들/잘못된 모드 경로에서 대응 내부 함수는 `-2`를 반환한다.
export는 `+2`로 바꾸고 legacy 호출자는 이를 일반 성공 `stat`가 아니라 DLL
오류로 처리한다. 오류 처리를 모두 `stat=0`으로 통일해서는 안 된다.
닫힌 임의 포인터·음수 크기·부족한 버퍼의 전체 원본 동작은 확인하지 않았다.

네이티브 어댑터에서는 게임의 32비트 핸들 값을 실제 64비트 주소로 사용하지
않는다. 핸들 테이블로 스트림을 소유하고 버퍼 범위·길이·열림 상태를 검사한다.
정상 범위는 원본 계약을 보존하고, 정의되지 않은 위험한 입력은 명시적 진단을
낸다. CP932 경로를 호스트 파일 API로 넘기는 변환도 이 경계에서 담당한다.

현재 관리되는 zlib 구현을 사용하는 어댑터와 원본 생성 파일을 교차 읽기하는
검사가 필요하다. 1.1.4 원문은 역사적 비교 자료다. 버전·gzip 헤더의 OS 바이트
등이 다르면 압축 결과 바이트가 달라질 수 있으므로 우선 복원된 데이터와
stat 계약을 검증한다. 원본 저장 포맷과 이식판 사이의 호환성이 목표다.

검증 입력은 빈 스트림, 1/4/16384/16385 bytes, 여러 차례 read/write,
EOF 추가 읽기, 없는 파일, 잘린 gzip, CRC 오류, 압축되지 않은 입력이다.
게임의 40바이트 이름+10바이트 길이 컨테이너는 gzip보다 위 계층이므로 별도로
비교해야 한다. 개인 세이브에 직접 실험하지 않는다.

## 54개 legacy 선언을 보존해야 하는 이유

OpenHSP의 `BindFUNC`는 `subid`를 하나 줄인다. 이어서 `exec_dllcmd`는
`STRUCTPRM_SUBID_OLDDLLINIT=-6`일 때만 `stat=-result` 및 양수 반환의
wait/error 규약을 처리한다. 일반 DLL은 `stat=result`다.
[고정 OpenHSP 호출 처리](https://github.com/onitama/OpenHSP/blob/3dbb8729dad53ee876fc681696c8fe542698860b/src/hsp3/win32gui/hsp3extlib.cpp)

원본 54개는 exrand 2, hspda 4, hspext_ext 8, elona 1, z 4, hmm 20,
hspsock 4, hspinet 6, water 5개다. 원시 복원 결과는 명시적 타입 목록을
출력해 legacy 속성을 잃었다. 예를 들어 다음 수치 선언은 같은 4인수 배치와
legacy 속성을 함께 표현한다.

```hsp
#func HMMBITON "_HMMBITON@16" $1
#func HMMBITCHECK "_HMMBITCHECK@16" $0
#func zOpen "_zOpen@16" $5
```

전체 매핑은 `work/reports/ax-roundtrip-2.335/legacy-dll-declarations.json`이다.
54개 모두 수치 플래그를 다시 풀었을 때 원본 AX의 네 인수 타입과 일치했다.
이는 변환 설계 검증이다. 아래에서는 실제 HSP3.4 컴파일 산출물도 대조했다.

재현 가능한 변환은 다음과 같다.

```sh
python3 tools/restore_legacy_declarations.py
python3 -m unittest discover -s tests -p 'test_restore_legacy_declarations.py' -v
```

입력은 제어 흐름 보정본 `work/decompiled/2.335-hsp34/start.hsp`
(SHA-256 `a9a6e5cdc6f08a664fb581647f06181667b5943e86de57cd0dfef32567823669`)과
원본 AX다. 출력은 `work/decompiled/2.335-reference/start.hsp`
(SHA-256 `c3b02e2d2d3b1e5ffb659c8412b017a20a20e9b25e88b2f1a7cedfe986069f55`) 및
`manifest.json`이다. 입력 해시·선언 정체·인수 배치·54개 개수를 검증하며,
원본 폴더와 입력 경로를 보호하고 다른 기존 출력은 덮어쓰지 않는다.

ASCII 선언 줄만 바이트로 치환하므로 CP932 전체 파일을 Unicode로 왕복
변환하지 않는다. 이 구별은 같은 문자로 디코딩되지만 재인코딩 시 바이트가
바뀌는 CP932 중복 코드에도 중요하다. 7개 합성 검사와 실제 변환·동일 출력
재실행이 통과했다. 이 검사는 원본 DLL 실행 검증을 대신하지 않는다.

Windows HSP3.4 전체 컴파일 결과를
`work/windows/reference-build/reference-fixed-01/start.ax`로 회수했다.
SHA-256은 `cec8809573ed8172033ac86d94709c40289b65398dad1fcc778c394108cf3b72`다.
원본 54개를 라이브러리·export 이름으로 짝지어 다음 메타데이터를 별도로 검사했다.

- `subid=-5`, 인수 타입·스택 오프셋, 스택 크기, 호출/함수 플래그는 모두 일치했다.
- water 함수 다섯 개의 인수 owner 번호는 함수표 재배치에 따라 달라졌다.
  각 AX의 함수표에서 owner를 라이브러리·export 정체로 해석하면 54개 모두 일치한다.
- 기록은 `work/reports/ax-roundtrip-2.335/final-legacy-comparison.json`이다.
  원시 숫자 비교의 차이 다섯 건도 제거하지 않고 함께 남겼다.

이 검사로 선언 복원을 확인했다. 전체 명령/분기 비교는 별도 검사이며,
게임이나 원본 DLL을 실행해 같은 결과를 얻었다고 주장하지 않는다.

## 공개 소스 확인 결과와 남은 관문

- 공식 HSP3.4 ZIP의 1,897개 항목과 C/C++·HSP·설명 파일을 검사했다.
  `HMMBITON`, `_zOpen@16` 구현 소스는 발견하지 못했다.
- 고정 OpenHSP checkout의 C/C++ 소스에도 두 플러그인 구현은 없었다.
- [공식 HMM 배포 ZIP](https://www.onionsoft.net/hsp/file/hmmdll.zip)은 DLL,
  HSP 선언·샘플·설명서를 포함하며 C/C++ 소스는 없다. 그 ZIP의 DLL 해시는
  원본 게임 DLL과 다르므로 동일 버전 바이너리로 취급하지 않는다.
- zlib의 핵심 압축 소스는 공개되어 있다. HSP용 `z.hpi` 래퍼 원본 소스의
  공식 배포 위치는 이번 검색에서 확인하지 못했다. 존재하지 않는다는 뜻은 아니다.

이 두 묶음의 남은 관문은 작은 Windows x86 참조 실행, legacy 선언 복원,
네이티브 VM 값·버퍼·핸들 어댑터의 비교 검증이다. 공개 DLL 소스가 없다는
이유만으로 새 게임 엔진 전체를 먼저 작성해야 한다는 근거는 발견되지 않았다.
