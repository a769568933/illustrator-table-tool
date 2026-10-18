#pragma once
#include "text/TextLayout.hpp"
#include <optional>

struct OverlayFrame {
    std::shared_ptr<const table::TextLayout> layout;
    std::uint32_t width=0,height=0;
    double scaleX=1,scaleY=1;
    table::Point offsetPixels{};
    table::Color background{1,1,1};
    std::uint32_t anchor=0,caret=0;
    std::optional<std::pair<std::uint32_t,std::uint32_t>> composition;
    bool showCaret=true;
};
struct OverlayBitmap {std::uint32_t width=0,height=0;std::vector<std::uint8_t> rgb;};
OverlayBitmap renderEditorOverlay(const OverlayFrame& frame);
