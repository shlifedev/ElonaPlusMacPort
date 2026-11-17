// gzio 1.1.4-equivalent gzip stream for the z.hpi replacement.
//
// The control flow below follows zlib 1.1.4 gzio.c (Copyright (C) 1995-2002
// Jean-loup Gailly, zlib license; see src/plugins/THIRD_PARTY.md) so that the
// observable stat/data contract of the original z.hpi is preserved, while the
// actual deflate/inflate comes from the current zlib linked by the build.
#include "zgzip.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <system_error>
#include <vector>

#include <zlib.h>

namespace elona::plugins {
namespace {

constexpr unsigned kBufSize = 16384;      // Z_BUFSIZE in gzio 1.1.4
constexpr int kMaxWBits = 15;            // MAX_WBITS
constexpr int kDefMemLevel = 8;          // DEF_MEM_LEVEL
constexpr unsigned char kOsCode = 0x0b;  // OS_CODE for WIN32 in zlib 1.1.4
constexpr int kEof = -1;

constexpr int kHeadCrc = 0x02;
constexpr int kExtraField = 0x04;
constexpr int kOrigName = 0x08;
constexpr int kComment = 0x10;
constexpr int kReserved = 0xE0;

std::FILE* open_file(const std::filesystem::path& path, bool write) {
    std::error_code ec;
    // MSVC fopen fails on a directory; POSIX fopen("rb") succeeds and fails
    // later in fread. Match the original Windows result.
    if (std::filesystem::is_directory(path, ec)) return nullptr;
#ifdef _WIN32
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), write ? L"wb" : L"rb") != 0) return nullptr;
    return file;
#else
    return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}

}  // namespace

class GzipStream {
public:
    ~GzipStream() {
        // Abandoning a stream mirrors process exit in the original: the CRT
        // flushes and closes FILE, pending deflate output and trailer are lost.
        if (stream_ready_) {
            if (mode_ == 'w') deflateEnd(&stream_); else inflateEnd(&stream_);
        }
        if (file_ != nullptr) std::fclose(file_);
    }

    char mode() const noexcept { return mode_; }

    // gz_open. Returns false where the original returns NULL.
    bool open(const std::filesystem::path& path, char mode, int level, int strategy) {
        mode_ = mode;
        crc_ = crc32(0L, Z_NULL, 0);
        std::memset(&stream_, 0, sizeof(stream_));
        if (mode_ == 'w') {
            if (deflateInit2(&stream_, level, Z_DEFLATED, -kMaxWBits, kDefMemLevel, strategy) != Z_OK) return false;
            stream_ready_ = true;
            outbuf_.resize(kBufSize);
            stream_.next_out = outbuf_.data();
        } else {
            inbuf_.resize(kBufSize);
            stream_.next_in = inbuf_.data();
            if (inflateInit2(&stream_, -kMaxWBits) != Z_OK) return false;
            stream_ready_ = true;
        }
        stream_.avail_out = kBufSize;
        file_ = open_file(path, mode_ == 'w');
        if (file_ == nullptr) return false;
        if (mode_ == 'w') {
            const unsigned char header[10] = {0x1f, 0x8b, Z_DEFLATED, 0, 0, 0, 0, 0, 0, kOsCode};
            std::fwrite(header, 1, sizeof(header), file_);  // fprintf result ignored by gzio
        } else {
            check_header();
        }
        return true;
    }

