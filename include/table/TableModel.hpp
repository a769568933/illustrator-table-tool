#pragma once
#include "Types.hpp"
#include <vector>

namespace table {
class TableModel {
    std::vector<double> rowHeights;
    std::vector<double> columnWidths;
    std::vector<Cell> anchors;
    CellId nextId = 1;
    TableModel() = default;
public:
    static TableModel create(int rows, int columns, double widthPt, double heightPt);
    int rows() const { return static_cast<int>(rowHeights.size()); }
    int columns() const { return static_cast<int>(columnWidths.size()); }
    const std::vector<double>& heights() const { return rowHeights; }
    const std::vector<double>& widths() const { return columnWidths; }
    const std::vector<Cell>& cells() const { return anchors; }
    double width() const;
    double height() const;
    const Cell& cellAt(int row, int column) const;
    const Cell& cellById(CellId id) const;
    void setText(CellId id, const std::u16string& text);
    void resizeBoundary(bool vertical, int boundary, double delta);
    void merge(CellRange range);
    void split(CellId id);
};
}
