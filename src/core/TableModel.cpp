#include "table/TableModel.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>

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
void TableModel::resizeBoundary(bool vertical, int boundary, double delta) {
    auto& sizes=vertical ? columnWidths : rowHeights;
    if (boundary<1 || boundary>static_cast<int>(sizes.size()) || !std::isfinite(delta))
        throw ValidationError("Invalid resize boundary or delta");
    double lower=minimumTrack-sizes[boundary-1];
    double upper=boundary<static_cast<int>(sizes.size()) ? sizes[boundary]-minimumTrack : std::numeric_limits<double>::max()-(vertical ? width() : height());
    delta=std::clamp(delta,lower,upper);
    sizes[boundary-1]+=delta;
    if (boundary<static_cast<int>(sizes.size())) sizes[boundary]-=delta;
}
void TableModel::merge(CellRange range) {
    if(range.firstRow>range.lastRow) std::swap(range.firstRow,range.lastRow);
    if(range.firstColumn>range.lastColumn) std::swap(range.firstColumn,range.lastColumn);
    cellAt(range.firstRow,range.firstColumn); cellAt(range.lastRow,range.lastColumn);
    std::vector<Cell> included;
    for(const auto& cell : anchors) {
        if(cell.row>range.lastRow || cell.column>range.lastColumn ||
           cell.row+cell.rowSpan<=range.firstRow || cell.column+cell.columnSpan<=range.firstColumn) continue;
        if(cell.row<range.firstRow || cell.column<range.firstColumn ||
           cell.row+cell.rowSpan-1>range.lastRow || cell.column+cell.columnSpan-1>range.lastColumn)
            throw ValidationError("Merge range cannot cut an existing merged cell");
        included.push_back(cell);
    }
    if(included.size()<2) return;
    std::sort(included.begin(),included.end(),[](const Cell& a,const Cell& b){return a.row==b.row ? a.column<b.column : a.row<b.row;});
    Cell merged=included.front(); merged.text.clear(); merged.originals.clear();
    merged.rowSpan=range.lastRow-range.firstRow+1; merged.columnSpan=range.lastColumn-range.firstColumn+1;
    bool first=true;
    for(const auto& cell : included) {
        if(!first) merged.text+=u'\n'; merged.text+=cell.text; first=false;
        if(cell.originals.empty()) merged.originals.push_back({cell.id,cell.row,cell.column,cell.text});
        else {
            auto saved=cell.originals;
            if(cell.text!=cell.mergedText) for(auto& original : saved)
                if(original.row==cell.row && original.column==cell.column) original.text=cell.text;
            merged.originals.insert(merged.originals.end(),saved.begin(),saved.end());
        }
    }
    merged.mergedText=merged.text;
    auto next=*this;
    next.anchors.erase(std::remove_if(next.anchors.begin(),next.anchors.end(),[&](const Cell& cell){
        return cell.row>=range.firstRow&&cell.row<=range.lastRow&&cell.column>=range.firstColumn&&cell.column<=range.lastColumn;
    }),next.anchors.end());
    next.anchors.push_back(std::move(merged)); *this=std::move(next);
}
void TableModel::split(CellId id) {
    const auto cell=cellById(id);
    if(cell.rowSpan==1 && cell.columnSpan==1) return;
    if(cell.originals.size()!=static_cast<std::size_t>(cell.rowSpan*cell.columnSpan)) throw ValidationError("Merged cell originals are incomplete");
    auto next=*this;
    next.anchors.erase(std::remove_if(next.anchors.begin(),next.anchors.end(),[=](const Cell& c){return c.id==id;}),next.anchors.end());
    for(const auto& original : cell.originals) {
        Cell restored{original.id,original.row,original.column,1,1,original.text};
        if(original.row==cell.row && original.column==cell.column && cell.text!=cell.mergedText) restored.text=cell.text;
        next.anchors.push_back(std::move(restored));
    }
    *this=std::move(next);
}
}
