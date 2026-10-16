#include <windows.h>
#include "HostAcceptance.hpp"
#include "DocumentAdapter.hpp"
#include "AIDocumentList.h"
#include "AIFilePath.h"
#include "AIFileFormat.h"
#include "table/Commands.hpp"
#include "Trace.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>
extern "C" {AIFilePathSuite* sAIFilePath=nullptr;}

namespace {
template<class S,class R> struct Lease {
    S* suite;R value=nullptr;
    ~Lease(){if(value) suite->Release(value);}
};
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
std::wstring requestPath() {
    wchar_t directory[MAX_PATH]{};const auto length=GetTempPathW(MAX_PATH,directory);
    if(!length||length>=MAX_PATH) return {};
    return std::wstring(directory)+L"IllustratorTableTool.run-acceptance";
}
}
void runHostAcceptance(Suites& suites,bool interactive,AIDocumentHandle* createdDocument) {
    traceStage("acceptance.begin");
    auto documents=suites.get<AIDocumentListSuite>(kAIDocumentListSuite,kAIDocumentListVersion);
    AIDocumentHandle created=nullptr;ai::UnicodeString preset;
    // Every run creates its own document; it never edits or closes existing user documents.
    checked(documents->New(preset,nullptr,kDialogOff,&created));
    require(created!=nullptr,"Test document was not created");
    if(createdDocument) *createdDocument=created;
    auto text=suites.get<AITextFrameSuite>(kAITextFrameSuite,kAITextFrameVersion);
    auto ranges=suites.get<ATE::TextRangeSuite>(kTextRangeSuite,kTextRangeSuiteVersion);
    auto chars=suites.get<ATE::CharFeaturesSuite>(kCharFeaturesSuite,kCharFeaturesSuiteVersion);
    auto paragraphs=suites.get<ATE::ParaFeaturesSuite>(kParaFeaturesSuite,kParaFeaturesSuiteVersion);
    auto art=suites.get<AIArtSuite>(kAIArtSuite,kAIArtVersion);
    auto document=suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion);
    DocumentAdapter adapter(suites);
    auto model=table::TableModel::create(2,2,table::mm(120),table::mm(60));
    const auto first=model.cellAt(0,0).id;
    model.setText(first,u"AAA BBB");model.setText(model.cellAt(0,1).id,u"中文 12");
    table::CellStyle rightAligned;rightAligned.alignment=2;model.setStyle({0,1,0,1},rightAligned);
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
    auto fontSizeIn=[&](const NativeTable& subject,table::CellId id,int offset) {
        auto found=std::find_if(subject.cells.begin(),subject.cells.end(),[&](const NativeCell& cell){return cell.id==id;});
        require(found!=subject.cells.end(),"Expected native cell is missing");
        Lease<ATE::TextRangeSuite,ATE::TextRangeRef> range{ranges};checked(text->GetATETextRange(found->text,&range.value));
        checked(ranges->SetRange(range.value,offset,offset+1));
        Lease<ATE::CharFeaturesSuite,ATE::CharFeaturesRef> features{chars};checked(ranges->GetUniqueCharFeatures(range.value,&features.value));
        ATE::ATEBool8 assigned=false;ATETextDOM::Float value=0;checked(chars->GetFontSize(features.value,&assigned,&value));
        require(assigned,"Test character font size is not assigned");return value;
    };
    auto fontSize=[&](table::CellId id,int offset){return fontSizeIn(native,id,offset);};
    auto alignment=[&](table::CellId id,int offset) {
        auto found=std::find_if(native.cells.begin(),native.cells.end(),[&](const NativeCell& cell){return cell.id==id;});
        require(found!=native.cells.end(),"Expected paragraph cell is missing");
        Lease<ATE::TextRangeSuite,ATE::TextRangeRef> range{ranges};checked(text->GetATETextRange(found->text,&range.value));
        checked(ranges->SetRange(range.value,offset,offset+1));
        Lease<ATE::ParaFeaturesSuite,ATE::ParaFeaturesRef> para{paragraphs};checked(ranges->GetUniqueParaFeatures(range.value,&para.value));
        ATE::ATEBool8 assigned=false;ATE::ParagraphJustification value=ATE::kLeftJustify;
        checked(paragraphs->GetJustification(para.value,&assigned,&value));require(assigned,"Test paragraph alignment is not assigned");return value;
    };
    require(std::abs(fontSize(first,0)-24)<0.01&&std::abs(fontSize(first,4)-12)<0.01,"Mixed-font test setup failed");
    traceStage("acceptance.mixedFont.setup.passed");
    adapter.synchronize(native);
    adapter.applyStructure(native,table::applyCommand(native.model,{0,0,0,1},table::Command::Merge));
    adapter.synchronize(native);
    require(native.model.cellById(first).text==u"AAA BBB\n中文 12","Merge changed cell content");
    require(std::abs(fontSize(first,0)-24)<0.01&&std::abs(fontSize(first,4)-12)<0.01,"Merge destroyed mixed character formatting");
    require(alignment(first,0)==ATE::kLeftJustify&&alignment(first,8)==ATE::kRightJustify,"Merge destroyed paragraph formatting");
    traceStage("acceptance.merge.mixedFont.passed");
    struct FilePathScope {
        AIFilePathSuite* previous=sAIFilePath;
        ~FilePathScope(){sAIFilePath=previous;}
    } filePathScope;
    sAIFilePath=suites.get<AIFilePathSuite>(kAIFilePathSuite,kAIFilePathVersion);
    wchar_t temporary[MAX_PATH]{};require(GetTempPathW(MAX_PATH,temporary)>0,"Cannot locate test artifact directory");
    const auto savedPath=std::wstring(temporary)+L"IllustratorTableTool-Acceptance-"+std::to_wstring(GetTickCount64())+L".ai";
    ai::FilePath saved(ai::UnicodeString(reinterpret_cast<const ASUnicode*>(savedPath.c_str())));
    AIFileFormatHandle format=nullptr;checked(document->GetDocumentFileFormat(&format));const char* writer=nullptr;
    checked(suites.get<AIFileFormatSuite>(kAIFileFormatSuite,kAIFileFormatVersion)->GetFileFormatName(format,&writer));
    traceStage("acceptance.save.start");
    checked(document->WriteDocumentWithOptions(saved,writer,kFileFormatWrite|kFileFormatSuppressUI,nullptr,false,nullptr));
    traceStage("acceptance.save.passed");
    AIArtHandle copied=nullptr;checked(art->DuplicateArt(native.group,kPlaceAboveAll,nullptr,&copied));
    auto foundTables=adapter.discoverTables();require(foundTables.size()==2,"Copied table was not discovered independently");
    auto copiedTable=std::find_if(foundTables.begin(),foundTables.end(),[&](const NativeTable& value){return value.group==copied;});
    require(copiedTable!=foundTables.end(),"Copied group has no independent native references");
    adapter.applyStructure(*copiedTable,table::applyCommand(copiedTable->model,{0,0,0,1},table::Command::Split));
    require(std::abs(fontSizeIn(*copiedTable,first,0)-24)<0.01&&std::abs(fontSizeIn(*copiedTable,first,4)-12)<0.01,"Copied table lost native format snapshots");
    adapter.synchronize(native);require(native.model.cells().size()==3,"Editing copy changed original table");
    auto dictionaries=suites.get<AIDictionarySuite>(kAIDictionarySuite,kAIDictionaryVersion);
    AIDictionaryRef damaged=nullptr;checked(art->GetDictionary(copied,&damaged));
    const auto damageError=dictionaries->SetStringEntry(damaged,dictionaries->Key("IllustratorTableTool.Model.v1"),"deliberately-invalid-test-entry");
    dictionaries->Release(damaged);checked(damageError);
    std::vector<RejectedTable> rejected;auto healthy=adapter.discoverTables(&rejected);
    require(healthy.size()==1&&healthy.front().group==native.group&&rejected.size()==1,"Damaged table prevented healthy native table discovery");
    traceStage("acceptance.malformedTable.isolated.passed");
    auto staleCopy=*copiedTable;checked(art->DisposeArt(copied));bool refused=false;
    try{adapter.synchronize(staleCopy);}catch(...){refused=true;}
    require(refused,"Deleted native table was not refused");traceStage("acceptance.copy.deletedGuard.passed");
    table::CellStyle mergedStyle;mergedStyle.fontSize=20;mergedStyle.fill={0.15,0.4,0.8};mergedStyle.alignment=1;
    adapter.applyStyle(native,{0,0,0,1},mergedStyle);
    adapter.applyStructure(native,table::applyCommand(native.model,{0,0,0,1},table::Command::Split));
    adapter.synchronize(native);
    require(native.model.cells().size()==4&&native.model.cellAt(0,1).text==u"中文 12","Split did not restore original cells");
    require(alignment(native.model.cellAt(0,1).id,0)==ATE::kRightJustify,"Split did not restore original paragraph alignment");
    require(std::abs(fontSize(first,0)-24)<0.01&&std::abs(fontSize(first,4)-12)<0.01,"Split destroyed mixed character formatting");
    AIPathStyle restoredPaint;restoredPaint.Init();AIBoolean advanced=false;
    auto firstNative=std::find_if(native.cells.begin(),native.cells.end(),[&](const NativeCell& cell){return cell.id==first;});
    checked(suites.get<AIPathStyleSuite>(kAIPathStyleSuite,kAIPathStyleVersion)->GetPathStyle(firstNative->fill,&restoredPaint,&advanced));
    const auto& ink=restoredPaint.fill.color;
    const bool white=ink.kind==kThreeColor ? ink.c.rgb.red>0.99&&ink.c.rgb.green>0.99&&ink.c.rgb.blue>0.99 :
        ink.kind==kFourColor&&ink.c.f.cyan<0.01&&ink.c.f.magenta<0.01&&ink.c.f.yellow<0.01&&ink.c.f.black<0.01;
    require(white&&native.model.cellById(first).style.fontSize==12,"Split native appearance differs from restored model style");
    traceStage("acceptance.split.mixedFont.passed");
    for(const auto command:{table::Command::InsertRowBelow,table::Command::InsertColumnRight,table::Command::DeleteRows,table::Command::DeleteColumns}) {
        adapter.applyStructure(native,table::applyCommand(native.model,{1,1,1,1},command));
        adapter.synchronize(native);
        require(native.model.cellById(first).text==u"AAA BBB","Row or column operation changed surviving content");
        require(std::abs(fontSize(first,0)-24)<0.01&&std::abs(fontSize(first,4)-12)<0.01,"Row or column operation changed surviving formatting");
    }
    require(native.model.rows()==2&&native.model.columns()==2,"Row and column round trip changed dimensions");
    traceStage("acceptance.tracks.passed");
    for(const auto flag:{kArtLocked,kArtHidden}) {
        checked(art->SetArtUserAttr(native.group,flag,flag));refused=false;
        try{adapter.synchronize(native);}catch(...){refused=true;}
        checked(art->SetArtUserAttr(native.group,flag,0));require(refused,"Locked or hidden native table was not refused");
    }
    traceStage("acceptance.lockedHiddenGuard.passed");
    auto staleDocument=native;checked(document->SetDocumentModified(false));checked(documents->Close(created));
    if(createdDocument) *createdDocument=nullptr;
    refused=false;try{adapter.synchronize(staleDocument);}catch(...){refused=true;}
    require(refused,"Closed native document was not refused");traceStage("acceptance.closedDocumentGuard.passed");
    checked(documents->OpenNoHistory(saved,kAIUnknownColorModel,kDialogOff,false,&created));
    if(createdDocument) *createdDocument=created;
    foundTables=adapter.discoverTables();require(foundTables.size()==1,"Saved table metadata did not reopen");native=foundTables.front();
    require(native.model.cells().size()==3,"Saved merged structure was not restored");
    adapter.applyStructure(native,table::applyCommand(native.model,{0,0,0,1},table::Command::Split));adapter.synchronize(native);
    require(native.model.cellAt(0,1).text==u"中文 12"&&std::abs(fontSize(first,0)-24)<0.01&&std::abs(fontSize(first,4)-12)<0.01,
        "Saved merged table did not preserve original text and formatting");
    traceStage("acceptance.reopen.split.passed");
    traceStage("acceptance.all.passed");
    if(interactive) MessageBoxW(nullptr,L"本轮原生合并、拆分和混合字号验收通过。\n测试结果保留在新建文档中。",L"开发验收",MB_OK|MB_ICONINFORMATION);
}
bool hasAutoHostAcceptanceRequest() {
    const auto path=requestPath();return !path.empty()&&GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;
}
bool runAutoHostAcceptance(Suites& suites) {
    const auto path=requestPath();if(path.empty()||!DeleteFileW(path.c_str())) return false;
    auto documents=suites.get<AIDocumentListSuite>(kAIDocumentListSuite,kAIDocumentListVersion);
    ai::int32 count=0;checked(documents->Count(&count));
    if(count!=0) {traceStage("acceptance.auto.refused.existingDocuments");return false;}
    AIDocumentHandle own=nullptr;
    try {runHostAcceptance(suites,false,&own);}
    catch(const std::exception& error) {traceStage("acceptance.auto.failed");traceStage(error.what());}
    catch(ASErr error) {traceStage("acceptance.auto.SDK.failed");traceStage(std::to_string(error).c_str());}
    catch(...) {traceStage("acceptance.auto.unexpected.failed");}
    if(own) {
        // Only the document returned by this test is eligible for cleanup.
        checked(documents->Activate(own,true));
        checked(suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion)->SetDocumentModified(false));
        checked(documents->Close(own));
    }
    checked(documents->Count(&count));traceStage("acceptance.auto.finished");return count==0;
}
