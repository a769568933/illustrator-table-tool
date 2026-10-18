#include "table/Commands.hpp"
#include "table/Serialization.hpp"
#include "table/Geometry.hpp"
#include "table/Interaction.hpp"
#include "table/TextEditor.hpp"
#include "table/RichText.hpp"
#include <iostream>
#include <iterator>
#include <sstream>
#include <iomanip>

namespace {
std::string json(const std::string& text) {
    std::ostringstream out;out<<'"';
    for(unsigned char c:text) {
        if(c=='"'||c=='\\') out<<'\\'<<c;
        else if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<int>(c)<<std::dec;
        else out<<c;
    }
    return out.str()+'"';
}
std::string jsonText(const std::u16string& text) {
    std::ostringstream out;out<<'"';
    for(char16_t c:text) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<unsigned>(c);
    return out.str()+'"';
}
int integer(const char* input) {
    std::size_t used=0;int value=std::stoi(input,&used);if(input[used]) throw table::ValidationError("Invalid number");return value;
}
std::string block(std::istream& stream,std::size_t maximum){
    std::string count;std::getline(stream,count);std::size_t used=0;const auto size=std::stoull(count,&used);
    if(used!=count.size()||size>maximum)throw table::ValidationError("Invalid text block size");
    std::string result(static_cast<std::size_t>(size),0);
    if(!stream.read(result.data(),static_cast<std::streamsize>(size))||stream.get()!='\n')throw table::ValidationError("Incomplete text block");return result;
}
}
int main(int argc,char** argv) {
    try {
        using namespace table;
        if(argc<2) throw ValidationError("Missing simulation command");
        const std::string action=argv[1];
        const bool editing=action=="edit"||action=="selectText"||action=="deleteCluster"||action=="tab";
        std::string input;
        if(action!="create") {
            input.assign(std::istreambuf_iterator<char>(std::cin),{});
            if(input.size()>(editing?20:16)*1024*1024) throw ValidationError("Simulation input exceeds limit");
        }
        std::string inserted;std::vector<std::uint32_t> boundaries;
        if(action=="format"){std::istringstream protocol(input);input=block(protocol,16*1024*1024);inserted=block(protocol,2048);}
        if(editing){std::istringstream protocol(input);input=block(protocol,16*1024*1024);inserted=block(protocol,maximumTextBytes);
            std::uint64_t boundary=0;while(protocol>>boundary){if(boundary>maximumTextBytes||boundaries.size()>maximumTextBytes)throw ValidationError("Invalid text boundary");boundaries.push_back(static_cast<std::uint32_t>(boundary));}
            if(!protocol.eof())throw ValidationError("Invalid text boundary data");}
        const int rows=action=="create"&&argc>2?integer(argv[2]):4,columns=action=="create"&&argc>3?integer(argv[3]):4;
        auto model=action=="create" ? TableModel::create(rows,columns,std::max(mm(120),columns*minimumTrack),std::max(mm(60),rows*minimumTrack)) : deserialize(input);
        CellRange selected{0,0,0,0};
        std::optional<EditSession> editor;
        if(!editing&&argc>=6) selected={integer(argv[2]),integer(argv[3]),integer(argv[4]),integer(argv[5])};
        if(editing){
            if(argc!=7)throw ValidationError("Missing text edit selection");
            const int anchor=integer(argv[4]),caret=integer(argv[5]);if(anchor<0||caret<0)throw ValidationError("Invalid text selection");
            const auto& cell=model.cellAt(integer(argv[2]),integer(argv[3]));EditSession session{cell.id,static_cast<std::uint32_t>(anchor),static_cast<std::uint32_t>(caret),0};
            if(action=="selectText"){
                if(!isTextBoundary(cell.text,session.anchor)||!isTextBoundary(cell.text,session.caret))throw ValidationError("Invalid text selection");editor=session;
            }else{
                auto change=action=="edit"?replaceSelection(model,session,decodeUtf8(inserted)):
                    action=="tab"?navigateCell(model,session,integer(argv[6])!=0):eraseCluster(model,session,boundaries,integer(argv[6])!=0);
                model=std::move(change.model);editor=change.session;
            }
            const auto& active=model.cellById(editor->cell);selected={active.row,active.column,active.row+active.rowSpan-1,active.column+active.columnSpan-1};
        }
        if(action=="select"||action=="dragSelect") {
            auto center=[&](int r,int c){auto b=Geometry::cellBounds(model,model.cellAt(r,c).id);return Point{(b.left+b.right)/2,(b.top+b.bottom)/2};};
            Interaction interaction;interaction.pointerDown(model,center(selected.firstRow,selected.firstColumn),false,1);
            if(action=="dragSelect"){interaction.pointerMove(center(selected.lastRow,selected.lastColumn));interaction.pointerUp();}
            else interaction.pointerDown(model,center(selected.lastRow,selected.lastColumn),true,1);
            selected=*interaction.selection();
        } else if(action=="command") {
            if(argc!=7) throw ValidationError("Missing structural command");
            const auto command=static_cast<Command>(integer(argv[6]));
            model=applyCommand(model,selected,command);
            if(command!=Command::DistributeRows&&command!=Command::DistributeColumns)selected={0,0,0,0};
        } else if(action=="sample") {
            model.setText(model.cellAt(0,0).id,u"产品名称");model.setText(model.cellAt(0,1).id,u"数量");
            model.setText(model.cellAt(1,0).id,u"中文文字 ABC");model.setText(model.cellAt(1,1).id,u"12");
        } else if(action=="style") {
            auto style=model.cellAt(selected.firstRow,selected.firstColumn).style;
            style.fill={1,0.92,0.70};style.fillEnabled=true;style.fontSize=18;model.setStyle(selected,style);
        } else if(action=="format"){
            if(argc!=11)throw ValidationError("Missing style patch");const auto mask=integer(argv[6]);CellStylePatch patch;
            if(mask&1)patch.font=decodeUtf8(inserted);if(mask&2)patch.fontSize=std::stod(argv[7]);if(mask&4)patch.alignment=integer(argv[8]);
            if(mask&8)patch.verticalAlignment=integer(argv[9]);if(mask&16)patch.fillEnabled=integer(argv[10])!=0;model.patchStyle(selected,patch);
        } else if(action!="inspect"&&action!="create"&&!editing) throw ValidationError("Unknown simulation command");
        std::cout<<"{\"state\":"<<json(serialize(model))<<",\"rows\":"<<model.rows()<<",\"columns\":"<<model.columns()
            <<",\"width\":"<<model.width()<<",\"height\":"<<model.height()<<",\"selection\":["<<selected.firstRow<<','<<selected.firstColumn<<','<<selected.lastRow<<','<<selected.lastColumn<<"],\"cells\":[";
        bool first=true;
        for(const auto& cell:model.cells()) {
            if(!first) std::cout<<',';first=false;
            auto b=Geometry::cellBounds(model,cell.id);
            std::cout<<"{\"id\":"<<cell.id<<",\"row\":"<<cell.row<<",\"column\":"<<cell.column<<",\"rowSpan\":"<<cell.rowSpan<<",\"columnSpan\":"<<cell.columnSpan
                <<",\"x\":"<<b.left<<",\"y\":"<<b.top<<",\"width\":"<<b.right-b.left<<",\"height\":"<<b.bottom-b.top
                <<",\"text\":"<<jsonText(cell.text)<<",\"size\":"<<cell.style.fontSize<<",\"fillEnabled\":"<<(cell.style.fillEnabled?"true":"false")<<",\"font\":"<<jsonText(cell.style.font)<<",\"alignment\":"<<cell.style.alignment<<",\"verticalAlignment\":"<<cell.style.verticalAlignment<<",\"fill\":["<<cell.style.fill.red<<','<<cell.style.fill.green<<','<<cell.style.fill.blue<<"],\"runs\":[";
            bool firstRun=true;for(const auto& run:cell.formatting.runs){if(!firstRun)std::cout<<',';firstRun=false;
                std::cout<<"{\"start\":"<<run.start<<",\"length\":"<<run.length<<",\"size\":"<<run.fontSize<<",\"font\":"<<jsonText(run.font)<<",\"color\":["<<run.color.red<<','<<run.color.green<<','<<run.color.blue<<"]}";}
            std::cout<<"]}";
        }
        std::cout<<"],\"editor\":";
        if(editor)std::cout<<"{\"cellId\":"<<editor->cell<<",\"anchor\":"<<editor->anchor<<",\"caret\":"<<editor->caret<<",\"batch\":"<<editor->batch<<'}';else std::cout<<"null";
        std::cout<<"}\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