    // gzread
    int read(unsigned char* buf, unsigned len) {
        if (mode_ != 'r') return Z_STREAM_ERROR;
        if (z_err_ == Z_DATA_ERROR || z_err_ == Z_ERRNO) return -1;
        if (z_err_ == Z_STREAM_END) return 0;

        unsigned char* start = buf;
        unsigned char* next_out = buf;
        stream_.next_out = buf;
        stream_.avail_out = len;

        while (stream_.avail_out != 0) {
            if (transparent_) {
                unsigned n = stream_.avail_in;
                if (n > stream_.avail_out) n = stream_.avail_out;
                if (n > 0) {
                    std::memcpy(stream_.next_out, stream_.next_in, n);
                    next_out += n;
                    stream_.next_out = next_out;
                    stream_.next_in += n;
                    stream_.avail_out -= n;
                    stream_.avail_in -= n;
                }
                if (stream_.avail_out > 0) {
                    stream_.avail_out -= static_cast<unsigned>(std::fread(next_out, 1, stream_.avail_out, file_));
                }
                len -= stream_.avail_out;
                if (len == 0) z_eof_ = true;
                return static_cast<int>(len);
            }
            if (stream_.avail_in == 0 && !z_eof_) {
                stream_.avail_in = static_cast<unsigned>(std::fread(inbuf_.data(), 1, kBufSize, file_));
                if (stream_.avail_in == 0) {
                    z_eof_ = true;
                    if (std::ferror(file_)) {
                        z_err_ = Z_ERRNO;
                        break;
                    }
                }
                stream_.next_in = inbuf_.data();
            }
            z_err_ = inflate(&stream_, Z_NO_FLUSH);

            if (z_err_ == Z_STREAM_END) {
                crc_ = crc32(crc_, start, static_cast<uInt>(stream_.next_out - start));
                start = stream_.next_out;
                if (get_long() != crc_) {
                    z_err_ = Z_DATA_ERROR;
                } else {
                    (void)get_long();
                    check_header();  // concatenated members
                    if (z_err_ == Z_OK) {
                        const uLong total_in = stream_.total_in;
                        const uLong total_out = stream_.total_out;
                        inflateReset(&stream_);
                        stream_.total_in = total_in;
                        stream_.total_out = total_out;
                        crc_ = crc32(0L, Z_NULL, 0);
                    }
                }
            }
            if (z_err_ != Z_OK || z_eof_) break;
        }
        crc_ = crc32(crc_, start, static_cast<uInt>(stream_.next_out - start));
        return static_cast<int>(len - stream_.avail_out);
    }

    // gzwrite
    int write(const unsigned char* buf, unsigned len) {
        if (mode_ != 'w') return Z_STREAM_ERROR;
        stream_.next_in = const_cast<Bytef*>(buf);
        stream_.avail_in = len;
        while (stream_.avail_in != 0) {
            if (stream_.avail_out == 0) {
                stream_.next_out = outbuf_.data();
                if (std::fwrite(outbuf_.data(), 1, kBufSize, file_) != kBufSize) {
                    z_err_ = Z_ERRNO;
                    break;
                }
                stream_.avail_out = kBufSize;
            }
            z_err_ = deflate(&stream_, Z_NO_FLUSH);
            if (z_err_ != Z_OK) break;
        }
        crc_ = crc32(crc_, buf, len);
        return static_cast<int>(len - stream_.avail_in);
    }

    // gzclose. The stream object is destroyed by the caller afterwards.
    int close() {
        if (mode_ == 'w') {
            const int err = do_flush(Z_FINISH);
            if (err != Z_OK) return destroy();
            put_long(crc_);
            put_long(static_cast<uLong>(stream_.total_in));
        }
        return destroy();
    }

private:
    int get_byte() {
        if (z_eof_) return kEof;
        if (stream_.avail_in == 0) {
            stream_.avail_in = static_cast<unsigned>(std::fread(inbuf_.data(), 1, kBufSize, file_));
            if (stream_.avail_in == 0) {
                z_eof_ = true;
                if (std::ferror(file_)) z_err_ = Z_ERRNO;
                return kEof;
            }
            stream_.next_in = inbuf_.data();
        }
        stream_.avail_in--;
        return *(stream_.next_in)++;
    }

