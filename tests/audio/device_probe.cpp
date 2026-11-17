// Runs against the actual AudioOut + MusicPlayer used by elona_gui_host.
// Evidence is PCM requested by the SDL playback thread, not loopback capture
// or a claim that a person heard the speakers. No game window/input is needed.
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "audio/music.h"
#include "platform/audio_out.h"
#include "test_media.h"

using namespace elona;
using namespace elona::audio_tests;

namespace {
struct BlockStats { std::uint64_t calls = 0, frames = 0, nonzero = 0, invalid = 0; float peak = 0; };
Bytes read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + p.u8string());
    return Bytes(std::istreambuf_iterator<char>(f), {});
}
Bytes wav() {
    Bytes fmt, pcm, body;
    put16(fmt, 1); put16(fmt, 1); put32(fmt, 44100); put32(fmt, 88200); put16(fmt, 2); put16(fmt, 16);
    for (int i = 0; i < 8820; ++i) put16(pcm, std::uint16_t(std::int16_t(std::sin(i * 0.03) * 150)));
    put_tag(body, "WAVE");
    for (const auto& c : {chunk("fmt ", fmt), chunk("data", pcm)}) body.insert(body.end(), c.begin(), c.end());
    return chunk("RIFF", body);
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: elona_audio_device_probe <game-root> <report.json>\n";
        return 2;
    }
    try {
        const std::filesystem::path game = std::filesystem::u8path(argv[1]);
        audio::LoadError le;
        auto sf = audio::SoundFont::load_file(ELONA_AUDIO_DEFAULT_SOUNDFONT, le);
        if (!sf) throw std::runtime_error(le.message);
        auto tiny = audio::SoundFont::load_memory(build_sf2(), le);
        if (!tiny) throw std::runtime_error(le.message);
        SmfTrack track;
        track.ev(0, {0x90, 69, 70}); track.ev(96, {0x80, 69, 0}); track.end(0);
        const Bytes short_midi = build_smf({track});
        const Bytes game_midi = read(game / "sound/battle1.mid");
        const Bytes game_mp3 = read(game / "sound/oo39.mp3");
        audio::MusicPlayer player;
        player.set_volume(0.15f);
        // Device must die before player/observations captured by its callback.
        std::array<BlockStats, 10> stats{};
        std::mutex stats_mutex;
        std::atomic<int> phase{0};
        std::string error;
        auto device = platform::create_sdl_audio(error);
        if (!device) throw std::runtime_error(error);
        const std::string driver = SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "";
        if (driver.empty() || driver == "dummy" || driver == "disk") throw std::runtime_error("not a real audio driver: " + driver);
        if (!device->start_music_output([&](float* out, std::size_t frames) {
            const int p = phase.load();
            player.render(out, frames);
            BlockStats b;
            b.calls = 1; b.frames = frames;
            for (std::size_t i = 0; i < frames * 2; ++i) {
                if (!std::isfinite(out[i])) ++b.invalid;
                else { b.peak = std::max(b.peak, std::abs(out[i])); b.nonzero += std::abs(out[i]) > 1e-8f; }
            }
            std::lock_guard<std::mutex> lock(stats_mutex);
            auto& s = stats[p];
            s.calls += b.calls; s.frames += b.frames; s.nonzero += b.nonzero; s.invalid += b.invalid;
            s.peak = std::max(s.peak, b.peak);
        }, error)) throw std::runtime_error(error);
        auto wait = [&](int ms) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            while (std::chrono::steady_clock::now() < end) {
                device->update();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
        auto play = [&](const Bytes& b, std::shared_ptr<const audio::SoundFont> font, bool loop) {
            auto s = audio::open_music(b, std::move(font), "device-probe", le);
            if (!s) throw std::runtime_error(le.message);
            player.play(std::move(s), loop);
        };
        wait(200);  // idle output is silence
        phase = 1; play(short_midi, tiny, false); wait(650);
        const bool ended = player.state() == audio::MusicPlayer::State::finished;
        phase = 2; play(short_midi, tiny, true); wait(1100);
        const auto loops = player.loops_completed();
        // battle1.mid begins with setup events and over a second of silence.
        phase = 3; play(game_midi, sf, false); wait(2200);
        const auto music_before_wav = player.position_frames();
        phase = 4;
        if (!device->play_wav(wav(), true, error)) throw std::runtime_error(error);
        wait(650);
        const auto music_during_wav = player.position_frames();
        device->stop();  // WAV stop must leave music running
        phase = 5; wait(300);
        const auto music_after_wav = player.position_frames();
        phase = 6; play(game_mp3, nullptr, false); wait(1600);
        const auto mp3_position = player.position_frames();
        player.stop(); phase = 7; wait(200);
        phase = 8; play(game_mp3, nullptr, false); wait(1600);
        const auto replay_position = player.position_frames();
        player.stop(); phase = 9; wait(200);
        device.reset();  // callback stopped before inspecting plain counters

        bool ok = ended && loops >= 2 && music_during_wav > music_before_wav && music_after_wav > music_during_wav;
        for (int i = 0; i < 10; ++i) {
            const bool silent = i == 0 || i == 7 || i == 9;
            ok &= stats[i].calls > 0 && stats[i].invalid == 0 && (silent ? stats[i].nonzero == 0 : stats[i].nonzero > 100);
        }
        ok &= mp3_position > 44100 && replay_position > 44100 && replay_position < 88200;
        const std::array<const char*, 10> names{"idle", "midi_one_shot", "midi_loop", "game_midi", "wav_plus_music",
            "music_after_wav_stop", "replace_with_mp3", "stop", "replay_mp3", "stop_again"};
        std::ofstream report(std::filesystem::u8path(argv[2]));
        report << "{\n  \"driver\": \"" << driver << "\",\n  \"evidence\": \"actual SDL playback-thread requests and submitted PCM; no speaker/loopback capture\",\n";
        report << "  \"midi_finished\": " << (ended ? "true" : "false") << ", \"midi_loops\": " << loops << ",\n";
        report << "  \"music_positions_before_during_after_wav\": [" << music_before_wav << ',' << music_during_wav << ',' << music_after_wav << "],\n";
        report << "  \"mp3_positions_first_replay\": [" << mp3_position << ',' << replay_position << "],\n  \"phases\": [\n";
        for (int i = 0; i < 10; ++i) {
            const auto& s = stats[i];
            report << "    {\"name\":\"" << names[i] << "\",\"callbacks\":" << s.calls << ",\"frames\":" << s.frames
                   << ",\"nonzero_samples\":" << s.nonzero << ",\"invalid_samples\":" << s.invalid << ",\"peak\":" << s.peak << '}' << (i == 9 ? "\n" : ",\n");
        }
        report << "  ],\n  \"passed\": " << (ok ? "true" : "false") << "\n}\n";
        if (!report) throw std::runtime_error("cannot write report");
        std::cout << driver << " device integration " << (ok ? "PASS" : "FAIL") << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "audio device probe: " << e.what() << '\n';
        return 2;
    }
}
