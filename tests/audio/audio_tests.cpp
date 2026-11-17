// Unit tests for the native music renderer (synthetic media, no game assets).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "audio/music.h"
#include "audio/smf.h"
#include "test_media.h"

using namespace elona::audio;
using namespace elona::audio_tests;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        ++g_checks;                                                                                  \
        if (!(cond)) {                                                                               \
            ++g_failures;                                                                            \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                        \
    do {                                                                                             \
        ++g_checks;                                                                                  \
        const double va_ = (a), vb_ = (b);                                                           \
        if (!(std::fabs(va_ - vb_) <= (tol))) {                                                      \
            ++g_failures;                                                                            \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_NEAR failed: " #a " = " << va_      \
                      << " vs " << vb_ << " (tol " << (tol) << ")\n";                                \
        }                                                                                            \
    } while (0)

namespace {

std::shared_ptr<const SoundFont> test_sf() {
    static std::shared_ptr<const SoundFont> sf = [] {
        LoadError e;
        auto s = SoundFont::load_memory(build_sf2(), e);
        if (!s) std::fprintf(stderr, "test SoundFont failed: %s\n", e.message.c_str());
        return s;
    }();
    return sf;
}

std::vector<float> render(MusicStream& s, double seconds, std::size_t block = 333) {
    const std::size_t frames = static_cast<std::size_t>(seconds * kSampleRate);
    std::vector<float> out(frames * 2);
    for (std::size_t pos = 0; pos < frames; pos += block) {
        const std::size_t n = std::min(block, frames - pos);
        s.render(out.data() + pos * 2, n);
    }
    return out;
}

double rms(const std::vector<float>& pcm, double t0, double t1) {
    const std::size_t a = static_cast<std::size_t>(t0 * kSampleRate), b = static_cast<std::size_t>(t1 * kSampleRate);
    double sum = 0;
    for (std::size_t i = a; i < b && i * 2 + 1 < pcm.size(); ++i) sum += pcm[i * 2] * pcm[i * 2] + pcm[i * 2 + 1] * pcm[i * 2 + 1];
    return std::sqrt(sum / std::max<std::size_t>(1, (b - a) * 2));
}

// Dominant frequency from zero crossings of L+R.
double freq(const std::vector<float>& pcm, double t0, double t1) {
    const std::size_t a = static_cast<std::size_t>(t0 * kSampleRate), b = static_cast<std::size_t>(t1 * kSampleRate);
    int crossings = 0;
    float prev = pcm[a * 2] + pcm[a * 2 + 1];
    for (std::size_t i = a + 1; i < b; ++i) {
        const float v = pcm[i * 2] + pcm[i * 2 + 1];
        if ((prev < 0 && v >= 0) || (prev >= 0 && v < 0)) ++crossings;
        prev = v;
    }
    return crossings / 2.0 / (t1 - t0);
}

std::unique_ptr<MusicStream> open_bytes(const Bytes& b, LoadError* err_out = nullptr) {
    LoadError e;
    auto s = open_music(b, test_sf(), "test", e);
    if (err_out) *err_out = e;
    if (!s && !err_out) std::fprintf(stderr, "open failed: %s\n", e.message.c_str());
    return s;
}

// One track at 120 BPM / 480 tpq: 480 ticks = 0.5 s.
SmfTrack note_track(int ch, int key, std::uint32_t on_delay, std::uint32_t len, int velocity = 100) {
    SmfTrack t;
    t.ev(on_delay, {0x90 | ch, key, velocity});
    t.ev(len, {0x80 | ch, key, 0});
    return t;
}

// ---------------------------------------------------------------- parser
void test_parser_errors() {
    auto expect = [](const Bytes& b, bool unsupported, const char* what) {
        MidiSong song;
        SmfError err;
        const bool ok = parse_smf(b.data(), b.size(), song, err);
        ++g_checks;
        if (ok || err.unsupported != unsupported || err.message.empty()) {
            ++g_failures;
            std::fprintf(stderr, "parser case '%s': ok=%d unsupported=%d msg=%s\n", what, ok, err.unsupported, err.message.c_str());
        }
    };
    SmfTrack good = note_track(0, 69, 0, 480);
    good.end(0);
    const Bytes valid = build_smf({good});
    {
        MidiSong song;
        SmfError err;
        CHECK(parse_smf(valid.data(), valid.size(), song, err));
        CHECK(song.format == 0 && song.tracks == 1 && song.division == 480);
        CHECK(song.events.size() == 3);
    }
    expect({}, false, "empty");
    expect(Bytes(valid.begin(), valid.begin() + 10), false, "short header");
    Bytes b = valid; b[0] = 'X'; expect(b, false, "no MThd");
    b = valid; b[7] = 2; expect(b, false, "MThd length < 6");
    b = valid; b[8] = 0; b[9] = 2; expect(b, true, "format 2");
    b = valid; b[9] = 3; expect(b, false, "unknown format");
    b = valid; b[11] = 0; expect(b, false, "zero tracks");
    b = valid; b[11] = 2; expect(b, false, "format 0 with 2 tracks");
    b = valid; b[12] = 0; b[13] = 0; expect(b, false, "division 0");
    b = valid; b[21] = 0xff; expect(b, false, "chunk length beyond file");
    b = valid; b.resize(b.size() - 6); expect(b, false, "chunk length beyond truncated file");
    {
        SmfTrack t; t.data = {0x81, 0x82, 0x83, 0x84, 0x05, 0x90, 60, 1}; expect(build_smf({t}), false, "5-byte VLQ");
    }
    { SmfTrack t; t.data = {0x00, 0x3c, 0x40}; expect(build_smf({t}), false, "data byte without running status"); }
    { SmfTrack t; t.data = {0x00, 0x90, 0x3c, 0x90}; expect(build_smf({t}), false, "status where data expected"); }
    { SmfTrack t; t.data = {0x00, 0x90, 0x3c}; expect(build_smf({t}), false, "truncated note"); }
    { SmfTrack t; t.tempo(0, 0); t.end(0); expect(build_smf({t}), false, "tempo 0"); }
    { SmfTrack t; t.ev(0, {0xff, 0x51, 2, 1, 2}); expect(build_smf({t}), false, "tempo length 2"); }
    { SmfTrack t; t.ev(0, {0xff, 0x01, 0x7f, 'a'}); expect(build_smf({t}), false, "meta length beyond track"); }
    { SmfTrack t; t.ev(0, {0xf0, 0x7f, 0x41}); expect(build_smf({t}), false, "SysEx length beyond track"); }
    { SmfTrack t; t.ev(0, {0xf8}); expect(build_smf({t}), false, "real-time byte in track"); }
    { SmfTrack t; t.data = {0x00}; expect(build_smf({t}), false, "delta without event"); }
    b = valid; b[12] = 0xe7; b[13] = 0x00; expect(b, false, "SMPTE ticks per frame 0");
    b = valid; b[12] = 0xe0; b[13] = 0x28; expect(b, false, "SMPTE -32 fps");
    // Unknown chunks are skipped; running status works; missing End of Track tolerated.
    {
        Bytes x = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xe0,
                   'X', 'Y', 'Z', 'W', 0, 0, 0, 2, 1, 2,
                   'M', 'T', 'r', 'k', 0, 0, 0, 7, 0x00, 0x90, 60, 100, 0x10, 60, 0};
        MidiSong song;
        SmfError err;
        CHECK(parse_smf(x.data(), x.size(), song, err));
        CHECK(song.events.size() == 2 && song.events[1].status == 0x90 && song.events[1].data2 == 0 &&
              song.events[1].tick == 16);
    }
    // RIFF RMID wrapper.
    {
        Bytes r = {'R', 'I', 'F', 'F'};
        put32(r, static_cast<std::uint32_t>(4 + 8 + valid.size()));
        put_tag(r, "RMID");
        put_tag(r, "data");
        put32(r, static_cast<std::uint32_t>(valid.size()));
        r.insert(r.end(), valid.begin(), valid.end());
        MidiSong song;
        SmfError err;
        CHECK(parse_smf(r.data(), r.size(), song, err));
        CHECK(song.events.size() == 3);
        Bytes bad = r;
        bad.resize(bad.size() - 3);
        expect(bad, false, "RMID data chunk truncated");
    }
}

void test_timing() {
    SmfTrack t;
    t.ev(0, {0x90, 60, 100});
    t.tempo(480, 250000);     // after 0.5 s: 240 BPM
    t.ev(480, {0x80, 60, 0}); // +0.25 s
    t.end(960);               // +0.5 s
    const Bytes b = build_smf({t});
    MidiSong song;
    SmfError err;
    CHECK(parse_smf(b.data(), b.size(), song, err));
    build_timing(song, kSampleRate);
    CHECK(song.events[1].frame == 22050);
    CHECK(song.events[2].frame == 22050 + 11025);
    CHECK(song.length_frames == 22050 + 11025 + 22050);
    // Format 1 with the tempo in a separate track affects all tracks.
    SmfTrack conductor;
    conductor.tempo(0, 1000000);  // 60 BPM
    conductor.end(0);
    SmfTrack notes = note_track(0, 60, 480, 480);
    notes.end(0);
    const Bytes b1 = build_smf({conductor, notes});
    CHECK(parse_smf(b1.data(), b1.size(), song, err));
    build_timing(song, kSampleRate);
    CHECK(song.length_frames == 2 * 44100);
    // SMPTE: 25 fps x 40 ticks = 1000 ticks per second.
    SmfTrack s = note_track(0, 60, 500, 500);
    s.end(0);
    Bytes bs = build_smf({s}, 0xe728);
    CHECK(parse_smf(bs.data(), bs.size(), song, err));
    build_timing(song, kSampleRate);
    CHECK(song.smpte_fps == 25 && song.events[0].frame == 22050 && song.length_frames == 44100);
}

// ---------------------------------------------------------------- synthesis
void test_note_and_program() {
    CHECK(test_sf() != nullptr);
    if (!test_sf()) return;
    CHECK(test_sf()->has_preset(0, 1) && test_sf()->has_preset(128, 0) && !test_sf()->has_preset(9, 0));
    // Program 0 note 69 -> 441 Hz; note-off -> silence (1 ms release).
    {
        SmfTrack t = note_track(0, 69, 0, 480);
        t.end(480);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 1.0);
        CHECK_NEAR(freq(pcm, 0.05, 0.45), 441.0, 6.0);
        CHECK(rms(pcm, 0.05, 0.45) > 0.005);
        CHECK(rms(pcm, 0.52, 0.98) < 1e-4);
        CHECK(s->info().notes == 1);
    }
    // Program change 1 (+12) -> 882 Hz; key 81 on program 0 -> 882 Hz.
    {
        SmfTrack t;
        t.ev(0, {0xc0, 1});
        t.ev(0, {0x90, 69, 100});
        t.ev(480, {0x80, 69, 0});
        t.ev(0, {0xc0, 0});
        t.ev(0, {0x90, 81, 100});
        t.ev(480, {0x80, 81, 0});
        t.end(0);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 1.0);
        CHECK_NEAR(freq(pcm, 0.05, 0.45), 882.0, 10.0);
        CHECK_NEAR(freq(pcm, 0.55, 0.95), 882.0, 10.0);
    }
    // GS bank select: MSB 8 -> variation (+7), unknown MSB 9 -> capital tone; LSB ignored.
    {
        SmfTrack t;
        t.ev(0, {0xb0, 0, 8});
        t.ev(0, {0xb0, 32, 3});
        t.ev(0, {0xc0, 0});
        t.ev(0, {0x90, 69, 100});
        t.ev(480, {0x80, 69, 0});
        t.ev(0, {0xb0, 0, 9});
        t.ev(0, {0xc0, 0});
        t.ev(0, {0x90, 69, 100});
        t.ev(480, {0x80, 69, 0});
        t.end(0);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 1.0);
        CHECK_NEAR(freq(pcm, 0.05, 0.45), 441.0 * std::pow(2.0, 7.0 / 12.0), 8.0);
        CHECK_NEAR(freq(pcm, 0.55, 0.95), 441.0, 6.0);
    }
    // Percussion: channel 10 uses bank 128 (+24); program change selects another kit (+19).
    {
        SmfTrack t;
        t.ev(0, {0x99, 69, 100});
        t.ev(480, {0x89, 69, 0});
        t.ev(0, {0xc9, 8});
        t.ev(0, {0x99, 69, 100});
        t.ev(480, {0x89, 69, 0});
        t.end(0);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 1.0);
        CHECK_NEAR(freq(pcm, 0.05, 0.45), 441.0 * 4, 25.0);
        CHECK_NEAR(freq(pcm, 0.55, 0.95), 441.0 * std::pow(2.0, 19.0 / 12.0), 20.0);
    }
    // GS "use for rhythm part" makes channel 11 a drum part; GS reset restores defaults.
    {
        SmfTrack t;
        t.sysex(0, {0x41, 0x10, 0x42, 0x12, 0x40, 0x1a, 0x15, 0x01, 0x10, 0xf7});  // part 11 -> rhythm
        t.ev(0, {0x9a, 69, 100});
        t.ev(480, {0x8a, 69, 0});
        t.sysex(0, {0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7});  // GS reset
        t.ev(0, {0x9a, 69, 100});
        t.ev(480, {0x8a, 69, 0});
        t.end(0);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 1.0);
        CHECK_NEAR(freq(pcm, 0.05, 0.45), 441.0 * 4, 25.0);
        CHECK_NEAR(freq(pcm, 0.55, 0.95), 441.0, 6.0);
        CHECK(s->info().ignored_sysex == 0);
    }
    // Pitch bend +8191 with default range 2 semitones.
    {
        SmfTrack t;
        t.ev(0, {0xe0, 0x7f, 0x7f});
        t.ev(0, {0x90, 69, 100});
        t.ev(480, {0x80, 69, 0});
        t.end(0);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 0.5);
        CHECK_NEAR(freq(pcm, 0.05, 0.45), 441.0 * std::pow(2.0, 2.0 / 12.0), 8.0);
    }
    // Volume 0 -> silence; a tempo change moves the second note.
    {
        SmfTrack t;
        t.ev(0, {0xb0, 7, 0});
        t.ev(0, {0x90, 69, 100});
        t.ev(480, {0x80, 69, 0});
        t.end(0);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 0.5);
        CHECK(rms(pcm, 0.05, 0.45) < 1e-5);
    }
    {
        SmfTrack t;
        t.tempo(0, 1000000);  // 60 BPM: 480 ticks = 1 s
        t.ev(480, {0x90, 69, 100});
        t.ev(240, {0x80, 69, 0});
        t.end(0);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 1.6);
        CHECK(rms(pcm, 0.05, 0.95) < 1e-5);
        CHECK(rms(pcm, 1.05, 1.45) > 0.005);
        CHECK(rms(pcm, 1.52, 1.59) < 1e-4);
    }
}

