#pragma once
#include "Types.hpp"
#include <vector>
#include <string_view>

namespace table {
class TableModel {
    std::vector<double> rowHeights;
    std::vector<double> columnWidths;
    std::vector<Cell> anchors;
    CellId nextId = 1;
    TableModel() = default;
    void changeTracks(bool vertical, int first, int count, bool insert);
    friend std::string serialize(const TableModel& model);
    friend TableModel deserialize(std::string_view data);
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
    void scale(double horizontal,double vertical);
    void merge(CellRange range);
    void split(CellId id);
    void insertRows(int before,int count) { changeTracks(false,before,count,true); }
    void insertColumns(int before,int count) { changeTracks(true,before,count,true); }
    void eraseRows(int first,int count) { changeTracks(false,first,count,false); }
    void eraseColumns(int first,int count) { changeTracks(true,first,count,false); }
};
}
