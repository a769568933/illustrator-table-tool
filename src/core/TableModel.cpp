#include "table/TableModel.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace table {
TableModel TableModel::create(int rows, int columns, double widthPt, double heightPt) {
    if (rows < 1 || rows > 100 || columns < 1 || columns > 50 || rows * columns > 2000)
        throw ValidationError("Table must have 1-100 rows, 1-50 columns and at most 2000 cells");
    if (!std::isfinite(widthPt) || !std::isfinite(heightPt) ||
        widthPt < columns * minimumTrack || heightPt < rows * minimumTrack)
        throw ValidationError("Each row and column must be at least 2 mm");
    TableModel result;
    result.rowHeights.assign(rows,heightPt/rows);
    result.columnWidths.assign(columns,widthPt/columns);
    for (int row=0; row<rows; ++row) for (int column=0; column<columns; ++column)
        result.anchors.push_back({result.nextId++,row,column,1,1,{}});
    return result;
}
double TableModel::width() const { return std::accumulate(columnWidths.begin(),columnWidths.end(),0.0); }
double TableModel::height() const { return std::accumulate(rowHeights.begin(),rowHeights.end(),0.0); }
const Cell& TableModel::cellAt(int row, int column) const {
    if (row < 0 || row >= rows() || column < 0 || column >= columns())
        throw ValidationError("Cell coordinates out of range");
    for (const auto& cell : anchors)
        if (row >= cell.row && row < cell.row+cell.rowSpan &&
            column >= cell.column && column < cell.column+cell.columnSpan) return cell;
    throw ValidationError("Cell coverage is incomplete");
}
const Cell& TableModel::cellById(CellId id) const {
    auto found=std::find_if(anchors.begin(),anchors.end(),[=](const Cell& c){return c.id==id;});
    if (found == anchors.end()) throw ValidationError("Unknown cell identifier");
    return *found;
}
void TableModel::setText(CellId id, const std::u16string& text) {
    auto found=std::find_if(anchors.begin(),anchors.end(),[=](const Cell& c){return c.id==id;});
    if (found == anchors.end()) throw ValidationError("Unknown cell identifier");
    found->text=text;
}
}
