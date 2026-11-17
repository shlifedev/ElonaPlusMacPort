// MIDI playback: own SMF sequencer driving a TinySoundFont synthesizer.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "music_internal.h"
#include "smf.h"
#include "tsf.h"

namespace elona::audio {
namespace {

constexpr int kDrumBank = 128;

struct ChannelState {
    int bank_msb = 0;
    int program = 0;
    bool drum = false;
};

class MidiStream final : public MusicStream {
public:
    MidiStream(MidiSong song, std::shared_ptr<const SoundFont> soundfont, tsf* synth)
        : song_(std::move(song)), soundfont_(std::move(soundfont)), synth_(synth) {
        info_.kind = MusicKind::midi;
        info_.length_frames = song_.length_frames;
        info_.smf_format = song_.format;
        info_.tracks = song_.tracks;
        info_.division = song_.division;
        for (const auto& e : song_.events) {
            if (e.kind == MidiEvent::Kind::channel) {
                const int type = e.status & 0xf0;
                info_.channels_used = static_cast<std::uint16_t>(info_.channels_used | (1u << (e.status & 0x0f)));
                if (type == 0x90 && e.data2 != 0) ++info_.notes;
                if (type == 0xb0 && e.data1 == 111) ++info_.loop_markers;
            } else if (e.kind == MidiEvent::Kind::sysex && classify_sysex(e) == SysexKind::other) {
                ++info_.ignored_sysex;
            }
        }
        tail_limit_ = static_cast<std::uint64_t>(kMaxTailSeconds * kSampleRate);
        quiet_limit_ = static_cast<std::uint64_t>(kTailQuietSeconds * kSampleRate);
        reset_channels(true);
    }

    ~MidiStream() override { tsf_close(synth_); }

    const MusicInfo& info() const noexcept override { return info_; }
    bool finished() const noexcept override { return finished_; }
    void set_loop(bool loop) noexcept override { loop_ = loop; }
    bool looping() const noexcept override { return loop_; }
    std::uint64_t position_frames() const noexcept override { return pos_; }
    std::uint32_t loops_completed() const noexcept override { return loops_; }
    // Diagnostics for tests.
    int active_voices() const { return tsf_active_voice_count(synth_); }
    std::uint32_t missing_presets() const { return missing_presets_; }

    void rewind() override {
        reset_channels(true);
        pos_ = 0;
        next_ = 0;
        loops_ = 0;
        quiet_frames_ = 0;
        finished_ = false;
    }

    std::size_t render(float* out, std::size_t frames) override {
        std::size_t produced = 0;
        std::size_t music = 0;
        while (produced < frames) {
            if (finished_) {
                std::memset(out + produced * kChannels, 0, (frames - produced) * kChannels * sizeof(float));
                break;
            }
            while (next_ < song_.events.size() && song_.events[next_].frame <= pos_) apply(song_.events[next_++]);
            std::uint64_t target;
            if (next_ < song_.events.size()) {
                target = song_.events[next_].frame;
            } else if (pos_ < song_.length_frames) {
                target = song_.length_frames;
            } else if (loop_) {
                // Whole-file loop: release everything (no stuck notes), restore
                // the power-on channel state and play again from tick 0.
                reset_channels(false);
                pos_ = 0;
                next_ = 0;
                ++loops_;
                if (song_.length_frames == 0) {  // nothing to loop: avoid spinning
                    finished_ = true;
                }
                continue;
            } else {
                // Release tail after the last event: ends when no voice is left,
                // when the output stayed inaudible for a while, or at the cap.
                if (tsf_active_voice_count(synth_) == 0 || quiet_frames_ >= quiet_limit_ ||
                    pos_ >= song_.length_frames + tail_limit_) {
                    tsf_note_off_all(synth_);
                    finished_ = true;
                    continue;
                }
                target = pos_ + 256;  // re-check every 256 frames
            }
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(frames - produced, target - pos_));
            float* chunk = out + produced * kChannels;
            tsf_render_float(synth_, chunk, static_cast<int>(n), 0);
            if (pos_ >= song_.length_frames) {
                float peak = 0.0f;
                for (std::size_t i = 0; i < n * kChannels; ++i) peak = std::max(peak, std::fabs(chunk[i]));
                quiet_frames_ = peak < kTailSilence ? quiet_frames_ + n : 0;
            }
            pos_ += n;
            produced += n;
            music += n;
        }
        return music;
    }

private:
    enum class SysexKind { gm_on, gs_reset, xg_on, gs_rhythm_part, master_volume, other };

