// PCM output for the HSP mm* commands (sndPlaySound semantics: one PCM sound
// at a time; a new sound replaces the previous one).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace elona::platform {

class AudioOut {
public:
    virtual ~AudioOut() = default;
    virtual const char* name() const = 0;
    // Decode a RIFF WAVE image and start it. Returns false with a reason when
    // the data cannot be decoded (the caller decides how to report it).
    virtual bool play_wav(const std::vector<std::uint8_t>& wav, bool loop, std::string& error) = 0;
    virtual void stop() = 0;
    // Called once per host frame: refills looping sounds.
    virtual void update() = 0;
    // Duration in ms of a WAVE image (for SND_SYNC waits); -1 if unknown.
    virtual int duration_ms(const std::vector<std::uint8_t>& wav) = 0;
    // Continuous music output (44100 Hz, 2 ch, interleaved float32) pulled from
    // `render` on the audio thread; mixed by the device with the PCM sounds.
    // Returns false when this device cannot play music (null device).
    virtual bool start_music_output(std::function<void(float*, std::size_t)> render, std::string& error) = 0;
};

// Null device: decodes the RIFF header for validation/duration, plays nothing.
std::unique_ptr<AudioOut> create_null_audio();
#ifdef ELONA_GUI
std::unique_ptr<AudioOut> create_sdl_audio(std::string& error);
#endif

}  // namespace elona::platform
