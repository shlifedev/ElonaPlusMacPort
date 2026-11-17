// hmm.dll DirectSound/DirectMusic/DirectInput entry points.
//
// This host does not implement DirectSound/DirectMusic/DirectInput. Instead of
// faking success, the init functions report the documented and x86-verified
// failure value (raw EAX 0 -> stat 0). The original game then sets
// cfg_music/cfg_sound = 2, which is NOT "off": it switches to the HSP builtin
// MCI path (mmload/mmplay/mmstop), implemented by the host. Joypad: cfg_joypad 0.
// DIGETJOYNUM reports 0 connected joysticks. Every playback/load function is
// left unregistered, so reaching one is a diagnosed stop.
#include "native_dll.h"

namespace elona::app {

void register_media_bindings() {
    auto unavailable = [](const char* what) {
        return [what](DllCall& c) -> std::int32_t {
            c.host->note(std::string(what) + ": no DirectX backend; reported init failure (stat 0) -> game uses the mm* fallback");
            return 0;
        };
    };
    register_dll_handler("hmm.dll", "_DMINIT@16", unavailable("DMINIT (DirectMusic/MIDI)"));
    register_dll_handler("hmm.dll", "_DSINIT@16", unavailable("DSINIT (DirectSound)"));
    register_dll_handler("hmm.dll", "_DIINIT@16", unavailable("DIINIT (DirectInput joypad)"));
    // DirectSound was never initialised (DSINIT failed above), so no buffer is
    // loaded: buffer operations report the documented failure value stat 0.
    // The original game calls these unconditionally for ambient sound.
    // Verified by the plugin worker against the shipped 2.335 hmm.dll (static
    // branches + x86 run plugins-claude-07, tests/plugins/reference/hmm_audio.txt):
    // uninitialised DSSTOP/DSSETVOLUME/CHECKPLAY/DSPLAY/DMPLAY/DMSTOP -> raw 0;
    // DSRELEASE/DSEND/DMEND always raw -1 (stat 1).
    auto ds_buffer_fail = [](const char* what) {
        return [what](DllCall& c) -> std::int32_t {
            if (c.args.empty() || c.args[0].kind != DllArgKind::Int) throw DllRefusal(std::string(what) + ": unexpected argument layout");
            return 0;  // uninitialised / out of range / empty slot: raw 0 (x86 verified)
        };
    };
    register_dll_handler("hmm.dll", "_DSSTOP@16", ds_buffer_fail("DSSTOP"));
    register_dll_handler("hmm.dll", "_DSRELEASE@16", [](DllCall&) -> std::int32_t { return -1; });
    register_dll_handler("hmm.dll", "_DSEND@16", [](DllCall&) -> std::int32_t { return -1; });
    register_dll_handler("hmm.dll", "_DMEND@16", [](DllCall&) -> std::int32_t { return -1; });
    register_dll_handler("hmm.dll", "_DSPLAY@16", ds_buffer_fail("DSPLAY"));
    register_dll_handler("hmm.dll", "_DMPLAY@16", [](DllCall&) -> std::int32_t { return 0; });
    register_dll_handler("hmm.dll", "_DMSTOP@16", [](DllCall&) -> std::int32_t { return 0; });
    register_dll_handler("hmm.dll", "_DSSETVOLUME@16", ds_buffer_fail("DSSETVOLUME"));
    // CHECKPLAY (DSCHECKPLAY in HMM 1.4 docs): 0 = not playing. No DirectSound
    // buffer can be playing here; the game also uses it in MCI fallback mode
    // to pick a free bank 7..12.
    register_dll_handler("hmm.dll", "_CHECKPLAY@16", ds_buffer_fail("CHECKPLAY"));
    // Uninitialised DSGETMASTERVOLUME: internal -1 -> raw +1 -> HSP DLL error 38
    // (x86 verified). The game only calls it when cfg_sound == 1.
    register_dll_handler("hmm.dll", "_DSGETMASTERVOLUME@16", [](DllCall&) -> std::int32_t { return 1; });
    register_dll_handler("hmm.dll", "_DIGETJOYNUM@16", [](DllCall& c) -> std::int32_t {
        c.host->note("DIGETJOYNUM: joystick support not implemented; 0 devices");
        return 0;  // stat 0
    });
}

}  // namespace elona::app
