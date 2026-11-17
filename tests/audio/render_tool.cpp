// Offline renderer: MIDI/MP3 -> 16-bit stereo 44100 Hz WAV (listening checks
// and Windows offline validation; does not open an audio device).
//   elona_music_render INPUT OUTPUT.wav [--soundfont SF2] [--seconds N] [--loop]
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/music.h"

using namespace elona::audio;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s INPUT OUTPUT.wav [--soundfont SF2] [--seconds N] [--loop]\n", argv[0]);
        return 2;
    }
    std::string soundfont = ELONA_AUDIO_DEFAULT_SOUNDFONT;
    double seconds = -1;
    bool loop = false;
    for (int i = 3; i < argc; ++i) {
        const std::string k = argv[i];
        if (k == "--soundfont" && i + 1 < argc) soundfont = argv[++i];
        else if (k == "--seconds" && i + 1 < argc) seconds = std::stod(argv[++i]);
        else if (k == "--loop") loop = true;
        else {
            std::fprintf(stderr, "unknown option %s\n", k.c_str());
            return 2;
        }
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    LoadError err;
    std::shared_ptr<const SoundFont> sf;
    if (bytes.size() >= 4 && (std::string(bytes.begin(), bytes.begin() + 4) == "MThd" ||
                              std::string(bytes.begin(), bytes.begin() + 4) == "RIFF")) {
        sf = SoundFont::load_file(soundfont, err);
        if (!sf) {
            std::fprintf(stderr, "%s: %s\n", music_error_name(err.code), err.message.c_str());
            return 1;
        }
    }
    auto stream = open_music(std::move(bytes), sf, argv[1], err);
    if (!stream) {
        std::fprintf(stderr, "%s: %s\n", music_error_name(err.code), err.message.c_str());
        return 1;
    }
    stream->set_loop(loop);
    if (loop && seconds < 0) seconds = 2.0 * stream->info().length_frames / kSampleRate + 1.0;
    const std::uint64_t limit = seconds < 0 ? ~0ull : static_cast<std::uint64_t>(seconds * kSampleRate);
    std::vector<std::int16_t> pcm;
    std::vector<float> buf(4096 * 2);
    std::uint64_t frames = 0;
    double peak = 0;
    while (frames < limit && !stream->finished()) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(4096, limit - frames));
        const std::size_t n = stream->render(buf.data(), want);
        for (std::size_t i = 0; i < n * 2; ++i) {
            const float v = std::fmax(-1.0f, std::fmin(1.0f, buf[i]));
            peak = std::fmax(peak, std::fabs(buf[i]));
            pcm.push_back(static_cast<std::int16_t>(std::lrint(v * 32767.0f)));
        }
        frames += n;
    }
    std::ofstream out(argv[2], std::ios::binary);
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(pcm.size() * 2);
    out.write("RIFF", 4); u32(36 + data_bytes); out.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
    u32(kSampleRate); u32(kSampleRate * 4); u16(4); u16(16); out.write("data", 4); u32(data_bytes);
    out.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(data_bytes));
    std::printf("%s: %s %.2f s (length %.2f s, loops %u) peak %.3f\n", argv[2],
                stream->info().kind == MusicKind::midi ? "midi" : "mp3", frames / double(kSampleRate),
                stream->info().length_frames / double(kSampleRate), stream->loops_completed(), peak);
    return out ? 0 : 1;
}
