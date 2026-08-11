#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace auto_ducking {

std::wstring BaseName(std::wstring_view path);
std::wstring FormatPeakBar(float peak, std::size_t width);
std::wstring Truncate(std::wstring_view value, std::size_t width);

} // namespace auto_ducking
