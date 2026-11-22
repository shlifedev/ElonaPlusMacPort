// Single translation unit holding the third-party single-header
// implementations (fetched by scripts/bootstrap_audio.py into .cache/audio):
//   TinySoundFont tsf.h @ a3621a19 (MIT)  - SoundFont 2 synthesizer
//   dr_mp3.h v0.7.4 @ aec2dd20 (public domain or MIT-0) - MP3 decoder
// Compiled with upstream warnings suppressed; our code lives elsewhere.
#define TSF_IMPLEMENTATION
#include "tsf.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
