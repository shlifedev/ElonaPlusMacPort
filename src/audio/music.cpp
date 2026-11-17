#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#include "music_internal.h"
#include "tsf.h"

namespace elona::audio {

const char* music_error_name(MusicError error) noexcept {
    switch (error) {
    case MusicError::none: return "none";
    case MusicError::invalid_data: return "invalid_data";
    case MusicError::unsupported: return "unsupported";
    case MusicError::soundfont_error: return "soundfont_error";
    case MusicError::io_error: return "io_error";
    }
    return "invalid";
}

// ---------------------------------------------------------------- SoundFont
SoundFont::Impl::~Impl() {
    if (base) tsf_close(base);
}

SoundFont::SoundFont(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SoundFont::~SoundFont() = default;

std::shared_ptr<const SoundFont> SoundFont::load_memory(const std::vector<std::uint8_t>& bytes, LoadError& error) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "sfbk", 4) != 0) {
        error = {MusicError::soundfont_error, "not a SoundFont 2 (RIFF sfbk) image"};
        return nullptr;
    }
    if (bytes.size() > 0x7fffffffu) {
        error = {MusicError::soundfont_error, "SoundFont larger than 2 GiB"};
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->base = tsf_load_memory(bytes.data(), static_cast<int>(bytes.size()));
    if (impl->base == nullptr) {
        error = {MusicError::soundfont_error, "SoundFont could not be parsed"};
        return nullptr;
    }
    if (tsf_get_presetcount(impl->base) <= 0) {
        error = {MusicError::soundfont_error, "SoundFont has no presets"};
        return nullptr;
    }
    return std::shared_ptr<const SoundFont>(new SoundFont(std::move(impl)));
}

std::shared_ptr<const SoundFont> SoundFont::load_file(const std::filesystem::path& path, LoadError& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = {MusicError::io_error, "cannot open SoundFont " + path.u8string()};
        return nullptr;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        error = {MusicError::io_error, "cannot read SoundFont " + path.u8string()};
        return nullptr;
    }
    auto sf = load_memory(bytes, error);
    if (!sf) error.message += " (" + path.u8string() + ")";
    return sf;
}

int SoundFont::preset_count() const noexcept { return tsf_get_presetcount(impl_->base); }

bool SoundFont::has_preset(int bank, int program) const noexcept {
    return tsf_get_presetindex(impl_->base, bank, program) != -1;
}

// ---------------------------------------------------------------- open_music
std::unique_ptr<MusicStream> open_music(std::vector<std::uint8_t> bytes, std::shared_ptr<const SoundFont> soundfont,
                                        const std::string& name, LoadError& error) {
    error = LoadError{};
    if (looks_like_midi(bytes)) return open_midi(std::move(bytes), std::move(soundfont), name, error);
    if (looks_like_mp3(bytes)) return open_mp3(std::move(bytes), name, error);
    error = {MusicError::invalid_data, name + ": neither a Standard MIDI File nor an MP3 stream"};
    return nullptr;
}

// ---------------------------------------------------------------- MusicPlayer
MusicPlayer::MusicPlayer() = default;
MusicPlayer::~MusicPlayer() = default;

void MusicPlayer::play(std::unique_ptr<MusicStream> stream, bool loop) {
    std::unique_ptr<MusicStream> old;
    if (stream) stream->set_loop(loop);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        old = std::move(stream_);
        stream_ = std::move(stream);
        state_ = stream_ ? State::playing : State::idle;
    }
    // the old stream is destroyed here, on the control thread
}

void MusicPlayer::stop() { play(nullptr, false); }

void MusicPlayer::set_volume(float gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    gain_ = std::max(0.0f, gain);
}

MusicPlayer::State MusicPlayer::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::uint64_t MusicPlayer::position_frames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stream_ ? stream_->position_frames() : 0;
}

std::uint32_t MusicPlayer::loops_completed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stream_ ? stream_->loops_completed() : 0;
}

void MusicPlayer::render(float* out, std::size_t frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_ || state_ != State::playing) {
        std::memset(out, 0, frames * kChannels * sizeof(float));
        return;
    }
    stream_->render(out, frames);
    if (stream_->finished()) state_ = State::finished;  // released later by play/stop
    const float g = gain_;
    for (std::size_t i = 0; i < frames * kChannels; ++i) out[i] = std::clamp(out[i] * g, -1.0f, 1.0f);
}

}  // namespace elona::audio
