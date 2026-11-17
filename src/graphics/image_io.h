#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hsp_screen.h"

namespace elona::gfx {

// Byte layout identical to Win32 Bmscr::BmpSave for a 24-bit screen:
// BITMAPFILEHEADER(14) + BITMAPINFOHEADER(40) + bottom-up padded BGR rows.
std::vector<std::uint8_t> encode_bmp(const Dib& dib);
bool save_bmp(const Dib& dib, const std::filesystem::path& path, std::string* error = nullptr);

struct DecodedImage {
    int width = 0;
    int height = 0;
    int components = 0;               // components present in the file (stb_image)
    std::vector<std::uint8_t> rgba;   // always 4 bytes per pixel, top-down
};

// Decode BMP/PNG/JPEG/GIF/TGA/PSD bytes. Returns false with a reason; never
// substitutes an empty image.
bool decode_image(const std::vector<std::uint8_t>& bytes, DecodedImage& out, std::string& error);

}  // namespace elona::gfx
