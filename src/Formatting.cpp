#include "Formatting.hpp"

#include <algorithm>
#include <cmath>

namespace auto_ducking {

std::wstring BaseName(const std::wstring_view path) {
    const auto separator = path.find_last_of(L"\\/");
    if (separator == std::wstring_view::npos) {
        return std::wstring(path);
    }
    return std::wstring(path.substr(separator + 1));
}

std::wstring FormatPeakBar(const float peak, const std::size_t width) {
    const float bounded = std::clamp(peak, 0.0F, 1.0F);
    const auto filled = static_cast<std::size_t>(
        std::lround(bounded * static_cast<float>(width)));

    std::wstring result(width, L'.');
    std::fill_n(result.begin(), std::min(filled, width), L'#');
    return result;
}

std::wstring Truncate(const std::wstring_view value, const std::size_t width) {
    if (value.size() <= width) {
        return std::wstring(value);
    }
    if (width <= 3) {
        return std::wstring(value.substr(0, width));
    }
    return std::wstring(value.substr(0, width - 3)) + L"...";
}

} // namespace auto_ducking