    void check_header() {
        static const int magic[2] = {0x1f, 0x8b};
        for (unsigned len = 0; len < 2; ++len) {
            const int c = get_byte();
            if (c != magic[len]) {
                if (len != 0) { stream_.avail_in++; stream_.next_in--; }
                if (c != kEof) {
                    stream_.avail_in++;
                    stream_.next_in--;
                    transparent_ = true;
                }
                z_err_ = stream_.avail_in != 0 ? Z_OK : Z_STREAM_END;
                return;
            }
        }
        const int method = get_byte();
        const int flags = get_byte();
        if (method != Z_DEFLATED || (flags & kReserved) != 0) {
            z_err_ = Z_DATA_ERROR;
            return;
        }
        for (unsigned len = 0; len < 6; ++len) (void)get_byte();
        if ((flags & kExtraField) != 0) {
            unsigned len = static_cast<unsigned>(get_byte());
            len += static_cast<unsigned>(get_byte()) << 8;
            while (len-- != 0 && get_byte() != kEof) {}
        }
        if ((flags & kOrigName) != 0) {
            int c;
            while ((c = get_byte()) != 0 && c != kEof) {}
        }
        if ((flags & kComment) != 0) {
            int c;
            while ((c = get_byte()) != 0 && c != kEof) {}
        }
        if ((flags & kHeadCrc) != 0) {
            for (unsigned len = 0; len < 2; ++len) (void)get_byte();
        }
        z_err_ = z_eof_ ? Z_DATA_ERROR : Z_OK;
    }

    // 32-bit little-endian; gzio's uLong is 32-bit on Win32.
    std::uint32_t get_long() {
        std::uint32_t x = static_cast<std::uint32_t>(get_byte());
        x += static_cast<std::uint32_t>(get_byte()) << 8;
        x += static_cast<std::uint32_t>(get_byte()) << 16;
        const int c = get_byte();
        if (c == kEof) z_err_ = Z_DATA_ERROR;
        x += static_cast<std::uint32_t>(c) << 24;
        return x;
    }

    void put_long(uLong value) {
        const std::uint32_t x = static_cast<std::uint32_t>(value);
        const unsigned char bytes[4] = {static_cast<unsigned char>(x), static_cast<unsigned char>(x >> 8),
                                        static_cast<unsigned char>(x >> 16), static_cast<unsigned char>(x >> 24)};
        for (unsigned char b : bytes) std::fputc(b, file_);  // fputc results ignored by gzio
    }

    int do_flush(int flush) {
        bool done = false;
        stream_.avail_in = 0;
        for (;;) {
            const unsigned len = kBufSize - stream_.avail_out;
            if (len != 0) {
                if (std::fwrite(outbuf_.data(), 1, len, file_) != len) {
                    z_err_ = Z_ERRNO;
                    return Z_ERRNO;
                }
                stream_.next_out = outbuf_.data();
                stream_.avail_out = kBufSize;
            }
            if (done) break;
            z_err_ = deflate(&stream_, flush);
            if (len == 0 && z_err_ == Z_BUF_ERROR) z_err_ = Z_OK;
            done = (stream_.avail_out != 0 || z_err_ == Z_STREAM_END);
            if (z_err_ != Z_OK && z_err_ != Z_STREAM_END) break;
        }
        return z_err_ == Z_STREAM_END ? Z_OK : z_err_;
    }

    int destroy() {
        int err = Z_OK;
        if (stream_ready_) {
            err = mode_ == 'w' ? deflateEnd(&stream_) : inflateEnd(&stream_);
            stream_ready_ = false;
        }
        if (file_ != nullptr) {
            if (std::fclose(file_) != 0) err = Z_ERRNO;
            file_ = nullptr;
        }
        if (z_err_ < 0) err = z_err_;
        return err;
    }

    z_stream stream_{};
    bool stream_ready_ = false;
    int z_err_ = Z_OK;
    bool z_eof_ = false;
    std::FILE* file_ = nullptr;
    std::vector<unsigned char> inbuf_;
    std::vector<unsigned char> outbuf_;
    uLong crc_ = 0;
    bool transparent_ = false;
    char mode_ = '\0';
};

GzipRegistry::GzipRegistry() = default;
GzipRegistry::~GzipRegistry() { close_all(); }

