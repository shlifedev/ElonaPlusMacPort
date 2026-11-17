#include "hspda.h"

#include <array>
#include <cstring>
#include <utility>

namespace elona::plugins {
namespace {

struct IntRecord {
    std::int32_t value;
    std::int32_t index;
};

// Port of hspda.dll 0x10001540 (recursive quicksort over 16-byte records).
// Returns false where the original would scan outside the record buffer.
bool quicksort_int(std::vector<IntRecord>& rec, std::int32_t order, std::int32_t lo, std::int32_t hi) {
    const std::int32_t n = static_cast<std::int32_t>(rec.size());
    for (;;) {
        // add eax,[edx]; cdq; sub eax,edx; sar eax,1  (int32 wraparound sum)
        const std::int32_t sum = static_cast<std::int32_t>(static_cast<std::uint32_t>(rec[lo].value) +
                                                           static_cast<std::uint32_t>(rec[hi].value));
        const std::int32_t pivot = sum / 2;  // truncation toward zero == the cdq/sub/sar sequence
        std::int32_t i = lo;
        std::int32_t j = hi;
        for (;;) {
            if (order == 0) {
                while (rec[i].value < pivot) { if (++i >= n) return false; }
                while (rec[j].value > pivot) { if (--j < 0) return false; }
            } else {
                while (rec[i].value > pivot) { if (++i >= n) return false; }
                while (rec[j].value < pivot) { if (--j < 0) return false; }
            }
            if (i >= j) break;
            std::swap(rec[i], rec[j]);
            ++i;
            --j;
            if (i >= n || j < 0) return false;
        }
        --i;
        if (lo < i) {
            if (!quicksort_int(rec, order, lo, i)) return false;
        }
        ++j;
        if (hi > j) {
            lo = j;
            continue;
        }
        return true;
    }
}

// ---- lstrcmpA ASCII model -------------------------------------------------
// Primary order of printable ASCII under Windows CompareString word sort.
// '-' and '\'' are absent: they are ignored at the primary level ("word sort")
// and only break ties. Letters compare case-insensitively at primary level.
// The table is checked by tests against CompareStringA dumps recorded on the
// Windows reference host (tests/plugins/reference/lstrcmpa_ascii.txt).
constexpr const char kPrimaryOrder[] =
    " !\"#$%&()*,./:;?@[]^_`{|}~\\+<=>0123456789abcdefghijklmnopqrstuvwxyz";

struct Weights {
    std::array<std::int16_t, 128> primary{};
    Weights() {
        primary.fill(-1);
        std::int16_t w = 1;
        for (const char* p = kPrimaryOrder; *p != '\0'; ++p) {
            primary[static_cast<unsigned char>(*p)] = w++;
        }
        for (char c = 'A'; c <= 'Z'; ++c) primary[static_cast<unsigned char>(c)] = primary[static_cast<unsigned char>(c - 'A' + 'a')];
    }
};

const Weights& weights() {
    static const Weights instance;
    return instance;
}

bool is_special(unsigned char c) { return c == '-' || c == '\''; }

int sign(int v) { return (v > 0) - (v < 0); }

}  // namespace

PluginResult hspda_sortval(HspVarType type, void* data, std::size_t data_bytes, std::int32_t length,
                           std::int32_t order, std::vector<std::int32_t>* original_index) {
    if (length <= 0) return legacy_ok(-1);
    if (type == HspVarType::dbl) {
        return refused(PluginOutcome::unsupported, "sortval: double arrays are not reproduced (2.335 sorts int only)");
    }
    if (type != HspVarType::int32) return legacy_ok(-1);
    if (data == nullptr || static_cast<std::size_t>(length) > data_bytes / sizeof(std::int32_t)) {
        return refused(PluginOutcome::unsafe_input, "sortval: len[1] exceeds the variable buffer");
    }
    auto* values = static_cast<std::int32_t*>(data);
    std::vector<IntRecord> rec(static_cast<std::size_t>(length));
    for (std::int32_t i = 0; i < length; ++i) rec[static_cast<std::size_t>(i)] = IntRecord{values[i], i};
    if (length > 1 && !quicksort_int(rec, order, 0, length - 1)) {
        return refused(PluginOutcome::unsafe_input,
                       "sortval: original quicksort would scan outside the array (int32 pivot overflow)");
    }
    for (std::int32_t i = 0; i < length; ++i) values[i] = rec[static_cast<std::size_t>(i)].value;
    if (original_index != nullptr) {
        original_index->resize(rec.size());
        for (std::size_t i = 0; i < rec.size(); ++i) (*original_index)[i] = rec[i].index;
    }
    return legacy_ok(0);
}

std::optional<int> hspda_lstrcmpa_ascii(std::string_view a, std::string_view b) {
    const Weights& w = weights();
    for (std::string_view s : {a, b}) {
        for (char ch : s) {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 0x20 || c > 0x7e) return std::nullopt;
        }
    }
    // 1. primary weights, ignoring '-' and '\''.
    std::size_t i = 0, j = 0;
    for (;;) {
        while (i < a.size() && is_special(static_cast<unsigned char>(a[i]))) ++i;
        while (j < b.size() && is_special(static_cast<unsigned char>(b[j]))) ++j;
        if (i == a.size() || j == b.size()) {
            if (i != a.size()) return 1;
            if (j != b.size()) return -1;
            break;
        }
        const int pa = w.primary[static_cast<unsigned char>(a[i])];
        const int pb = w.primary[static_cast<unsigned char>(b[j])];
        if (pa != pb) return sign(pa - pb);
        ++i;
        ++j;
    }
    // 2. case: lowercase before uppercase, first difference wins.
    i = j = 0;
    for (;;) {
        while (i < a.size() && is_special(static_cast<unsigned char>(a[i]))) ++i;
        while (j < b.size() && is_special(static_cast<unsigned char>(b[j]))) ++j;
        if (i == a.size() || j == b.size()) break;
        const bool ua = a[i] >= 'A' && a[i] <= 'Z';
        const bool ub = b[j] >= 'A' && b[j] <= 'Z';
        if (ua != ub) return ua ? 1 : -1;
        ++i;
        ++j;
    }
    // 3. special characters: compared as (position, character) sequences.
    std::vector<std::pair<std::size_t, unsigned char>> sa, sb;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (is_special(static_cast<unsigned char>(a[k]))) sa.emplace_back(k, static_cast<unsigned char>(a[k]));
    for (std::size_t k = 0; k < b.size(); ++k)
        if (is_special(static_cast<unsigned char>(b[k]))) sb.emplace_back(k, static_cast<unsigned char>(b[k]));
    const std::size_t m = sa.size() < sb.size() ? sa.size() : sb.size();
    for (std::size_t k = 0; k < m; ++k) {
        if (sa[k].first != sb[k].first) return sa[k].first < sb[k].first ? 1 : -1;
        if (sa[k].second != sb[k].second) return sa[k].second == '\'' ? -1 : 1;
    }
    if (sa.size() != sb.size()) return sa.size() < sb.size() ? -1 : 1;
    return 0;
}

