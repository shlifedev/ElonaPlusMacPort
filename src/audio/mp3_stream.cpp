// MP3 playback through dr_mp3, converted to 44100 Hz stereo float.
#include <algorithm>
#include <cstring>

#include "dr_mp3.h"
#include "music_internal.h"

namespace elona::audio {
namespace {

constexpr std::size_t kChunk = 1152 * 2;  // source frames decoded per refill

class Mp3Stream final : public MusicStream {
public:
    Mp3Stream(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    ~Mp3Stream() override {
        if (open_) drmp3_uninit(&mp3_);
    }

    bool open(const std::string& name, LoadError& error) {
        if (!drmp3_init_memory(&mp3_, bytes_.data(), bytes_.size(), nullptr)) {
            error = {MusicError::invalid_data, name + ": not a decodable MP3 stream"};
            return false;
        }
        open_ = true;
        src_channels_ = static_cast<int>(mp3_.channels);
        src_rate_ = static_cast<int>(mp3_.sampleRate);
        if ((src_channels_ != 1 && src_channels_ != 2) || src_rate_ < 8000 || src_rate_ > 48000) {
            error = {MusicError::unsupported, name + ": unsupported MP3 layout (" + std::to_string(src_channels_) +
                                                  " ch, " + std::to_string(src_rate_) + " Hz)"};
            return false;
        }
        const drmp3_uint64 frames = drmp3_get_pcm_frame_count(&mp3_);
        if (frames == 0) {
            error = {MusicError::invalid_data, name + ": MP3 contains no decodable frames"};
            return false;
        }
        if (!drmp3_seek_to_pcm_frame(&mp3_, 0)) {
            error = {MusicError::invalid_data, name + ": MP3 cannot be rewound"};
            return false;
        }
        src_frames_ = frames;
        step_ = static_cast<double>(src_rate_) / kSampleRate;
        info_.kind = MusicKind::mp3;
        info_.source_rate = src_rate_;
        info_.source_channels = src_channels_;
        info_.length_frames = static_cast<std::uint64_t>(static_cast<double>(frames) / step_);
        buffer_.assign(kChunk * 2, 0.0f);  // stereo, allocated once
        return true;
    }

    const MusicInfo& info() const noexcept override { return info_; }
    bool finished() const noexcept override { return finished_; }
    void set_loop(bool loop) noexcept override { loop_ = loop; }
    bool looping() const noexcept override { return loop_; }
    std::uint64_t position_frames() const noexcept override { return pos_; }
    std::uint32_t loops_completed() const noexcept override { return loops_; }

    void rewind() override {
        drmp3_seek_to_pcm_frame(&mp3_, 0);
        restart_pass();
        loops_ = 0;
        finished_ = false;
    }

    std::size_t render(float* out, std::size_t frames) override {
        std::size_t produced = 0;
        while (produced < frames && !finished_) {
            // Need source frames idx and idx+1 for linear interpolation.
            const std::uint64_t idx = static_cast<std::uint64_t>(phase_);
            if (!ensure(idx + 1)) {
                if (!ensure(idx)) {  // genuinely past the last frame
                    if (loop_) {
                        drmp3_seek_to_pcm_frame(&mp3_, 0);
                        restart_pass();
                        ++loops_;
                        continue;
                    }
                    finished_ = true;
                    break;
                }
            }
            const double frac = phase_ - static_cast<double>(idx);
            const float* a = frame(idx);
            const float* b = available(idx + 1) ? frame(idx + 1) : a;
            out[produced * 2] = static_cast<float>(a[0] + (b[0] - a[0]) * frac);
            out[produced * 2 + 1] = static_cast<float>(a[1] + (b[1] - a[1]) * frac);
            ++produced;
            ++pos_;
            phase_ += step_;
        }
        if (produced < frames) std::memset(out + produced * 2, 0, (frames - produced) * 2 * sizeof(float));
        return produced;
    }

private:
    void restart_pass() {
        pos_ = 0;
        phase_ = 0.0;
        base_ = 0;
        count_ = 0;
        eof_ = false;
    }

    bool available(std::uint64_t idx) const { return idx >= base_ && idx < base_ + count_; }
    const float* frame(std::uint64_t idx) const { return buffer_.data() + (idx - base_) * 2; }

    // Makes source frame `idx` resident; keeps one frame of history for interpolation.
    bool ensure(std::uint64_t idx) {
        while (!available(idx)) {
            if (eof_ || idx < base_) return false;
            // Keep the last frame (history) and append a new chunk after it.
            std::size_t keep = 0;
            if (count_ > 0) {
                std::memcpy(buffer_.data(), frame(base_ + count_ - 1), 2 * sizeof(float));
                base_ += count_ - 1;
                keep = 1;
            } else {
                base_ = base_ + count_;
            }
            count_ = keep;
            const std::size_t room = buffer_.size() / 2 - keep;
            float* dst = buffer_.data() + keep * 2;
            drmp3_uint64 got;
            if (src_channels_ == 2) {
                got = drmp3_read_pcm_frames_f32(&mp3_, room, dst);
            } else {
                got = drmp3_read_pcm_frames_f32(&mp3_, room, dst);  // mono into the first half
                for (std::size_t i = static_cast<std::size_t>(got); i-- > 0;) {
                    dst[i * 2] = dst[i];
                    dst[i * 2 + 1] = dst[i];
                }
            }
            if (got == 0) {
                eof_ = true;
                return available(idx);
            }
            count_ += static_cast<std::size_t>(got);
        }
        return true;
    }

    std::vector<std::uint8_t> bytes_;
    drmp3 mp3_{};
    bool open_ = false;
    int src_channels_ = 0;
    int src_rate_ = 0;
    drmp3_uint64 src_frames_ = 0;
    double step_ = 1.0;
    MusicInfo info_;
    std::vector<float> buffer_;
    std::uint64_t base_ = 0;   // source index of buffer_[0]
    std::size_t count_ = 0;    // resident source frames
    double phase_ = 0.0;       // source position of the next output frame
    std::uint64_t pos_ = 0;
    std::uint32_t loops_ = 0;
    bool eof_ = false;
    bool loop_ = false;
    bool finished_ = false;
};

}  // namespace

bool looks_like_mp3(const std::vector<std::uint8_t>& b) {
    if (b.size() >= 3 && std::memcmp(b.data(), "ID3", 3) == 0) return true;
    return b.size() >= 2 && b[0] == 0xff && (b[1] & 0xe0) == 0xe0 && ((b[1] >> 1) & 3) != 0;  // sync + layer
}

std::unique_ptr<MusicStream> open_mp3(std::vector<std::uint8_t> bytes, const std::string& name, LoadError& error) {
    auto stream = std::make_unique<Mp3Stream>(std::move(bytes));
    if (!stream->open(name, error)) return nullptr;
    return stream;
}

}  // namespace elona::audio
