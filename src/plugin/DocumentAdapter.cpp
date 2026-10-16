#include "DocumentAdapter.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include "table/Serialization.hpp"

namespace {
struct RangeLease {
    ATE::TextRangeSuite* suite;
    ATE::TextRangeRef value=nullptr;
    ~RangeLease(){ if(value) suite->Release(value); }
};
struct DictionaryLease {
    AIDictionarySuite* suite;
    AIDictionaryRef value=nullptr;
    ~DictionaryLease(){if(value) suite->Release(value);}
};
constexpr const char* modelKey="IllustratorTableTool.Model.v1";
constexpr const char* roleKey="IllustratorTableTool.Role.v1";
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
 stories(suites.get<ATE::StorySuite>(kStorySuite,kStorySuiteVersion)),
 dictionaries(suites.get<AIDictionarySuite>(kAIDictionarySuite,kAIDictionaryVersion)),
 layers(suites.get<AILayerSuite>(kAILayerSuite,kAILayerVersion)) {}
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
        checked(art->SetArtName(table.group,ai::UnicodeString::FromUTF8("画布表格")));
        table.frame=newPath(table.group); setPaint(table.frame,false,false,0);
        rectangle(table.frame,table,{0,0,model.width(),model.height()});
        role(table.frame,"frame");
        for(const auto& cell : model.cells()) table.cells.push_back(createCell(table,cell));
        for(const auto& edge : table::Geometry::visibleEdges(model)) {
            auto path=newPath(table.group); line(path,table,edge); setPaint(path,false,true,0);
            role(path,"edge:"+std::to_string(table.edges.size()));table.edges.push_back(path);
        }
        writeModel(table,model);
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
    auto editable=[&](AIArtHandle object) {
        ai::int32 flags=0;return art->ValidArt(object,false)&&!art->GetArtUserAttr(object,kArtLocked|kArtHidden,&flags)&&!flags;
    };
    if(!editable(table.frame)) return false;
    for(const auto& cell : table.cells) if(!editable(cell.fill)||!editable(cell.text)) return false;
    for(auto edge : table.edges) if(!editable(edge)) return false;
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
        writeModel(table,model);
        table.model=model;
    } catch(...) { undo->UndoChanges(); throw; }
}
void DocumentAdapter::role(AIArtHandle object,const std::string& value) {
    DictionaryLease dictionary{dictionaries};checked(art->GetDictionary(object,&dictionary.value));
    checked(dictionaries->SetStringEntry(dictionary.value,dictionaries->Key(roleKey),value.c_str()));
}
std::string DocumentAdapter::role(AIArtHandle object) const {
    if(!art->HasDictionary(object)) return {};
    DictionaryLease dictionary{dictionaries};checked(art->GetDictionary(object,&dictionary.value));
    auto key=dictionaries->Key(roleKey);if(!dictionaries->IsKnown(dictionary.value,key)) return {};
    const char* value=nullptr;checked(dictionaries->GetStringEntry(dictionary.value,key,&value));return value ? value : "";
}
void DocumentAdapter::writeModel(const NativeTable& table,const table::TableModel& model) {
    auto bytes=table::serialize(model);
    DictionaryLease dictionary{dictionaries};checked(art->GetDictionary(table.group,&dictionary.value));
    checked(dictionaries->SetBinaryEntry(dictionary.value,dictionaries->Key(modelKey),bytes.data(),bytes.size()));
}
void DocumentAdapter::setText(AIArtHandle frame,const std::u16string& value) {
    RangeLease range{ranges};checked(text->GetATETextRange(frame,&range.value));
    checked(ranges->Remove(range.value));
    auto content=value;std::replace(content.begin(),content.end(),u'\n',u'\r');
    static_assert(sizeof(ATETextDOM::Unicode)==sizeof(char16_t),"SDK Unicode must be UTF-16");
    if(!content.empty()) checked(ranges->InsertBefore_AsUnicode(range.value,reinterpret_cast<const ATETextDOM::Unicode*>(content.data()),static_cast<ATETextDOM::Int32>(content.size())));
}
NativeCell DocumentAdapter::createCell(const NativeTable& table,const table::Cell& cell) {
    NativeCell native{cell.id,newPath(table.group),nullptr};
    auto bounds=table::Geometry::cellBounds(table.model,cell.id);
    rectangle(native.fill,table,bounds);setPaint(native.fill,true,false,1);
    auto container=newPath(table.group);rectangle(container,table,textBounds(bounds));setPaint(container,false,false,0);
    checked(text->NewInPathText(kPlaceInsideOnTop,table.group,kHorizontalTextOrientation,container,nullptr,false,&native.text));
    setText(native.text,cell.text);
    RangeLease range{ranges};checked(text->GetATETextRange(native.text,&range.value));
    ATE::CharFeaturesRef chars=nullptr;checked(ranges->GetUniqueCharFeatures(range.value,&chars));
    auto error=features->SetFontSize(chars,12);
    if(!error) error=ranges->ReplaceOrAddLocalCharFeatures(range.value,chars);
    features->Release(chars);checked(error);
    role(native.fill,"fill:"+std::to_string(cell.id));role(native.text,"text:"+std::to_string(cell.id));
    return native;
}
NativeTable DocumentAdapter::readTable(AIArtHandle group) {
    DictionaryLease dictionary{dictionaries};checked(art->GetDictionary(group,&dictionary.value));
    auto key=dictionaries->Key(modelKey);std::size_t size=0;
    checked(dictionaries->GetBinaryEntry(dictionary.value,key,nullptr,&size));
    if(size==0||size>16*1024*1024) throw table::ValidationError("Invalid model size");
    std::string bytes(size,'\0');checked(dictionaries->GetBinaryEntry(dictionary.value,key,bytes.data(),&size));
    if(size!=bytes.size()) throw table::ValidationError("Model size changed");
    NativeTable table{table::deserialize(bytes)};table.group=group;checked(document->GetDocument(&table.document));
    std::map<std::string,AIArtHandle> roles;
    AIArtHandle child=nullptr;checked(art->GetArtFirstChild(group,&child));
    while(child) {
        auto value=role(child);
        if(!value.empty()&&!roles.emplace(value,child).second) throw table::ValidationError("Duplicate table artwork roles");
        AIArtHandle next=nullptr;checked(art->GetArtSibling(child,&next));child=next;
    }
    auto required=[&](const std::string& name,short expected) {
        auto found=roles.find(name);if(found==roles.end()) throw table::ValidationError("Missing table artwork");
        short type=0;checked(art->GetArtType(found->second,&type));if(type!=expected) throw table::ValidationError("Unexpected table artwork type");
        return found->second;
    };
    table.frame=required("frame",kPathArt);
    for(const auto& cell : table.model.cells()) table.cells.push_back({cell.id,required("fill:"+std::to_string(cell.id),kPathArt),required("text:"+std::to_string(cell.id),kTextFrameArt)});
    const auto edges=table::Geometry::visibleEdges(table.model);
    for(std::size_t i=0;i<edges.size();++i) table.edges.push_back(required("edge:"+std::to_string(i),kPathArt));
    if(roles.size()!=1+2*table.cells.size()+table.edges.size()) throw table::ValidationError("Unexpected table artwork roles");
    if(!valid(table)) throw table::ValidationError("Table is locked, hidden or incomplete");
    ai::int16 count=0;checked(paths->GetPathSegmentCount(table.frame,&count));if(count!=4) throw table::ValidationError("Invalid table frame");
    AIPathSegment corners[4]{};checked(paths->GetPathSegments(table.frame,0,4,corners));
    auto near=[](AIReal a,AIReal b){return std::abs(a-b)<0.001;};
    if(!near(corners[0].p.v,corners[1].p.v)||!near(corners[2].p.v,corners[3].p.v)||!near(corners[0].p.h,corners[3].p.h)||!near(corners[1].p.h,corners[2].p.h))
        throw table::ValidationError("Rotated or sheared table: restore its original direction before editing");
    const double width=corners[1].p.h-corners[0].p.h,height=corners[0].p.v-corners[3].p.v;
    table.model.scale(width/table.model.width(),height/table.model.height());table.origin=corners[0].p;
    for(const auto& cell : table.cells) {
        RangeLease range{ranges};checked(text->GetATETextRange(cell.text,&range.value));
        ATETextDOM::Int32 length=0;checked(ranges->GetSize(range.value,&length));
        if(length<0||length>1024*1024) throw table::ValidationError("Cell text exceeds storage limit");
        std::vector<ATETextDOM::Unicode> buffer(static_cast<std::size_t>(length)+1,0);ATETextDOM::Int32 actual=0;
        checked(ranges->GetContents_AsUnicode(range.value,buffer.data(),length+1,&actual));
        if(actual<0||actual>length) throw table::ValidationError("Invalid cell text size");
        std::u16string value;value.reserve(actual);for(int i=0;i<actual;++i) value+=static_cast<char16_t>(buffer[i]);
        std::replace(value.begin(),value.end(),u'\r',u'\n');table.model.setText(cell.id,value);
    }
    return table;
}
std::vector<NativeTable> DocumentAdapter::discoverTables() {
    std::vector<NativeTable> result;AIDocumentHandle current=nullptr;
    if(document->GetDocument(&current)||!current) return result;
    AILayerHandle layer=nullptr;checked(layers->GetFirstLayer(&layer));std::vector<AIArtHandle> stack;
    while(layer) {
        AIArtHandle root=nullptr;checked(art->GetFirstArtOfLayer(layer,&root));if(root) stack.push_back(root);
        AILayerHandle next=nullptr;checked(layers->GetNextLayer(layer,&next));layer=next;
    }
    std::size_t visited=0;
    while(!stack.empty()) {
        auto item=stack.back();stack.pop_back();if(++visited>100000) throw table::ValidationError("Document artwork limit exceeded");
        short type=0;checked(art->GetArtType(item,&type));if(type!=kGroupArt) continue;
        bool tableGroup=false;
        if(art->HasDictionary(item)) {
            DictionaryLease dict{dictionaries};checked(art->GetDictionary(item,&dict.value));
            tableGroup=dictionaries->IsKnown(dict.value,dictionaries->Key(modelKey));
        }
        if(tableGroup) {
            try {result.push_back(readTable(item));} catch(const table::ValidationError&) {} // Never modify a damaged group.
            continue;
        }
        AIArtHandle child=nullptr;checked(art->GetArtFirstChild(item,&child));
        while(child) {stack.push_back(child);AIArtHandle next=nullptr;checked(art->GetArtSibling(child,&next));child=next;}
    }
    return result;
}
void DocumentAdapter::synchronize(NativeTable& table) {table=readTable(table.group);}
void DocumentAdapter::applyStructure(NativeTable& table,const table::TableModel& model) {
    if(!valid(table)) throw static_cast<ASErr>(kBadParameterErr);
    auto next=table;next.model=model;next.cells.clear();next.edges.clear();
    try {
        for(const auto& cell:model.cells()) {
            auto existing=std::find_if(table.cells.begin(),table.cells.end(),[&](const NativeCell& value){return value.id==cell.id;});
            if(existing==table.cells.end()) next.cells.push_back(createCell(next,cell));
            else {
                next.cells.push_back(*existing);
                if(cell.text!=table.model.cellById(cell.id).text) setText(existing->text,cell.text);
            }
        }
        for(const auto& cell:table.cells) {
            if(std::none_of(model.cells().begin(),model.cells().end(),[&](const table::Cell& value){return value.id==cell.id;})) {
                checked(art->DisposeArt(cell.fill));checked(art->DisposeArt(cell.text));
            }
        }
        for(auto path:table.edges) checked(art->DisposeArt(path));
        for(const auto& edge:table::Geometry::visibleEdges(model)) {
            auto path=newPath(next.group);line(path,next,edge);setPaint(path,false,true,0);
            role(path,"edge:"+std::to_string(next.edges.size()));next.edges.push_back(path);
        }
        applyGeometry(next,model);
        auto label=ai::UnicodeString::FromUTF8("修改表格结构");checked(undo->SetUndoTextUS(label,label));table=std::move(next);
    } catch(...) {undo->UndoChanges();throw;}
}
void DocumentAdapter::detach(const NativeTable& table) {
    if(!valid(table)) throw static_cast<ASErr>(kBadParameterErr);
    DictionaryLease dict{dictionaries};checked(art->GetDictionary(table.group,&dict.value));
    checked(dictionaries->DeleteEntry(dict.value,dictionaries->Key(modelKey)));
    std::vector<AIArtHandle> objects{table.frame};for(const auto& c:table.cells){objects.push_back(c.fill);objects.push_back(c.text);}objects.insert(objects.end(),table.edges.begin(),table.edges.end());
    for(auto object:objects){DictionaryLease item{dictionaries};checked(art->GetDictionary(object,&item.value));checked(dictionaries->DeleteEntry(item.value,dictionaries->Key(roleKey)));}
    auto label=ai::UnicodeString::FromUTF8("转换为普通对象");checked(undo->SetUndoTextUS(label,label));
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
