#include <cstring>

#include "audio_out.h"

namespace elona::platform {
namespace {

std::uint32_t le32(const std::uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); }

// Minimal RIFF/WAVE walk: returns byte rate and data size.
bool wav_info(const std::vector<std::uint8_t>& w, std::uint32_t& byte_rate, std::uint32_t& data_size) {
    if (w.size() < 12 || std::memcmp(w.data(), "RIFF", 4) || std::memcmp(w.data() + 8, "WAVE", 4)) return false;
    byte_rate = 0;
    data_size = 0;
    for (std::size_t o = 12; o + 8 <= w.size();) {
        std::uint32_t sz = le32(&w[o + 4]);
        if (!std::memcmp(&w[o], "fmt ", 4) && o + 8 + 12 <= w.size()) byte_rate = le32(&w[o + 16]);
        if (!std::memcmp(&w[o], "data", 4)) { data_size = sz; break; }
        o += 8 + sz + (sz & 1);
    }
    return byte_rate != 0;
}

class NullAudio final : public AudioOut {
public:
    const char* name() const override { return "null"; }
    bool play_wav(const std::vector<std::uint8_t>& wav, bool, std::string& error) override {
        std::uint32_t br, ds;
        if (!wav_info(wav, br, ds)) { error = "not a RIFF WAVE image"; return false; }
        return true;
    }
    void stop() override {}
    void update() override {}
    bool start_music_output(std::function<void(float*, std::size_t)>, std::string& error) override {
        error = "null audio device (deterministic/headless run)";
        return false;
    }
    int duration_ms(const std::vector<std::uint8_t>& wav) override {
        std::uint32_t br, ds;
        if (!wav_info(wav, br, ds)) return -1;
        return int(std::uint64_t(ds) * 1000 / br);
    }
};

}  // namespace

std::unique_ptr<AudioOut> create_null_audio() { return std::make_unique<NullAudio>(); }

int wav_duration_ms(const std::vector<std::uint8_t>& wav) {
    std::uint32_t br, ds;
    if (!wav_info(wav, br, ds)) return -1;
    return int(std::uint64_t(ds) * 1000 / br);
}

}  // namespace elona::platform