void test_sustain_and_notes_off() {
    if (!test_sf()) return;
    // Sustain pedal holds a released note until the pedal is lifted.
    {
        SmfTrack t;
        t.ev(0, {0xb0, 64, 127});
        t.ev(0, {0x90, 69, 100});
        t.ev(240, {0x80, 69, 0});
        t.ev(240, {0xb0, 64, 0});
        t.end(240);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 0.75);
        CHECK(rms(pcm, 0.3, 0.45) > 0.005);   // pedal held after note-off
        CHECK(rms(pcm, 0.52, 0.74) < 1e-4);  // released
    }
    // All notes off (123) releases; all sound off (120) cuts a long release.
    {
        SmfTrack t;
        t.ev(0, {0x90, 69, 100});
        t.ev(0, {0x90, 72, 100});
        t.ev(240, {0xb0, 123, 0});
        t.end(240);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 0.5);
        CHECK(rms(pcm, 0.05, 0.2) > 0.005);
        CHECK(rms(pcm, 0.28, 0.48) < 1e-4);
    }
    {
        SmfTrack t;
        t.ev(0, {0xc0, 2});  // 0.5 s release preset
        t.ev(0, {0x90, 69, 100});
        t.ev(240, {0xb0, 120, 0});
        t.end(240);
        auto s = open_bytes(build_smf({t}));
        auto pcm = render(*s, 0.5);
        CHECK(rms(pcm, 0.28, 0.48) < 1e-4);
    }
}

