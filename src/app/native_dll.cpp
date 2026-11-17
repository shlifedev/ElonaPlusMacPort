#include "native_dll.h"

#include <map>

namespace elona::app {
namespace {
std::map<std::string, DllHandler>& table() {
    static std::map<std::string, DllHandler> t;
    return t;
}
std::string key(std::string_view library, std::string_view symbol) {
    std::string k(library);
    for (auto& c : k) c = char(std::tolower(static_cast<unsigned char>(c)));
    k += '!';
    k += symbol;
    return k;
}
}  // namespace

std::int32_t* DllArg::i32(std::size_t index) const {
    if (kind != DllArgKind::VarRef || !data) throw DllRefusal("argument is not a variable reference");
    if (bytes_remaining < (index + 1) * sizeof(std::int32_t)) throw DllRefusal("int32 access beyond variable storage");
    return reinterpret_cast<std::int32_t*>(data) + index;
}

void register_dll_handler(std::string_view library, std::string_view symbol, DllHandler handler) {
    table()[key(library, symbol)] = std::move(handler);
}

const DllHandler* find_dll_handler(std::string_view library, std::string_view symbol) {
    auto it = table().find(key(library, symbol));
    return it == table().end() ? nullptr : &it->second;
}

std::vector<std::string> registered_dll_handlers() {
    std::vector<std::string> out;
    for (auto& [k, v] : table()) out.push_back(k);
    return out;
}

}  // namespace elona::app
