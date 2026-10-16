#include <windows.h>
#include "DocumentAdapter.hpp"
#include "Trace.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
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
constexpr const char* snapshotKey="IllustratorTableTool.NativeSnapshot.v1";
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
    if(table.archive) {
        ai::int32 flags=0;
        if(!art->ValidArt(table.archive,false)||art->GetArtUserAttr(table.archive,kArtHidden,&flags)||!(flags&kArtHidden)) return false;
        for(const auto& snapshot:table.snapshots)
            if(!art->ValidArt(snapshot.fill,false)||!art->ValidArt(snapshot.text,false)) return false;
    }
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
    auto archiveKey=dictionaries->Key(snapshotKey);
    if(table.archive) checked(dictionaries->SetIntegerEntry(dictionary.value,archiveKey,1));
    else if(dictionaries->IsKnown(dictionary.value,archiveKey)) checked(dictionaries->DeleteEntry(dictionary.value,archiveKey));
}
void DocumentAdapter::setText(AIArtHandle frame,const std::u16string& value) {
    RangeLease range{ranges};checked(text->GetATETextRange(frame,&range.value));
    checked(ranges->Remove(range.value));
    auto content=value;std::replace(content.begin(),content.end(),u'\n',u'\r');
    static_assert(sizeof(ATETextDOM::Unicode)==sizeof(char16_t),"SDK Unicode must be UTF-16");
    if(!content.empty()) checked(ranges->InsertBefore_AsUnicode(range.value,reinterpret_cast<const ATETextDOM::Unicode*>(content.data()),static_cast<ATETextDOM::Int32>(content.size())));
}
std::u16string DocumentAdapter::textContent(AIArtHandle frame) const {
    RangeLease range{ranges};checked(text->GetATETextRange(frame,&range.value));
    ATETextDOM::Int32 length=0;checked(ranges->GetSize(range.value,&length));
    if(length<0||length>1024*1024) throw table::ValidationError("Cell text exceeds storage limit");
    std::vector<ATETextDOM::Unicode> buffer(static_cast<std::size_t>(length)+1,0);ATETextDOM::Int32 actual=0;
    checked(ranges->GetContents_AsUnicode(range.value,buffer.data(),length+1,&actual));
    if(actual<0||actual>length) throw table::ValidationError("Invalid cell text size");
    std::u16string value;value.reserve(actual);for(int i=0;i<actual;++i) value+=static_cast<char16_t>(buffer[i]);
    std::replace(value.begin(),value.end(),u'\r',u'\n');return value;
}
NativeCell DocumentAdapter::duplicateCell(const NativeCell& source,table::CellId id,AIArtHandle parent,bool snapshot) {
    NativeCell copy{id,nullptr,nullptr};
    checked(art->DuplicateArt(source.fill,kPlaceInsideOnTop,parent,&copy.fill));
    checked(art->DuplicateArt(source.text,kPlaceInsideOnTop,parent,&copy.text));
    checked(art->SetArtUserAttr(copy.fill,kArtHidden|kArtLocked,0));
    checked(art->SetArtUserAttr(copy.text,kArtHidden|kArtLocked,0));
    role(copy.fill,(snapshot?"snapshot-fill:":"fill:")+std::to_string(id));
    role(copy.text,(snapshot?"snapshot-text:":"text:")+std::to_string(id));return copy;
}
void DocumentAdapter::formattedText(AIArtHandle destination,const std::vector<AIArtHandle>& sources) {
    setText(destination,{});
    bool first=true;
    for(auto source:sources) {
        RangeLease insertion{ranges},content{ranges};checked(text->GetATETextRange(destination,&insertion.value));
        if(!first) {const ATETextDOM::Unicode newline='\r';checked(ranges->InsertAfter_AsUnicode(insertion.value,&newline,1));}
        // Reacquire after insertion: a text range's endpoints need not expand with inserted text.
        RangeLease end{ranges};checked(text->GetATETextRange(destination,&end.value));
        checked(text->GetATETextRange(source,&content.value));
        checked(ranges->InsertAfter_AsTextRange(end.value,content.value));first=false;
    }
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
    const auto archiveRole=roles.find("archive");
    if(archiveRole!=roles.end()) {
        table.archive=required("archive",kGroupArt);
        std::map<std::string,AIArtHandle> saved;
        AIArtHandle item=nullptr;checked(art->GetArtFirstChild(table.archive,&item));
        while(item) {
            const auto name=role(item);
            if(name.empty()||!saved.emplace(name,item).second||saved.size()>4000) throw table::ValidationError("Invalid native format archive");
            AIArtHandle next=nullptr;checked(art->GetArtSibling(item,&next));item=next;
        }
        std::set<table::CellId> originals;
        for(const auto& cell:table.model.cells()) for(const auto& original:cell.originals) originals.insert(original.id);
        for(auto id:originals) {
            auto fill=saved.find("snapshot-fill:"+std::to_string(id)),content=saved.find("snapshot-text:"+std::to_string(id));
            if(fill==saved.end()&&content==saved.end()) continue; // Newly inserted blank slots have no archive yet.
            if(fill==saved.end()||content==saved.end()) throw table::ValidationError("Incomplete native format snapshot");
            short fillType=0,textType=0;checked(art->GetArtType(fill->second,&fillType));checked(art->GetArtType(content->second,&textType));
            if(fillType!=kPathArt||textType!=kTextFrameArt) throw table::ValidationError("Unexpected native format snapshot type");
            table.snapshots.push_back({id,fill->second,content->second});
        }
        if(saved.size()!=2*table.snapshots.size()) throw table::ValidationError("Unexpected native format snapshot roles");
    }
    auto archiveKey=dictionaries->Key(snapshotKey);
    if(dictionaries->IsKnown(dictionary.value,archiveKey)) {
        ai::int32 version=0;checked(dictionaries->GetIntegerEntry(dictionary.value,archiveKey,&version));
        if(version!=1||!table.archive) throw table::ValidationError("Missing or unsupported native format archive");
    }
    if(roles.size()!=1+2*table.cells.size()+table.edges.size()+(table.archive?1:0)) throw table::ValidationError("Unexpected table artwork roles");
    if(!valid(table)) throw table::ValidationError("Table is locked, hidden or incomplete");
    ai::int16 count=0;checked(paths->GetPathSegmentCount(table.frame,&count));if(count!=4) throw table::ValidationError("Invalid table frame");
    AIPathSegment corners[4]{};checked(paths->GetPathSegments(table.frame,0,4,corners));
    auto sameCoordinate=[](AIReal a,AIReal b){return std::abs(a-b)<0.001;};
    if(!sameCoordinate(corners[0].p.v,corners[1].p.v)||!sameCoordinate(corners[2].p.v,corners[3].p.v)||!sameCoordinate(corners[0].p.h,corners[3].p.h)||!sameCoordinate(corners[1].p.h,corners[2].p.h))
        throw table::ValidationError("Rotated or sheared table: restore its original direction before editing");
    const double width=corners[1].p.h-corners[0].p.h,height=corners[0].p.v-corners[3].p.v;
    table.model.scale(width/table.model.width(),height/table.model.height());table.origin=corners[0].p;
    for(const auto& cell : table.cells) {
        table.model.setText(cell.id,textContent(cell.text));
    }
    return table;
}
std::vector<NativeTable> DocumentAdapter::discoverTables(std::vector<RejectedTable>* rejected) {
    if(rejected) rejected->clear();
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
        auto reject=[&](const std::wstring& reason) {
            if(!rejected) return;
            AIRealRect bounds{};
            if(art->GetArtBounds(item,&bounds)==kNoErr) rejected->push_back({bounds,reason});
        };
        if(art->HasDictionary(item)) {
            try {
                DictionaryLease dict{dictionaries};checked(art->GetDictionary(item,&dict.value));
                tableGroup=dictionaries->IsKnown(dict.value,dictionaries->Key(modelKey));
                if(tableGroup) result.push_back(readTable(item));
            } catch(const table::ValidationError& error) {
                const bool transformed=std::string(error.what()).find("Rotated or sheared")!=std::string::npos;
                reject(transformed?L"此表格已旋转或倾斜，请恢复原方向后再编辑。":L"此表格的数据或对象不完整，或已锁定、隐藏。请恢复对象后重试。");
            } catch(ASErr) {
                // A wrong dictionary entry type is an SDK error, not a model validation error.
                // Isolate it to this group so other tables remain available.
                reject(L"此表格的数据类型异常，暂时无法编辑。请使用未损坏的副本。");
            }
        }
        if(tableGroup) continue;
        AIArtHandle child=nullptr;checked(art->GetArtFirstChild(item,&child));
        while(child) {stack.push_back(child);AIArtHandle next=nullptr;checked(art->GetArtSibling(child,&next));child=next;}
    }
    return result;
}
void DocumentAdapter::synchronize(NativeTable& table) {
    if(!valid(table)) throw table::ValidationError("Table belongs to a closed document or contains invalid artwork");
    table=readTable(table.group);
}
void DocumentAdapter::applyStructure(NativeTable& table,const table::TableModel& model,std::optional<TrackDeletion> deletion) {
    traceStage("structure.begin");
    if(!valid(table)) throw static_cast<ASErr>(kBadParameterErr);
    auto next=table;next.model=model;next.cells.clear();next.edges.clear();
    try {
        std::map<table::CellId,NativeCell> snapshots;
        for(const auto& saved:table.snapshots) snapshots.emplace(saved.id,saved);
        std::set<table::CellId> needed;
        auto ensureArchive=[&] {
            if(next.archive) return;
            checked(art->NewArt(kGroupArt,kPlaceInsideOnTop,next.group,&next.archive));role(next.archive,"archive");
            checked(art->SetArtName(next.archive,ai::UnicodeString::FromUTF8("表格原始格式（隐藏）")));
            checked(art->SetArtUserAttr(next.archive,kArtHidden,kArtHidden));
        };
        for(const auto& owner:model.cells()) for(const auto& original:owner.originals) {
            needed.insert(original.id);
            auto saved=snapshots.find(original.id);
            auto old=std::find_if(table.cells.begin(),table.cells.end(),[&](const NativeCell& c){return c.id==original.id;});
            const table::Cell* oldModel=old==table.cells.end()?nullptr:&table.model.cellById(original.id);
            const NativeCell* migrated=nullptr;
            if(deletion&&oldModel&&!oldModel->originals.empty()) {
                const int begin=deletion->vertical?oldModel->column:oldModel->row;
                if(begin>=deletion->first&&begin<deletion->first+deletion->count) {
                    const int row=original.row+(!deletion->vertical?deletion->count:0);
                    const int column=original.column+(deletion->vertical?deletion->count:0);
                    for(const auto& prior:oldModel->originals) if(prior.row==row&&prior.column==column)
                        for(const auto& snapshot:table.snapshots) if(snapshot.id==prior.id) migrated=&snapshot;
                }
            }
            if(migrated&&saved!=snapshots.end()) {
                ensureArchive();NativeCell source{original.id,migrated->fill,saved->second.text};
                auto replacement=duplicateCell(source,original.id,next.archive,true);
                const auto anchorText=textContent(saved->second.text),otherText=textContent(migrated->text);
                if(anchorText.empty()) formattedText(replacement.text,{migrated->text});
                else if(!otherText.empty()) formattedText(replacement.text,{saved->second.text,migrated->text});
                if(textContent(replacement.text)!=original.text) throw table::ValidationError("Migrated native format snapshot does not match saved text");
                snapshots[original.id]=replacement;continue;
            }
            if(saved!=snapshots.end()&&textContent(saved->second.text)==original.text) continue;
            if(old!=table.cells.end()&&textContent(old->text)==original.text) {
                ensureArchive();auto source=*old;
                // Editing a merged cell updates its saved text, while its original fill remains restorable.
                if(saved!=snapshots.end()) source.fill=saved->second.fill;
                snapshots[original.id]=duplicateCell(source,original.id,next.archive,true);
            } else if(saved!=snapshots.end()) {
                throw table::ValidationError("Native format snapshot does not match saved text");
            }
        }
        next.snapshots.clear();
        for(const auto& saved:snapshots) if(needed.count(saved.first)) next.snapshots.push_back(saved.second);
        traceStage("structure.cells.start");
        for(const auto& cell:model.cells()) {
            auto existing=std::find_if(table.cells.begin(),table.cells.end(),[&](const NativeCell& value){return value.id==cell.id;});
            auto saved=snapshots.find(cell.id);
            const auto* old=existing==table.cells.end()?nullptr:&table.model.cellById(cell.id);
            if(old&&!cell.originals.empty()&&cell.text!=old->text&&cell.text==cell.mergedText) {
                auto merged=duplicateCell(*existing,cell.id,next.group,false);
                std::vector<const table::Cell*> members;
                for(const auto& prior:table.model.cells())
                    if(std::any_of(cell.originals.begin(),cell.originals.end(),[&](const table::OriginalCell& original){return original.id==prior.id;})) members.push_back(&prior);
                std::sort(members.begin(),members.end(),[](const table::Cell* a,const table::Cell* b){return a->row==b->row?a->column<b->column:a->row<b->row;});
                std::vector<AIArtHandle> sources;
                for(const auto* member:members) for(const auto& native:table.cells) if(native.id==member->id) sources.push_back(native.text);
                formattedText(merged.text,sources);next.cells.push_back(merged);
            } else if(saved!=snapshots.end()&&(existing==table.cells.end()||(old&&!old->originals.empty()&&cell.originals.empty()))) {
                if(textContent(saved->second.text)==cell.text) next.cells.push_back(duplicateCell(saved->second,cell.id,next.group,false));
                else if(existing!=table.cells.end()&&textContent(existing->text)==cell.text) {
                    NativeCell source{cell.id,saved->second.fill,existing->text};next.cells.push_back(duplicateCell(source,cell.id,next.group,false));
                } else throw table::ValidationError("Restored native format snapshot does not match cell text");
            } else if(existing==table.cells.end()) next.cells.push_back(createCell(next,cell));
            else if(cell.text==old->text) next.cells.push_back(*existing);
            else {
                // Legacy test documents have no native archive; retain their saved plain text and cell defaults.
                if(table.archive) throw table::ValidationError("Missing native format snapshot");
                next.cells.push_back(createCell(next,cell));
            }
            if(textContent(next.cells.back().text)!=cell.text) throw table::ValidationError("Native formatted copy changed cell content");
        }
        for(const auto& cell:table.cells) {
            if(std::none_of(next.cells.begin(),next.cells.end(),[&](const NativeCell& value){return value.fill==cell.fill;})) checked(art->DisposeArt(cell.fill));
            if(std::none_of(next.cells.begin(),next.cells.end(),[&](const NativeCell& value){return value.text==cell.text;})) checked(art->DisposeArt(cell.text));
        }
        for(const auto& saved:table.snapshots) {
            if(std::none_of(next.snapshots.begin(),next.snapshots.end(),[&](const NativeCell& value){return value.fill==saved.fill;})) checked(art->DisposeArt(saved.fill));
            if(std::none_of(next.snapshots.begin(),next.snapshots.end(),[&](const NativeCell& value){return value.text==saved.text;})) checked(art->DisposeArt(saved.text));
        }
        if(next.archive&&next.snapshots.empty()) {checked(art->DisposeArt(next.archive));next.archive=nullptr;}
        for(auto path:table.edges) checked(art->DisposeArt(path));
        traceStage("structure.oldEdges.removed");
        for(const auto& edge:table::Geometry::visibleEdges(model)) {
            auto path=newPath(next.group);line(path,next,edge);setPaint(path,false,true,0);
            role(path,"edge:"+std::to_string(next.edges.size()));next.edges.push_back(path);
        }
        traceStage("structure.newEdges.created");
        applyGeometry(next,model);
        traceStage("structure.geometry.done");
        styleEdges(next,model);
        auto label=ai::UnicodeString::FromUTF8("修改表格结构");checked(undo->SetUndoTextUS(label,label));table=std::move(next);
        traceStage("structure.done");
    } catch(...) {undo->UndoChanges();throw;}
}
void DocumentAdapter::detach(const NativeTable& table) {
    if(!valid(table)) throw static_cast<ASErr>(kBadParameterErr);
    DictionaryLease dict{dictionaries};checked(art->GetDictionary(table.group,&dict.value));
    checked(dictionaries->DeleteEntry(dict.value,dictionaries->Key(modelKey)));
    if(dictionaries->IsKnown(dict.value,dictionaries->Key(snapshotKey))) checked(dictionaries->DeleteEntry(dict.value,dictionaries->Key(snapshotKey)));
    if(table.archive) checked(art->DisposeArt(table.archive));
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
