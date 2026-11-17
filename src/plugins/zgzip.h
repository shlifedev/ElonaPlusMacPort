#pragma once
// z.hpi zOpen/zRead/zWrite/zClose replacement (z.hpi SHA-256 92aa6835...bee).
//
// The original is a thin wrapper over zlib 1.1.4 gzio (gzopen/gzread/gzwrite/
// gzclose). This module re-implements the gzio 1.1.4 stream state machine
// (header parsing, transparent pass-through of non-gzip input, CRC/ISIZE
// trailer, concatenated members, 16 KiB buffering) on top of the raw
// deflate/inflate of a current zlib. Compressed bytes are not expected to match
// the original; decompressed data and stat are.
//
// Handles: game-visible handles are int32 IDs from a registry (1, 2, 3, ...;
// never reused within a registry, never a host pointer). Handle 0 reproduces
// the original NULL-pointer branches. Any other unknown/closed handle would be
// a stale pointer in the original and is refused (unsafe_input).
//
// Raw-return contract (same as exrand.h): result.raw_return is the original
// export EAX. For outcome ok the legacy stat is -raw_return (also in .stat).
//   zOpen : success EAX 0 (stat 0), failure EAX -1 (stat 1); out_handle is
//           written in both cases (0 on failure).
//   zRead : EAX -n (stat n bytes); internal negative results clamp to stat 0.
//   zWrite: EAX -n (stat n bytes); NULL/read handle -> EAX 2 -> hsp_dll_error.
//   zClose: inner 0 -> EAX 0 (stat 0), otherwise EAX -1 (stat 1).
//
// Paths: the caller passes an already-resolved host filesystem path. CP932 ->
// host path mapping belongs to the platform file layer, not this module.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>

#include "plugin_result.h"

namespace elona::plugins {

class GzipStream;  // gzio 1.1.4 equivalent stream (zgzip.cpp)

class GzipRegistry {
public:
    GzipRegistry();
    ~GzipRegistry();
    GzipRegistry(const GzipRegistry&) = delete;
    GzipRegistry& operator=(const GzipRegistry&) = delete;

    // zOpen out_handle, path, mode, level   (mode 0 = read "rb", else write "wb")
    // level: only the low byte matters; mode char = low byte + '0'. Supported
    // chars are '0'..'9', 'f' (filtered), 'h' (huffman only) and NUL (default
    // level). Others reach unverified MSVC fopen mode parsing -> unsupported.
    PluginResult open(std::int32_t& out_handle, const std::filesystem::path& resolved_path, std::int32_t mode,
                      std::int32_t level);

    // zRead buffer, handle, count. `buffer_capacity` = bytes writable from
    // `buffer` to the end of the VM variable. count < 0 or count > capacity
    // would overrun memory in the original -> unsafe_input (buffer untouched).
    PluginResult read(std::int32_t handle, std::uint8_t* buffer, std::size_t buffer_capacity, std::int32_t count);

    // zWrite buffer, handle, count. Same bounds rule as read.
    PluginResult write(std::int32_t handle, const std::uint8_t* buffer, std::size_t buffer_capacity,
                       std::int32_t count);

    // zClose handle. The handle becomes invalid even when close reports stat 1
    // (the original frees the stream unconditionally).
    PluginResult close(std::int32_t handle);

    std::size_t open_count() const noexcept;
    // Host shutdown: abandons every stream like original process exit (FILE is
    // closed, pending deflate output and the gzip trailer are NOT written).
    void close_all() noexcept;

private:
    std::map<std::int32_t, std::unique_ptr<GzipStream>> streams_;
    std::int32_t next_handle_ = 1;
};

}  // namespace elona::plugins
