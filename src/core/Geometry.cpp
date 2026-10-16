#include "table/Geometry.hpp"
#include <numeric>

namespace table {
namespace {
std::vector<double> positions(const std::vector<double>& sizes) {
    std::vector<double> result(sizes.size()+1,0);
    std::partial_sum(sizes.begin(),sizes.end(),result.begin()+1);
    return result;
}
}
Rect Geometry::cellBounds(const TableModel& model, CellId id) {
    const auto& cell=model.cellById(id);
    auto x=positions(model.widths()), y=positions(model.heights());
    return {x[cell.column],y[cell.row],x[cell.column+cell.columnSpan],y[cell.row+cell.rowSpan]};
}
std::vector<Edge> Geometry::visibleEdges(const TableModel& model) {
    auto x=positions(model.widths()), y=positions(model.heights());
    std::vector<Edge> result;
    for (int c=0; c<=model.columns(); ++c) for (int r=0; r<model.rows(); ++r) {
        if (c>0 && c<model.columns() && model.cellAt(r,c-1).id==model.cellAt(r,c).id) continue;
        result.push_back({{x[c],y[r]},{x[c],y[r+1]},true,c,r});
    }
    for (int r=0; r<=model.rows(); ++r) for (int c=0; c<model.columns(); ++c) {
        if (r>0 && r<model.rows() && model.cellAt(r-1,c).id==model.cellAt(r,c).id) continue;
        result.push_back({{x[c],y[r]},{x[c+1],y[r]},false,r,c});
    }
    return result;
}
}
