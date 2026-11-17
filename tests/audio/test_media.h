#pragma once
// Synthetic test media: a tiny SoundFont 2 whose presets differ only by
// coarse tuning of one looped 441 Hz sine sample (so the active preset can be
// identified from the output frequency), and a small SMF writer.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace elona::audio_tests {

using Bytes = std::vector<std::uint8_t>;

inline void put16(Bytes& b, unsigned v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff); }
inline void put32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xff); }
inline void put_tag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }
inline void put_name(Bytes& b, const std::string& s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) b.push_back(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
}
inline Bytes chunk(const char* tag, const Bytes& body) {
    Bytes b;
    put_tag(b, tag);
    put32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1) b.push_back(0);
    return b;
}
inline Bytes list(const char* type, const std::vector<Bytes>& chunks) {
    Bytes body;
    put_tag(body, type);
    for (const auto& c : chunks) body.insert(body.end(), c.begin(), c.end());
    return chunk("LIST", body);
}

struct TestPreset {
    int bank;
    int program;
    int coarse_semitones;   // pitch offset relative to the 441 Hz root (key 69)
    int release_timecents;  // releaseVolEnv; -12000 ~ 1 ms
};

// Presets: (0,0) +0, (0,1) +12, (8,0) +7, (0,2) +0 with 0.5 s release,
// (128,0) +24 drum kit, (128,8) +19 second kit.
inline std::vector<TestPreset> default_presets() {
    return {{0, 0, 0, -12000}, {0, 1, 12, -12000}, {8, 0, 7, -12000}, {0, 2, 0, -1200}, {128, 0, 24, -12000},
            {128, 8, 19, -12000}};
}

inline Bytes build_sf2(const std::vector<TestPreset>& presets = default_presets()) {
    // Sample: 4400 frames of a 441 Hz sine at 44100 Hz (exactly 44 periods), looped.
    const int frames = 4400;
    Bytes smpl;
    for (int i = 0; i < frames; ++i) {
        const double v = std::sin(2.0 * 3.14159265358979323846 * 441.0 * i / 44100.0) * 0.5;
        put16(smpl, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(v * 32767))));
    }
    for (int i = 0; i < 46; ++i) put16(smpl, 0);

    Bytes ifil, isng, inam;
    put16(ifil, 2); put16(ifil, 1);
    put_name(isng, "EMU8000", 8);
    put_name(inam, "elona-test", 12);
    const Bytes info = list("INFO", {chunk("ifil", ifil), chunk("isng", isng), chunk("INAM", inam)});
    const Bytes sdta = list("sdta", {chunk("smpl", smpl)});

    Bytes phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
    unsigned pgen_index = 0, igen_index = 0;
    for (std::size_t i = 0; i < presets.size(); ++i) {
        const TestPreset& p = presets[i];
        put_name(phdr, "p" + std::to_string(i), 20);
        put16(phdr, static_cast<unsigned>(p.program));
        put16(phdr, static_cast<unsigned>(p.bank));
        put16(phdr, static_cast<unsigned>(i));  // pbag index
        put32(phdr, 0); put32(phdr, 0); put32(phdr, 0);
        put16(pbag, pgen_index); put16(pbag, 0);
        put16(pgen, 41); put16(pgen, static_cast<unsigned>(i));  // instrument
        ++pgen_index;
        put_name(inst, "i" + std::to_string(i), 20);
        put16(inst, static_cast<unsigned>(i));
        put16(ibag, igen_index); put16(ibag, 0);
        put16(igen, 51); put16(igen, static_cast<std::uint16_t>(static_cast<std::int16_t>(p.coarse_semitones)));
        put16(igen, 38); put16(igen, static_cast<std::uint16_t>(static_cast<std::int16_t>(p.release_timecents)));
        put16(igen, 54); put16(igen, 1);   // loop continuously
        put16(igen, 53); put16(igen, 0);   // sampleID (last)
        igen_index += 4;
    }
    put_name(phdr, "EOP", 20); put16(phdr, 0); put16(phdr, 0); put16(phdr, static_cast<unsigned>(presets.size()));
    put32(phdr, 0); put32(phdr, 0); put32(phdr, 0);
    put16(pbag, pgen_index); put16(pbag, 0);
    for (int i = 0; i < 10; ++i) pmod.push_back(0);
    put16(pgen, 0); put16(pgen, 0);
    put_name(inst, "EOI", 20); put16(inst, static_cast<unsigned>(presets.size()));
    put16(ibag, igen_index); put16(ibag, 0);
    for (int i = 0; i < 10; ++i) imod.push_back(0);
    put16(igen, 0); put16(igen, 0);
    put_name(shdr, "sine441", 20);
    put32(shdr, 0); put32(shdr, frames); put32(shdr, 0); put32(shdr, frames); put32(shdr, 44100);
    shdr.push_back(69); shdr.push_back(0); put16(shdr, 0); put16(shdr, 1);
    put_name(shdr, "EOS", 20);
    for (int i = 0; i < 26; ++i) shdr.push_back(0);
    const Bytes pdta = list("pdta", {chunk("phdr", phdr), chunk("pbag", pbag), chunk("pmod", pmod), chunk("pgen", pgen),
                                     chunk("inst", inst), chunk("ibag", ibag), chunk("imod", imod), chunk("igen", igen),
                                     chunk("shdr", shdr)});
    Bytes body;
    put_tag(body, "sfbk");
    body.insert(body.end(), info.begin(), info.end());
    body.insert(body.end(), sdta.begin(), sdta.end());
    body.insert(body.end(), pdta.begin(), pdta.end());
    return chunk("RIFF", body);
}

