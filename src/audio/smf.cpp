#include "smf.h"

#include <algorithm>
#include <cmath>

namespace elona::audio {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
std::uint32_t le32(const std::uint8_t* p) {
    return (std::uint32_t(p[3]) << 24) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[1]) << 8) | p[0];
}

struct Reader {
    const std::uint8_t* p;
    std::size_t size;
    std::size_t pos = 0;
    bool vlq(std::uint32_t& out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos >= size) return false;
            const std::uint8_t b = p[pos++];
            out = (out << 7) | (b & 0x7f);
            if ((b & 0x80) == 0) return true;
        }
        return false;  // more than 4 bytes
    }
};

std::string at(int track, std::size_t offset, const char* what) {
    return "track " + std::to_string(track) + " offset " + std::to_string(offset) + ": " + what;
}

bool parse_track(const std::uint8_t* data, std::size_t size, int track, MidiSong& song, std::vector<MidiEvent>& out,
                 SmfError& error) {
    Reader r{data, size};
    std::uint32_t tick = 0;
    std::uint8_t running = 0;
    while (r.pos < r.size) {
        std::uint32_t delta = 0;
        if (!r.vlq(delta)) {
            error.message = at(track, r.pos, "truncated or overlong delta time");
            return false;
        }
        if (tick > 0xffffffffu - delta) {
            error.message = at(track, r.pos, "tick overflow");
            return false;
        }
        tick += delta;
        if (r.pos >= r.size) {
            error.message = at(track, r.pos, "event missing after delta time");
            return false;
        }
        std::uint8_t status = r.p[r.pos];
        MidiEvent ev;
        ev.tick = tick;
        ev.track = static_cast<std::uint16_t>(track);
        if (status == 0xff) {
            if (r.pos + 2 > r.size) {
                error.message = at(track, r.pos, "truncated meta event");
                return false;
            }
            const std::uint8_t type = r.p[r.pos + 1];
            r.pos += 2;
            std::uint32_t len = 0;
            if (!r.vlq(len) || len > r.size - r.pos) {
                error.message = at(track, r.pos, "meta event length exceeds track");
                return false;
            }
            const std::uint8_t* body = r.p + r.pos;
            r.pos += len;
            if (type == 0x51) {
                if (len != 3) {
                    error.message = at(track, r.pos, "tempo meta event must have 3 bytes");
                    return false;
                }
                ev.kind = MidiEvent::Kind::tempo;
                ev.tempo_us = (std::uint32_t(body[0]) << 16) | (std::uint32_t(body[1]) << 8) | body[2];
                if (ev.tempo_us == 0) {
                    error.message = at(track, r.pos, "tempo of 0 microseconds per quarter note");
                    return false;
                }
                out.push_back(ev);
            } else if (type == 0x2f) {
                ev.kind = MidiEvent::Kind::end_of_track;
                out.push_back(ev);
                return true;  // bytes after End of Track are ignored
            }
            // other meta events (text, markers, key/time signature) carry no sound
            continue;
        }
        if (status == 0xf0 || status == 0xf7) {
            ++r.pos;
            std::uint32_t len = 0;
            if (!r.vlq(len) || len > r.size - r.pos) {
                error.message = at(track, r.pos, "SysEx length exceeds track");
                return false;
            }
            ev.kind = MidiEvent::Kind::sysex;
            ev.status = status;
            ev.sysex_offset = static_cast<std::uint32_t>(song.sysex_data.size());
            ev.sysex_size = len;
            song.sysex_data.insert(song.sysex_data.end(), r.p + r.pos, r.p + r.pos + len);
            r.pos += len;
            running = 0;  // SysEx cancels running status
            out.push_back(ev);
            continue;
        }
        if (status >= 0xf1) {
            error.message = at(track, r.pos, "system common/real-time byte inside a track");
            return false;
        }
        if (status & 0x80) {
            running = status;
            ++r.pos;
        } else if (running == 0) {
            error.message = at(track, r.pos, "data byte without running status");
            return false;
        }
        const std::uint8_t type = running & 0xf0;
        const int count = (type == 0xc0 || type == 0xd0) ? 1 : 2;
        if (r.pos + static_cast<std::size_t>(count) > r.size) {
            error.message = at(track, r.pos, "truncated channel message");
            return false;
        }
        ev.kind = MidiEvent::Kind::channel;
        ev.status = running;
        ev.data1 = r.p[r.pos];
        ev.data2 = count == 2 ? r.p[r.pos + 1] : 0;
        if ((ev.data1 & 0x80) || (count == 2 && (ev.data2 & 0x80))) {
            error.message = at(track, r.pos, "status byte where a data byte was expected");
            return false;
        }
        r.pos += static_cast<std::size_t>(count);
        out.push_back(ev);
    }
    // Missing End of Track: tolerated, the track simply ends at its last event.
    return true;
}

}  // namespace

