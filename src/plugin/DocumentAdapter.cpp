#include "DocumentAdapter.hpp"
#include <algorithm>

namespace {
struct RangeLease {
    ATE::TextRangeSuite* suite;
    ATE::TextRangeRef value=nullptr;
    ~RangeLease(){ if(value) suite->Release(value); }
};
table::Rect textBounds(table::Rect bounds) {
    const auto padding=std::min({table::mm(1),(bounds.right-bounds.left)/4,(bounds.bottom-bounds.top)/4});
    bounds.left+=padding; bounds.top+=padding; bounds.right-=padding; bounds.bottom-=padding;
    return bounds;
}
}
DocumentAdapter::DocumentAdapter(Suites& suites) :
 art(suites.get<AIArtSuite>(kAIArtSuite,kAIArtVersion)),
 paths(suites.get<AIPathSuite>(kAIPathSuite,kAIPathVersion)),
 styles(suites.get<AIPathStyleSuite>(kAIPathStyleSuite,kAIPathStyleVersion)),
 text(suites.get<AITextFrameSuite>(kAITextFrameSuite,kAITextFrameVersion)),
 document(suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion)),
 undo(suites.get<AIUndoSuite>(kAIUndoSuite,kAIUndoVersion)),
 ranges(suites.get<ATE::TextRangeSuite>(kTextRangeSuite,kTextRangeSuiteVersion)),
 features(suites.get<ATE::CharFeaturesSuite>(kCharFeaturesSuite,kCharFeaturesSuiteVersion)),
 stories(suites.get<ATE::StorySuite>(kStorySuite,kStorySuiteVersion)) {}