PluginResult hspda_sortnote(std::string_view text, std::size_t block_size, std::int32_t order, std::string& out,
                            HspdaCompare compare) {
    // Count pass (0x10001d55..0x10001d79): every LF, CR or CRLF ends an entry
    // and one more entry always follows, so text ending in a break gets an
    // empty last entry and an empty string is one empty entry.
    std::size_t count = 1;
    for (std::size_t k = 0; k < text.size(); ++k) {
        if (text[k] == '\n') {
            ++count;
        } else if (text[k] == '\r') {
            if (k + 1 < text.size() && text[k + 1] == '\n') ++k;
            ++count;
        }
    }
    // Split pass (0x10001940): CR or LF ends an entry; one following LF is
    // skipped, so "LF LF" yields fewer split entries than counted entries.
    std::vector<std::string_view> entries;
    std::size_t start = 0;
    for (std::size_t k = 0; k < text.size();) {
        const char c = text[k];
        if (c == '\r' || c == '\n') {
            entries.push_back(text.substr(start, k - start));
            ++k;
            if (k < text.size() && text[k] == '\n') ++k;
            start = k;
        } else {
            ++k;
        }
    }
    entries.push_back(text.substr(start));
    if (entries.size() != count) {
        // The remaining records are uninitialised heap memory in the original.
        return refused(PluginOutcome::unsafe_input,
                       "sortnote: LF LF sequence sorts uninitialised entries in the original");
    }
    // Bubble sort (0x10001650).
    const std::size_t n = entries.size();
    for (std::size_t pass = 1; pass < n; ++pass) {
        for (std::size_t k = n - 1; k-- > pass - 1;) {
            const std::optional<int> r = compare(entries[k + 1], entries[k]);
            if (!r) return refused(PluginOutcome::unsupported, "sortnote: lstrcmpA ordering not reproduced for these bytes");
            const bool swap = order == 0 ? (*r <= 0) : (*r >= 0);
            if (swap) std::swap(entries[k], entries[k + 1]);
        }
    }
    // Join (0x10001990): every entry + CRLF.
    std::string joined;
    for (std::string_view e : entries) {
        joined.append(e);
        joined.append("\r\n");
    }
    if (joined.size() + 1 > block_size) {
        return refused(PluginOutcome::unsafe_input, "sortnote: result overflows the original temporary buffer");
    }
    out = std::move(joined);
    return legacy_ok(0);
}

