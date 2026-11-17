#pragma once
// Native music renderer for ElonaPlus 2.335 (MIDI via SoundFont, MP3).
//
// Output format is fixed: 44100 Hz, 2 channels, interleaved float32 (L,R),
// nominal range [-1, 1]. No audio device is opened here; the host owns the
// device (SDL3) and pulls blocks from MusicPlayer::render on its audio thread,
// typically through a dedicated SDL_AudioStream so that music and WAV effects
// are mixed by SDL and never interrupt each other.
//
// Inputs are bytes the host already read through its game-file resolver, plus
// a verified SoundFont path. Format detection is by content (SMF "MThd",
// RIFF "RMID", MP3 frame sync / ID3), never by file extension.
//
// Failures are reported, never hidden: open_music() returns nullptr with a
// MusicError. There is no silent fallback stream.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace elona::audio {

inline constexpr int kSampleRate = 44100;
inline constexpr int kChannels = 2;

enum class MusicKind : std::uint8_t { midi, mp3 };

enum class MusicError : std::uint8_t {
    none,
    invalid_data,     // not a recognised/consistent MIDI or MP3 image
    unsupported,      // recognised but not implemented (e.g. SMF format 2)
    soundfont_error,  // SoundFont missing/invalid, or MIDI requested without one
    io_error,         // file could not be read (SoundFont::load_file)
};

struct LoadError {
    MusicError code = MusicError::none;
    std::string message;
    explicit operator bool() const noexcept { return code != MusicError::none; }
};

const char* music_error_name(MusicError error) noexcept;

// Immutable SoundFont shared by all MIDI streams (thread-safe to share; load
// and stream creation happen on the control thread).
class SoundFont {
public:
    static std::shared_ptr<const SoundFont> load_file(const std::filesystem::path& path, LoadError& error);
    static std::shared_ptr<const SoundFont> load_memory(const std::vector<std::uint8_t>& bytes, LoadError& error);
    ~SoundFont();
    SoundFont(const SoundFont&) = delete;
    SoundFont& operator=(const SoundFont&) = delete;

    int preset_count() const noexcept;
    bool has_preset(int bank, int program) const noexcept;
    struct Impl;
    const Impl& impl() const noexcept { return *impl_; }

private:
    explicit SoundFont(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Static facts about an opened stream (for diagnostics and tests).
struct MusicInfo {
    MusicKind kind = MusicKind::midi;
    std::uint64_t length_frames = 0;  // musical length: MIDI last event / MP3 decoded frames
    // MIDI only
    int smf_format = 0;
    int tracks = 0;
    int division = 0;           // ticks per quarter note, or SMPTE encoded (negative fps)
    std::uint32_t notes = 0;
    std::uint16_t channels_used = 0;  // bit mask
    std::uint32_t ignored_sysex = 0;  // SysEx other than GM/GS/XG reset and GS rhythm-part
    std::uint32_t loop_markers = 0;   // CC111 seen; ignored like DirectMusic/MCI (whole-file loop)
    // MP3 only
    int source_rate = 0;
    int source_channels = 0;
};

// One playable track. Not thread-safe: use from one thread (MusicPlayer
// serialises access).
class MusicStream {
public:
    virtual ~MusicStream() = default;
    virtual const MusicInfo& info() const noexcept = 0;
    // Writes exactly `frames` frames (interleaved stereo float). Returns the
    // number of frames that belong to the track; the remainder is zero-filled
    // and finished() becomes true. With looping enabled the track restarts at
    // its end (whole-file loop like DMPLAY 65535 / mmplay loop mode).
    virtual std::size_t render(float* out, std::size_t frames) = 0;
    virtual bool finished() const noexcept = 0;
    virtual void set_loop(bool loop) noexcept = 0;
    virtual bool looping() const noexcept = 0;
    virtual void rewind() = 0;                      // back to the start, sound cut
    virtual std::uint64_t position_frames() const noexcept = 0;  // within the current pass
    virtual std::uint32_t loops_completed() const noexcept = 0;
};

// Opens MIDI (needs `soundfont`) or MP3 bytes. Returns nullptr and sets
// `error` on failure. `name` is only used in messages.
std::unique_ptr<MusicStream> open_music(std::vector<std::uint8_t> bytes, std::shared_ptr<const SoundFont> soundfont,
                                        const std::string& name, LoadError& error);

// Thread-safe facade: control calls from the game thread, render() from the
// audio thread. Streams are created and destroyed outside the audio thread.
class MusicPlayer {
public:
    enum class State : std::uint8_t { idle, playing, finished };

    MusicPlayer();
    ~MusicPlayer();

    // Replaces the current track (the old one stops immediately).
    void play(std::unique_ptr<MusicStream> stream, bool loop);
    void stop();                       // immediate stop, track released
    void set_volume(float gain);       // linear, default 1.0
    State state() const;
    std::uint64_t position_frames() const;
    std::uint32_t loops_completed() const;

    // Audio thread: writes `frames` stereo frames (overwrites `out`); silence
    // when idle/finished. Output is clamped to [-1, 1].
    void render(float* out, std::size_t frames);

private:
    mutable std::mutex mutex_;
    std::unique_ptr<MusicStream> stream_;  // replaced/freed only by control calls
    float gain_ = 1.0f;
    State state_ = State::idle;
};

}  // namespace elona::audio