void test_end_tail_loop_rewind() {
    if (!test_sf()) return;
    // Release tail continues after the last event, then the stream finishes.
    {
        SmfTrack t;
        t.ev(0, {0xc0, 2});
        t.ev(0, {0x90, 69, 100});
        t.ev(240, {0x80, 69, 0});
        t.end(0);  // song length 0.25 s
        auto s = open_bytes(build_smf({t}));
        CHECK(s->info().length_frames == 11025);
        std::vector<float> buf(512 * 2);
        std::size_t music = 0;
        int guard = 0;
        while (!s->finished() && guard++ < 10000) music += s->render(buf.data(), 512);
        CHECK(s->finished());
        CHECK(music > 11025 + 4410);              // tail rang past the end
        CHECK(music < 11025 + 5 * 44100);         // and stopped (not a stuck note)
        const std::size_t again = s->render(buf.data(), 512);
        CHECK(again == 0);
        CHECK(std::all_of(buf.begin(), buf.end(), [](float v) { return v == 0.0f; }));
    }
    // A note never released: non-loop finishes within the tail limit.
    {
        SmfTrack t;
        t.ev(0, {0x90, 69, 100});
        t.end(480);
        auto s = open_bytes(build_smf({t}));
        std::vector<float> buf(4410 * 2);
        int blocks = 0;
        while (!s->finished() && blocks < 200) {
            s->render(buf.data(), 4410);
            ++blocks;
        }
        CHECK(s->finished());
        CHECK(blocks <= 60);  // 0.5 s song + at most 5 s tail
    }
    // Looping: whole-file restart, stuck notes released at the loop point,
    // voices stay bounded and sound continues pass after pass.
    {
        SmfTrack t;
        t.ev(0, {0xc0, 1});
        t.ev(0, {0x90, 69, 100});  // never released within the pass
        t.end(480);
        auto s = open_bytes(build_smf({t}));
        s->set_loop(true);
        auto pcm = render(*s, 5.25, 1000);
        CHECK(s->loops_completed() == 10);
        CHECK(!s->finished());
        CHECK_NEAR(freq(pcm, 4.55, 4.95), 882.0, 10.0);  // program re-applied each pass
        CHECK(rms(pcm, 4.55, 4.95) > 0.005);
        CHECK(rms(pcm, 4.55, 4.95) < 0.06);          // no pile-up of stuck voices
    }
    // Rewind cuts sound immediately and replays from the start.
    {
        SmfTrack t;
        t.ev(0, {0xc0, 2});
        t.ev(0, {0x90, 69, 100});
        t.ev(4800, {0x80, 69, 0});
        t.end(0);
        auto s = open_bytes(build_smf({t}));
        render(*s, 1.0);
        CHECK(s->position_frames() == 44100);
        s->rewind();
        CHECK(s->position_frames() == 0);
        auto pcm = render(*s, 0.3);
        CHECK(rms(pcm, 0.05, 0.25) > 0.005);
        CHECK(s->position_frames() == 13230);
    }
    // CC111 loop markers are counted and ignored (whole-file loop).
    {
        SmfTrack t;
        t.ev(0, {0xb0, 111, 0});
        t.ev(0, {0x90, 69, 100});
        t.ev(480, {0x80, 69, 0});
        t.end(0);
        auto s = open_bytes(build_smf({t}));
        CHECK(s->info().loop_markers == 1);
    }
}

