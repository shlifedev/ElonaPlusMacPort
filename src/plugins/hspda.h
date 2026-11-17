#pragma once
// hspda.dll (2.335 bundled build, SHA-256 a1fcd341...eb08, PE timestamp
// 2007-07-28) replacement for the four functions 2.335 declares:
//   #func func_1 "_sortval@16"   $202   (exinfo: var, order)
//   #func func_2 "_sortnote@16"  $202   (exinfo: var, order)
//   #func xnotesel "_xnotesel@16" $83   (PVal*, max, -, -)
//   #func xnoteadd "_xnoteadd@16" $6    (BMSCR* unused, str, -, -)
// This build differs from the SDK 3.4 hspda.dll (different hash, older bugs;
// e.g. SDK changelog 2009/6/9 "sortnote adds empty lines", "xnoteadd buffer
// overflow"). The behaviour below follows the bundled DLL's instructions.
//
// Raw-return contract as in plugin_result.h: result.raw_return = EAX.
//   sortval/sortnote/xnotesel: 0 -> stat 0, -1 -> stat 1.
//   xnoteadd: -index -> stat index.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plugin_result.h"

namespace elona::plugins {

// HSP3 PVal::flag values relevant to hspda.
enum class HspVarType : std::int16_t { label = 1, str = 2, dbl = 3, int32 = 4 };

// sortval var, order. `length` is PVal::len[1] (first dimension only; the
// original sorts the first len[1] elements from the start of the buffer).
// int arrays: exact port of the original quicksort (pivot = truncated
// average of the end values with int32 wraparound, Hoare-style scans).
// double arrays: not reproduced -> unsupported. Other types / length <= 0:
// EAX -1 (stat 1), data untouched. `original_index` (optional) receives the
// sortget table.
PluginResult hspda_sortval(HspVarType type, void* data, std::size_t data_bytes, std::int32_t length,
                           std::int32_t order, std::vector<std::int32_t>* original_index = nullptr);

// String comparison used by sortnote. The original calls lstrcmpA, i.e.
// CompareStringA(LOCALE_USER_DEFAULT, 0, ...). Return nullopt when the pair is
// outside what the implementation reproduces.
using HspdaCompare = std::optional<int> (*)(std::string_view a, std::string_view b);

// Built-in model of lstrcmpA restricted to printable ASCII (0x20..0x7e),
// checked against Windows CompareStringA dumps (tests/plugins/reference).
// Any other byte -> nullopt.
std::optional<int> hspda_lstrcmpa_ascii(std::string_view a, std::string_view b);

// sortnote var, order.
// `text` is the variable's C string (bytes before the first NUL) and
// `block_size` the variable buffer size (the original's temporary buffer).
// On ok, `out` is the new variable content (the dispatcher assigns it as a
// string to the same variable/element). Behaviour (static reading, confirmed
// by x86 runs plugins-claude-05 unless marked [static]):
//   * entries = 1 + number of LF, CR, CRLF breaks (CRLF counts once), so
//     text ending in a break gains an empty entry and "" becomes "\r\n";
//   * [static] the splitter treats "<CR or LF>LF" as one break, so "\n\n"
//     sorts uninitialised records in the original -> unsafe_input;
//   * bubble sort with lstrcmpA; order 0 swaps when cmp(next, cur) <= 0,
//     otherwise when >= 0;
//   * output = every entry + CRLF;
//   * [static] output longer than block_size-1 overflowed -> unsafe_input.
PluginResult hspda_sortnote(std::string_view text, std::size_t block_size, std::int32_t order, std::string& out,
                            HspdaCompare compare = hspda_lstrcmpa_ascii);

// xnotesel / xnoteadd state (one per process, owned by the host).
class HspdaXNote {
public:
    // xnotesel var, max. `buffer`/`capacity`: the variable's current data
    // pointer and buffer size. max 0 -> 256. A non-string variable returns
    // EAX -1 (stat 1) and leaves the selection unusable.
    PluginResult select(HspVarType type, std::uint8_t* buffer, std::size_t capacity, std::int32_t max_items);

    // xnoteadd "str". `buffer`/`capacity` must be the selected variable's
    // current buffer; the original keeps the raw pointer from xnotesel, so a
    // reallocated variable would be a stale pointer -> unsafe_input. Lines are
    // separated by CR (CRLF skipped); LF alone is line content. Equal line ->
    // stat = its index, else "str\r\n" is appended at the terminating NUL.
    PluginResult add(std::uint8_t* buffer, std::size_t capacity, std::string_view str);

    // Reference counts (sortget table) for tests.
    const std::vector<std::int32_t>& counts() const noexcept { return counts_; }

private:
    std::uint8_t* buffer_ = nullptr;
    std::size_t capacity_ = 0;
    bool valid_ = false;
    std::vector<std::int32_t> counts_;
};

}  // namespace elona::plugins