PluginResult HspdaXNote::select(HspVarType type, std::uint8_t* buffer, std::size_t capacity, std::int32_t max_items) {
    if (max_items == 0) max_items = 256;
    if (type != HspVarType::str) {
        valid_ = false;  // the original stores the pointer but returns -1 without resetting
        return legacy_ok(-1);
    }
    if (max_items < 0) return refused(PluginOutcome::unsafe_input, "xnotesel: negative item count");
    buffer_ = buffer;
    capacity_ = capacity;
    valid_ = buffer != nullptr;
    counts_.assign(static_cast<std::size_t>(max_items), 0);
    if (!valid_) return refused(PluginOutcome::unsafe_input, "xnotesel: null buffer");
    return legacy_ok(0);
}

PluginResult HspdaXNote::add(std::uint8_t* buffer, std::size_t capacity, std::string_view str) {
    if (!valid_) return refused(PluginOutcome::unsafe_input, "xnoteadd: no valid xnotesel selection");
    if (buffer != buffer_) {
        return refused(PluginOutcome::unsafe_input, "xnoteadd: selected variable was reallocated after xnotesel");
    }
    capacity_ = capacity;
    if (str.size() + 3 > 4096) return refused(PluginOutcome::unsafe_input, "xnoteadd: item overflows the 4096-byte stack buffer");
    // Find the terminator inside the buffer.
    const void* nul = std::memchr(buffer_, 0, capacity_);
    if (nul == nullptr) return refused(PluginOutcome::unsafe_input, "xnoteadd: unterminated buffer");
    const std::size_t used = static_cast<std::size_t>(static_cast<const std::uint8_t*>(nul) - buffer_);
    std::size_t pos = 0;
    std::int32_t index = 0;
    while (pos < used) {
        std::size_t end = pos;
        while (end < used && buffer_[end] != '\r') ++end;
        if (end - pos > 4095) return refused(PluginOutcome::unsafe_input, "xnoteadd: line overflows the stack buffer");
        const std::string_view line(reinterpret_cast<const char*>(buffer_ + pos), end - pos);
        if (line == str) {
            if (static_cast<std::size_t>(index) >= counts_.size()) {
                return refused(PluginOutcome::unsafe_input, "xnoteadd: index beyond the xnotesel item table");
            }
            ++counts_[static_cast<std::size_t>(index)];
            return legacy_ok(-index);
        }
        pos = end;
        if (pos < used && buffer_[pos] == '\r') {
            ++pos;
            if (pos < used && buffer_[pos] == '\n') ++pos;
            ++index;
        }
    }
    if (static_cast<std::size_t>(index) >= counts_.size()) {
        return refused(PluginOutcome::unsafe_input, "xnoteadd: index beyond the xnotesel item table");
    }
    if (used + str.size() + 2 + 1 > capacity_) {
        return refused(PluginOutcome::unsafe_input, "xnoteadd: append overflows the variable buffer");
    }
    std::memcpy(buffer_ + used, str.data(), str.size());
    buffer_[used + str.size()] = '\r';
    buffer_[used + str.size() + 1] = '\n';
    buffer_[used + str.size() + 2] = 0;
    ++counts_[static_cast<std::size_t>(index)];
    return legacy_ok(-index);
}

}  // namespace elona::plugins
