#include "text/TextLayout.hpp"
#include "table/EditSessionPolicy.hpp"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace table;
namespace {
int checks=0;
void expect(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
template<class F> void rejects(F fn,const char* name){bool caught=false;try{fn();}catch(const ValidationError&){caught=true;}expect(caught,name);}
LayoutRequest request(std::u16string text){return {std::move(text),{},CellStyle{},100,40};}
void chinese_layout_clusters(){
    auto layout=layoutText(request(u"e\u0301"));
    expect(layout->boundaries()==std::vector<std::uint32_t>({0,2}),"combining cluster is indivisible");
    expect(!layout->outlines().empty(),"glyphs have vector outlines");
    const auto caret=layout->caret(2);expect(caret.left>mm(1)&&caret.bottom>caret.top,"caret in cell points");
    expect(layout->hitTest({caret.left,0.5*(caret.top+caret.bottom)})==2,"caret position hits the cluster end");
    expect(!layout->selection(0,2).empty(),"selection has highlight geometry");
    rejects([&]{layout->caret(1);},"caret splits combining cluster");
    auto chinese=layoutText(request(u"中文\n123"));
    expect(chinese->caret(3).top>chinese->caret(0).top,"newline moves caret to next paragraph");
    expect(chinese->selection(0,6).size()>=2,"multiline highlight");
    expect(!chinese->overflows(),"short multiline content fits");
}
void edit_joined_cluster_caret(){
    for(const auto& text:{std::u16string(u"\u0301"),std::u16string(u"e\n\u0301")}){
        auto model=TableModel::create(1,1,100,40);const auto id=model.cellAt(0,0).id;model.setText(id,text);
        const auto change=text.size()==1?replaceSelection(model,{id,0,0,0},u"e"):replaceSelection(model,{id,1,2,0},u"");
        BoundEditSession session({1,2,3},{id,0,0,0});std::shared_ptr<const TextLayout> published;
        session.commit(change,[&](const TableModel& next){const auto& cell=next.cellById(id);published=layoutText({cell.text,cell.formatting,cell.style,100,40});});
        session.select(snapEditSelection(session.selection(),published->boundaries()));
        expect(session.selection().caret==2&&session.selection().anchor==2,"committed selection snaps after joining a cluster");
        expect(published->caret(session.selection().caret).bottom>0,"joined-cluster caret renders after publication");
        auto marked=snapEditSelection({id,0,1,0},published->boundaries());
        expect(marked.anchor==0&&marked.caret==2&&!published->selection(marked.anchor,marked.caret).empty(),"composition underline expands to joined cluster");
        marked=snapEditSelection({id,2,1,0},published->boundaries());
        expect(marked.anchor==2&&marked.caret==0,"reversed range preserves direction while covering cluster");
    }
}
void outline_holes_and_curves(){
    auto r=request(u"O8中");r.style.font=u"Arial";auto layout=layoutText(r);
    std::size_t contours=0,curves=0;
    for(const auto& path:layout->outlines()) for(const auto& contour:path.contours){
        ++contours;expect(contour.closed,"font contours closed");
        for(const auto& segment:contour.segments) if(segment.cubic)++curves;
    }
    expect(contours>=5,"holes remain separate contours");expect(curves>0,"curved glyphs remain cubic curves");
}
void mixed_size_and_paragraphs(){
    auto r=request(u"AA");r.style.font=u"Arial";r.style.padding=0;
    auto twelvePoint=layoutText(r);r.style.fontSize=24;auto twentyFourPoint=layoutText(r);
    expect(std::abs(twentyFourPoint->caret(2).left-2*twelvePoint->caret(2).left)<0.1,"point size correctly converted to DIPs and back");
    r=request(u"A\nB");r.formatting={{{0,1,u"Arial",24,{}},{2,1,u"Arial",12,{1,0,0}}},{{0,0},{2,2}}};
    auto mixed=layoutText(r);
    expect(mixed->caret(2).left>70,"second paragraph right aligned independently");
    expect(mixed->caret(0).bottom-mixed->caret(0).top>mixed->caret(2).bottom-mixed->caret(2).top,"different font sizes affect caret height");
    bool red=false;for(const auto& path:mixed->outlines())if(path.color.red==1&&path.color.green==0)red=true;
    expect(red,"character colors reach vector output");
}
void overflow_and_empty(){
    for(double height:{mm(2),mm(4),mm(6)}){
        auto blank=request(u"");blank.heightPt=height;
        auto layout=layoutText(blank);
        expect(!layout->overflows(),"empty short cell has no overflow marker");
        expect(layout->hitTest({10,height/2})==0,"short empty cell remains editable");
        expect(layout->caret(0).bottom>layout->caret(0).top,"empty short cell retains caret metrics");
    }
    auto r=request(u"中文中文中文中文中文中文");r.widthPt=20;r.heightPt=10;
    auto overflowing=layoutText(r);expect(overflowing->overflows(),"overflow detected without discarding text");
    expect(overflowing->boundaries().back()==12,"overflow content remains editable");
    auto empty=layoutText(request(u""));expect(empty->outlines().empty(),"empty text generates no art");
    expect(empty->boundaries()==std::vector<std::uint32_t>({0}),"empty text has one caret stop");
    expect(empty->caret(0).bottom>empty->caret(0).top,"empty cell has a visible caret");
    auto trailing=layoutText(request(u"A\n"));expect(trailing->caret(2).top>trailing->caret(0).top,"trailing empty paragraph kept");
}
void layout_failure_has_no_output(){
    auto missing=request(u"中A");missing.style.font=u"ITT Definitely Missing Font 123456";
    auto fallback=layoutText(missing);expect(!fallback->fontSubstitutions().empty(),"missing font substitution reported");
    auto r=request(u"O8");r.limits.maxSegments=1;rejects([&]{layoutText(r);},"curve budget ignored");
    r=request(u"AB");r.limits.maxGlyphs=1;rejects([&]{layoutText(r);},"glyph budget ignored");
    r=request(std::u16string(32769,u'A'));rejects([&]{layoutText(r);},"text unit budget ignored");
    r=request(std::u16string(1,char16_t(0xd800)));rejects([&]{layoutText(r);},"invalid UTF16 accepted");
    r=request(u"A");r.widthPt=0;rejects([&]{layoutText(r);},"zero width accepted");
    auto good=layoutText(request(u"O"));expect(!good->outlines().empty(),"failed layout does not poison later layouts");
}
void performance_records(){
    for(const auto& scenario:{std::pair<int,bool>{16,false},{400,false},{2000,true}}){
        const auto started=std::chrono::steady_clock::now();std::size_t paths=0,segments=0;
        for(int i=0;i<scenario.first;++i){
            auto layout=layoutText(request(scenario.second?u"":u"产品 中文 123"));paths+=layout->outlines().size();
            for(const auto& p:layout->outlines())for(const auto& c:p.contours)segments+=c.segments.size();
        }
        auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);
        expect(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))!=FALSE,"layout process memory measured");
        std::cout<<"layout performance cells="<<scenario.first<<" paths="<<paths<<" segments="<<segments<<" ms="<<elapsed
            <<" peakWorkingSetMiB="<<memory.PeakWorkingSetSize/(1024*1024)<<'\n';
    }
}
void nine_alignment_positions(){
    auto base=request(u"A\nB");base.widthPt=180;base.heightPt=120;
    auto top=layoutText(base);const auto topCaret=top->caret(0);const auto last=top->caret(2);
    base.style.verticalAlignment=1;auto middle=layoutText(base);base.style.verticalAlignment=2;auto bottom=layoutText(base);
    const auto dy=bottom->caret(0).top-topCaret.top;
    expect(dy>50&&std::abs(middle->caret(0).top-topCaret.top-dy/2)<0.01,"vertical center uses half of free space for entire text block");
    expect(std::abs(bottom->caret(2).top-last.top-dy)<0.01,"all paragraphs receive same vertical shift");
    expect(std::abs(bottom->outlines().front().contours.front().start.y-top->outlines().front().contours.front().start.y-dy)<0.01,"vector glyphs share vertical offset with caret");
    for(int vertical=0;vertical<3;++vertical)for(int horizontal=0;horizontal<3;++horizontal){
        auto r=request(u"AB");r.widthPt=180;r.heightPt=120;r.style.alignment=horizontal;r.style.verticalAlignment=vertical;
        auto layout=layoutText(r);auto caret=layout->caret(1);
        expect(layout->hitTest({caret.left,(caret.top+caret.bottom)/2})==1,"nine-position caret hit testing round trips");
        auto selected=layout->selection(0,1);expect(!selected.empty()&&std::abs(selected.front().top-caret.top)<0.01,"selection and caret agree after alignment");
        if(horizontal==1)expect(layout->caret(0).left>60,"horizontal center positions text away from left edge");
        if(horizontal==2)expect(layout->caret(0).left>130,"horizontal right aligns inside cell");
    }
    auto large=base;large.heightPt=8;large.style.verticalAlignment=2;
    auto clipped=layoutText(large);expect(clipped->overflows()&&clipped->caret(0).top>=large.style.padding,"overflow remains top anchored and editable");
}
void merged_blank_neighbours_do_not_overflow(){
    auto model=TableModel::create(1,4,240,30);model.merge({0,0,0,3});const auto id=model.cellAt(0,0).id;
    auto layout=[&]{const auto& cell=model.cellById(id);return layoutText({cell.text,cell.formatting,cell.style,240,30});};
    expect(!layout()->overflows(),"blank merged cell must not report phantom overflow");
    model.split(id);model.setText(id,u"Title");model.merge({0,0,0,3});
    expect(!layout()->overflows(),"short merged label with empty neighbours must fit one line");
    model.setText(id,u"A\nB\nC\nD\nE");expect(layout()->overflows(),"genuine multiline overflow remains detectable");
}
}
int main(){try{
    merged_blank_neighbours_do_not_overflow();nine_alignment_positions();chinese_layout_clusters();edit_joined_cluster_caret();outline_holes_and_curves();mixed_size_and_paragraphs();overflow_and_empty();layout_failure_has_no_output();performance_records();
    std::cout<<checks<<" layout checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
