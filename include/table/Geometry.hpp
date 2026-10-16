#pragma once
#include "TableModel.hpp"

namespace table {
class Geometry {
public:
    static Rect cellBounds(const TableModel& model, CellId id);
    static std::vector<Edge> visibleEdges(const TableModel& model);
};
}
