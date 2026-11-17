#pragma once
// Internal glue between the public API and the third-party single-header
// libraries (TinySoundFont, dr_mp3). Not installed for host use.
#include <memory>
#include <vector>

#include "music.h"

struct tsf;

namespace elona::audio {

struct SoundFont::Impl {
    tsf* base = nullptr;  // owns the parsed SoundFont; streams use tsf_copy()
    ~Impl();
};

// Overall output gain of the synthesizer in dB (chosen from the renders of
// the 2.335 tracks, see docs/AUDIO_IMPLEMENTATION.md).
// Applied through tsf_set_volume (linear) together with the SysEx master volume.
inline constexpr float kSynthGainDb = -14.0f;
// Voices are preallocated so that rendering never allocates.
inline constexpr int kMaxVoices = 256;
// After the last MIDI event, release tails may ring at most this long, and
// end earlier once the output stays below kTailSilence for kTailQuietSeconds.
inline constexpr double kMaxTailSeconds = 5.0;
inline constexpr float kTailSilence = 1e-4f;  // -80 dBFS
inline constexpr double kTailQuietSeconds = 0.2;

std::unique_ptr<MusicStream> open_midi(std::vector<std::uint8_t> bytes, std::shared_ptr<const SoundFont> soundfont,
                                       const std::string& name, LoadError& error);
std::unique_ptr<MusicStream> open_mp3(std::vector<std::uint8_t> bytes, const std::string& name, LoadError& error);

bool looks_like_midi(const std::vector<std::uint8_t>& bytes);
bool looks_like_mp3(const std::vector<std::uint8_t>& bytes);

}  // namespace elona::audio