    SysexKind classify_sysex(const MidiEvent& e) const {
        const std::uint8_t* p = song_.sysex_data.data() + e.sysex_offset;
        const std::size_t n = e.sysex_size;
        if (e.status != 0xf0) return SysexKind::other;
        if (n >= 4 && p[0] == 0x7e && p[2] == 0x09 && p[3] == 0x01) return SysexKind::gm_on;
        if (n >= 6 && p[0] == 0x7f && p[2] == 0x04 && p[3] == 0x01) return SysexKind::master_volume;
        if (n >= 8 && p[0] == 0x41 && p[2] == 0x42 && p[3] == 0x12) {
            if (p[4] == 0x40 && p[5] == 0x00 && p[6] == 0x7f && p[7] == 0x00) return SysexKind::gs_reset;
            if (p[4] == 0x40 && (p[5] & 0xf0) == 0x10 && p[6] == 0x15) return SysexKind::gs_rhythm_part;
        }
        if (n >= 7 && p[0] == 0x43 && (p[1] & 0xf0) == 0x10 && p[2] == 0x4c && p[3] == 0x00 && p[4] == 0x00 &&
            p[5] == 0x7e && p[6] == 0x00)
            return SysexKind::xg_on;
        return SysexKind::other;
    }

    void select_preset(int ch) {
        ChannelState& c = channels_[static_cast<std::size_t>(ch)];
        int ok = 0;
        if (c.drum) {
            ok = tsf_channel_set_bank_preset(synth_, ch, kDrumBank, c.program);
            if (!ok) ok = tsf_channel_set_bank_preset(synth_, ch, kDrumBank, 0);
        } else {
            // GS: bank = CC0 MSB (CC32 selects the map and is ignored); unknown
            // variation falls back to the capital tone in bank 0.
            ok = tsf_channel_set_bank_preset(synth_, ch, c.bank_msb, c.program);
            if (!ok) ok = tsf_channel_set_bank_preset(synth_, ch, 0, c.program);
        }
        if (!ok) ++missing_presets_;  // keeps the previous preset
    }

    void reset_channel(int ch) {
        ChannelState& c = channels_[static_cast<std::size_t>(ch)];
        c = ChannelState{};
        c.drum = (ch == 9);
        tsf_channel_set_pitchwheel(synth_, ch, 8192);
        tsf_channel_set_pitchrange(synth_, ch, 2.0f);
        tsf_channel_set_tuning(synth_, ch, 0.0f);
        tsf_channel_set_sustain(synth_, ch, 0);
        tsf_channel_midi_control(synth_, ch, 7, 100);   // GM default volume
        tsf_channel_midi_control(synth_, ch, 11, 127);  // expression
        tsf_channel_midi_control(synth_, ch, 10, 64);   // pan centre
        tsf_channel_midi_control(synth_, ch, 101, 127);  // RPN null
        tsf_channel_midi_control(synth_, ch, 100, 127);
        select_preset(ch);
    }

    // hard: cut sounds immediately (rewind/GS reset); soft: release (loop).
    void reset_channels(bool hard) {
        for (int ch = 0; ch < 16; ++ch) {
            if (hard) tsf_channel_sounds_off_all(synth_, ch);
            else tsf_channel_note_off_all(synth_, ch);
            reset_channel(ch);
        }
        master_volume_ = 1.0f;
        tsf_set_volume(synth_, base_gain_ * master_volume_);
    }