bool parse_smf(const std::uint8_t* data, std::size_t size, MidiSong& song, SmfError& error) {
    song = MidiSong{};
    error = SmfError{};
    if (data == nullptr) {
        error.message = "no data";
        return false;
    }
    // RIFF RMID wrapper
    if (size >= 12 && std::equal(data, data + 4, "RIFF") && std::equal(data + 8, data + 12, "RMID")) {
        std::size_t pos = 12;
        bool found = false;
        while (pos + 8 <= size) {
            const std::uint32_t len = le32(data + pos + 4);
            if (len > size - pos - 8) break;
            if (std::equal(data + pos, data + pos + 4, "data")) {
                data += pos + 8;
                size = len;
                found = true;
                break;
            }
            pos += 8 + len + (len & 1);
        }
        if (!found) {
            error.message = "RIFF RMID without a valid data chunk";
            return false;
        }
    }
    if (size < 14 || !std::equal(data, data + 4, "MThd")) {
        error.message = "missing MThd header";
        return false;
    }
    const std::uint32_t header_len = be32(data + 4);
    if (header_len < 6 || header_len > size - 8) {
        error.message = "invalid MThd length";
        return false;
    }
    song.format = be16(data + 8);
    song.tracks = be16(data + 10);
    const std::uint16_t division = be16(data + 12);
    if (song.format > 2) {
        error.message = "unknown SMF format " + std::to_string(song.format);
        return false;
    }
    if (song.format == 2) {
        error.unsupported = true;
        error.message = "SMF format 2 (independent sequences) is not supported";
        return false;
    }
    if (song.tracks == 0) {
        error.message = "SMF declares no tracks";
        return false;
    }
    if (song.format == 0 && song.tracks != 1) {
        error.message = "SMF format 0 must have exactly one track";
        return false;
    }
    if (division & 0x8000) {
        const int fps = -static_cast<int>(static_cast<std::int8_t>(division >> 8));
        const int tpf = division & 0xff;
        if ((fps != 24 && fps != 25 && fps != 29 && fps != 30) || tpf == 0) {
            error.message = "invalid SMPTE division";
            return false;
        }
        song.smpte_fps = fps;
        song.smpte_ticks_per_frame = tpf;
        song.division = -fps;
    } else {
        if (division == 0) {
            error.message = "division of 0 ticks per quarter note";
            return false;
        }
        song.division = division;
    }
    std::size_t pos = 8 + header_len;
    std::vector<std::vector<MidiEvent>> per_track;
    while (static_cast<int>(per_track.size()) < song.tracks) {
        if (pos + 8 > size) {
            error.message = "file ends before track " + std::to_string(per_track.size());
            return false;
        }
        const std::uint32_t len = be32(data + pos + 4);
        if (len > size - pos - 8) {
            error.message = "chunk " + std::to_string(per_track.size()) + " length exceeds file";
            return false;
        }
        if (std::equal(data + pos, data + pos + 4, "MTrk")) {
            per_track.emplace_back();
            if (!parse_track(data + pos + 8, len, static_cast<int>(per_track.size()) - 1, song, per_track.back(), error)) {
                return false;
            }
        }
        // unknown chunks are skipped as the SMF specification requires
        pos += 8 + len;
    }
    std::size_t total = 0;
    for (const auto& t : per_track) total += t.size();
    song.events.reserve(total);
    for (const auto& t : per_track) song.events.insert(song.events.end(), t.begin(), t.end());
    std::stable_sort(song.events.begin(), song.events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.track < b.track;
    });
    for (const auto& e : song.events) song.length_tick = std::max(song.length_tick, e.tick);
    return true;
}

void build_timing(MidiSong& song, int sample_rate) {
    if (song.smpte_fps != 0) {
        const double fps = song.smpte_fps == 29 ? 30000.0 / 1001.0 : song.smpte_fps;
        const double seconds_per_tick = 1.0 / (fps * song.smpte_ticks_per_frame);
        for (auto& e : song.events) e.frame = static_cast<std::uint64_t>(std::llround(e.tick * seconds_per_tick * sample_rate));
        song.length_frames = static_cast<std::uint64_t>(std::llround(song.length_tick * seconds_per_tick * sample_rate));
        return;
    }
    // Tempo changes apply from their tick onward (events are sorted by tick).
    double seconds = 0.0;
    std::uint32_t last_tick = 0;
    double seconds_per_tick = 0.5 / song.division;  // default 120 BPM
    for (auto& e : song.events) {
        seconds += (e.tick - last_tick) * seconds_per_tick;
        last_tick = e.tick;
        e.frame = static_cast<std::uint64_t>(std::llround(seconds * sample_rate));
        if (e.kind == MidiEvent::Kind::tempo) seconds_per_tick = e.tempo_us / 1e6 / song.division;
    }
    seconds += (song.length_tick - last_tick) * seconds_per_tick;
    song.length_frames = static_cast<std::uint64_t>(std::llround(seconds * sample_rate));
}

}  // namespace elona::audio
