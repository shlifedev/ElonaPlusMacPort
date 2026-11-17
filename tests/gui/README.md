# 독립 GUI fixture

이 UTF-8 `.hsp` 파일은 게임 코드에서 복사하지 않고 별도로 작성한 최소 재현이다. 실제 HSP 컴파일 입력은 CP932로 생성한다.

```sh
python3 tests/gui/prepare_fixtures.py
for fixture in work/gui-fixtures/*.hsp; do
  build/native-macos-arm64-legacy-cp932-debug/elona_hspcmp -d \
    --compath=.cache/upstream/OpenHSP/common/ \
    -o"${fixture%.hsp}.ax" "$fixture" || exit 1
done
```

위 명령은 생성·컴파일만 한다. 이번 작업에서 GUI 실행은 검증하지 않았다. Headless probe에 넣을 fixture가 아니다.

- `pixel_roundtrip`: buffer→screen→buffer 복사와 RGB 읽기.
- `color_key`: gmode 2에서 검정 투명색 처리.
- `bitmap_export`: 8×8 BMP 출력. 나중에 실행할 때 전용 scratch 폴더를 작업 디렉터리로 지정하고 파일 존재·decoded pixels를 확인한다.
- `input_control`: mesbox, 문자열 변경, focus 및 후속 IME 수동 검증.

기대 계약과 다음 단계는 [GUI_FEASIBILITY.md](../../docs/GUI_FEASIBILITY.md)에 있다. 생성기는 기존의 다른 내용이나 symlink를 덮어쓰지 않는다.
