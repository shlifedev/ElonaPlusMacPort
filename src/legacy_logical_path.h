#pragma once

// Pure CP932/Windows logical-path operation. Does not access the filesystem.
// The upstream getpath ABI requires output capacity at least input length + 1.
void elona_legacy_getpath(const char* input, char* output, int flags);
