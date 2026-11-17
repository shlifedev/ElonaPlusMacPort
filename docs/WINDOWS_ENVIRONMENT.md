# Windows 검증 환경과 실행 기록

## 전용 환경

사용자가 제공한 SSH 별칭 `home/windows`를 사용한다. 작업 경로는 `%USERPROFILE%\ElonaPlusNative-2.335`이며 `.elona-native-workspace` 표식이 있는 디렉터리에서만 스냅샷 빌드를 수행한다. 원본 ZIP은 해시를 확인한 별도 복사본이다. 기존 게임 설치·개인 세이브·시스템 설정을 변경하지 않았다.

| 항목 | 관측값 |
|---|---|
| OS | Windows 11 Pro, 10.0.26200, AMD64 |
| PowerShell | 5.1.26100.9711 |
| Visual Studio | Community 18.10.12217.157 / MSVC 19.51.36260 |
| CMake/Ninja | Visual Studio 동봉 도구 |
| SSH 프로세스 | session 0 |
| 대화형 Explorer | session 1 |
| Python | WindowsApps 별칭 발견; 실제 인터프리터는 검증하지 않음 |
| Rust | 경로는 존재하지만 실행 시 신뢰되지 않은 mount point 오류. Windows C++ 빌드에는 사용하지 않음 |

`tools/windows_probe.py`는 환경을 읽어 `work/windows/environment.json`에 기록한다. `ssh home/windows` 별칭에는 `/`가 있으므로 파일 전송에 `scp` 대신 SFTP를 사용한다. PowerShell 명령은 UTF-16LE `-EncodedCommand`로 전달한다.

## 네이티브 빌드와 검증

`tools/windows_stage.py`는 CMake·소스·고정한 OpenHSP·검토한 fixture를 ZIP으로 만들고 전용 경로에 전송한다. ZIP 및 파일별 SHA-256을 검사한 뒤 `tools/windows_build.ps1`로 x64 빌드한다. 기존 스냅샷을 재사용해도 manifest와 파일 해시를 확인한다.

```sh
python3 tools/windows_stage.py --legacy-cp932
```

원격 Python에 의존하지 않고 C++ 문자열 테스트와 Mac에서 검증한 AX fixture를 실행한다. .NET `BaseStream`으로 stdout/stderr 원시 바이트를 수집하며 CRLF만 LF로 정규화한다. 빈 fixture 묶음·중복 이름·입력 해시 불일치는 실패한다.

실제 해결한 빌드 환경 문제:

- PowerShell 5가 CMake의 stderr 경고를 종료 오류로 처리했다. CMD 안에서 stderr를 합치고 실제 프로세스 종료 코드를 확인하도록 수정했다.
- Windows 한국어 기본 코드 페이지로 upstream UTF-8 주석을 읽으면 전처리 구문 오류가 났다. 모든 관련 C++ target에 `/utf-8`을 적용했다.
- Mac ZIP의 수정 시간이 원격 Windows 시계보다 미래면 Ninja가 manifest를 계속 재생성했다. ZIP의 파일 시간을 고정한 과거 시각으로 기록했다. 시스템 시계는 변경하지 않았다.

20개 의미 fixture와 C++ 문자열 검사가 양 플랫폼에서 통과했다. 공식 HSP 3.4 x86에서 생성한 AX의 헤더만 변환하여 양 플랫폼에서 실행한 15개 비교도 모두 일치했다. 자세한 범위는 [NATIVE_RUNTIME.md](NATIVE_RUNTIME.md)와 [LEGACY_AX.md](LEGACY_AX.md)를 참조한다. 원격 빌드는 게임 전체 플레이 검사가 아니다.

## 원본 게임의 실제 GUI 관측

별도 `reference-game\run-001\elonaplus2.335`에 원본 ZIP을 CP932 파일명으로 풀고 EXE 해시를 확인했다. `tools/reference/ProbeGameWindow.cs`는 정확히 자신이 시작한 PID의 창만 관측하고 10초 뒤 캡처한다. 사용자 바탕화면·다른 창은 캡처하지 않으며 키 입력도 보내지 않는다. 끝나면 해당 프로세스를 닫고 종료가 지연되면 그 PID만 정리한다.

1. SSH의 session 0에서는 프로세스가 살아 있으나 visible window가 0개였다. 이 결과를 GUI 성공으로 처리하지 않았다.
2. 동일 사용자·`Interactive` logon·`Limited` 권한의 일회성 예약 작업으로 session 1에서 같은 helper를 실행했다. 추가 자격 증명이나 관리자 권한 상승은 사용하지 않았다.
3. `ElonaPlus 2.335` 창 1개, 646×509 크기의 캡처를 얻었다. 이미지를 직접 확인했으며 **일본어/영어 언어 선택 화면**이 보였다.
4. 원본 프로세스가 종료됐고 일회성 예약 작업은 `finally`에서 삭제됐다.

기록: `work/windows/original-window.stdout`(session 0 실패), `interactive-window.stdout`(session 1 성공), `original-title.png`(언어 선택 화면). **타이틀 메뉴·새 게임·플레이·저장/로드까지 검증한 결과는 아니다.** 이후 GUI 비교는 같은 방식으로 원본과 복원본의 별도 복사본에서 진행한다. 원본 설치를 직접 조작하지 않는다.