// ---- SMF writer ------------------------------------------------------------
struct SmfTrack {
    Bytes data;
    void delta(std::uint32_t d) {
        std::uint8_t buf[5];
        int n = 0;
        buf[n++] = d & 0x7f;
        while (d >>= 7) buf[n++] = static_cast<std::uint8_t>((d & 0x7f) | 0x80);
        while (n) data.push_back(buf[--n]);
    }
    void ev(std::uint32_t d, std::initializer_list<int> bytes) {
        delta(d);
        for (int b : bytes) data.push_back(static_cast<std::uint8_t>(b));
    }
    void tempo(std::uint32_t d, std::uint32_t us) { ev(d, {0xff, 0x51, 3, int(us >> 16), int((us >> 8) & 0xff), int(us & 0xff)}); }
    void sysex(std::uint32_t d, const Bytes& payload) {  // payload without F0, including F7
        delta(d);
        data.push_back(0xf0);
        delta(static_cast<std::uint32_t>(payload.size()));
        data.insert(data.end(), payload.begin(), payload.end());
    }
    void end(std::uint32_t d) { ev(d, {0xff, 0x2f, 0}); }
};

inline Bytes build_smf(const std::vector<SmfTrack>& tracks, int division = 480, int format = -1) {
    Bytes b;
    auto be32 = [&](std::uint32_t v) { for (int i = 3; i >= 0; --i) b.push_back((v >> (8 * i)) & 0xff); };
    auto be16 = [&](unsigned v) { b.push_back((v >> 8) & 0xff); b.push_back(v & 0xff); };
    b.insert(b.end(), {'M', 'T', 'h', 'd'});
    be32(6);
    be16(format >= 0 ? static_cast<unsigned>(format) : (tracks.size() == 1 ? 0u : 1u));
    be16(static_cast<unsigned>(tracks.size()));
    be16(static_cast<unsigned>(division & 0xffff));
    for (const auto& t : tracks) {
        b.insert(b.end(), {'M', 'T', 'r', 'k'});
        be32(static_cast<std::uint32_t>(t.data.size()));
        b.insert(b.end(), t.data.begin(), t.data.end());
    }
    return b;
}

}  // namespace elona::audio_tests
