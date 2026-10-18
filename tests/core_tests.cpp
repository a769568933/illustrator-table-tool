#include "table/TableModel.hpp"
#include "table/Geometry.hpp"
#include "table/Interaction.hpp"
#include "table/Serialization.hpp"
#include "table/Commands.hpp"
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
void interaction() {
    using namespace table;
    auto model=TableModel::create(2,2,100,60);
    auto scaled=TableModel::create(2,2,4000,4000);
    for(double zoom:{0.25,1.0,4.0})for(double sx:{0.1,1.0,10.0})for(double sy:{0.1,1.0,10.0}){
        const auto near=Point{2000+2/(zoom*sx),1000};
        auto vertical=Geometry::hitTest(scaled,near,zoom,3,sx,sy);
        expect(vertical.kind==HitKind::Edge&&vertical.edge->vertical,"scaled vertical edge keeps pixel tolerance");
        expect(Geometry::hitTest(scaled,{2000+4/(zoom*sx),1000},zoom,3,sx,sy).kind==HitKind::Cell,"scaled vertical cell outside tolerance");
        auto horizontal=Geometry::hitTest(scaled,{1000,2000+2/(zoom*sy)},zoom,3,sx,sy);
        expect(horizontal.kind==HitKind::Edge&&!horizontal.edge->vertical,"scaled horizontal edge keeps pixel tolerance");
        Interaction drag;drag.pointerDown(scaled,near,false,zoom,sx,sy);expect(drag.dragging(),"interaction uses anisotropic pixel tolerance");
    }
    expect(std::abs(Geometry::screenDistance({0,0},{1,1},2,10,.1)-std::hypot(20,.2))<1e-9,"double click distance uses both transform axes");
    for (double zoom : {0.25,1.0,4.0}) {
        const auto hit=Geometry::hitTest(model,{50+2/zoom,15},zoom,3);
        expect(hit.kind==HitKind::Edge && hit.edge->vertical && hit.edge->track==1, "screen pixel tolerance at all zooms");
        expect(Geometry::hitTest(model,{50+4/zoom,15},zoom,3).kind==HitKind::Cell,"outside border tolerance selects cell");
    }
    Interaction action;
    action.pointerDown(model,{10,10},false,1);
    expect(action.selection()->firstRow==0 && action.selection()->lastColumn==0,"click selects one cell");
    action.pointerDown(model,{80,45},true,1);
    expect(action.selection()->firstRow==0 && action.selection()->lastRow==1 && action.selection()->lastColumn==1,"shift selects rectangle");
    action.pointerDown(model,{150,150},false,1);
    expect(!action.selection(),"outside clears selection");
    action.pointerDown(model,{50,15},false,1);
    action.pointerMove({60,15});
    expect(close(model.widths()[0],50),"drag never mutates source");
    auto resized=action.pointerUp();
    expect(resized && close(resized->widths()[0],60) && close(resized->widths()[1],40),"internal drag changes adjacent columns");
    expect(close(resized->width(),100),"internal drag preserves total width");
    action.pointerDown(model,{100,15},false,1); action.pointerMove({120,15});
    resized=action.pointerUp();
    expect(resized && close(resized->width(),120) && close(resized->widths()[0],50),"outer drag changes last column and total width");
    action.pointerDown(model,{20,30},false,1); action.pointerMove({20,100});
    resized=action.pointerUp();
    expect(resized && close(resized->heights()[1],minimumTrack) && close(resized->height(),60),"row drag clamps to minimum");
    action.pointerDown(model,{20,60},false,1); action.pointerMove({20,70});
    resized=action.pointerUp();
    expect(resized && close(resized->height(),70),"bottom border drag");
    action.pointerDown(model,{50,15},false,1); action.pointerMove({70,15}); action.cancel();
    expect(!action.pointerUp() && !action.preview(),"cancel discards drag");
    action.pointerDown(model,{50,15},false,1);
    expect(!action.pointerUp(),"stationary click commits nothing");
    rejects([&] { Geometry::hitTest(model,{10,10},0,3); });
    rejects([&] { Geometry::hitTest(model,{10,10},1,-1); });
}
void merging() {
    using namespace table;
    auto model=TableModel::create(3,3,90,90);
    auto a=model.cellAt(0,0).id,b=model.cellAt(0,1).id,c=model.cellAt(1,0).id,d=model.cellAt(1,1).id;
    model.setText(a,u"甲"); model.setText(b,u"乙"); model.setText(c,u"丙"); model.setText(d,u"丁");
    model.merge({0,0,1,1});
    expect(model.cellAt(1,1).id==a && model.cells().size()==6,"merge anchors and coverage");
    expect(model.cellAt(0,0).text==u"甲\n乙\n丙\n丁","merge text in row-major order");
    expect(Geometry::visibleEdges(model).size()==20,"merged internal edges hidden");
    expect(Geometry::hitTest(model,{30,15},1,3).kind==HitKind::Cell,"hidden border cannot be dragged");
    rejects([&] { model.merge({1,1,2,2}); });
    expect(model.cells().size()==6 && model.cellAt(0,0).text==u"甲\n乙\n丙\n丁","partial merge failure preserves state");
    model.split(a);
    expect(model.cells().size()==9 && model.cellAt(0,1).id==b,"split restores stable IDs");
    expect(model.cellAt(0,0).text==u"甲" && model.cellAt(1,0).text==u"丙","unchanged merge restores originals");
    model.merge({0,0,1,1}); model.setText(a,u"新内容"); model.split(a);
    expect(model.cellAt(0,0).text==u"新内容" && model.cellAt(0,1).text==u"乙" && model.cellAt(1,1).text==u"丁","edited merge retains new and original content");
    model.merge({0,0,0,1}); model.merge({0,0,1,1}); model.split(a);
    expect(model.cells().size()==9 && model.cellAt(0,1).text==u"乙","nested merges flatten without text loss");
    rejects([&] {model.merge({-1,0,1,1});});
    model.merge({1,1,0,0});
    expect(model.cellAt(1,1).id==a,"range direction normalized");
    auto selectionModel=TableModel::create(3,3,90,90);selectionModel.merge({0,1,1,2});
    Interaction selection;selection.pointerDown(selectionModel,{15,15},false,1);selection.pointerDown(selectionModel,{45,75},true,1);
    expect(selection.selection()->lastColumn==2,"rectangle selection includes entire intersected merged cells");
}
void structure() {
    using namespace table;
    auto model=TableModel::create(3,3,90,90);
    auto a=model.cellAt(0,0).id,b=model.cellAt(1,0).id;
    model.setText(a,u"anchor"); model.setText(b,u"survivor");
    model.merge({0,0,1,1});
    model.insertRows(1,1);
    expect(model.rows()==4 && model.cellAt(2,1).rowSpan==3,"insert inside merge expands span");
    model.insertColumns(1,1);
    expect(model.columns()==4 && model.cellAt(2,2).columnSpan==3,"column insert expands span");
    model.split(a);
    expect(model.cellAt(1,1).text.empty() && model.cellAt(2,0).id==b && model.cellAt(2,0).text==u"survivor","split after insert restores old and new slots");
    model.merge({0,0,2,2});
    model.eraseRows(0,2);
    expect(model.rows()==2 && model.cellAt(0,0).id==a,"delete merged anchor migrates stable identity");
    model.split(a);
    expect(model.cellAt(0,0).text.find(u"anchor")!=std::u16string::npos && model.cellAt(0,0).text.find(u"survivor")!=std::u16string::npos,"anchor deletion retains remaining and anchor text");
    auto before=model.width(); model.eraseColumns(1,2);
    expect(model.columns()==2 && close(model.width(),before-60),"delete columns shrinks width");
    rejects([&]{model.eraseRows(0,model.rows());});
    rejects([&]{model.eraseColumns(0,model.columns());});
    rejects([&]{model.insertRows(-1,1);}); rejects([&]{model.insertColumns(3,1);});
    rejects([&]{model.insertRows(0,0);}); rejects([&]{model.eraseRows(1,3);});
    expect(model.rows()==2 && model.columns()==2,"failed structure edits preserve dimensions");
    auto maximum=TableModel::create(100,20,mm(40),mm(200));
    rejects([&]{maximum.insertColumns(0,1);}); rejects([&]{maximum.insertRows(100,1);});
    auto same=model.cellAt(0,0).id; model.insertColumns(0,1);
    expect(model.cellAt(0,1).id==same,"insert at start shifts rather than expands cell");
}
void persistence() {
    using namespace table;
    auto model=TableModel::create(3,3,mm(90),mm(30));
    auto id=model.cellAt(0,0).id;
    model.setText(id,u"中文\n\"quoted\" \\ emoji \U0001f642");
    model.setText(model.cellAt(1,1).id,u"原始内容");
    model.merge({0,0,1,1}); model.insertColumns(1,1);
    auto encoded=serialize(model);
    expect(encoded.find("\xe4\xb8\xad\xe6\x96\x87")!=std::string::npos,"format stores UTF-8 text");
    auto decoded=deserialize(encoded);
    expect(serialize(decoded)==encoded,"deterministic model round trip");
    decoded.split(id);
    expect(decoded.cellAt(1,2).text==u"原始内容","merge history survives persistence");
    rejects([]{deserialize("ITT99\n");});
    rejects([&]{deserialize(encoded.substr(0,encoded.size()/2));});
    rejects([&]{deserialize(encoded+"unexpected data");});
    rejects([]{deserialize("ITT1\n100 50 1\n");});
    auto bad=model;
    rejects([&]{bad.setText(id,std::u16string(1,static_cast<char16_t>(0xd800)));});
    std::string duplicate="ITT1\n1 2 3\n10\n10 10\n2\n1 0 0 1 1\n0\n\n0\n\n0\n1 0 1 1 1\n0\n\n0\n\n0\n";
    rejects([&]{deserialize(duplicate);});
    auto scaled=TableModel::create(2,2,100,60); auto scaledId=scaled.cellAt(0,0).id;
    scaled.setText(scaledId,u"keep");scaled.scale(2,3);
    expect(close(scaled.width(),200)&&close(scaled.height(),180)&&scaled.cellAt(0,0).text==u"keep","axis scaling preserves cell identity and text");
    rejects([&]{scaled.scale(0,1);});rejects([&]{scaled.scale(0.001,1);});
    expect(close(scaled.width(),200),"invalid scale is atomic");
}
void styling() {
    using namespace table;
    auto model=TableModel::create(2,2,100,60);
    CellStyle style;style.fill={0.2,0.4,0.6};style.fontSize=18;style.padding=mm(2);
    style.font=u"AdobeSongStd-Light";style.alignment=1;style.borderWidth=1.5;
    model.setStyle({0,0,0,1},style);
    expect(model.cellAt(0,1).style.fontSize==18 && model.cellAt(1,0).style.fontSize==12,"style applies only to selected cells");
    const auto saved=serialize(model);
    auto copy=deserialize(saved);copy.merge({0,0,1,1});copy.split(copy.cellAt(0,0).id);
    expect(copy.cellAt(0,1).style.font==style.font && copy.cellAt(1,1).style.fontSize==12,"styles survive save and merge split");
    style.fill.red=2;rejects([&]{model.setStyle({0,0,1,1},style);});
    expect(serialize(model)==saved,"invalid style leaves model unchanged");
    style.fill.red=0;style.fontSize=std::numeric_limits<double>::infinity();
    rejects([&]{model.setStyle({0,0,0,0},style);});
    auto legacy=deserialize("ITT1\n1 1 2\n20\n20\n1\n1 0 0 1 1\n0\n\n0\n\n0\n");
    expect(legacy.cellAt(0,0).style.fontSize==12,"legacy documents use default styles");
}
void sidebarWorkflow() {
    using namespace table;
    auto model=TableModel::create(3,3,mm(90),mm(45));
    model.setText(model.cellAt(0,0).id,u"名称");model.setText(model.cellAt(0,1).id,u"价格");
    const auto original=serialize(model);
    model=applyCommand(model,{0,0,1,1},Command::Merge);
    expect(model.cellAt(1,1).text==u"名称\n价格\n\n","sidebar merge retains row-major cell text");
    model=applyCommand(model,{0,0,0,0},Command::InsertRowBelow);
    expect(model.rows()==4&&model.cellAt(2,1).rowSpan==3,"sidebar insertion expands an intersected merge");
    model=applyCommand(model,{0,0,2,1},Command::Split);
    expect(model.cellAt(0,1).text==u"价格"&&model.cellAt(1,0).text.empty(),"sidebar split restores content around an inserted row");
    model=applyCommand(model,{1,0,1,2},Command::DeleteRows);
    expect(serialize(model).find("价格")!=std::string::npos,"sidebar deletion retains surviving content");
    auto restored=deserialize(original);
    expect(restored.cellAt(0,1).text==u"价格"&&restored.rows()==3,"simulation undo snapshot restores complete table");
    rejects([&]{applyCommand(model,{0,0,model.rows()-1,0},Command::DeleteRows);});
}
}
void distribute_selected_tracks(){
    using namespace table;
    auto model=TableModel::create(4,4,120,80);model.resizeBoundary(true,2,8);model.resizeBoundary(false,2,6);
    model.setText(model.cellAt(1,1).id,u"保留内容");model.merge({1,1,2,2});
    const auto merged=model.cellAt(1,1).id;const auto original=serialize(model);
    const auto columns=applyCommand(model,{2,2,1,1},Command::DistributeColumns);
    expect(std::abs(columns.widths()[1]-columns.widths()[2])<1e-8,"selected columns evenly distributed");
    expect(columns.widths()[0]==model.widths()[0]&&columns.widths()[3]==model.widths()[3],"outside columns unchanged");
    expect(columns.width()==model.width()&&columns.height()==model.height(),"distribution preserves table extent");
    expect(columns.cellById(merged).text==model.cellById(merged).text&&columns.cellById(merged).columnSpan==2,"distribution preserves merged text and span");
    const auto rows=applyCommand(columns,{1,1,2,2},Command::DistributeRows);
    expect(std::abs(rows.heights()[1]-rows.heights()[2])<1e-8,"selected rows evenly distributed");
    expect(rows.heights()[0]==model.heights()[0]&&rows.heights()[3]==model.heights()[3],"outside rows unchanged");
    expect(serialize(model)==original,"distribution does not mutate undo source");
    expect(serialize(deserialize(serialize(rows)))==serialize(rows),"distributed sizes survive save roundtrip");
    auto single=applyCommand(model,{1,1,1,1},Command::DistributeRows);
    expect(serialize(single)==original,"one selected row is a no-op");
    rejects([&]{applyCommand(model,{0,0,9,3},Command::DistributeRows);});
    auto minimal=TableModel::create(100,20,20*minimumTrack,100*minimumTrack);
    minimal=applyCommand(minimal,{0,0,99,19},Command::DistributeRows);
    minimal=applyCommand(minimal,{0,0,99,19},Command::DistributeColumns);
    expect(serialize(deserialize(serialize(minimal)))==serialize(minimal),"minimum-size distribution stays serializable");
}
void drag_cell_selection(){
    using namespace table;
    auto model=TableModel::create(4,4,120,80);Interaction action;
    const auto before=serialize(model);
    action.pointerDown(model,{10,10},false,1);action.pointerMove({85,55});
    auto selected=action.selection();
    expect(selected&&selected->firstRow==0&&selected->firstColumn==0&&selected->lastRow==2&&selected->lastColumn==2,"mouse drag selects rectangle without Shift");
    expect(!action.pointerUp()&&serialize(model)==before,"selection drag creates no artwork or undo change");
    action.pointerDown(model,{85,55},false,1);action.pointerMove({10,10});
    expect(action.selection()->firstRow==0&&action.selection()->lastRow==2,"reverse drag keeps original anchor");action.pointerUp();
    action.pointerDown(model,{10,10},false,1);action.pointerMove({300,300});
    expect(action.selection()->lastRow==3&&action.selection()->lastColumn==3,"drag outside table clamps to last cell");action.pointerUp();
    action.pointerDown(model,{10,10},false,1);action.pointerMove({30,20});
    expect(action.selection()->lastRow==1&&action.selection()->lastColumn==1,"crossing exact track intersection keeps selection mode");action.pointerUp();
    model.merge({1,1,2,2});action.pointerDown(model,{10,10},false,1);action.pointerMove({40,30});
    expect(action.selection()->lastRow==2&&action.selection()->lastColumn==2,"drag includes complete merged cell");action.pointerUp();
    action.pointerDown(model,{30,10},false,1);action.pointerMove({35,10});
    expect(action.pointerUp().has_value(),"starting on an edge still resizes tracks");
}
void transparent_font_and_alignment_styles(){
    using namespace table;auto model=TableModel::create(2,2,100,80);
    expect(!model.cellAt(0,0).style.fillEnabled,"new tables default to transparent cells");
    auto legacy=deserialize("ITT3\n1 1 2\n40\n100\n1\n1 0 0 1 1\n1 1 1 0 0 0 0 0 0 12 0.75 2.8346456692913389 0 0\n0\n\n0\n\n0\n0\n0\n\n0\n");
    expect(legacy.cellAt(0,0).style.fillEnabled&&legacy.cellAt(0,0).style.verticalAlignment==0,"old ITT3 tables retain opaque fill and top alignment");
    auto red=model.cellAt(0,0).style;red.text={1,0,0};red.fontSize=20;red.fillEnabled=true;model.setStyle({0,0,0,0},red);
    auto id=model.cellAt(0,0).id;model.setRichText(id,u"AB",{{{0,1,u"Arial",24,{0,0,1}},{1,1,u"Arial",12,{0,1,0}}},{{0,0}}});
    CellStylePatch patch;patch.font=u"宋体";patch.alignment=1;patch.verticalAlignment=2;
    model.patchStyle({0,0,0,1},patch);
    expect(model.cellAt(0,0).style.font==u"宋体"&&model.cellAt(0,1).style.font==u"宋体","font patch applies to all selected cells");
    const auto& cell=model.cellAt(0,0);
    expect(cell.style.fillEnabled&&!model.cellAt(0,1).style.fillEnabled&&cell.style.text.red==1&&cell.style.fontSize==20,"font and alignment preserve individual fill and size");
    expect(cell.formatting.runs.at(0).font==u"宋体"&&cell.formatting.runs.at(0).fontSize==24&&cell.formatting.runs.at(1).color.green==1,"font patch preserves rich text sizes and colors");
    expect(cell.formatting.paragraphs.at(0).alignment==1&&cell.style.verticalAlignment==2,"alignment updates paragraph overrides and vertical placement");
    expect(model.cellAt(1,0).style.font.empty()&&model.cellAt(1,0).style.verticalAlignment==0,"style patch leaves unselected cells unchanged");
    patch={};patch.fontSize=18;patch.fillEnabled=false;model.patchStyle({0,0,0,0},patch);
    expect(model.cellAt(0,0).formatting.runs.at(0).fontSize==18&&!model.cellAt(0,0).style.fillEnabled,"size and transparency patch apply without clearing formatting");
    model.merge({0,0,0,1});const auto saved=serialize(model);expect(saved.substr(0,5)=="ITT4\n","new style fields use versioned serialization");
    auto restored=deserialize(saved);expect(restored.cellAt(0,0).style.verticalAlignment==2&&!restored.cellAt(0,0).style.fillEnabled,"transparent merged style survives saving");
    restored.split(restored.cellAt(0,0).id);expect(restored.cellAt(0,1).style.font==u"宋体"&&restored.cellAt(0,1).style.verticalAlignment==2,"merge snapshots preserve new styles");
    const auto before=serialize(restored);patch={};patch.verticalAlignment=3;rejects([&]{restored.patchStyle({0,0,1,1},patch);});
    expect(serialize(restored)==before,"invalid alignment leaves all cells unchanged");
}
int main() {
    try { transparent_font_and_alignment_styles(); creation(); geometry(); interaction(); merging(); structure(); persistence(); styling(); sidebarWorkflow(); distribute_selected_tracks(); drag_cell_selection(); std::cout << checks << " checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