void test_open_errors_and_player() {
    LoadError e;
    CHECK(!open_music({}, test_sf(), "empty", e) && e.code == MusicError::invalid_data);
    CHECK(!open_music({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'}, test_sf(), "wav", e) &&
          e.code == MusicError::invalid_data);
    SmfTrack t = note_track(0, 69, 0, 480);
    t.end(0);
    Bytes midi = build_smf({t});
    CHECK(!open_music(midi, nullptr, "nosf", e) && e.code == MusicError::soundfont_error);
    Bytes fmt2 = midi;
    fmt2[9] = 2;
    CHECK(!open_music(fmt2, test_sf(), "fmt2", e) && e.code == MusicError::unsupported);
    Bytes truncated = midi;
    truncated.resize(truncated.size() - 4);
    CHECK(!open_music(truncated, test_sf(), "trunc", e) && e.code == MusicError::invalid_data && !e.message.empty());
    Bytes fake_mp3 = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x12, 0x34};
    CHECK(!open_music(fake_mp3, nullptr, "fake.mp3", e) && e.code == MusicError::invalid_data);
    Bytes sync_garbage(4000, 0x55);
    sync_garbage[0] = 0xff;
    sync_garbage[1] = 0xfb;
    CHECK(!open_music(sync_garbage, nullptr, "garbage.mp3", e));
    CHECK(!SoundFont::load_memory({1, 2, 3}, e) && e.code == MusicError::soundfont_error);
    Bytes bad_sf = build_sf2();
    bad_sf.resize(200);
    CHECK(!SoundFont::load_memory(bad_sf, e));
    CHECK(!SoundFont::load_file("/nonexistent/x.sf2", e) && e.code == MusicError::io_error);

    // Player state machine.
    MusicPlayer player;
    std::vector<float> buf(1024 * 2, 1.0f);
    player.render(buf.data(), 1024);
    CHECK(player.state() == MusicPlayer::State::idle);
    CHECK(std::all_of(buf.begin(), buf.end(), [](float v) { return v == 0.0f; }));
    auto s = open_music(midi, test_sf(), "p", e);
    CHECK(s != nullptr);
    player.play(std::move(s), false);
    CHECK(player.state() == MusicPlayer::State::playing);
    player.render(buf.data(), 1024);
    CHECK(std::any_of(buf.begin(), buf.end(), [](float v) { return v != 0.0f; }));
    CHECK(player.position_frames() == 1024);
    for (int i = 0; i < 200 && player.state() == MusicPlayer::State::playing; ++i) player.render(buf.data(), 1024);
    CHECK(player.state() == MusicPlayer::State::finished);
    player.play(open_music(midi, test_sf(), "p2", e), true);
    player.set_volume(0.0f);
    player.render(buf.data(), 1024);
    CHECK(std::all_of(buf.begin(), buf.end(), [](float v) { return v == 0.0f; }));
    player.set_volume(100.0f);  // clamped output
    player.render(buf.data(), 1024);
    CHECK(std::all_of(buf.begin(), buf.end(), [](float v) { return v >= -1.0f && v <= 1.0f; }));
    for (int i = 0; i < 100; ++i) player.render(buf.data(), 1024);
    CHECK(player.state() == MusicPlayer::State::playing);  // looping never finishes
    CHECK(player.loops_completed() >= 3);
    player.stop();
    CHECK(player.state() == MusicPlayer::State::idle);
    player.render(buf.data(), 1024);
    CHECK(std::all_of(buf.begin(), buf.end(), [](float v) { return v == 0.0f; }));
}

}  // namespace

int run_game_render_tests(int argc, char** argv);

int main(int argc, char** argv) {
    test_parser_errors();
    test_timing();
    test_note_and_program();
    test_sustain_and_notes_off();
    test_end_tail_loop_rewind();
    test_open_errors_and_player();
    const int game = run_game_render_tests(argc, argv);
    std::printf("audio tests: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0 && game == 0) ? 0 : 1;
}
