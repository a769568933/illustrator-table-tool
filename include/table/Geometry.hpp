#pragma once
#include "TableModel.hpp"
#include <optional>

namespace table {
enum class HitKind { Outside, Cell, Edge };
struct HitResult { HitKind kind = HitKind::Outside; CellId cell = 0; std::optional<Edge> edge; };
class Geometry {
public:
    static Rect cellBounds(const TableModel& model, CellId id);
    static std::vector<Edge> visibleEdges(const TableModel& model);
    static HitResult hitTest(const TableModel& model, Point point, double zoom, double tolerancePx);
};
}
