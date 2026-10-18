#include "table/Serialization.hpp"
#include "table/RichText.hpp"
#include <sstream>
#include <iomanip>
#include <locale>
#include <limits>
#include <cmath>
#include <set>
#include <algorithm>

namespace table {
namespace {
constexpr std::size_t maxDocumentBytes=16*1024*1024, maxTextBytes=1024*1024;
[[noreturn]] void invalid(){throw ValidationError("Invalid or unsupported table data");}
void writeText(std::ostream& out,const std::u16string& text) {
    auto bytes=encodeUtf8(text);if(out.tellp()+static_cast<std::streamoff>(bytes.size()+32)>static_cast<std::streamoff>(maxDocumentBytes)) invalid();
    out<<bytes.size()<<'\n';out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out<<'\n';
}
template<class T> T number(std::istream& in) {T result{};if(!(in>>result)) invalid();return result;}
std::u16string readText(std::istream& in) {
    auto size=number<std::size_t>(in);if(size>maxTextBytes||in.get()!='\n') invalid();
    std::string bytes(size,'\0');if(!in.read(bytes.data(),static_cast<std::streamsize>(size))||in.get()!='\n') invalid();
    return decodeUtf8(bytes);
}
void writeStyle(std::ostream& out,const CellStyle& style) {
    validateStyle(style);
    for(const auto color:{style.fill,style.border,style.text}) out<<color.red<<' '<<color.green<<' '<<color.blue<<' ';
    out<<style.fontSize<<' '<<style.borderWidth<<' '<<style.padding<<' '<<style.alignment<<' '<<style.revision<<' '<<style.fillEnabled<<' '<<style.verticalAlignment<<'\n';
    writeText(out,style.font);
}
CellStyle readStyle(std::istream& in,bool extended) {
    CellStyle style;
    for(auto* color:{&style.fill,&style.border,&style.text}) {
        color->red=number<double>(in);color->green=number<double>(in);color->blue=number<double>(in);
    }
    style.fontSize=number<double>(in);style.borderWidth=number<double>(in);style.padding=number<double>(in);
    style.alignment=number<int>(in);style.revision=number<std::uint64_t>(in);
    style.fillEnabled=true;
    if(extended){const auto fill=number<int>(in);if(fill<0||fill>1)invalid();style.fillEnabled=fill!=0;style.verticalAlignment=number<int>(in);}
    if(in.get()!='\n') invalid();style.font=readText(in);validateStyle(style);return style;
}
void writeFormatting(std::ostream& out,const TextFormatting& format) {
    out<<format.runs.size()<<'\n';
    for(const auto& run:format.runs) {
        out<<run.start<<' '<<run.length<<' '<<run.fontSize<<' '<<run.color.red<<' '<<run.color.green<<' '<<run.color.blue<<'\n';
        writeText(out,run.font);
    }
    out<<format.paragraphs.size()<<'\n';
    for(const auto& p:format.paragraphs) out<<p.start<<' '<<p.alignment<<'\n';
    if(out.tellp()>static_cast<std::streamoff>(maxDocumentBytes)) invalid();
}
TextFormatting readFormatting(std::istream& in,std::u16string_view text) {
    TextFormatting result;auto count=number<std::size_t>(in);
    if(count>maximumFormatRuns||count>text.size()) invalid();
    for(std::size_t i=0;i<count;++i) {
        TextRun run;run.start=number<std::uint32_t>(in);run.length=number<std::uint32_t>(in);run.fontSize=number<double>(in);
        run.color.red=number<double>(in);run.color.green=number<double>(in);run.color.blue=number<double>(in);
        if(in.get()!='\n') invalid();run.font=readText(in);result.runs.push_back(std::move(run));
    }
    count=number<std::size_t>(in);if(count>maximumFormatRuns||count>text.size()+1) invalid();
    for(std::size_t i=0;i<count;++i) {
        ParagraphRun p;p.start=number<std::uint32_t>(in);p.alignment=number<int>(in);
        if(in.get()!='\n') invalid();result.paragraphs.push_back(p);
    }
    validateFormatting(text,result);return result;
}
void validate(const TableModel& model,CellId nextId) {
    const int rows=model.rows(),cols=model.columns();
    if(rows<1||rows>100||cols<1||cols>50||rows*cols>2000||model.cells().empty()||model.cells().size()>static_cast<std::size_t>(rows*cols)) invalid();
    for(const auto* sizes : {&model.widths(),&model.heights()}) for(auto v : *sizes) if(!std::isfinite(v)||v<minimumTrack) invalid();
    if(!std::isfinite(model.width())||!std::isfinite(model.height())) invalid();
    std::vector<bool> occupied(rows*cols,false); std::set<CellId> ids;
    for(const auto& c : model.cells()) {
        validateStyle(c.style);
        validateFormatting(c.text,c.formatting);validateText(c.mergedText);
        if(c.id==0||!ids.insert(c.id).second||c.row<0||c.row>=rows||c.column<0||c.column>=cols||c.rowSpan<1||c.rowSpan>rows-c.row||c.columnSpan<1||c.columnSpan>cols-c.column) invalid();
        for(int r=c.row;r<c.row+c.rowSpan;++r) for(int col=c.column;col<c.column+c.columnSpan;++col) {
            auto slot=r*cols+col;if(occupied[slot]) invalid();occupied[slot]=true;
        }
    }
    if(std::find(occupied.begin(),occupied.end(),false)!=occupied.end()) invalid();
    for(const auto& c : model.cells()) {
        if(c.originals.empty()) {if(c.rowSpan*c.columnSpan!=1) invalid();continue;}
        if(c.originals.size()!=static_cast<std::size_t>(c.rowSpan*c.columnSpan)) invalid();
        std::set<std::pair<int,int>> slots; bool anchor=false;
        for(const auto& original : c.originals) {
            validateStyle(original.style);
            validateFormatting(original.text,original.formatting);
            if(original.row<c.row||original.row>=c.row+c.rowSpan||original.column<c.column||original.column>=c.column+c.columnSpan||
                !slots.insert({original.row,original.column}).second||original.id==0) invalid();
            if(original.row==c.row&&original.column==c.column) {if(original.id!=c.id) invalid();anchor=true;}
            else if(!ids.insert(original.id).second) invalid();
        }
        if(!anchor) invalid();
    }
    if(nextId==0||nextId<=*ids.rbegin()||nextId>std::numeric_limits<CellId>::max()-2000) invalid();
}
}
std::string serialize(const TableModel& model) {
    validate(model,model.nextId);
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<double>::max_digits10);
    out<<"ITT4\n"<<model.rows()<<' '<<model.columns()<<' '<<model.nextId<<'\n';
    for(auto h:model.rowHeights) out<<h<<' ';out<<'\n';
    for(auto w:model.columnWidths) out<<w<<' ';out<<'\n';
    out<<model.anchors.size()<<'\n';
    for(const auto& c:model.anchors) {
        out<<c.id<<' '<<c.row<<' '<<c.column<<' '<<c.rowSpan<<' '<<c.columnSpan<<'\n';
        writeStyle(out,c.style);
        writeText(out,c.text);writeFormatting(out,c.formatting);writeText(out,c.mergedText);out<<c.originals.size()<<'\n';
        for(const auto& original:c.originals) {
            out<<original.id<<' '<<original.row<<' '<<original.column<<'\n';writeStyle(out,original.style);writeText(out,original.text);writeFormatting(out,original.formatting);
        }
    }
    auto result=out.str();if(result.size()>maxDocumentBytes) invalid();return result;
}
TableModel deserialize(std::string_view data) {
    if(data.size()>maxDocumentBytes) invalid();
    std::istringstream in{std::string(data)};in.imbue(std::locale::classic());
    std::string version;std::getline(in,version);if(version!="ITT1"&&version!="ITT2"&&version!="ITT3"&&version!="ITT4") invalid();
    int rows=number<int>(in),cols=number<int>(in);
    if(rows<1||rows>100||cols<1||cols>50||rows*cols>2000) invalid();
    TableModel model;model.nextId=number<CellId>(in);
    for(int r=0;r<rows;++r) model.rowHeights.push_back(number<double>(in));
    for(int c=0;c<cols;++c) model.columnWidths.push_back(number<double>(in));
    auto count=number<std::size_t>(in);if(count<1||count>static_cast<std::size_t>(rows*cols)) invalid();
    std::size_t totalOriginals=0;
    for(std::size_t i=0;i<count;++i) {
        Cell c;c.id=number<CellId>(in);c.row=number<int>(in);c.column=number<int>(in);c.rowSpan=number<int>(in);c.columnSpan=number<int>(in);
        if(in.get()!='\n') invalid();c.style.fillEnabled=true;if(version!="ITT1") c.style=readStyle(in,version=="ITT4");else c.style.font.clear();
        c.text=readText(in);if(version=="ITT3"||version=="ITT4") c.formatting=readFormatting(in,c.text);c.mergedText=readText(in);
        auto originals=number<std::size_t>(in);if(originals>static_cast<std::size_t>(rows*cols)) invalid();
        totalOriginals+=originals;if(totalOriginals>static_cast<std::size_t>(rows*cols)) invalid();
        for(std::size_t j=0;j<originals;++j) {
            OriginalCell original;original.id=number<CellId>(in);original.row=number<int>(in);original.column=number<int>(in);
            if(in.get()!='\n') invalid();original.style.fillEnabled=true;if(version!="ITT1") original.style=readStyle(in,version=="ITT4");else original.style.font.clear();
            original.text=readText(in);if(version=="ITT3"||version=="ITT4") original.formatting=readFormatting(in,original.text);c.originals.push_back(std::move(original));
        }
        model.anchors.push_back(std::move(c));
    }
    in>>std::ws;if(!in.eof()) invalid();validate(model,model.nextId);return model;
}
}
