#include <windows.h>
#include "HostAcceptance.hpp"
#include "DocumentAdapter.hpp"
#include "AIDocumentList.h"
#include "table/Commands.hpp"
#include "Trace.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace {
template<class S,class R> struct Lease {
    S* suite;R value=nullptr;
    ~Lease(){if(value) suite->Release(value);}
};
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
}
void runHostAcceptance(Suites& suites) {
    traceStage("acceptance.begin");
    auto documents=suites.get<AIDocumentListSuite>(kAIDocumentListSuite,kAIDocumentListVersion);
    AIDocumentHandle created=nullptr;ai::UnicodeString preset;
    // Every run creates its own document; it never edits or closes existing user documents.
    checked(documents->New(preset,nullptr,kDialogOff,&created));
    require(created!=nullptr,"Test document was not created");
    auto text=suites.get<AITextFrameSuite>(kAITextFrameSuite,kAITextFrameVersion);
    auto ranges=suites.get<ATE::TextRangeSuite>(kTextRangeSuite,kTextRangeSuiteVersion);
    auto chars=suites.get<ATE::CharFeaturesSuite>(kCharFeaturesSuite,kCharFeaturesSuiteVersion);
    DocumentAdapter adapter(suites);
    auto model=table::TableModel::create(2,2,table::mm(120),table::mm(60));
    const auto first=model.cellAt(0,0).id;
    model.setText(first,u"AAA BBB");model.setText(model.cellAt(0,1).id,u"中文 12");
    auto native=adapter.createTable(model,{80,500});
    {
        Lease<ATE::TextRangeSuite,ATE::TextRangeRef> range{ranges};
        checked(text->GetATETextRange(native.cells[0].text,&range.value));
        checked(ranges->SetRange(range.value,0,3));
        Lease<ATE::CharFeaturesSuite,ATE::CharFeaturesRef> features{chars};
        checked(ranges->GetUniqueCharFeatures(range.value,&features.value));
        checked(chars->SetFontSize(features.value,24));
        checked(ranges->ReplaceOrAddLocalCharFeatures(range.value,features.value));
    }
    auto fontSize=[&](table::CellId id,int offset) {
        auto found=std::find_if(native.cells.begin(),native.cells.end(),[&](const NativeCell& cell){return cell.id==id;});
        require(found!=native.cells.end(),"Expected native cell is missing");
        Lease<ATE::TextRangeSuite,ATE::TextRangeRef> range{ranges};checked(text->GetATETextRange(found->text,&range.value));
        checked(ranges->SetRange(range.value,offset,offset+1));
        Lease<ATE::CharFeaturesSuite,ATE::CharFeaturesRef> features{chars};checked(ranges->GetUniqueCharFeatures(range.value,&features.value));
        ATE::ATEBool8 assigned=false;ATETextDOM::Float value=0;checked(chars->GetFontSize(features.value,&assigned,&value));
        require(assigned,"Test character font size is not assigned");return value;
    };
    require(std::abs(fontSize(first,0)-24)<0.01&&std::abs(fontSize(first,4)-12)<0.01,"Mixed-font test setup failed");
    traceStage("acceptance.mixedFont.setup.passed");
    adapter.synchronize(native);
    adapter.applyStructure(native,table::applyCommand(native.model,{0,0,0,1},table::Command::Merge));
    adapter.synchronize(native);
    require(native.model.cellById(first).text==u"AAA BBB\n中文 12","Merge changed cell content");
    require(std::abs(fontSize(first,0)-24)<0.01&&std::abs(fontSize(first,4)-12)<0.01,"Merge destroyed mixed character formatting");
    traceStage("acceptance.merge.mixedFont.passed");
    adapter.applyStructure(native,table::applyCommand(native.model,{0,0,0,1},table::Command::Split));
    adapter.synchronize(native);
    require(native.model.cells().size()==4&&native.model.cellAt(0,1).text==u"中文 12","Split did not restore original cells");
    require(std::abs(fontSize(first,0)-24)<0.01&&std::abs(fontSize(first,4)-12)<0.01,"Split destroyed mixed character formatting");
    traceStage("acceptance.split.mixedFont.passed");
    MessageBoxW(nullptr,L"本轮原生合并、拆分和混合字号验收通过。\n测试结果保留在新建文档中。",L"开发验收",MB_OK|MB_ICONINFORMATION);
}
