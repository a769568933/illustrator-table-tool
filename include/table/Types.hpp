#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace table {
using CellId = std::uint64_t;
constexpr double mm(double value) { return value * 72.0 / 25.4; }
constexpr double minimumTrack = mm(2);
class ValidationError : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
struct Point { double x = 0; double y = 0; };
struct Rect { double left; double top; double right; double bottom; };
struct Cell {
    CellId id = 0;
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    std::u16string text;
};
struct Edge {
    Point from;
    Point to;
    bool vertical;
    int track; // Boundary index: 0 and track count are outside borders.
    int segment;
};
}
