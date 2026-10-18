#include "table/Geometry.hpp"
#include <numeric>
#include <algorithm>
#include <cmath>
#include <limits>

namespace table {
namespace {
std::vector<double> positions(const std::vector<double>& sizes) {
    std::vector<double> result(sizes.size()+1,0);
    std::partial_sum(sizes.begin(),sizes.end(),result.begin()+1);
    return result;
}
}
double Geometry::screenDistance(Point from,Point to,double zoom,double scaleX,double scaleY){
    if(!std::isfinite(zoom)||zoom<=0||!std::isfinite(scaleX)||scaleX<=0||!std::isfinite(scaleY)||scaleY<=0)throw ValidationError("Invalid screen transform");
    return std::hypot((from.x-to.x)*scaleX,(from.y-to.y)*scaleY)*zoom;
}
HitResult Geometry::hitTest(const TableModel& model, Point point, double zoom, double tolerancePx,double scaleX,double scaleY) {
    if (!std::isfinite(zoom) || zoom<=0 || !std::isfinite(tolerancePx) || tolerancePx<0 ||
        !std::isfinite(point.x) || !std::isfinite(point.y)) throw ValidationError("Invalid hit-test parameters");
    screenDistance(point,point,zoom,scaleX,scaleY);
    double closest=std::numeric_limits<double>::infinity();
    std::optional<Edge> best;
    for (const auto& edge : visibleEdges(model)) {
        double x=std::clamp(point.x,edge.from.x,edge.to.x);
        double y=std::clamp(point.y,edge.from.y,edge.to.y);
        double distance=screenDistance(point,{x,y},zoom,scaleX,scaleY);
        if (distance<=tolerancePx && distance<closest) { best=edge; closest=distance; }
    }
    if (best) return {HitKind::Edge,0,best};
    if (point.x<0 || point.y<0 || point.x>=model.width() || point.y>=model.height()) return {};
    auto x=positions(model.widths()),y=positions(model.heights());
    int c=static_cast<int>(std::upper_bound(x.begin(),x.end(),point.x)-x.begin())-1;
    int r=static_cast<int>(std::upper_bound(y.begin(),y.end(),point.y)-y.begin())-1;
    return {HitKind::Cell,model.cellAt(r,c).id,{}};
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
