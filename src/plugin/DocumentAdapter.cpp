#include "DocumentAdapter.hpp"
#include "table/Serialization.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace {
constexpr const char* modelKey="IllustratorTableTool.Model.v3";
constexpr const char* roleKey="IllustratorTableTool.Role.v3";
struct DictionaryLease {AIDictionarySuite* suite;AIDictionaryRef value=nullptr;~DictionaryLease(){if(value)suite->Release(value);}};
bool sameColor(const table::Color& a,const table::Color& b){return a.red==b.red&&a.green==b.green&&a.blue==b.blue;}
bool sameTextStyle(const table::Cell& a,const table::Cell& b){
    const auto& x=a.style;const auto& y=b.style;
    if(a.text!=b.text||x.font!=y.font||x.fontSize!=y.fontSize||!sameColor(x.text,y.text)||x.padding!=y.padding||x.alignment!=y.alignment||x.verticalAlignment!=y.verticalAlignment||
        a.formatting.runs.size()!=b.formatting.runs.size()||a.formatting.paragraphs.size()!=b.formatting.paragraphs.size())return false;
    for(std::size_t i=0;i<a.formatting.runs.size();++i){const auto& p=a.formatting.runs[i];const auto& q=b.formatting.runs[i];
        if(p.start!=q.start||p.length!=q.length||p.font!=q.font||p.fontSize!=q.fontSize||!sameColor(p.color,q.color))return false;}
    for(std::size_t i=0;i<a.formatting.paragraphs.size();++i){const auto& p=a.formatting.paragraphs[i];const auto& q=b.formatting.paragraphs[i];if(p.start!=q.start||p.alignment!=q.alignment)return false;}
    return true;
}
bool sameBounds(table::Rect a,table::Rect b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
}
DocumentAdapter::DocumentAdapter(Suites& value):suites(value),
 art(value.get<AIArtSuite>(kAIArtSuite,kAIArtVersion)),paths(value.get<AIPathSuite>(kAIPathSuite,kAIPathVersion)),
 document(value.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion)),undo(value.get<AIUndoSuite>(kAIUndoSuite,kAIUndoVersion)),
 dictionaries(value.get<AIDictionarySuite>(kAIDictionarySuite,kAIDictionaryVersion)),layers(value.get<AILayerSuite>(kAILayerSuite,kAILayerVersion)),vector(value){}
