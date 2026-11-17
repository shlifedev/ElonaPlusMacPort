// Scancode values are USB HID keyboard usage ids, identical to SDL_Scancode,
// so this file does not depend on SDL headers.
#include "keymap_vk.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace elona::platform {

int scancode_to_vk(int sc) {
    if (sc >= 4 && sc <= 29) return 'A' + (sc - 4);           // A-Z
    if (sc >= 30 && sc <= 38) return '1' + (sc - 30);         // 1-9
    if (sc == 39) return '0';
    if (sc >= 58 && sc <= 69) return 0x70 + (sc - 58);        // F1-F12
    if (sc >= 89 && sc <= 97) return 0x61 + (sc - 89);        // keypad 1-9 -> VK_NUMPAD1..9
    switch (sc) {
    case 40: return 0x0d;   // RETURN
    case 41: return 0x1b;   // ESCAPE
    case 42: return 0x08;   // BACKSPACE
    case 43: return 0x09;   // TAB
    case 44: return 0x20;   // SPACE
    case 45: return 0xbd;   // -
    case 46: return 0xde;   // = (JIS ^ position)
    case 47: return 0xdb;   // [
    case 48: return 0xdd;   // ]
    case 49: return 0xdc;   // backslash
    case 51: return 0xbb;   // ; (JIS)
    case 52: return 0xba;   // ' (JIS :)
    case 53: return 0xc0;   // `
    case 54: return 0xbc;   // ,
    case 55: return 0xbe;   // .
    case 56: return 0xbf;   // /
    case 57: return 0x14;   // CAPSLOCK
    case 70: return 0x2c;   // PRINTSCREEN
    case 71: return 0x91;   // SCROLLLOCK
    case 72: return 0x13;   // PAUSE
    case 73: return 0x2d;   // INSERT
    case 74: return 0x24;   // HOME
    case 75: return 0x21;   // PAGEUP
    case 76: return 0x2e;   // DELETE
    case 77: return 0x23;   // END
    case 78: return 0x22;   // PAGEDOWN
    case 79: return 0x27;   // RIGHT
    case 80: return 0x25;   // LEFT
    case 81: return 0x28;   // DOWN
    case 82: return 0x26;   // UP
    case 83: return 0x90;   // NUMLOCK
    case 84: return 0x6f;   // KP /
    case 85: return 0x6a;   // KP *
    case 86: return 0x6d;   // KP -
    case 87: return 0x6b;   // KP +
    case 88: return 0x0d;   // KP ENTER
    case 98: return 0x60;   // KP 0
    case 99: return 0x6e;   // KP .
    case 135: return 0xe2;  // INTERNATIONAL1 (JIS _ ro)
    case 137: return 0xdc;  // INTERNATIONAL3 (JIS yen)
    case 224: case 228: return 0x11;  // CTRL
    case 225: case 229: return 0x10;  // SHIFT
    case 226: case 230: return 0x12;  // ALT
    default: return 0;
    }
}

int vk_from_name(std::string_view name) {
    std::string n;
    for (char c : name) n.push_back(char(std::toupper(static_cast<unsigned char>(c))));
    if (n.empty()) return 0;
    if (std::isdigit(static_cast<unsigned char>(n[0]))) return std::atoi(n.c_str());
    if (n.size() == 1 && ((n[0] >= 'A' && n[0] <= 'Z'))) return n[0];
    if (n.size() >= 2 && n[0] == 'F' && std::isdigit(static_cast<unsigned char>(n[1]))) {
        int k = std::atoi(n.c_str() + 1);
        if (k >= 1 && k <= 24) return 0x6f + k;
    }
    if (n.rfind("NUMPAD", 0) == 0 && n.size() == 7) return 0x60 + (n[6] - '0');
    struct { const char* name; int vk; } table[] = {
        {"RETURN", 0x0d}, {"ENTER", 0x0d}, {"ESCAPE", 0x1b}, {"ESC", 0x1b}, {"SPACE", 0x20}, {"TAB", 0x09},
        {"BACK", 0x08}, {"BACKSPACE", 0x08}, {"LEFT", 0x25}, {"UP", 0x26}, {"RIGHT", 0x27}, {"DOWN", 0x28},
        {"SHIFT", 0x10}, {"CONTROL", 0x11}, {"CTRL", 0x11}, {"ALT", 0x12}, {"MENU", 0x12}, {"HOME", 0x24},
        {"END", 0x23}, {"PRIOR", 0x21}, {"PAGEUP", 0x21}, {"NEXT", 0x22}, {"PAGEDOWN", 0x22}, {"INSERT", 0x2d},
        {"DELETE", 0x2e}, {"LBUTTON", 0x01}, {"RBUTTON", 0x02}, {"MBUTTON", 0x04},
    };
    for (auto& e : table)
        if (n == e.name) return e.vk;
    return 0;
}

}  // namespace elona::platform
