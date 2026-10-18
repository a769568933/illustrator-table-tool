#pragma once
#include "table/Types.hpp"
#include <memory>

namespace table {
struct GlyphSegment { Point control1{},control2{},end{}; bool cubic=false; };
struct GlyphContour { Point start{}; std::vector<GlyphSegment> segments; bool closed=false; };
struct GlyphPath { std::vector<GlyphContour> contours; Color color{}; bool evenOdd=false; };
struct LayoutLimits { std::uint32_t maxTextUnits=32768,maxGlyphs=32768,maxSegments=250000; };
struct LayoutRequest {
    std::u16string text;
    TextFormatting formatting;
    CellStyle style;
    double widthPt=0,heightPt=0;
    LayoutLimits limits{};
};
class TextLayout {
public:
    virtual ~TextLayout()=default;
    virtual std::uint32_t hitTest(Point point) const=0;
    virtual Rect caret(std::uint32_t position) const=0;
    virtual std::vector<Rect> selection(std::uint32_t first,std::uint32_t last) const=0;
    virtual const std::vector<std::uint32_t>& boundaries() const=0;
    virtual const std::vector<GlyphPath>& outlines() const=0;
    virtual const std::vector<std::u16string>& fontSubstitutions() const=0;
    virtual bool overflows() const=0;
};
std::shared_ptr<const TextLayout> layoutText(const LayoutRequest& request);
std::vector<std::u16string> installedFontFamilies();
}
