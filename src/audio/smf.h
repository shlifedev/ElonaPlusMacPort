#pragma once
// Standard MIDI File parser and tempo map (internal to the music renderer,
// exposed for tests).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace elona::audio {

struct MidiEvent {
    enum class Kind : std::uint8_t { channel, sysex, tempo, end_of_track };
    std::uint64_t frame = 0;  // absolute output frame (filled by build_timing)
    std::uint32_t tick = 0;   // absolute tick
    std::uint16_t track = 0;
    Kind kind = Kind::channel;
    std::uint8_t status = 0;  // channel: status byte; sysex: 0xF0 or 0xF7
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint32_t tempo_us = 0;     // tempo: microseconds per quarter note
    std::uint32_t sysex_offset = 0;  // into MidiSong::sysex_data
    std::uint32_t sysex_size = 0;
};

struct MidiSong {
    int format = 0;
    int tracks = 0;
    int division = 0;  // >0 ticks per quarter; <0 SMPTE: -(fps) in high byte form, see smpte_*
    int smpte_fps = 0;           // 24, 25, 29 (=29.97), 30 when SMPTE timing
    int smpte_ticks_per_frame = 0;
    std::vector<MidiEvent> events;  // merged, sorted by (tick, track, order)
    std::vector<std::uint8_t> sysex_data;
    std::uint32_t length_tick = 0;     // max end-of-track / last event tick
    std::uint64_t length_frames = 0;   // filled by build_timing
};

struct SmfError {
    bool unsupported = false;  // valid but not implemented (format 2)
    std::string message;
};

// Parses an SMF image, or a RIFF RMID wrapper around one. Returns false and
// fills `error` for malformed input.
bool parse_smf(const std::uint8_t* data, std::size_t size, MidiSong& song, SmfError& error);

// Converts ticks to output frames at `sample_rate` using the tempo events.
void build_timing(MidiSong& song, int sample_rate);

}  // namespace elona::audio
