#include "table/Interaction.hpp"
#include <algorithm>

namespace table {
void Interaction::selectCell(const TableModel& model,CellId id) {
    const auto& cell=model.cellById(id);cancel();anchor=id;
    selected=CellRange{cell.row,cell.column,cell.row+cell.rowSpan-1,cell.column+cell.columnSpan-1};
}
void Interaction::pointerDown(const TableModel& model, Point point, bool shift, double zoom,double scaleX,double scaleY) {
    cancel();
    auto hit=Geometry::hitTest(model,point,zoom,3,scaleX,scaleY);
    if (hit.kind==HitKind::Edge && hit.edge->track>0) {
        source=model; candidate=model; boundary=hit.edge; start=point;
        return;
    }
    if (hit.kind!=HitKind::Cell) { clearSelection(); return; }
    const auto& cell=model.cellById(hit.cell);
    if (!shift || !anchor) anchor=cell.id;
    const Cell* first=nullptr;
    try { first=&model.cellById(*anchor); }
    catch (const ValidationError&) { anchor=cell.id; first=&cell; }
    selected=CellRange{std::min(first->row,cell.row),std::min(first->column,cell.column),
        std::max(first->row+first->rowSpan-1,cell.row+cell.rowSpan-1),
        std::max(first->column+first->columnSpan-1,cell.column+cell.columnSpan-1)};
    // A rectangular selection must include every merged cell that it touches.
    bool expanded;
    do {
        expanded=false;
        for (const auto& member:model.cells()) {
            const int lastRow=member.row+member.rowSpan-1;
            const int lastColumn=member.column+member.columnSpan-1;
            if (member.row>selected->lastRow || lastRow<selected->firstRow ||
                member.column>selected->lastColumn || lastColumn<selected->firstColumn) continue;
            const CellRange next{std::min(selected->firstRow,member.row),
                std::min(selected->firstColumn,member.column),
                std::max(selected->lastRow,lastRow),std::max(selected->lastColumn,lastColumn)};
            expanded=expanded || next.firstRow!=selected->firstRow ||
                next.firstColumn!=selected->firstColumn || next.lastRow!=selected->lastRow ||
                next.lastColumn!=selected->lastColumn;
            selected=next;
        }
    } while (expanded);
}
void Interaction::pointerMove(Point point) {
    if (!boundary || !source) return;
    auto next=*source;
    double delta=boundary->vertical ? point.x-start.x : point.y-start.y;
    next.resizeBoundary(boundary->vertical,boundary->track,delta);
    changed=next.widths()!=source->widths() || next.heights()!=source->heights();
    candidate=std::move(next);
}
std::optional<TableModel> Interaction::pointerUp() {
    auto result=changed ? candidate : std::nullopt;
    cancel();
    return result;
}
void Interaction::cancel() { source.reset(); candidate.reset(); boundary.reset(); changed=false; }
}
