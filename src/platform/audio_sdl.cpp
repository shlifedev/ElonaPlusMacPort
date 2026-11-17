// SDL3 PCM playback for sndPlaySound-style WAV output.
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <limits>

#include "audio_out.h"

namespace elona::platform {
int wav_duration_ms(const std::vector<std::uint8_t>& wav);  // audio_null.cpp

namespace {
class SdlAudio final : public AudioOut {
public:
    ~SdlAudio() override {
        stop();
        if (music_) SDL_DestroyAudioStream(music_);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    const char* name() const override { return "sdl3"; }

    bool play_wav(const std::vector<std::uint8_t>& wav, bool loop, std::string& error) override {
        stop();
        SDL_AudioSpec spec;
        Uint8* buf = nullptr;
        Uint32 len = 0;
        SDL_IOStream* io = SDL_IOFromConstMem(wav.data(), wav.size());
        if (!io || !SDL_LoadWAV_IO(io, true, &spec, &buf, &len)) {
            error = std::string("SDL_LoadWAV: ") + SDL_GetError();
            return false;
        }
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream_) {
            SDL_free(buf);
            error = std::string("SDL_OpenAudioDeviceStream: ") + SDL_GetError();
            return false;
        }
        if (len > static_cast<Uint32>(std::numeric_limits<int>::max())) {
            SDL_free(buf);
            stop();
            error = "WAVE image is too large for SDL audio stream";
            return false;
        }
        pcm_.assign(buf, buf + len);
        SDL_free(buf);
        loop_ = loop;
        if (!SDL_PutAudioStreamData(stream_, pcm_.data(), int(pcm_.size())) ||
            !SDL_ResumeAudioStreamDevice(stream_)) {
            error = std::string("SDL WAV playback: ") + SDL_GetError();
            stop();
            return false;
        }
        return true;
    }
    void stop() override {
        if (stream_) SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        loop_ = false;
    }
    void update() override {
        if (stream_ && loop_ && !pcm_.empty() && SDL_GetAudioStreamQueued(stream_) < int(pcm_.size() / 2))
            SDL_PutAudioStreamData(stream_, pcm_.data(), int(pcm_.size()));
    }
    int duration_ms(const std::vector<std::uint8_t>& wav) override { return wav_duration_ms(wav); }

    bool start_music_output(std::function<void(float*, std::size_t)> render, std::string& error) override {
        if (music_) return true;
        render_ = std::move(render);
        SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 44100};
        music_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudio::music_callback, this);
        if (!music_) {
            error = std::string("SDL_OpenAudioDeviceStream(music): ") + SDL_GetError();
            return false;
        }
        if (!SDL_ResumeAudioStreamDevice(music_)) {
            error = std::string("SDL_ResumeAudioStreamDevice(music): ") + SDL_GetError();
            SDL_DestroyAudioStream(music_);
            music_ = nullptr;
            render_ = {};
            return false;
        }
        return true;
    }

private:
    static void SDLCALL music_callback(void* user, SDL_AudioStream* stream, int additional, int) {
        auto* self = static_cast<SdlAudio*>(user);
        if (additional <= 0 || !self->render_) return;
        constexpr std::size_t frame_bytes = sizeof(float) * 2;
        std::size_t remaining = (std::size_t(additional) + frame_bytes - 1) / frame_bytes;
        // SDL may request different block sizes over the life of a device.
        // Keep application-side callback memory bounded and preallocated;
        // MusicPlayer::render writes the entire output block, including silence.
        while (remaining) {
            const std::size_t frames = std::min(remaining, self->scratch_.size() / 2);
            self->render_(self->scratch_.data(), frames);
            if (!SDL_PutAudioStreamData(stream, self->scratch_.data(), int(frames * frame_bytes))) return;
            remaining -= frames;
        }
    }
    SDL_AudioStream* music_ = nullptr;
    std::function<void(float*, std::size_t)> render_;
    std::array<float, 1024 * 2> scratch_{};
    SDL_AudioStream* stream_ = nullptr;
    std::vector<std::uint8_t> pcm_;
    bool loop_ = false;
};
}  // namespace

std::unique_ptr<AudioOut> create_sdl_audio(std::string& error) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        error = std::string("SDL_Init(AUDIO): ") + SDL_GetError();
        return nullptr;
    }
    return std::make_unique<SdlAudio>();
}

}  // namespace elona::platform
