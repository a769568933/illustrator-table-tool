#pragma once
#include "Geometry.hpp"

namespace table {
class Interaction {
    std::optional<TableModel> source, candidate;
    std::optional<Edge> boundary;
    std::optional<CellRange> selected;
    std::optional<CellId> anchor;
    Point start;
    bool changed = false,selecting=false;
    void extendSelection(const TableModel& model,const Cell& cell);
public:
    const std::optional<CellRange>& selection() const { return selected; }
    const std::optional<TableModel>& preview() const { return candidate; }
    bool dragging() const { return boundary.has_value()||selecting; }
    void pointerDown(const TableModel& model, Point point, bool shift, double zoom,double scaleX=1,double scaleY=1);
    void selectCell(const TableModel& model,CellId id);
    void pointerMove(Point point);
    std::optional<TableModel> pointerUp();
    void cancel();
    void clearSelection() { selected.reset(); anchor.reset(); }
};
}
