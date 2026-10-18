#pragma once
#include "Types.hpp"
#include <string_view>

namespace table {
constexpr std::size_t maximumTextBytes=1024*1024;
constexpr std::size_t maximumFormatRuns=65536;
void validateText(std::u16string_view text);
bool isTextBoundary(std::u16string_view text,std::uint32_t position);
void validateFormatting(std::u16string_view text,const TextFormatting& formatting);
TextFormatting materializeFormatting(std::u16string_view text,const TextFormatting& formatting,const CellStyle& fallback);
void appendFormattedText(std::u16string& destination,TextFormatting& formatting,
    std::u16string_view source,const TextFormatting& sourceFormatting,const CellStyle& fallback,bool separator);
}
