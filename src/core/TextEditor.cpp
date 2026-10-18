#include "table/TextEditor.hpp"
#include "table/RichText.hpp"
#include <algorithm>
#include <limits>

namespace table {
namespace {
const Cell& checkedCell(const TableModel& model,const EditSession& session) {
    const auto& cell=model.cellById(session.cell);
    if(!isTextBoundary(cell.text,session.anchor)||!isTextBoundary(cell.text,session.caret)) throw ValidationError("Invalid text selection");
    return cell;
}
std::u16string normalized(std::u16string_view input) {
    validateText(input);std::u16string result;result.reserve(input.size());
    for(std::size_t i=0;i<input.size();++i) {
        if(input[i]==u'\r') {result+=u'\n';if(i+1<input.size()&&input[i+1]==u'\n')++i;}
        else result+=input[i];
    }
    return result;
}
TextFormatting replaceFormat(const Cell& cell,std::u16string_view value,std::uint32_t first,std::uint32_t last,std::uint32_t inserted) {
    const auto source=materializeFormatting(cell.text,cell.formatting,cell.style);
    TextFormatting result;
    TextRun insertion{first,inserted,cell.style.font,cell.style.fontSize,cell.style.text};
    const auto position=first==cell.text.size()&&first>0?first-1:first;
    for(auto run:source.runs) {
        if(position>=run.start&&position<run.start+run.length) {insertion.font=run.font;insertion.fontSize=run.fontSize;insertion.color=run.color;}
        if(run.start<first) {run.length=std::min(run.start+run.length,first)-run.start;if(run.length) result.runs.push_back(std::move(run));}
    }
    if(inserted) result.runs.push_back(std::move(insertion));
    for(auto run:source.runs) {
        const auto end=run.start+run.length;
        if(end<=last) continue;
        const auto start=std::max(run.start,last);run.start=first+inserted+(start-last);run.length=end-start;
        result.runs.push_back(std::move(run));
    }
    auto alignment=[&](std::uint32_t oldPosition) {
        int value=cell.style.alignment;
        for(const auto& p:source.paragraphs) {if(p.start>oldPosition) break;value=p.alignment;}
        return value;
    };
    for(std::uint32_t start=0;;) {
        const auto oldPosition=start<=first?start:start<=first+inserted?first:last+(start-first-inserted);
        result.paragraphs.push_back({start,alignment(oldPosition)});
        if(result.paragraphs.size()>maximumFormatRuns) throw ValidationError("Too many paragraphs");
        const auto next=value.find(u'\n',start);if(next==std::u16string_view::npos) break;
        start=static_cast<std::uint32_t>(next+1);
    }
    return materializeFormatting(value,result,cell.style);
}
}
EditChange replaceSelection(const TableModel& model,const EditSession& session,std::u16string_view input) {
    const auto& cell=checkedCell(model,session);const auto text=normalized(input);
    const auto first=std::min(session.anchor,session.caret),last=std::max(session.anchor,session.caret);
    auto value=cell.text.substr(0,first)+text+cell.text.substr(last);validateText(value);
    auto nextSession=session;nextSession.anchor=nextSession.caret=first+static_cast<std::uint32_t>(text.size());
    if(value==cell.text) return {model,nextSession,false,false};
    auto format=replaceFormat(cell,value,first,last,static_cast<std::uint32_t>(text.size()));
    auto next=model;next.setRichText(cell.id,value,format);return {std::move(next),nextSession,true,false};
}
EditChange eraseCluster(const TableModel& model,const EditSession& session,const std::vector<std::uint32_t>& boundaries,bool backward) {
    const auto& cell=checkedCell(model,session);
    if(boundaries.empty()||boundaries.size()>cell.text.size()+1||boundaries.front()!=0||boundaries.back()!=cell.text.size())
        throw ValidationError("Invalid layout cluster boundaries");
    std::uint32_t previous=0;bool first=true;
    for(const auto boundary:boundaries) {
        if((!first&&boundary<=previous)||!isTextBoundary(cell.text,boundary)) throw ValidationError("Invalid layout cluster boundaries");
        first=false;previous=boundary;
    }
    if(!std::binary_search(boundaries.begin(),boundaries.end(),session.anchor)||!std::binary_search(boundaries.begin(),boundaries.end(),session.caret))
        throw ValidationError("Selection splits a text cluster");
    if(session.anchor!=session.caret) return replaceSelection(model,session,u"");
    auto selection=session;
    const auto position=std::lower_bound(boundaries.begin(),boundaries.end(),session.caret);
    if(backward) {
        if(position==boundaries.begin()) return {model,session,false,false};
        selection.anchor=*(position-1);
    } else {
        if(position+1==boundaries.end()) return {model,session,false,false};
        selection.caret=*(position+1);
    }
    return replaceSelection(model,selection,u"");
}
EditChange navigateCell(const TableModel& model,const EditSession& session,bool backward) {
    checkedCell(model,session);
    std::vector<const Cell*> cells;for(const auto& cell:model.cells())cells.push_back(&cell);
    std::sort(cells.begin(),cells.end(),[](const Cell* a,const Cell* b){return a->row==b->row?a->column<b->column:a->row<b->row;});
    const auto it=std::find_if(cells.begin(),cells.end(),[&](const Cell* c){return c->id==session.cell;});
    if(backward&&it==cells.begin()) return {model,session,false,false};
    if(session.batch==std::numeric_limits<std::uint64_t>::max()) throw ValidationError("Input session sequence limit");
    auto next=model;bool added=false;CellId id=0;
    if(backward) id=(*(it-1))->id;
    else if(it+1!=cells.end()) id=(*(it+1))->id;
    else {next.insertRows(next.rows(),1);id=next.cellAt(next.rows()-1,0).id;added=true;}
    return {std::move(next),{id,0,0,session.batch+1},added,added};
}
}
