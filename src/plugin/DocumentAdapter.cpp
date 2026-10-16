#include <windows.h>
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
template<class Suite,class Ref> struct AteLease {
    Suite* suite;Ref value=nullptr;
    ~AteLease(){if(value) suite->Release(value);}
};
struct DictionaryLease {
    AIDictionarySuite* suite;
    AIDictionaryRef value=nullptr;
    ~DictionaryLease(){if(value) suite->Release(value);}
};
constexpr const char* modelKey="IllustratorTableTool.Model.v1";
constexpr const char* roleKey="IllustratorTableTool.Role.v1";
table::Rect textBounds(table::Rect bounds,double requested) {
    const auto padding=std::min({requested,(bounds.right-bounds.left)/4,(bounds.bottom-bounds.top)/4});
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
 layers(suites.get<AILayerSuite>(kAILayerSuite,kAILayerVersion)),
 fonts(suites.get<AIFontSuite>(kAIFontSuite,kAIFontVersion)),
 atePaint(suites.get<AIATEPaintSuite>(kAIATEPaintSuite,kAIATEPaintVersion)),
 conversion(suites.get<AIColorConversionSuite>(kAIColorConversionSuite,kAIColorConversionVersion)),
 ateFonts(suites.get<ATE::FontSuite>(kFontSuite,kFontSuiteVersion)),
 paints(suites.get<ATE::ApplicationPaintSuite>(kApplicationPaintSuite,kApplicationPaintSuiteVersion)),
 paragraphs(suites.get<ATE::ParaFeaturesSuite>(kParaFeaturesSuite,kParaFeaturesSuiteVersion)),
 frames(suites.get<ATE::TextFrameSuite>(kTextFrameSuite,kTextFrameSuiteVersion)) {}
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
            AIArtHandle container=nullptr; checked(text->GetPathObject(cell.text,&container)); rectangle(container,table,textBounds(bounds,model.cellById(cell.id).style.padding));
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
    auto container=newPath(table.group);rectangle(container,table,textBounds(bounds,cell.style.padding));setPaint(container,false,false,0);
    checked(text->NewInPathText(kPlaceInsideOnTop,table.group,kHorizontalTextOrientation,container,nullptr,false,&native.text));
    setText(native.text,cell.text);
    styleCell(native,cell.style);
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
    auto sameCoordinate=[](AIReal a,AIReal b){return std::abs(a-b)<0.001;};
    if(!sameCoordinate(corners[0].p.v,corners[1].p.v)||!sameCoordinate(corners[2].p.v,corners[3].p.v)||!sameCoordinate(corners[0].p.h,corners[3].p.h)||!sameCoordinate(corners[1].p.h,corners[2].p.h))
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
        styleEdges(next,model);
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
AIColor DocumentAdapter::color(const table::Color& value) {
    ai::int16 mode=kDocUnknownColor;checked(document->GetDocumentColorModel(&mode));
    AIColor result;result.Init();
    if(mode==kDocRGBColor) {
        result.kind=kThreeColor;result.c.rgb.red=value.red;result.c.rgb.green=value.green;result.c.rgb.blue=value.blue;
    } else {
        result.kind=kFourColor;
        if(value.red==value.green&&value.green==value.blue) {
            result.c.f.cyan=result.c.f.magenta=result.c.f.yellow=0;result.c.f.black=1-value.red;
        } else {
            AIFloatSampleComponent rgb[3]={static_cast<AIFloatSampleComponent>(value.red),static_cast<AIFloatSampleComponent>(value.green),static_cast<AIFloatSampleComponent>(value.blue)},cmyk[4]{};
            AIColorConvertOptions options;AIBoolean inGamut=false;
            checked(conversion->ConvertSampleColor(kAIRGBColorSpace,rgb,kAICMYKColorSpace,cmyk,options,&inGamut));
            result.c.f.cyan=cmyk[0];result.c.f.magenta=cmyk[1];result.c.f.yellow=cmyk[2];result.c.f.black=cmyk[3];
        }
    }
    return result;
}
bool DocumentAdapter::overflows(const NativeCell& cell) {
    AteLease<ATE::TextFrameSuite,ATE::TextFrameRef> frame{frames};checked(text->GetATETextFrame(cell.text,&frame.value));
    RangeLease visible{ranges},complete{ranges};
    checked(frames->GetTextRange(frame.value,false,&visible.value));checked(frames->GetTextRange(frame.value,true,&complete.value));
    ATETextDOM::Int32 visibleSize=0,completeSize=0;checked(ranges->GetSize(visible.value,&visibleSize));checked(ranges->GetSize(complete.value,&completeSize));
    return completeSize>visibleSize;
}
std::vector<FontChoice> DocumentAdapter::installedFonts() {
    ai::int32 count=0;checked(fonts->CountFonts(&count));std::vector<FontChoice> result;
    for(ai::int32 i=0;i<count;++i) {
        AIFontKey key=0;checked(fonts->IndexFontList(i,&key));AIBoolean substituted=false;
        checked(fonts->GetFontIsSubstituted(key,substituted));if(substituted) continue;
        char name[512]{};ASUnicode label[512]{};
        checked(fonts->GetPostScriptFontName(key,name,512));checked(fonts->GetUserFontUINameUnicode(key,label,512));
        FontChoice choice;
        for(auto p=name;*p;++p) choice.name+=static_cast<unsigned char>(*p);
        for(auto p=label;*p;++p) choice.label+=static_cast<char16_t>(*p);
        if(choice.label.empty()) choice.label=choice.name;result.push_back(std::move(choice));
    }
    std::sort(result.begin(),result.end(),[](const FontChoice& a,const FontChoice& b){return a.label<b.label;});return result;
}
ATE::FontRef DocumentAdapter::resolveFont(const std::u16string& name) {
    if(name.empty()) return nullptr;
    ai::int32 count=0;checked(fonts->CountFonts(&count));
    for(ai::int32 i=0;i<count;++i) {
        AIFontKey key=0;checked(fonts->IndexFontList(i,&key));char candidate[512]{};
        checked(fonts->GetPostScriptFontName(key,candidate,512));std::u16string value;
        for(auto p=candidate;*p;++p) value+=static_cast<unsigned char>(*p);
        if(value!=name) continue;
        AIBoolean substituted=false;checked(fonts->GetFontIsSubstituted(key,substituted));if(substituted) break;
        ATE::FontRef result=nullptr;checked(fonts->FontFromFontKey(key,&result));return result;
    }
    MessageBoxW(nullptr,L"表格使用的字体当前不可用。请安装原字体，或在“单元格样式”中选择已安装的字体后重试。",L"字体不可用",MB_OK|MB_ICONINFORMATION);
    throw table::ValidationError("Font is unavailable");
}
void DocumentAdapter::styleCell(const NativeCell& native,const table::CellStyle& style) {
    AteLease<ATE::FontSuite,ATE::FontRef> font{ateFonts,resolveFont(style.font)};
    RangeLease range{ranges};checked(text->GetATETextRange(native.text,&range.value));
    AteLease<ATE::CharFeaturesSuite,ATE::CharFeaturesRef> chars{features};checked(ranges->GetUniqueCharFeatures(range.value,&chars.value));
    checked(features->SetFontSize(chars.value,style.fontSize));
    if(font.value) checked(features->SetFont(chars.value,font.value));
    auto ink=color(style.text);AteLease<ATE::ApplicationPaintSuite,ATE::ApplicationPaintRef> paint{paints};
    checked(atePaint->CreateATEApplicationPaint(&ink,&paint.value));checked(features->SetFillColor(chars.value,paint.value));
    checked(features->SetFill(chars.value,true));checked(features->SetStroke(chars.value,false));
    checked(ranges->ReplaceOrAddLocalCharFeatures(range.value,chars.value));
    AteLease<ATE::ParaFeaturesSuite,ATE::ParaFeaturesRef> para{paragraphs};checked(ranges->GetUniqueParaFeatures(range.value,&para.value));
    const ATE::ParagraphJustification align[]={ATE::kLeftJustify,ATE::kCenterJustify,ATE::kRightJustify};
    checked(paragraphs->SetJustification(para.value,align[style.alignment]));checked(ranges->ReplaceOrAddLocalParaFeatures(range.value,para.value));
    AIPathStyle fill;fill.Init();fill.fillPaint=true;fill.strokePaint=false;fill.fill.color=color(style.fill);
    checked(styles->SetPathStyle(native.fill,&fill));
}
void DocumentAdapter::styleEdges(const NativeTable& native,const table::TableModel& model) {
    const auto edges=table::Geometry::visibleEdges(model);
    for(std::size_t i=0;i<edges.size();++i) {
        const auto& edge=edges[i];const table::Cell* chosen=nullptr;
        auto choose=[&](int row,int column) {
            if(row<0||row>=model.rows()||column<0||column>=model.columns()) return;
            const auto& cell=model.cellAt(row,column);
            if(!chosen||cell.style.revision>chosen->style.revision) chosen=&cell;
        };
        if(edge.vertical) {choose(edge.segment,edge.track-1);choose(edge.segment,edge.track);}
        else {choose(edge.track-1,edge.segment);choose(edge.track,edge.segment);}
        if(!chosen) throw table::ValidationError("Missing border owner");
        AIPathStyle paint;paint.Init();paint.fillPaint=false;paint.strokePaint=chosen->style.borderWidth>0;
        paint.stroke.width=chosen->style.borderWidth;paint.stroke.miterLimit=4;paint.stroke.color=color(chosen->style.border);
        checked(styles->SetPathStyle(native.edges[i],&paint));
    }
}
void DocumentAdapter::applyStyle(NativeTable& native,table::CellRange range,const table::CellStyle& style) {
    if(!valid(native)) throw table::ValidationError("Table is not editable");
    AteLease<ATE::FontSuite,ATE::FontRef> font{ateFonts,resolveFont(style.font)};
    auto model=native.model;model.setStyle(range,style);
    try {
        for(const auto& cell:native.cells) {
            const auto& value=model.cellById(cell.id);
            if(value.style.revision!=native.model.cellById(cell.id).style.revision) styleCell(cell,value.style);
        }
        styleEdges(native,model);applyGeometry(native,model);
        auto label=ai::UnicodeString::FromUTF8("修改单元格样式");checked(undo->SetUndoTextUS(label,label));
    } catch(...) {undo->UndoChanges();throw;}
}