AIRealPoint DocumentAdapter::world(const NativeTable& table,table::Point point) const {
    return {static_cast<AIReal>(table.origin.h+point.x),static_cast<AIReal>(table.origin.v-point.y)};
}
void DocumentAdapter::rectangle(AIArtHandle path,const NativeTable& table,table::Rect b) {
    AIPathSegment segments[4]{};
    table::Point corners[]={{b.left,b.top},{b.right,b.top},{b.right,b.bottom},{b.left,b.bottom}};
    for(int i=0;i<4;++i) segments[i].p=segments[i].in=segments[i].out=world(table,corners[i]);
    checked(paths->SetPathSegmentCount(path,4));
    checked(paths->SetPathSegments(path,0,4,segments)); checked(paths->SetPathClosed(path,true));
}
void DocumentAdapter::line(AIArtHandle path,const NativeTable& table,const table::Edge& edge) {
    AIPathSegment segments[2]{};
    segments[0].p=segments[0].in=segments[0].out=world(table,edge.from);
    segments[1].p=segments[1].in=segments[1].out=world(table,edge.to);
    checked(paths->SetPathSegmentCount(path,2)); checked(paths->SetPathSegments(path,0,2,segments));
    checked(paths->SetPathClosed(path,false));
}
AIArtHandle DocumentAdapter::newPath(AIArtHandle group) {
    AIArtHandle result=nullptr; checked(art->NewArt(kPathArt,kPlaceInsideOnTop,group,&result)); return result;
}
void DocumentAdapter::setPaint(AIArtHandle path,bool fill,bool stroke,AIReal gray) {
    AIPathStyle style; style.Init(); style.fillPaint=fill; style.strokePaint=stroke;
    ai::int16 mode=kDocUnknownColor; checked(document->GetDocumentColorModel(&mode));
    auto neutral=[&](AIReal brightness) {
        AIColor color; color.Init();
        if(mode==kDocRGBColor) {
            color.kind=kThreeColor; color.c.rgb.red=color.c.rgb.green=color.c.rgb.blue=brightness;
        } else {
            color.kind=kFourColor; color.c.f.cyan=color.c.f.magenta=color.c.f.yellow=0; color.c.f.black=1-brightness;
        }
        return color;
    };
    style.fill.color=neutral(gray);
    style.stroke.color=neutral(0);
    style.stroke.width=0.75; style.stroke.miterLimit=4;
    checked(styles->SetPathStyle(path,&style));
}
NativeTable DocumentAdapter::createTable(const table::TableModel& model,AIRealPoint origin) {
    NativeTable table{model}; table.origin=origin;
    checked(document->GetDocument(&table.document));
    if(!table.document) throw static_cast<ASErr>(kBadParameterErr);
    try {
        checked(art->NewArt(kGroupArt,kPlaceAboveAll,nullptr,&table.group));
        checked(art->SetArtName(table.group,ai::UnicodeString::FromUTF8("画布表格（交互测试版）")));
        table.frame=newPath(table.group); setPaint(table.frame,false,false,0);
        rectangle(table.frame,table,{0,0,model.width(),model.height()});
        for(const auto& cell : model.cells()) {
            NativeCell native{cell.id,newPath(table.group),nullptr};
            auto bounds=table::Geometry::cellBounds(model,cell.id);
            rectangle(native.fill,table,bounds); setPaint(native.fill,true,false,1);
            auto container=newPath(table.group);
            rectangle(container,table,textBounds(bounds)); setPaint(container,false,false,0);
            checked(text->NewInPathText(kPlaceInsideOnTop,table.group,kHorizontalTextOrientation,container,nullptr,false,&native.text));
            RangeLease range{ranges}; checked(text->GetATETextRange(native.text,&range.value));
            static_assert(sizeof(ATETextDOM::Unicode)==sizeof(char16_t),"SDK Unicode must be UTF-16");
            checked(ranges->InsertBefore_AsUnicode(range.value,reinterpret_cast<const ATETextDOM::Unicode*>(cell.text.data()),static_cast<ATETextDOM::Int32>(cell.text.size())));
            ATE::CharFeaturesRef chars=nullptr; checked(ranges->GetUniqueCharFeatures(range.value,&chars));
            auto error=features->SetFontSize(chars,12);
            if(!error) error=ranges->ReplaceOrAddLocalCharFeatures(range.value,chars);
            features->Release(chars); checked(error);
            table.cells.push_back(native);
        }
        for(const auto& edge : table::Geometry::visibleEdges(model)) {
            auto path=newPath(table.group); line(path,table,edge); setPaint(path,false,true,0); table.edges.push_back(path);
        }
        auto label=ai::UnicodeString::FromUTF8("新建表格"); checked(undo->SetUndoTextUS(label,label));
        return table;
    } catch(...) { if(table.group) art->DisposeArt(table.group); throw; }
}
bool DocumentAdapter::valid(const NativeTable& table) const {
    AIDocumentHandle current=nullptr;
    if(document->GetDocument(&current) || !current || current!=table.document || !art->ValidArt(table.group,false)) return false;
    AIArtHandle ancestor=table.group;
    while(ancestor) {
        ai::int32 attr=0;
        if(art->GetArtUserAttr(ancestor,kArtLocked|kArtHidden,&attr) || attr) return false;
        AIArtHandle parent=nullptr; if(art->GetArtParent(ancestor,&parent)) return false; ancestor=parent;
    }
    if(!art->ValidArt(table.frame,false)) return false;
    for(const auto& cell : table.cells) if(!art->ValidArt(cell.fill,false)||!art->ValidArt(cell.text,false)) return false;
    for(auto edge : table.edges) if(!art->ValidArt(edge,false)) return false;
    return true;
}
void DocumentAdapter::applyGeometry(NativeTable& table,const table::TableModel& model) {
    if(!valid(table)) throw static_cast<ASErr>(kBadParameterErr);
    auto edges=table::Geometry::visibleEdges(model);
    if(edges.size()!=table.edges.size() || model.cells().size()!=table.cells.size()) throw static_cast<ASErr>(kBadParameterErr);
    try {
        rectangle(table.frame,table,{0,0,model.width(),model.height()});
        for(const auto& cell : table.cells) {
            auto bounds=table::Geometry::cellBounds(model,cell.id); rectangle(cell.fill,table,bounds);
            AIArtHandle container=nullptr; checked(text->GetPathObject(cell.text,&container)); rectangle(container,table,textBounds(bounds));
        }
        for(std::size_t i=0;i<edges.size();++i) line(table.edges[i],table,edges[i]);
        auto label=ai::UnicodeString::FromUTF8("调整表格尺寸"); checked(undo->SetUndoTextUS(label,label));
        table.model=model;
    } catch(...) { undo->UndoChanges(); throw; }
}
void DocumentAdapter::beginEdit(const NativeTable& table,table::CellId id) {
    if(!valid(table)) throw static_cast<ASErr>(kBadParameterErr);
    auto found=std::find_if(table.cells.begin(),table.cells.end(),[=](const NativeCell& c){return c.id==id;});
    if(found==table.cells.end()) throw static_cast<ASErr>(kBadParameterErr);
    RangeLease range{ranges}; checked(text->GetATETextRange(found->text,&range.value));
    ATE::StoryRef story=nullptr; checked(ranges->GetStory(range.value,&story));
    const auto focusError=document->SetTextFocus(story);
    stories->Release(story); checked(focusError);
    checked(ranges->Select(range.value,false));
    AIBoolean focused=false; checked(document->HasTextFocus(&focused));
    if(!focused) throw static_cast<ASErr>(kCantHappenErr);
}