    void apply(const MidiEvent& e) {
        if (e.kind == MidiEvent::Kind::sysex) {
            switch (classify_sysex(e)) {
            case SysexKind::gm_on:
            case SysexKind::gs_reset:
            case SysexKind::xg_on:
                reset_channels(true);
                break;
            case SysexKind::gs_rhythm_part: {
                const std::uint8_t* p = song_.sysex_data.data() + e.sysex_offset;
                const int part = p[5] & 0x0f;
                const int ch = part == 0 ? 9 : (part <= 9 ? part - 1 : part);
                channels_[static_cast<std::size_t>(ch)].drum = p[7] != 0;
                select_preset(ch);
                break;
            }
            case SysexKind::master_volume: {
                const std::uint8_t* p = song_.sysex_data.data() + e.sysex_offset;
                const int value = (p[5] << 7) | p[4];
                master_volume_ = value / 16383.0f;
                tsf_set_volume(synth_, base_gain_ * master_volume_);
                break;
            }
            case SysexKind::other:
                break;
            }
            return;
        }
        if (e.kind != MidiEvent::Kind::channel) return;
        const int ch = e.status & 0x0f;
        ChannelState& c = channels_[static_cast<std::size_t>(ch)];
        switch (e.status & 0xf0) {
        case 0x80:
            tsf_channel_note_off(synth_, ch, e.data1);
            break;
        case 0x90:
            if (e.data2 == 0) tsf_channel_note_off(synth_, ch, e.data1);
            else tsf_channel_note_on(synth_, ch, e.data1, e.data2 / 127.0f);
            break;
        case 0xb0:
            switch (e.data1) {
            case 0: c.bank_msb = e.data2; break;   // applied at the next program change
            case 32: break;                         // GS map select: ignored
            case 111: break;                        // RPG-Maker loop marker: ignored (whole-file loop)
            case 121:                               // reset all controllers (RP-015: not volume/pan)
                tsf_channel_set_pitchwheel(synth_, ch, 8192);
                tsf_channel_set_sustain(synth_, ch, 0);
                tsf_channel_midi_control(synth_, ch, 11, 127);
                tsf_channel_midi_control(synth_, ch, 101, 127);
                tsf_channel_midi_control(synth_, ch, 100, 127);
                break;
            default:
                tsf_channel_midi_control(synth_, ch, e.data1, e.data2);  // vol/pan/expr/sustain/RPN/notes off
            }
            break;
        case 0xc0:
            c.program = e.data1;
            select_preset(ch);
            break;
        case 0xe0:
            tsf_channel_set_pitchwheel(synth_, ch, e.data1 | (e.data2 << 7));
            break;
        default:  // poly/channel pressure: not modelled
            break;
        }
    }

    MidiSong song_;
    std::shared_ptr<const SoundFont> soundfont_;  // keeps the shared sample data alive
    tsf* synth_;
    MusicInfo info_;
    std::array<ChannelState, 16> channels_{};
    std::uint64_t pos_ = 0;
    std::size_t next_ = 0;
    std::uint64_t tail_limit_ = 0;
    std::uint64_t quiet_limit_ = 0;
    std::uint64_t quiet_frames_ = 0;
    std::uint32_t loops_ = 0;
    std::uint32_t missing_presets_ = 0;
    float master_volume_ = 1.0f;
    const float base_gain_ = std::pow(10.0f, kSynthGainDb / 20.0f);
    bool loop_ = false;
    bool finished_ = false;
};

}  // namespace

bool looks_like_midi(const std::vector<std::uint8_t>& b) {
    if (b.size() >= 4 && std::memcmp(b.data(), "MThd", 4) == 0) return true;
    return b.size() >= 12 && std::memcmp(b.data(), "RIFF", 4) == 0 && std::memcmp(b.data() + 8, "RMID", 4) == 0;
}

std::unique_ptr<MusicStream> open_midi(std::vector<std::uint8_t> bytes, std::shared_ptr<const SoundFont> soundfont,
                                       const std::string& name, LoadError& error) {
    if (!soundfont) {
        error = {MusicError::soundfont_error, name + ": MIDI playback requires a SoundFont"};
        return nullptr;
    }
    MidiSong song;
    SmfError smf_error;
    if (!parse_smf(bytes.data(), bytes.size(), song, smf_error)) {
        error = {smf_error.unsupported ? MusicError::unsupported : MusicError::invalid_data,
                 name + ": " + smf_error.message};
        return nullptr;
    }
    build_timing(song, kSampleRate);
    tsf* synth = tsf_copy(soundfont->impl().base);
    if (synth == nullptr) {
        error = {MusicError::soundfont_error, name + ": could not create a synthesizer instance"};
        return nullptr;
    }
    tsf_set_output(synth, TSF_STEREO_INTERLEAVED, kSampleRate, 0.0f);  // gain via tsf_set_volume
    if (!tsf_set_max_voices(synth, kMaxVoices)) {
        tsf_close(synth);
        error = {MusicError::soundfont_error, name + ": could not preallocate synthesizer voices"};
        return nullptr;
    }
    return std::make_unique<MidiStream>(std::move(song), std::move(soundfont), synth);
}

}  // namespace elona::audio
