# 네이티브 음악 렌더러 (MIDI · MP3)

상태: **구현·양 플랫폼 오프라인 검증, Mac CoreAudio 장치 콜백 통합 검증 완료.** 게임 창의 전체 음악 전환·장시간 청취는 별도 검증 대상이다.

## 구성
- `src/audio/`: 자체 SMF 파서·시퀀서(`smf.*`, `midi_stream.cpp`), MP3(`mp3_stream.cpp`), 공개 API `music.h`, `MusicPlayer`.
- 신시: TinySoundFont `tsf.h` @ `a3621a19…` (MIT, SHA-256 `70d55963…d197`).
- MP3: dr_mp3 v0.7.4 @ `aec2dd20…` (퍼블릭 도메인 또는 MIT-0, SHA-256 `f5c8020e…e915`).
- SoundFont: GeneralUser GS v2.0.3 @ `684543d5…` (GeneralUser GS License v2.0, SHA-256 `9575028c…8cfe`).
- 위 셋은 `python3 tools/bootstrap_audio.py`가 공식 GitHub 고정 커밋에서 받아 `.cache/audio`에 둔다. 전역 설치와 런타임 DLL은 없다. 원문 고지는 `licenses/`와 루트 `THIRD_PARTY_NOTICES.md`에 병합했다.
- FluidSynth는 Windows에서 의존 DLL이 많아 1차 구현에서 제외했다. 신시 계층은 교체할 수 있다.

## API (출력 44100 Hz · 2ch · interleaved float32)
- `SoundFont::load_file` → `open_music(bytes, sf, name, err)`: 형식은 내용으로 판별한다. 실패하면 `nullptr`과 `MusicError`를 돌려주며 무음으로 대체하지 않는다.
- `MusicStream::render/set_loop/rewind/finished`, 그리고 스레드 안전한 `MusicPlayer::play/stop/render`.
- 반복은 곡 전체를 반복한다(DMPLAY 65535 / mmload 루프 모드와 같은 의미). CC111 루프 마커(15곡)는 원본처럼 무시한다.
- 렌더 경로: 음성은 미리 할당하고, 렌더 중 파일 I/O나 할당은 하지 않는다.

## 시퀀서 의미
- GM/GS/XG 리셋, GS 리듬 파트 SysEx, 마스터 볼륨을 처리한다.
- 뱅크는 GS식 MSB만 쓰며, 없는 변형 음색은 bank 0으로 대체한다.
- 퍼커션은 10채널(bank 128)이고, 서스테인·all notes/sound off·피치 벤드·RPN을 처리한다.
- 곡이 끝나면 릴리스 꼬리를 남긴다. 꼬리는 −80 dBFS 미만이 0.2초 이어지면 끝나고, 최대 5초다.
- 반복 지점에서는 모든 노트를 릴리스하고 채널 상태를 초기화해 잔류 음을 막는다. rewind는 소리를 즉시 끊는다.
- 합성 이득은 −14 dB다. 원본 83곡의 MIDI 최대 피크는 0.877이다.

## 검증
- 합성 SF2·SMF 단위 검사:
  - 파서 오류: 길이, VLQ, tempo 0, SysEx 범위, 형식 2(`unsupported`), RMID
  - tempo 맵, program, 뱅크 대체, 퍼커션, GS 리듬 파트, 피치 벤드, 볼륨
  - sustain, notes off, 꼬리, 반복, rewind, 플레이어 상태
- 원본 오프라인 렌더: 83개 MIDI + 1개 MP3 전부 열림·종료, 유한한 값, 무음 아님. `gm_on.mid`는 노트가 없는 GM 리셋 파일이라 무음이 정상이다. 반복 동작도 확인했다.
  - macOS ARM64: 448개 검사 통과(`work/audio/render-report-macos.json`).
  - Windows x64(MSVC): 448개 검사 통과(`work/audio/audio-claude-x64-01/`, `tools/audio_windows_check.py`).
  - 두 플랫폼 모두 **오프라인 PCM 검증**이다. 실제 오디오 장치 출력은 GUI 연결 뒤 따로 확인해야 한다.
- 단독 빌드: `cmake -S tests/audio -B build/audio-macos -G Ninja && cmake --build build/audio-macos`. 원본 렌더는 `build/audio-macos/elona_audio_tests --game-dir elonaplus2.335`로 한다.

## 남은 일
- GUI의 `mm*`는 음악용 SDL_AudioStream과 효과음용 stream을 따로 사용한다. 실제 게임 창의 장시간 음악 전환, 사용자 청취 및 Windows 오디오 장치 출력 검증은 남아 있다.
- 음색이 원본과 같은지는 목표가 아니다. TinySoundFont는 SF2 모듈레이터와 리버브/코러스를 지원하지 않는다.

## 2026-11-18 통합 후속

`tests/audio/device_probe.cpp`는 게임과 같은 `AudioOut`/`MusicPlayer` 구현을 사용하고 실제 기본 재생 장치를 연다. `dummy`/`disk` 드라이버는 실패로 처리하며 CTest에는 자동 등록하지 않는다. Mac 결과 `work/audio/device-report-macos.json`은 `coreaudio`, PASS다.

- MIDI 1회 종료·짧은 MIDI 반복, 원본 `battle1.mid`, 원본 `oo39.mp3`의 SDL playback-thread 콜백에서 유한한 비영점 PCM을 확인했다.
- WAV를 반복 재생하고 정지하는 동안 MIDI 재생 위치가 계속 증가했다. MP3 교체/재시작과 정지 후 콜백의 영점 출력도 확인했다.
- 이는 실제 장치가 요청한 PCM과 애플리케이션 제출 경로의 증거다. 스피커/루프백 캡처나 사용자가 들었다는 증거는 아니다. WAV 최종 믹스 자체를 캡처한 검사도 아니다.
- 첫 probe(`device-report-macos-01.json`)는 `battle1.mid`의 무음 도입을 1.1초만 기다려 비영점 검사를 실패했다. 관측 시간을 2.2초로 늘린 최종 검사에서 통과했고 실패 기록을 보존했다.
- `audio_sdl.cpp`의 음악 콜백 버퍼를 고정 크기로 두고 요청을 chunk 처리해 앱의 콜백 내 가변 할당을 없앴다. WAV 큐 제출/장치 재개와 음악 장치 재개 실패를 호출자에게 반환한다.

재현:

```sh
cmake -S tests/audio -B build/audio-root-check -G Ninja -DCMAKE_BUILD_TYPE=Release -DELONA_AUDIO_DEVICE_PROBE=ON
cmake --build build/audio-root-check --target elona_audio_device_probe
build/audio-root-check/elona_audio_device_probe elonaplus2.335 work/audio/device-report-macos-new.json
```

`work/audio/cross-platform-summary.json`: 기존 Mac/Windows 84곡 보고서를 비교했다. 곡 목록·길이·렌더 길이·종료·노트/SysEx 정보는 같고, 5곡의 peak 또는 RMS만 출력 소수점 마지막 자리에서 달랐다(peak 최대 0.000001, RMS 0.0000001). JSON 정밀도에서의 비교이며 PCM 바이트 동일성을 주장하지 않는다.
