// Offline render of every 2.335 music file (read-only) with the pinned
// SoundFont. Skipped (reported) when the original folder or SoundFont is
// absent, e.g. on CI. Usage:
//   elona_audio_tests [--game-dir DIR] [--soundfont SF2] [--report out.json]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "audio/music.h"

namespace fs = std::filesystem;
using namespace elona::audio;

extern int g_checks;
extern int g_failures;

namespace {

struct Result {
    std::string name;
    std::string kind;
    bool opened = false;
    std::string error;
    double length_s = 0;
    double rendered_s = 0;
    double peak = 0;
    double rms = 0;
    bool finished = false;
    bool finite = true;
    std::uint32_t notes = 0;
    std::uint32_t loop_markers = 0;
    std::uint32_t ignored_sysex = 0;
};

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::uint8_t> read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "game render: %s\n", what.c_str());
    }
}

}  // namespace

int run_game_render_tests(int argc, char** argv) {
    fs::path game_dir;
    fs::path soundfont = ELONA_AUDIO_DEFAULT_SOUNDFONT;
    fs::path report;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        if (k == "--game-dir") game_dir = argv[i + 1];
        else if (k == "--soundfont") soundfont = argv[i + 1];
        else if (k == "--report") report = argv[i + 1];
    }
    if (game_dir.empty()) {
        std::printf("SKIP game render: pass --game-dir <elonaplus2.335> to render the original music\n");
        return 0;
    }
    LoadError err;
    const auto t0 = std::chrono::steady_clock::now();
    auto sf = SoundFont::load_file(soundfont, err);
    check(sf != nullptr, "SoundFont: " + err.message);
    if (!sf) return 1;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(game_dir / "sound")) {
        const std::string ext = lower(e.path().extension().string());
        if (ext == ".mid" || ext == ".mp3") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<Result> results;
    std::vector<float> buf(4096 * 2);
    int midi = 0, mp3 = 0;
    for (const auto& path : files) {
        Result r;
        r.name = path.filename().string();
        auto stream = open_music(read(path), sf, r.name, err);
        r.opened = stream != nullptr;
        if (!stream) {
            r.error = err.message;
            results.push_back(r);
            check(false, r.name + ": " + err.message);
            continue;
        }
        const MusicInfo& info = stream->info();
        r.kind = info.kind == MusicKind::midi ? "midi" : "mp3";
        (info.kind == MusicKind::midi ? midi : mp3)++;
        r.length_s = static_cast<double>(info.length_frames) / kSampleRate;
        r.notes = info.notes;
        r.loop_markers = info.loop_markers;
        r.ignored_sysex = info.ignored_sysex;
        double sum = 0;
        std::uint64_t frames = 0;
        const std::uint64_t cap = info.length_frames + 30ull * kSampleRate;
        while (!stream->finished() && frames < cap) {
            const std::size_t n = stream->render(buf.data(), 4096);
            for (std::size_t i = 0; i < n * 2; ++i) {
                const double v = buf[i];
                if (!std::isfinite(v)) r.finite = false;
                r.peak = std::max(r.peak, std::fabs(v));
                sum += v * v;
            }
            frames += n;
        }
        r.finished = stream->finished();
        r.rendered_s = static_cast<double>(frames) / kSampleRate;
        r.rms = std::sqrt(sum / std::max<std::uint64_t>(1, frames * 2));
        check(r.finite, r.name + ": non-finite samples");
        check(r.finished, r.name + ": did not finish within length + 30 s");
        // gm_on.mid is a GM-reset-only file (no notes) that the game sends before
        // DirectMusic playback; it is expected to be silent.
        if (r.kind == "mp3" || r.notes > 0) check(r.rms > 1e-4, r.name + ": silent render");
        else check(r.peak == 0.0, r.name + ": note-less file produced sound");
        check(r.rendered_s + 1e-6 >= r.length_s, r.name + ": rendered less than its length");
        results.push_back(r);
    }
    check(midi == 83, "expected 83 MIDI files, found " + std::to_string(midi));
    check(mp3 == 1, "expected 1 MP3 file, found " + std::to_string(mp3));
    // Loop check on the shortest MIDI and the MP3: playing 1.25x length with
    // looping wraps exactly once and keeps producing sound.
    for (const char* kind : {"midi", "mp3"}) {
        const Result* best = nullptr;
        for (const auto& r : results)
            if (r.opened && r.kind == kind && (kind == std::string("mp3") || r.notes > 0) &&
                (!best || r.length_s < best->length_s))
                best = &r;
        if (!best) continue;
        auto stream = open_music(read(game_dir / "sound" / best->name), sf, best->name, err);
        stream->set_loop(true);
        const std::uint64_t total = static_cast<std::uint64_t>(stream->info().length_frames * 1.25);
        double late = 0;
        for (std::uint64_t f = 0; f < total; f += 4096) {
            stream->render(buf.data(), 4096);
            if (f > stream->info().length_frames + kSampleRate)
                for (float v : buf) late += v * v;
        }
        check(stream->loops_completed() == 1 && !stream->finished(), std::string("loop of ") + best->name);
        check(late > 0, std::string("sound after loop of ") + best->name);
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double total_audio = 0, max_peak = 0;
    for (const auto& r : results) {
        total_audio += r.rendered_s;
        max_peak = std::max(max_peak, r.peak);
    }
    std::printf("game render: %zu files (%d midi, %d mp3), %.0f s of audio in %.1f s, max peak %.3f\n", results.size(),
                midi, mp3, total_audio, elapsed, max_peak);
    if (!report.empty()) {
        std::ofstream out(report);
        out << "{\n  \"soundfont\": \"" << soundfont.filename().string() << "\",\n  \"sample_rate\": " << kSampleRate
            << ",\n  \"elapsed_s\": " << elapsed << ",\n  \"files\": [\n";
        for (std::size_t i = 0; i < results.size(); ++i) {
            const Result& r = results[i];
            out << "    {\"name\": \"" << r.name << "\", \"kind\": \"" << r.kind << "\", \"opened\": " << (r.opened ? "true" : "false")
                << ", \"length_s\": " << r.length_s << ", \"rendered_s\": " << r.rendered_s << ", \"peak\": " << r.peak
                << ", \"rms\": " << r.rms << ", \"finished\": " << (r.finished ? "true" : "false")
                << ", \"notes\": " << r.notes << ", \"loop_markers\": " << r.loop_markers
                << ", \"ignored_sysex\": " << r.ignored_sysex << "}" << (i + 1 < results.size() ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
    }
    return 0;
}
