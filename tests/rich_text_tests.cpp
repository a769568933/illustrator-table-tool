#include "table/TableModel.hpp"
#include "table/RichText.hpp"
#include "table/Serialization.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace table;
namespace {
int checks=0;
void expect(bool value,const char* name) {++checks;if(!value) throw std::runtime_error(name);}
template<class F> void rejects(F fn) {bool caught=false;try{fn();}catch(const ValidationError&){caught=true;}expect(caught,"invalid rich text accepted");}
TextFormatting format(double size) {return {{{0,1,u"",size,{0,0,0}}},{{0,0}}};}
void rich_text_roundtrip() {
    auto m=TableModel::create(1,2,mm(40),mm(10));
    m.setRichText(m.cellAt(0,0).id,u"甲",format(24));
    m.setRichText(m.cellAt(0,1).id,u"乙",format(12));
    auto data=serialize(m);expect(data.substr(0,5)=="ITT3\n","new model format");
    auto copy=deserialize(data);
    expect(copy.cellAt(0,0).text==u"甲","Unicode roundtrip");
    expect(copy.cellAt(0,0).formatting.runs.at(0).fontSize==24,"24pt survives persistence");
    expect(copy.cellAt(0,1).formatting.runs.at(0).fontSize==12,"12pt survives persistence");
    expect(serialize(copy)==data,"deterministic rich text roundtrip");
    m.merge({0,0,0,1});auto merged=deserialize(serialize(m));merged.split(merged.cellAt(0,0).id);
    expect(merged.cellAt(0,0).formatting.runs.at(0).fontSize==24,"saved snapshot restores format");
}
void merge_split_formats() {
    auto m=TableModel::create(1,2,mm(40),mm(10));
    const auto id=m.cellAt(0,0).id;
    m.setRichText(id,u"甲",format(24));m.setRichText(m.cellAt(0,1).id,u"乙",format(12));
    m.merge({0,0,0,1});
    expect(m.cellAt(0,0).text==u"甲\n乙","row order and separator");
    const auto& runs=m.cellAt(0,0).formatting.runs;
    expect(runs.front().start==0&&runs.front().fontSize==24,"first merge format");
    expect(runs.back().start==2&&runs.back().fontSize==12,"second merge format offset");
    auto style=m.cellAt(0,0).style;style.fontSize=32;style.alignment=2;m.setStyle({0,0,0,1},style);
    m.split(id);
    expect(m.cellAt(0,0).formatting.runs.at(0).fontSize==24,"style-only edit restores original 24pt");
    expect(m.cellAt(0,1).formatting.runs.at(0).fontSize==12,"style-only edit restores original 12pt");
    m.merge({0,0,0,1});m.setRichText(id,u"新",format(18));m.split(id);
    expect(m.cellAt(0,0).text==u"新"&&m.cellAt(0,0).formatting.runs.at(0).fontSize==18,"edited merge keeps new text and format");
    expect(m.cellAt(0,1).text==u"乙"&&m.cellAt(0,1).formatting.runs.at(0).fontSize==12,"edited merge keeps other original");
}
void inherited_style_is_preserved() {
    auto m=TableModel::create(1,2,mm(40),mm(10));
    m.setText(m.cellAt(0,0).id,u"A");m.setText(m.cellAt(0,1).id,u"B");
    auto style=m.cellAt(0,1).style;style.fontSize=20;style.alignment=2;m.setStyle({0,1,0,1},style);
    m.merge({0,0,0,1});
    expect(m.cellAt(0,0).formatting.runs.back().fontSize==20,"second cell implicit size materialized");
    expect(m.cellAt(0,0).formatting.paragraphs.back().start==2&&m.cellAt(0,0).formatting.paragraphs.back().alignment==2,"second paragraph alignment retained");
}
void reject_bad_ranges() {
    rejects([]{validateFormatting(u"A",{{{0,2,u"",12,{}}},{}});});
    rejects([]{validateFormatting(u"AB",{{{0,2,u"",12,{}},{1,1,u"",12,{}}},{}});});
    rejects([]{validateFormatting(u"\U0001F600",{{{1,1,u"",12,{}}},{}});});
    rejects([]{validateFormatting(u"A",{{{0,0,u"",12,{}}},{}});});
    rejects([]{validateFormatting(u"A",{{{0,1,u"",0,{}}},{}});});
    rejects([]{validateFormatting(u"AB",{{},{{0,0},{0,1}}});});
    rejects([]{validateFormatting(u"AB",{{},{{1,0}}});});
    rejects([]{validateFormatting(u"A",{{},{{0,3}}});});
    auto m=TableModel::create(1,1,mm(20),mm(10));const auto id=m.cellAt(0,0).id;
    const auto before=serialize(m);
    rejects([&]{m.setRichText(id,u"A",{{{0,2,u"",12,{}}},{}});});
    expect(serialize(m)==before,"failed setRichText is atomic");
    rejects([&]{m.setText(id,std::u16string(1,char16_t(0xd800)));});
    rejects([&]{m.setText(id,std::u16string(1,0));});
    rejects([&]{m.setText(id,std::u16string(1024*1024+1,u'A'));});
    rejects([&]{m.setText(id,std::u16string(350000,u'甲'));});
}
void merged_tracks_keep_formats() {
    auto m=TableModel::create(2,2,mm(40),mm(20));
    const auto id=m.cellAt(0,0).id;
    m.setRichText(id,u"甲",format(24));m.setRichText(m.cellAt(1,1).id,u"乙",format(18));
    m.merge({0,0,1,1});m.insertRows(1,1);m.insertColumns(1,1);
    auto copy=deserialize(serialize(m));copy.split(id);
    expect(copy.cellAt(0,0).formatting.runs.at(0).fontSize==24,"insert retains anchor format");
    expect(copy.cellAt(2,2).formatting.runs.at(0).fontSize==18,"insert moves snapshot format");
    m.eraseRows(0,1);auto remaining=deserialize(serialize(m));remaining.split(id);
    expect(remaining.cellAt(0,0).text.find(u"甲")!=std::u16string::npos,"deleted anchor text retained");
    expect(remaining.cellAt(0,0).formatting.runs.front().fontSize==24,"deleted anchor format retained");
}
void reject_bad_serialized_runs() {
    auto m=TableModel::create(1,1,mm(20),mm(10));
    m.setRichText(m.cellAt(0,0).id,u"A",format(12));auto bytes=serialize(m);
    const auto marker=bytes.find("1\nA\n");expect(marker!=std::string::npos,"fixture text exists");
    bytes.replace(marker+4,1,"999999999999");
    rejects([&]{deserialize(bytes);});
}
void document_budget() {
    auto m=TableModel::create(1,20,mm(100),mm(10));
    const std::u16string chunk(1024*1024,u'A');
    for(const auto& cell:m.cells()) m.setText(cell.id,chunk);
    rejects([&]{serialize(m);});
}
}
int main() {try {
    rich_text_roundtrip();merge_split_formats();inherited_style_is_preserved();reject_bad_ranges();merged_tracks_keep_formats();reject_bad_serialized_runs();document_budget();
    std::cout<<checks<<" rich text checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
