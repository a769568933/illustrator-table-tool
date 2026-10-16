#include "table/TableModel.hpp"
#include "table/Geometry.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
int checks = 0;
void expect(bool value, const char* description) {
    ++checks;
    if (!value) throw std::runtime_error(description);
}
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const table::ValidationError&) { rejected = true; }
    expect(rejected, "invalid operation must be rejected");
}
bool close(double a, double b) { return std::abs(a-b) < 1e-8; }
void creation() {
    using namespace table;
    auto model = TableModel::create(4, 4, mm(80), mm(40));
    expect(model.rows() == 4 && model.columns() == 4, "default dimensions");
    std::set<CellId> ids;
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        const auto& cell = model.cellAt(r,c);
        expect(cell.row == r && cell.column == c, "cell anchor");
        ids.insert(cell.id);
    }
    expect(ids.size() == 16, "stable distinct identifiers");
    auto id = model.cellAt(2,1).id;
    model.setText(id, u"中文\nMixed text");
    expect(model.cellAt(2,1).text == u"中文\nMixed text", "UTF-16 cell content");
    expect(model.cellAt(2,1).id == id, "text edits retain identifiers");
    auto max = TableModel::create(100,20,mm(40),mm(200));
    expect(max.cells().size() == 2000, "2000 cells allowed");
    rejects([] { TableModel::create(100,21,mm(42),mm(200)); });
    for (auto dimensions : {std::pair<int,int>{0,4}, {-1,4}, {4,0}, {101,1}, {1,51}})
        rejects([=] { TableModel::create(dimensions.first,dimensions.second,mm(200),mm(200)); });
    for (double value : {0.0, -1.0, mm(7.99), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        rejects([=] { TableModel::create(4,4,value,mm(40)); });
        rejects([=] { TableModel::create(4,4,mm(40),value); });
    }
    rejects([&] { model.cellAt(4,0); });
    rejects([&] { model.cellAt(0,-1); });
    rejects([&] { model.setText(999999,u"bad"); });
}
void geometry() {
    using namespace table;
    auto model = TableModel::create(2,2,100,60);
    auto box = Geometry::cellBounds(model,model.cellAt(1,1).id);
    expect(close(box.left,50) && close(box.top,30) && close(box.right,100) && close(box.bottom,60), "cell bounds");
    auto edges = Geometry::visibleEdges(model);
    expect(edges.size() == 12, "shared borders occur once");
    std::set<std::pair<int,int>> keys;
    for (const auto& edge : edges) {
        expect(keys.insert({edge.vertical ? edge.track : 100+edge.track,edge.segment}).second, "unique edge");
        expect(edge.vertical ? close(edge.from.x,edge.to.x) : close(edge.from.y,edge.to.y), "axis aligned borders");
    }
    const auto& shared = edges.at(2);
    (void)shared;
    rejects([&] { Geometry::cellBounds(model,0); });
}
}
int main() {
    try { creation(); geometry(); std::cout << checks << " checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
