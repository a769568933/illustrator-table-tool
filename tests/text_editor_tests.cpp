#include "table/TextEditor.hpp"
#include "table/Serialization.hpp"
#include <iostream>
#include <stdexcept>

using namespace table;
namespace {
int checks=0;
void expect(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
template<class F> void rejects(F fn){bool caught=false;try{fn();}catch(const ValidationError&){caught=true;}expect(caught,"invalid edit accepted");}
void replace_selected_chinese(){
    auto m=TableModel::create(1,1,mm(20),mm(10));const auto id=m.cellAt(0,0).id;m.setText(id,u"中文\n123");
    auto change=replaceSelection(m,{id,2,0,7},u"表格");
    expect(change.model.cellById(id).text==u"表格\n123","reverse Chinese selection replacement");
    expect(change.session.anchor==2&&change.session.caret==2,"replacement collapses selection");
    expect(change.changed&&!change.addedRow,"edit flags");
    expect(m.cellById(id).text==u"中文\n123","edit leaves input model unchanged");
    auto newline=replaceSelection(change.model,{id,2,2,7},u"\r\nA\rB");
    expect(newline.model.cellById(id).text==u"表格\nA\nB\n123","normalize clipboard line endings");
    const auto before=serialize(m);
    rejects([&]{replaceSelection(m,{id,20,20,0},u"x");});
    rejects([&]{replaceSelection(m,{999,0,0,0},u"x");});
    rejects([&]{replaceSelection(m,{id,0,0,0},std::u16string(1,0));});
    rejects([&]{replaceSelection(m,{id,0,0,0},std::u16string(1,char16_t(0xd800)));});
    expect(serialize(m)==before,"failed edit is atomic");
}
void delete_whole_cluster(){
    auto m=TableModel::create(1,1,mm(20),mm(10));const auto id=m.cellAt(0,0).id;m.setText(id,u"A\U0001F600e\u0301");
    auto one=eraseCluster(m,{id,5,5,0},{0,1,3,5},true);
    expect(one.model.cellById(id).text==u"A\U0001F600"&&one.session.caret==3,"backspace combining cluster");
    auto two=eraseCluster(one.model,one.session,{0,1,3},true);
    expect(two.model.cellById(id).text==u"A"&&two.session.caret==1,"backspace whole surrogate pair");
    auto forward=eraseCluster(m,{id,1,1,0},{0,1,3,5},false);
    expect(forward.model.cellById(id).text==u"Ae\u0301","delete forward emoji");
    expect(!eraseCluster(m,{id,0,0,0},{0,1,3,5},true).changed,"backspace at start no-op");
    expect(!eraseCluster(m,{id,5,5,0},{0,1,3,5},false).changed,"delete at end no-op");
    auto selected=eraseCluster(m,{id,1,5,0},{0,1,3,5},true);
    expect(selected.model.cellById(id).text==u"A","delete selected whole clusters");
    rejects([&]{eraseCluster(m,{id,2,2,0},{0,1,3,5},true);});
    rejects([&]{eraseCluster(m,{id,3,3,0},{0,1,2,3,5},true);});
    rejects([&]{eraseCluster(m,{id,3,3,0},{0,3,1,5},true);});
    rejects([&]{replaceSelection(m,{id,2,3,0},u"x");});
}
void tab_visits_merge_once(){
    auto m=TableModel::create(2,3,mm(60),mm(20));m.merge({0,0,0,1});
    const auto merged=m.cellAt(0,0).id,next=m.cellAt(0,2).id;
    auto one=navigateCell(m,{merged,0,0,4},false);
    expect(one.session.cell==next&&one.session.batch==5,"Tab visits next visible anchor");
    expect(!one.changed&&!one.addedRow,"navigation is not document mutation");
    auto back=navigateCell(one.model,one.session,true);
    expect(back.session.cell==merged,"Shift Tab skips merged continuations");
    auto first=navigateCell(m,{merged,0,0,0},true);
    expect(first.session.cell==merged&&!first.changed,"first Shift Tab stays put");
    const auto last=m.cellAt(1,2).id;auto added=navigateCell(m,{last,0,0,0},false);
    expect(added.addedRow&&added.changed&&added.model.rows()==3,"last Tab creates a row");
    expect(added.session.cell==added.model.cellAt(2,0).id,"last Tab enters new first cell");
    expect(added.model.heights().at(2)==mm(10),"new row inherits last height");
}
void last_tab_respects_limits(){
    auto m=TableModel::create(100,1,mm(20),mm(300));const auto saved=serialize(m);
    rejects([&]{navigateCell(m,{m.cellAt(99,0).id,0,0,0},false);});
    expect(serialize(m)==saved,"row limit preserves model");
    auto full=TableModel::create(40,50,mm(100),mm(80));
    rejects([&]{navigateCell(full,{full.cellAt(39,49).id,0,0,0},false);});
    expect(full.rows()==40,"total cell limit preserves model");
}
void replacement_preserves_formats(){
    auto m=TableModel::create(1,1,mm(20),mm(10));const auto id=m.cellAt(0,0).id;
    m.setRichText(id,u"AB\nC",{{{0,1,u"",12,{}},{1,3,u"",24,{1,0,0}}},{{0,0},{3,2}}});
    auto inserted=replaceSelection(m,{id,1,2,0},u"XY");const auto& c=inserted.model.cellById(id);
    expect(c.text==u"AXY\nC","formatted replacement content");
    expect(c.formatting.runs.front().fontSize==12,"prefix format retained");
    expect(c.formatting.runs.back().fontSize==24&&c.formatting.runs.back().start==1,"insert inherits replaced position and coalesces");
    expect(c.formatting.paragraphs.back().start==4&&c.formatting.paragraphs.back().alignment==2,"suffix paragraph shifts and retains alignment");
    auto empty=replaceSelection(m,{id,0,4,0},u"");
    expect(empty.model.cellById(id).text.empty()&&empty.model.cellById(id).formatting.runs.empty(),"delete all text clears character ranges");
    auto blank=TableModel::create(1,1,mm(20),mm(10));auto style=blank.cellAt(0,0).style;style.fontSize=18;
    blank.setStyle({0,0,0,0},style);
    auto first=replaceSelection(blank,{blank.cellAt(0,0).id,0,0,0},u"中");
    expect(first.model.cellAt(0,0).formatting.runs.front().fontSize==18,"empty cell inherits cell style");
}
}
int main(){try{
    replace_selected_chinese();delete_whole_cluster();tab_visits_merge_once();last_tab_respects_limits();replacement_preserves_formats();
    std::cout<<checks<<" editor checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