bool DocumentAdapter::rootEditable(const NativeTable& table)const{
    AIDocumentHandle current=nullptr;
    if(document->GetDocument(&current)||!current||current!=table.document||!table.group||!art->ValidArt(table.group,false))return false;
    auto ancestor=table.group;std::set<AIArtHandle> seen;
    while(ancestor){
        if(seen.size()>1000||!seen.insert(ancestor).second)return false;
        ai::int32 flags=0;if(art->GetArtUserAttr(ancestor,kArtLocked|kArtHidden,&flags)||flags)return false;
        AIArtHandle parent=nullptr;if(art->GetArtParent(ancestor,&parent))return false;ancestor=parent;
    }
    return true;
}
bool DocumentAdapter::valid(const NativeTable& table)const{
    if(!rootEditable(table))return false;
    auto editable=[&](AIArtHandle object){ai::int32 flags=0;return object&&art->ValidArt(object,false)&&!art->GetArtUserAttr(object,kArtLocked|kArtHidden,&flags)&&!flags;};
    if(!editable(table.frame))return false;
    for(const auto& c:table.cells)if(!editable(c.fill)||!editable(c.content))return false;
    for(auto edge:table.edges)if(!editable(edge))return false;
    std::vector<AIArtHandle> pending{table.group};std::set<AIArtHandle> seen;
    while(!pending.empty()){
        const auto item=pending.back();pending.pop_back();
        if(seen.size()>=100000||!seen.insert(item).second||!editable(item))return false;
        short type=0;if(art->GetArtType(item,&type))return false;
        if(type==kPathArt)continue;
        if(type!=kGroupArt&&type!=kCompoundPathArt)return false;
        AIArtHandle child=nullptr;if(art->GetArtFirstChild(item,&child))return false;
        while(child){
            if(pending.size()+seen.size()>=100000)return false;pending.push_back(child);
            AIArtHandle next=nullptr;if(art->GetArtSibling(child,&next))return false;child=next;
        }
    }
    return true;
}
void DocumentAdapter::recordPublishedArtwork(NativeTable& table)const{
    std::size_t stamp=0;checked(art->GetArtTimeStamp(table.group,kAITimeStampMaxFromArtAndChildren,&stamp));table.publishedStamp=stamp;
}
bool DocumentAdapter::matchesPublishedArtwork(const NativeTable& table)const{
    if(!table.publishedStamp||!valid(table))return false;
    std::size_t stamp=0;
    return !art->GetArtTimeStamp(table.group,kAITimeStampMaxFromArtAndChildren,&stamp)&&stamp==*table.publishedStamp;
}
void DocumentAdapter::role(AIArtHandle object,const std::string& value){DictionaryLease d{dictionaries};checked(art->GetDictionary(object,&d.value));checked(dictionaries->SetStringEntry(d.value,dictionaries->Key(roleKey),value.c_str()));}
std::string DocumentAdapter::role(AIArtHandle object)const{
    if(!art->HasDictionary(object))return {};
    DictionaryLease d{dictionaries};checked(art->GetDictionary(object,&d.value));const auto key=dictionaries->Key(roleKey);
    if(!dictionaries->IsKnown(d.value,key))return {};const char* value=nullptr;checked(dictionaries->GetStringEntry(d.value,key,&value));return value?value:"";
}
void DocumentAdapter::writeModel(const NativeTable& table,const table::TableModel& model){
    auto bytes=table::serialize(model);DictionaryLease d{dictionaries};checked(art->GetDictionary(table.group,&d.value));
    checked(dictionaries->SetBinaryEntry(d.value,dictionaries->Key(modelKey),bytes.data(),bytes.size()));
}
std::map<table::CellId,std::shared_ptr<const table::TextLayout>> DocumentAdapter::prepare(const table::TableModel& model,const NativeTable* previous){
    table::serialize(model);
    std::map<table::CellId,std::shared_ptr<const table::TextLayout>> result;
    std::size_t segments=0,objects=2+3*model.cells().size()+table::Geometry::visibleEdges(model).size();
    for(const auto& cell:model.cells()){
        const auto bounds=table::Geometry::cellBounds(model,cell.id);std::shared_ptr<const table::TextLayout> layout;
        if(previous){
            auto old=std::find_if(previous->cells.begin(),previous->cells.end(),[&](const NativeCell& c){return c.id==cell.id;});
            if(old!=previous->cells.end()&&old->layout){
                const auto oldBounds=table::Geometry::cellBounds(previous->model,cell.id);
                if(bounds.right-bounds.left==oldBounds.right-oldBounds.left&&bounds.bottom-bounds.top==oldBounds.bottom-oldBounds.top&&sameTextStyle(cell,previous->model.cellById(cell.id)))layout=old->layout;
            }
        }
        if(!layout)layout=table::layoutText({cell.text,cell.formatting,cell.style,bounds.right-bounds.left,bounds.bottom-bounds.top});
        for(const auto& path:layout->outlines()){
            objects+=1+path.contours.size();
            for(const auto& contour:path.contours){
                if(!contour.closed||contour.segments.empty()||contour.segments.size()>32765)throw table::ValidationError("Unsupported glyph contour");
                segments+=1+contour.segments.size();
            }
            if(objects>100000||segments>1000000)throw table::ValidationError("Table vector complexity exceeds limit");
        }
        result.emplace(cell.id,std::move(layout));
    }
    return result;
}
NativeTable DocumentAdapter::createTable(const table::TableModel& model,AIRealPoint origin){
    auto layouts=prepare(model,nullptr);NativeTable native{model};native.origin=origin;
    checked(document->GetDocument(&native.document));if(!native.document)throw static_cast<ASErr>(kBadParameterErr);
    try{
        checked(art->NewArt(kGroupArt,kPlaceAboveAll,nullptr,&native.group));checked(art->SetArtName(native.group,ai::UnicodeString::FromUTF8("画布表格")));
        native.frame=vector.path(native.group);vector.rectangle(native.frame,{0,0,model.width(),model.height()},native.transform());vector.paint(native.frame,{},false);role(native.frame,"frame");
        for(const auto& cell:model.cells()){
            const auto bounds=table::Geometry::cellBounds(model,cell.id);auto fill=vector.path(native.group);vector.rectangle(fill,bounds,native.transform());vector.paint(fill,cell.style.fill,cell.style.fillEnabled);
            auto content=vector.content(native.group,bounds,native.transform(),*layouts.at(cell.id));role(fill,"fill:"+std::to_string(cell.id));role(content,"content:"+std::to_string(cell.id));
            native.cells.push_back({cell.id,fill,content,layouts.at(cell.id)});
        }
        for(const auto& edge:table::Geometry::visibleEdges(model)){auto p=vector.path(native.group);vector.line(p,edge,native.transform());role(p,"edge:"+std::to_string(native.edges.size()));native.edges.push_back(p);}
        styleEdges(native,model);writeModel(native,model);auditVectorTable(suites,native.group);
        auto label=ai::UnicodeString::FromUTF8("新建表格");checked(undo->SetUndoTextUS(label,label));recordPublishedArtwork(native);return native;
    }catch(...){if(native.group)art->DisposeArt(native.group);throw;}
}
NativeTable DocumentAdapter::readTable(AIArtHandle group){
    DictionaryLease dictionary{dictionaries};checked(art->GetDictionary(group,&dictionary.value));const auto key=dictionaries->Key(modelKey);std::size_t size=0;
    checked(dictionaries->GetBinaryEntry(dictionary.value,key,nullptr,&size));if(!size||size>16*1024*1024)throw table::ValidationError("Invalid model size");
    std::string bytes(size,'\0');checked(dictionaries->GetBinaryEntry(dictionary.value,key,bytes.data(),&size));
    if(size!=bytes.size()||(bytes.substr(0,5)!="ITT3\n"&&bytes.substr(0,5)!="ITT4\n"))throw table::ValidationError("Unsupported table model");
    NativeTable native{table::deserialize(bytes)};native.group=group;checked(document->GetDocument(&native.document));
    std::map<std::string,AIArtHandle> roles;AIArtHandle child=nullptr;checked(art->GetArtFirstChild(group,&child));std::size_t visited=0;
    while(child){
        if(++visited>100000)throw table::ValidationError("Excessive table artwork");const auto name=role(child);
        if(name.empty()||!roles.emplace(name,child).second)throw table::ValidationError("Invalid or duplicate table artwork role");
        AIArtHandle next=nullptr;checked(art->GetArtSibling(child,&next));child=next;
    }
    auto required=[&](const std::string& name,short expected){auto found=roles.find(name);if(found==roles.end())throw table::ValidationError("Missing table artwork");short type=0;
        checked(art->GetArtType(found->second,&type));if(type!=expected)throw table::ValidationError("Unexpected table artwork type");return found->second;};
    native.frame=required("frame",kPathArt);
    for(const auto& c:native.model.cells())native.cells.push_back({c.id,required("fill:"+std::to_string(c.id),kPathArt),required("content:"+std::to_string(c.id),kGroupArt),{}});
    const auto edges=table::Geometry::visibleEdges(native.model);for(std::size_t i=0;i<edges.size();++i)native.edges.push_back(required("edge:"+std::to_string(i),kPathArt));
    if(roles.size()!=1+2*native.cells.size()+native.edges.size()||!valid(native))throw table::ValidationError("Table is locked, hidden or incomplete");
    auditVectorTable(suites,group);
    ai::int16 count=0;checked(paths->GetPathSegmentCount(native.frame,&count));if(count!=4)throw table::ValidationError("Invalid table frame");
    AIPathSegment corners[4]{};checked(paths->GetPathSegments(native.frame,0,4,corners));
    auto same=[](AIReal a,AIReal b){return std::abs(a-b)<0.001;};
    if(!same(corners[0].p.v,corners[1].p.v)||!same(corners[2].p.v,corners[3].p.v)||!same(corners[0].p.h,corners[3].p.h)||!same(corners[1].p.h,corners[2].p.h))throw table::ValidationError("Rotated or sheared table");
    native.scaleX=(corners[1].p.h-corners[0].p.h)/native.model.width();native.scaleY=(corners[0].p.v-corners[3].p.v)/native.model.height();native.origin=corners[0].p;
    if(!std::isfinite(native.scaleX)||!std::isfinite(native.scaleY)||native.scaleX<=0||native.scaleY<=0)throw table::ValidationError("Invalid table transform");
    auto layouts=prepare(native.model,nullptr);for(auto& c:native.cells)c.layout=layouts.at(c.id);recordPublishedArtwork(native);return native;
}
void DocumentAdapter::synchronize(NativeTable& table){
    if(!rootEditable(table))throw table::ValidationError("Table belongs to a closed document or invalid artwork");
    table=readTable(table.group);
}
std::vector<NativeTable> DocumentAdapter::discoverTables(std::vector<RejectedTable>* rejected){
    if(rejected)rejected->clear();std::vector<NativeTable> result;AIDocumentHandle current=nullptr;if(document->GetDocument(&current)||!current)return result;
    std::vector<AIArtHandle> stack;AILayerHandle layer=nullptr;checked(layers->GetFirstLayer(&layer));
    while(layer){AIArtHandle root=nullptr;checked(art->GetFirstArtOfLayer(layer,&root));if(root)stack.push_back(root);AILayerHandle next=nullptr;checked(layers->GetNextLayer(layer,&next));layer=next;}
    std::size_t visited=0;
    while(!stack.empty()){
        auto item=stack.back();stack.pop_back();if(++visited>100000)throw table::ValidationError("Document artwork limit exceeded");
        short type=0;checked(art->GetArtType(item,&type));if(type!=kGroupArt)continue;bool tableGroup=false;
        auto reject=[&](const wchar_t* reason){if(rejected){AIRealRect bounds{};if(!art->GetArtBounds(item,&bounds))rejected->push_back({bounds,reason});}};
        if(art->HasDictionary(item)){
            try{DictionaryLease d{dictionaries};checked(art->GetDictionary(item,&d.value));tableGroup=dictionaries->IsKnown(d.value,dictionaries->Key(modelKey));if(tableGroup)result.push_back(readTable(item));}
            catch(const table::ValidationError&){reject(L"此表格的数据、对象或变换暂不支持编辑，请检查锁定、隐藏和旋转状态。");}
            catch(ASErr){reject(L"此表格的数据类型异常，请使用未损坏的副本。");}
        }
        if(tableGroup)continue;AIArtHandle child=nullptr;checked(art->GetArtFirstChild(item,&child));
        while(child){stack.push_back(child);if(stack.size()>100000)throw table::ValidationError("Document artwork limit exceeded");AIArtHandle next=nullptr;checked(art->GetArtSibling(child,&next));child=next;}
    }
    return result;
}
void DocumentAdapter::styleEdges(const NativeTable& native,const table::TableModel& model){
    const auto edges=table::Geometry::visibleEdges(model);
    for(std::size_t i=0;i<edges.size();++i){const auto& edge=edges[i];const table::Cell* chosen=nullptr;
        auto choose=[&](int r,int c){if(r<0||r>=model.rows()||c<0||c>=model.columns())return;const auto& cell=model.cellAt(r,c);if(!chosen||cell.style.revision>chosen->style.revision)chosen=&cell;};
        if(edge.vertical){choose(edge.segment,edge.track-1);choose(edge.segment,edge.track);}else{choose(edge.track-1,edge.segment);choose(edge.track,edge.segment);}
        if(!chosen)throw table::ValidationError("Missing border owner");vector.paint(native.edges[i],{},false,chosen->style.border,chosen->style.borderWidth*std::sqrt(native.scaleX*native.scaleY));
    }
}
void DocumentAdapter::apply(NativeTable& native,const table::TableModel& model,const char* title){
    if(!valid(native))throw table::ValidationError("Table is not editable");const auto layouts=prepare(model,&native);
    auto next=native;next.model=model;next.cells.clear();next.edges.clear();
    bool bordersChanged=model.cells().size()!=native.model.cells().size();
    try{
        for(const auto& cell:model.cells()){
            auto old=std::find_if(native.cells.begin(),native.cells.end(),[&](const NativeCell& c){return c.id==cell.id;});
            auto bounds=table::Geometry::cellBounds(model,cell.id);const bool existing=old!=native.cells.end();
            const bool moved=!existing||!sameBounds(bounds,table::Geometry::cellBounds(native.model,cell.id));
            if(moved)bordersChanged=true;
            if(existing){const auto& previous=native.model.cellById(cell.id).style;
                if(previous.borderWidth!=cell.style.borderWidth||previous.revision!=cell.style.revision||!sameColor(previous.border,cell.style.border))bordersChanged=true;}
            auto fill=existing?old->fill:vector.path(native.group);
            if(moved)vector.rectangle(fill,bounds,next.transform());
            if(!existing||!sameColor(native.model.cellById(cell.id).style.fill,cell.style.fill)||native.model.cellById(cell.id).style.fillEnabled!=cell.style.fillEnabled)vector.paint(fill,cell.style.fill,cell.style.fillEnabled);
            if(!existing)role(fill,"fill:"+std::to_string(cell.id));
            auto content=existing?old->content:nullptr;
            if(moved||old->layout!=layouts.at(cell.id)){
                content=vector.content(native.group,bounds,next.transform(),*layouts.at(cell.id));role(content,"content:"+std::to_string(cell.id));
                if(existing)checked(art->DisposeArt(old->content));
            }
            next.cells.push_back({cell.id,fill,content,layouts.at(cell.id)});
        }
        for(const auto& old:native.cells)if(std::none_of(next.cells.begin(),next.cells.end(),[&](const NativeCell& c){return c.id==old.id;})){checked(art->DisposeArt(old.content));checked(art->DisposeArt(old.fill));}
        if(bordersChanged){
            for(const auto& edge:table::Geometry::visibleEdges(model)){auto p=vector.path(native.group);vector.line(p,edge,next.transform());role(p,"edge:"+std::to_string(next.edges.size()));next.edges.push_back(p);}
            for(auto old:native.edges)checked(art->DisposeArt(old));
            vector.rectangle(next.frame,{0,0,model.width(),model.height()},next.transform());styleEdges(next,model);
        }else next.edges=native.edges;
        writeModel(next,model);auditVectorTable(suites,next.group);
        const auto label=ai::UnicodeString::FromUTF8(title);checked(undo->SetUndoTextUS(label,label));recordPublishedArtwork(next);native=std::move(next);
    }catch(...){checked(undo->UndoChanges());throw;}
}
void DocumentAdapter::applyGeometry(NativeTable& native,const table::TableModel& model){apply(native,model,"调整表格尺寸");}
void DocumentAdapter::applyStructure(NativeTable& native,const table::TableModel& model,std::optional<TrackDeletion>){apply(native,model,"修改表格结构");}
void DocumentAdapter::applyText(NativeTable& native,const table::TableModel& model){apply(native,model,"输入表格文字");}
void DocumentAdapter::applyStyle(NativeTable& native,table::CellRange range,const table::CellStyle& style){auto model=native.model;model.setStyle(range,style);apply(native,model,"修改单元格样式");}
void DocumentAdapter::applyStylePatch(NativeTable& native,table::CellRange range,const table::CellStylePatch& patch){auto model=native.model;model.patchStyle(range,patch);apply(native,model,"修改单元格样式");}
std::vector<FontChoice> DocumentAdapter::installedFonts(){std::vector<FontChoice> result;for(auto name:table::installedFontFamilies())result.push_back({name,name});return result;}
void DocumentAdapter::detach(const NativeTable& native){
    if(!valid(native))throw table::ValidationError("Table is not editable");DictionaryLease d{dictionaries};checked(art->GetDictionary(native.group,&d.value));
    checked(dictionaries->DeleteEntry(d.value,dictionaries->Key(modelKey)));const auto label=ai::UnicodeString::FromUTF8("转换为普通对象");checked(undo->SetUndoTextUS(label,label));
}
