#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace table {
using CellId = std::uint64_t;
constexpr double mm(double value) { return value * 72.0 / 25.4; }
constexpr double minimumTrack = mm(2);
class ValidationError : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
struct Point { double x = 0; double y = 0; };
struct Rect { double left; double top; double right; double bottom; };
struct CellRange { int firstRow; int firstColumn; int lastRow; int lastColumn; };
struct Color { double red=0, green=0, blue=0; };
struct CellStyle {
    Color fill{1,1,1}, border{}, text{};
    double fontSize=12, borderWidth=0.75, padding=mm(1);
    std::u16string font;
    int alignment=0; // left, center, right
    std::uint64_t revision=0; // Most recently styled cell wins a shared border.
};
void validateStyle(const CellStyle& style);
struct OriginalCell { CellId id; int row; int column; std::u16string text; CellStyle style{}; };
struct Cell {
    CellId id = 0;
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    std::u16string text;
    std::vector<OriginalCell> originals = {};
    std::u16string mergedText = {};
    CellStyle style{};
};
struct Edge {
    Point from;
    Point to;
    bool vertical;
    int track; // Boundary index: 0 and track count are outside borders.
    int segment;
};
}
