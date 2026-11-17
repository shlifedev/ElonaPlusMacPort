#pragma once
#include <string_view>

namespace elona::platform {
// SDL3 scancode (USB HID usage id) -> Windows virtual-key code. 0 = unmapped.
int scancode_to_vk(int scancode);
// Names such as "RETURN", "ESCAPE", "LEFT", "A", "F1", "NUMPAD2" or a decimal
// number -> VK code; 0 when unknown. Used by deterministic input scripts.
int vk_from_name(std::string_view name);
}  // namespace elona::platform
