#include "table/TableModel.hpp"
#include "table/RichText.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>

namespace table {
void validateStyle(const CellStyle& style) {
    for(const auto color:{style.fill,style.border,style.text})
        for(double value:{color.red,color.green,color.blue})
            if(!std::isfinite(value)||value<0||value>1) throw ValidationError("Invalid color");
    if(!std::isfinite(style.fontSize)||style.fontSize<1||style.fontSize>1000||
        !std::isfinite(style.borderWidth)||style.borderWidth<0||style.borderWidth>100||
        !std::isfinite(style.padding)||style.padding<0||style.padding>mm(100)||
        style.alignment<0||style.alignment>2||style.font.size()>256)
        throw ValidationError("Invalid cell style");
}
void TableModel::setStyle(CellRange range,const CellStyle& value) {
    validateStyle(value);
    if(range.firstRow>range.lastRow) std::swap(range.firstRow,range.lastRow);
    if(range.firstColumn>range.lastColumn) std::swap(range.firstColumn,range.lastColumn);
    cellAt(range.firstRow,range.firstColumn);cellAt(range.lastRow,range.lastColumn);
    auto next=*this;auto style=value;style.revision=0;
    for(const auto& cell:anchors) style.revision=std::max(style.revision,cell.style.revision);
    if(style.revision==std::numeric_limits<std::uint64_t>::max()) throw ValidationError("Style revision limit");
    ++style.revision;
    for(auto& cell:next.anchors) {
        if(cell.row>range.lastRow||cell.column>range.lastColumn||
            cell.row+cell.rowSpan<=range.firstRow||cell.column+cell.columnSpan<=range.firstColumn) continue;
        if(cell.row<range.firstRow||cell.column<range.firstColumn||
            cell.row+cell.rowSpan-1>range.lastRow||cell.column+cell.columnSpan-1>range.lastColumn)
            throw ValidationError("Style range cannot cut a merged cell");
        cell.style=style;
        cell.formatting={};
    }
    *this=std::move(next);
}
TableModel TableModel::create(int rows, int columns, double widthPt, double heightPt) {
    if (rows < 1 || rows > 100 || columns < 1 || columns > 50 || rows * columns > 2000)
        throw ValidationError("Table must have 1-100 rows, 1-50 columns and at most 2000 cells");
    if (!std::isfinite(widthPt) || !std::isfinite(heightPt) ||
        widthPt < columns * minimumTrack || heightPt < rows * minimumTrack)
        throw ValidationError("Each row and column must be at least 2 mm");
    TableModel result;
    result.rowHeights.assign(rows,heightPt/rows);
    result.columnWidths.assign(columns,widthPt/columns);
    for (int row=0; row<rows; ++row) for (int column=0; column<columns; ++column)
        result.anchors.push_back({result.nextId++,row,column,1,1,{}});
    return result;
}
double TableModel::width() const { return std::accumulate(columnWidths.begin(),columnWidths.end(),0.0); }
double TableModel::height() const { return std::accumulate(rowHeights.begin(),rowHeights.end(),0.0); }
const Cell& TableModel::cellAt(int row, int column) const {
    if (row < 0 || row >= rows() || column < 0 || column >= columns())
        throw ValidationError("Cell coordinates out of range");
    for (const auto& cell : anchors)
        if (row >= cell.row && row < cell.row+cell.rowSpan &&
            column >= cell.column && column < cell.column+cell.columnSpan) return cell;
    throw ValidationError("Cell coverage is incomplete");
}
const Cell& TableModel::cellById(CellId id) const {
    auto found=std::find_if(anchors.begin(),anchors.end(),[=](const Cell& c){return c.id==id;});
    if (found == anchors.end()) throw ValidationError("Unknown cell identifier");
    return *found;
}
void TableModel::setText(CellId id, const std::u16string& text) {
    setRichText(id,text,{});
}
void TableModel::setRichText(CellId id,const std::u16string& text,const TextFormatting& formatting) {
    validateFormatting(text,formatting);
    auto found=std::find_if(anchors.begin(),anchors.end(),[=](const Cell& c){return c.id==id;});
    if (found == anchors.end()) throw ValidationError("Unknown cell identifier");
    auto next=*found;next.text=text;next.formatting=formatting;*found=std::move(next);
}
void TableModel::resizeBoundary(bool vertical, int boundary, double delta) {
    auto& sizes=vertical ? columnWidths : rowHeights;
    if (boundary<1 || boundary>static_cast<int>(sizes.size()) || !std::isfinite(delta))
        throw ValidationError("Invalid resize boundary or delta");
    double lower=minimumTrack-sizes[boundary-1];
    double upper=boundary<static_cast<int>(sizes.size()) ? sizes[boundary]-minimumTrack : std::numeric_limits<double>::max()-(vertical ? width() : height());
    delta=std::clamp(delta,lower,upper);
    sizes[boundary-1]+=delta;
    if (boundary<static_cast<int>(sizes.size())) sizes[boundary]-=delta;
}
void TableModel::scale(double horizontal,double vertical) {
    if(!std::isfinite(horizontal)||!std::isfinite(vertical)||horizontal<=0||vertical<=0) throw ValidationError("Invalid table scale");
    auto next=*this;
    for(auto& value:next.columnWidths) {value*=horizontal;if(!std::isfinite(value)||value+1e-8<minimumTrack) throw ValidationError("Scaled column is too small");value=std::max(value,minimumTrack);}
    for(auto& value:next.rowHeights) {value*=vertical;if(!std::isfinite(value)||value+1e-8<minimumTrack) throw ValidationError("Scaled row is too small");value=std::max(value,minimumTrack);}
    if(!std::isfinite(next.width())||!std::isfinite(next.height())) throw ValidationError("Scaled dimensions overflow");
    *this=std::move(next);
}
void TableModel::merge(CellRange range) {
    if(range.firstRow>range.lastRow) std::swap(range.firstRow,range.lastRow);
    if(range.firstColumn>range.lastColumn) std::swap(range.firstColumn,range.lastColumn);
    cellAt(range.firstRow,range.firstColumn); cellAt(range.lastRow,range.lastColumn);
    std::vector<Cell> included;
    for(const auto& cell : anchors) {
        if(cell.row>range.lastRow || cell.column>range.lastColumn ||
           cell.row+cell.rowSpan<=range.firstRow || cell.column+cell.columnSpan<=range.firstColumn) continue;
        if(cell.row<range.firstRow || cell.column<range.firstColumn ||
           cell.row+cell.rowSpan-1>range.lastRow || cell.column+cell.columnSpan-1>range.lastColumn)
            throw ValidationError("Merge range cannot cut an existing merged cell");
        included.push_back(cell);
    }
    if(included.size()<2) return;
    std::sort(included.begin(),included.end(),[](const Cell& a,const Cell& b){return a.row==b.row ? a.column<b.column : a.row<b.row;});
    Cell merged=included.front(); merged.text.clear(); merged.originals.clear();merged.formatting={};
    merged.rowSpan=range.lastRow-range.firstRow+1; merged.columnSpan=range.lastColumn-range.firstColumn+1;
    bool first=true;
    for(const auto& cell : included) {
        appendFormattedText(merged.text,merged.formatting,cell.text,cell.formatting,cell.style,!first);first=false;
        if(cell.originals.empty()) merged.originals.push_back({cell.id,cell.row,cell.column,cell.text,cell.style,cell.formatting});
        else {
            auto saved=cell.originals;
            if(cell.text!=cell.mergedText) for(auto& original : saved)
                if(original.row==cell.row && original.column==cell.column) {original.text=cell.text;original.formatting=cell.formatting;}
            merged.originals.insert(merged.originals.end(),saved.begin(),saved.end());
        }
    }
    merged.mergedText=merged.text;
    auto next=*this;
    next.anchors.erase(std::remove_if(next.anchors.begin(),next.anchors.end(),[&](const Cell& cell){
        return cell.row>=range.firstRow&&cell.row<=range.lastRow&&cell.column>=range.firstColumn&&cell.column<=range.lastColumn;
    }),next.anchors.end());
    next.anchors.push_back(std::move(merged)); *this=std::move(next);
}
void TableModel::split(CellId id) {
    const auto cell=cellById(id);
    if(cell.rowSpan==1 && cell.columnSpan==1) return;
    if(cell.originals.size()!=static_cast<std::size_t>(cell.rowSpan*cell.columnSpan)) throw ValidationError("Merged cell originals are incomplete");
    auto next=*this;
    next.anchors.erase(std::remove_if(next.anchors.begin(),next.anchors.end(),[=](const Cell& c){return c.id==id;}),next.anchors.end());
    for(const auto& original : cell.originals) {
        Cell restored{original.id,original.row,original.column,1,1,original.text};
        restored.style=original.style;
        restored.formatting=original.formatting;
        if(original.row==cell.row && original.column==cell.column && cell.text!=cell.mergedText) {restored.text=cell.text;restored.formatting=cell.formatting;}
        next.anchors.push_back(std::move(restored));
    }
    *this=std::move(next);
}
void TableModel::changeTracks(bool vertical,int first,int count,bool insert) {
    const int total=vertical ? columns() : rows();
    const int limit=vertical ? 50 : 100;
    if(count<=0 || first<0 || first>total ||
        (insert ? count>limit-total : count>=total || first>total-count))
        throw ValidationError("Invalid row or column range");
    const int remaining=insert ? total+count : total-count;
    if(remaining*(vertical ? rows() : columns())>2000) throw ValidationError("Table exceeds 2000 cells");
    auto next=*this;
    auto& sizes=vertical ? next.columnWidths : next.rowHeights;
    auto position=[=](const OriginalCell& cell){ return vertical ? cell.column : cell.row; };
    auto moveOriginal=[=](OriginalCell& cell,int p){ (vertical ? cell.column : cell.row)=p; };
    if(insert) {
        double size=sizes[std::min(first,total-1)];
        sizes.insert(sizes.begin()+first,count,size);
        for(auto& cell : next.anchors) {
            auto& begin=vertical ? cell.column : cell.row;
            auto& span=vertical ? cell.columnSpan : cell.rowSpan;
            if(first<=begin) {
                begin+=count;
                for(auto& original : cell.originals) moveOriginal(original,position(original)+count);
            } else if(first<begin+span) {
                span+=count;
                for(auto& original : cell.originals) if(position(original)>=first) moveOriginal(original,position(original)+count);
                for(int p=first;p<first+count;++p) {
                    const int crossBegin=vertical ? cell.row : cell.column;
                    const int crossSpan=vertical ? cell.rowSpan : cell.columnSpan;
                    for(int q=crossBegin;q<crossBegin+crossSpan;++q)
                        cell.originals.push_back({next.nextId++,vertical ? q : p,vertical ? p : q,{}});
                }
            }
        }
        for(int r=0;r<next.rows();++r) for(int c=0;c<next.columns();++c) {
            bool covered=false;
            for(const auto& cell : next.anchors)
                if(r>=cell.row&&r<cell.row+cell.rowSpan&&c>=cell.column&&c<cell.column+cell.columnSpan) {covered=true;break;}
            if(!covered) next.anchors.push_back({next.nextId++,r,c,1,1,{}});
        }
    } else {
        sizes.erase(sizes.begin()+first,sizes.begin()+first+count);
        std::vector<Cell> survivors;
        for(auto cell : next.anchors) {
            auto& begin=vertical ? cell.column : cell.row;
            auto& span=vertical ? cell.columnSpan : cell.rowSpan;
            const int oldBegin=begin, oldSpan=span;
            const int overlap=std::max(0,std::min(begin+span,first+count)-std::max(begin,first));
            if(overlap==oldSpan) continue;
            OriginalCell savedAnchor{};
            for(const auto& original : cell.originals) if(original.id==cell.id) savedAnchor=original;
            span-=overlap;
            begin=begin>=first+count ? begin-count : begin>=first ? first : begin;
            cell.originals.erase(std::remove_if(cell.originals.begin(),cell.originals.end(),[&](const OriginalCell& original){
                return position(original)>=first && position(original)<first+count;
            }),cell.originals.end());
            for(auto& original : cell.originals) if(position(original)>=first+count) moveOriginal(original,position(original)-count);
            if(oldBegin>=first && oldBegin<first+count) {
                for(auto& original : cell.originals) if(original.row==cell.row && original.column==cell.column) {
                    original.id=cell.id;
                    if(!savedAnchor.text.empty()) {
                        std::u16string joined;TextFormatting joinedFormat;
                        appendFormattedText(joined,joinedFormat,savedAnchor.text,savedAnchor.formatting,savedAnchor.style,false);
                        if(!original.text.empty()) appendFormattedText(joined,joinedFormat,original.text,original.formatting,original.style,true);
                        original.text=std::move(joined);original.formatting=std::move(joinedFormat);
                    }
                }
            }
            survivors.push_back(std::move(cell));
        }
        next.anchors=std::move(survivors);
    }
    *this=std::move(next);
}
}
