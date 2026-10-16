#include "table/Commands.hpp"
#include "table/Serialization.hpp"
#include "table/Geometry.hpp"
#include "table/Interaction.hpp"
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
}
int main(int argc,char** argv) {
    try {
        using namespace table;
        if(argc<2) throw ValidationError("Missing simulation command");
        const std::string action=argv[1];
        std::string input;
        if(action!="create") {
            input.assign(std::istreambuf_iterator<char>(std::cin),{});
            if(input.size()>16*1024*1024) throw ValidationError("Simulation input exceeds limit");
        }
        auto model=action=="create" ? TableModel::create(argc>2?integer(argv[2]):4,argc>3?integer(argv[3]):4,mm(120),mm(60)) : deserialize(input);
        CellRange selected{0,0,0,0};
        if(argc>=6) selected={integer(argv[2]),integer(argv[3]),integer(argv[4]),integer(argv[5])};
        if(action=="select") {
            auto center=[&](int r,int c){auto b=Geometry::cellBounds(model,model.cellAt(r,c).id);return Point{(b.left+b.right)/2,(b.top+b.bottom)/2};};
            Interaction interaction;interaction.pointerDown(model,center(selected.firstRow,selected.firstColumn),false,1);
            interaction.pointerDown(model,center(selected.lastRow,selected.lastColumn),true,1);selected=*interaction.selection();
        } else if(action=="command") {
            if(argc!=7) throw ValidationError("Missing structural command");
            model=applyCommand(model,selected,static_cast<Command>(integer(argv[6])));
            selected={0,0,0,0};
        } else if(action=="sample") {
            model.setText(model.cellAt(0,0).id,u"产品名称");model.setText(model.cellAt(0,1).id,u"数量");
            model.setText(model.cellAt(1,0).id,u"中文文字 ABC");model.setText(model.cellAt(1,1).id,u"12");
        } else if(action=="style") {
            auto style=model.cellAt(selected.firstRow,selected.firstColumn).style;
            style.fill={1,0.92,0.70};style.fontSize=18;model.setStyle(selected,style);
        } else if(action!="inspect"&&action!="create") throw ValidationError("Unknown simulation command");
        std::cout<<"{\"state\":"<<json(serialize(model))<<",\"rows\":"<<model.rows()<<",\"columns\":"<<model.columns()
            <<",\"width\":"<<model.width()<<",\"height\":"<<model.height()<<",\"selection\":["<<selected.firstRow<<','<<selected.firstColumn<<','<<selected.lastRow<<','<<selected.lastColumn<<"],\"cells\":[";
        bool first=true;
        for(const auto& cell:model.cells()) {
            if(!first) std::cout<<',';first=false;
            auto b=Geometry::cellBounds(model,cell.id);
            std::cout<<"{\"row\":"<<cell.row<<",\"column\":"<<cell.column<<",\"rowSpan\":"<<cell.rowSpan<<",\"columnSpan\":"<<cell.columnSpan
                <<",\"x\":"<<b.left<<",\"y\":"<<b.top<<",\"width\":"<<b.right-b.left<<",\"height\":"<<b.bottom-b.top
                <<",\"text\":"<<jsonText(cell.text)<<",\"size\":"<<cell.style.fontSize<<",\"fill\":["<<cell.style.fill.red<<','<<cell.style.fill.green<<','<<cell.style.fill.blue<<"]}";
        }
        std::cout<<"]}\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
