#include "image_io.h"

#include <cstdio>
#include <fstream>

// stb_image v2.23 is vendored in the pinned OpenHSP checkout (public domain).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_BMP
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_ONLY_PSD
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace elona::gfx {
namespace {
void put16(std::vector<std::uint8_t>& v, std::size_t o, std::uint32_t x) {
    v[o] = std::uint8_t(x);
    v[o + 1] = std::uint8_t(x >> 8);
}
void put32(std::vector<std::uint8_t>& v, std::size_t o, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v[o + i] = std::uint8_t(x >> (8 * i));
}
}  // namespace

std::vector<std::uint8_t> encode_bmp(const Dib& dib) {
    const std::uint32_t infsize = 40, bsize = std::uint32_t(dib.byte_size());
    std::vector<std::uint8_t> out(14 + infsize + bsize, 0);
    put16(out, 0, 0x4d42);
    put32(out, 2, 14 + infsize + bsize);
    put32(out, 10, 14 + infsize);
    put32(out, 14, 40);
    put32(out, 18, std::uint32_t(dib.width()));
    put32(out, 22, std::uint32_t(dib.height()));
    put16(out, 26, 1);
    put16(out, 28, 24);
    // biCompression=BI_RGB, biSizeImage=0, resolution/colour counts 0 like the
    // GPTR-zeroed BITMAPINFOHEADER of Bmscr::Init.
    std::copy(dib.data(), dib.data() + bsize, out.begin() + 14 + infsize);
    return out;
}

bool save_bmp(const Dib& dib, const std::filesystem::path& path, std::string* error) {
    auto bytes = encode_bmp(dib);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "cannot open for writing: " + path.u8string();
        return false;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!f) {
        if (error) *error = "write failed: " + path.u8string();
        return false;
    }
    return true;
}

bool decode_image(const std::vector<std::uint8_t>& bytes, DecodedImage& out, std::string& error) {
    int w = 0, h = 0, comp = 0;
    if (bytes.empty() || bytes.size() > 0x7fffffff) {
        error = "empty or oversized image";
        return false;
    }
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp, 4);
    if (!pixels) {
        error = std::string("image decode failed: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return false;
    }
    out.width = w;
    out.height = h;
    out.components = comp;
    out.rgba.assign(pixels, pixels + std::size_t(w) * h * 4);
    stbi_image_free(pixels);
    return true;
}

}  // namespace elona::gfx