PluginResult GzipRegistry::open(std::int32_t& out_handle, const std::filesystem::path& resolved_path,
                                std::int32_t mode, std::int32_t level) {
    const char mode_char = mode != 0 ? 'w' : 'r';
    const char level_char = static_cast<char>(static_cast<unsigned char>((static_cast<std::uint32_t>(level) & 0xffu) + 0x30u));
    int zlevel = Z_DEFAULT_COMPRESSION;
    int strategy = Z_DEFAULT_STRATEGY;
    if (level_char >= '0' && level_char <= '9') {
        zlevel = level_char - '0';
    } else if (level_char == 'f') {
        strategy = Z_FILTERED;
    } else if (level_char == 'h') {
        strategy = Z_HUFFMAN_ONLY;
    } else if (level_char != '\0') {
        // The character would be copied into the fopen mode ("rb?"/"wb?"),
        // whose MSVC CRT interpretation is not reproduced here.
        return refused(PluginOutcome::unsupported, "zOpen: level byte maps to an unverified fopen mode character");
    }
    if (next_handle_ == std::numeric_limits<std::int32_t>::max()) {
        return refused(PluginOutcome::host_error, "zOpen: handle space exhausted");
    }
    auto stream = std::make_unique<GzipStream>();
    if (!stream->open(resolved_path, mode_char, zlevel, strategy)) {
        out_handle = 0;  // the original stores the NULL gzFile
        return legacy_ok(-1);
    }
    const std::int32_t handle = next_handle_++;
    streams_.emplace(handle, std::move(stream));
    out_handle = handle;
    return legacy_ok(0);
}

PluginResult GzipRegistry::read(std::int32_t handle, std::uint8_t* buffer, std::size_t buffer_capacity,
                                std::int32_t count) {
    if (count < 0) return refused(PluginOutcome::unsafe_input, "zRead: negative count is a huge unsigned length");
    if (static_cast<std::size_t>(count) > buffer_capacity) {
        return refused(PluginOutcome::unsafe_input, "zRead: count exceeds the destination variable");
    }
    if (count > 0 && buffer == nullptr) return refused(PluginOutcome::unsafe_input, "zRead: null destination");
    if (handle == 0) return legacy_ok(0);  // gzread(NULL) = Z_STREAM_ERROR, clamped to stat 0
    const auto it = streams_.find(handle);
    if (it == streams_.end()) return refused(PluginOutcome::unsafe_input, "zRead: unknown or closed handle");
    int result = it->second->read(buffer, static_cast<unsigned>(count));
    if (result < 0) result = 0;
    return legacy_ok(-result);
}

PluginResult GzipRegistry::write(std::int32_t handle, const std::uint8_t* buffer, std::size_t buffer_capacity,
                                 std::int32_t count) {
    if (count < 0) return refused(PluginOutcome::unsafe_input, "zWrite: negative count is a huge unsigned length");
    if (static_cast<std::size_t>(count) > buffer_capacity) {
        return refused(PluginOutcome::unsafe_input, "zWrite: count exceeds the source variable");
    }
    if (count > 0 && buffer == nullptr) return refused(PluginOutcome::unsafe_input, "zWrite: null source");
    if (handle == 0) return legacy_dll_error(-Z_STREAM_ERROR, "zWrite: NULL handle (original EAX 2)");
    const auto it = streams_.find(handle);
    if (it == streams_.end()) return refused(PluginOutcome::unsafe_input, "zWrite: unknown or closed handle");
    static const std::uint8_t empty = 0;
    const int result = it->second->write(count == 0 ? &empty : buffer, static_cast<unsigned>(count));
    if (result < 0) return legacy_dll_error(-result, "zWrite: stream not opened for writing (original EAX 2)");
    return legacy_ok(-result);
}

PluginResult GzipRegistry::close(std::int32_t handle) {
    if (handle == 0) return legacy_ok(-1);  // gzclose(NULL) = Z_STREAM_ERROR -> stat 1
    const auto it = streams_.find(handle);
    if (it == streams_.end()) return refused(PluginOutcome::unsafe_input, "zClose: unknown or closed handle");
    const int inner = it->second->close();
    streams_.erase(it);
    return legacy_ok(inner == 0 ? 0 : -1);
}

std::size_t GzipRegistry::open_count() const noexcept { return streams_.size(); }

void GzipRegistry::close_all() noexcept { streams_.clear(); }

}  // namespace elona::plugins
