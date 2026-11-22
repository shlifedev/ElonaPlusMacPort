# borscht 로컬 호환 패치

대상: <https://github.com/Ruin0x11/borscht>, 커밋
`03c8be3398a59d9df445fd5a0c7850b3f2188869` (2.15R).

`0001-local-compatibility.patch`는 원본 코드의 의미 매핑이나 게임 로직을 바꾸지 않고
현재 stable Rust로 도구를 빌드하기 위한 패치다.

- `seek_stream_len` nightly feature 대신 현재 위치 저장 → 끝 위치 조회 → 복원을 사용한다.
- 이미 안정화된 `entry_insert`의 feature attribute를 제거한다.
- Rust 2018 소스의 let-chain을 동일한 두 단계 조건문으로 표현한다.

Cargo.lock과 의존성 버전은 변경하지 않았다. 현재 검증된 도구체인은
Homebrew Rust/Cargo 1.100.0, macOS ARM64다. 다른 도구체인은 빌드 시 실제 버전을
기록하며 아직 동일한 결과를 보장한 것으로 표시하지 않는다.

적용·빌드: `python3 scripts/bootstrap_borscht.py`.
기존 캐시가 다른 커밋이거나 등록된 패치 이외의 변경을 포함하면 덮어쓰지 않고 실패한다.

상위 프로젝트는 Ruin0x11의 MIT 라이선스다. 캐시의 `LICENSE` 원문을 보존한다.
이 사실이 추출된 게임 코드의 라이선스를 정하는 것은 아니다.
