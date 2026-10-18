#include <windows.h>
#include "HostAcceptance.hpp"
#include "DocumentAdapter.hpp"
#include "AIDocumentList.h"
#include "AIDocumentView.h"
#include "AIFilePath.h"
#include "AIFileFormat.h"
#include "AINativeAction.h"
#include "table/Commands.hpp"
#include "table/TextEditor.hpp"
#include "Trace.hpp"
#include <algorithm>
#include <stdexcept>
extern "C" {AIFilePathSuite* sAIFilePath=nullptr;}
namespace {
void require(bool condition,const char* name){if(!condition)throw std::runtime_error(name);}
std::wstring requestPath(){wchar_t path[MAX_PATH]{};const auto n=GetTempPathW(MAX_PATH,path);return n&&n<MAX_PATH?std::wstring(path)+L"IllustratorTableTool.run-acceptance":L"";}
}
void runHostAcceptance(Suites& suites,bool interactive,AIDocumentHandle* createdDocument){
    traceStage("frameless.begin");auto documents=suites.get<AIDocumentListSuite>(kAIDocumentListSuite,kAIDocumentListVersion);
    AIDocumentHandle created=nullptr;ai::UnicodeString preset;checked(documents->New(preset,nullptr,kDialogOff,&created));
    require(created!=nullptr,"Test document was not created");if(createdDocument)*createdDocument=created;
    auto art=suites.get<AIArtSuite>(kAIArtSuite,kAIArtVersion);auto document=suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion);
    DocumentAdapter adapter(suites);auto model=table::TableModel::create(2,2,table::mm(120),table::mm(60));
    const auto first=model.cellAt(0,0).id;
    model.setRichText(first,u"AAA BBB",{{{0,3,u"Arial",24,{}},{3,4,u"Arial",12,{}}},{{0,0}}});
    model.setRichText(model.cellAt(0,1).id,u"中文 O8",{{},{{0,2}}});
    AIRealPoint center{};checked(suites.get<AIDocumentViewSuite>(kAIDocumentViewSuite,kAIDocumentViewVersion)->GetDocumentViewCenter(nullptr,&center));
    auto native=adapter.createTable(model,{static_cast<AIReal>(center.h-model.width()/2),static_cast<AIReal>(center.v+model.height()/2)});
    auto audit=[&](const char* stage){auditVectorTable(suites,native.group);traceStage(stage);};
    audit("frameless.create.zeroText.passed");
    const auto untouched=native.cells.at(1).content;
    auto edited=table::replaceSelection(native.model,{first,7,7,0},u" 中");adapter.applyText(native,edited.model);adapter.synchronize(native);
    require(native.cells.at(1).content==untouched,"Text edit regenerated an unrelated cell");
    require(native.model.cellById(first).text==u"AAA BBB 中","Model-owned text edit failed");audit("frameless.input.zeroText.passed");
    adapter.applyStructure(native,table::applyCommand(native.model,{0,0,0,1},table::Command::Merge));adapter.synchronize(native);
    require(native.model.cells().size()==3&&native.model.cellById(first).text==u"AAA BBB 中\n中文 O8","Merge changed text");audit("frameless.merge.zeroText.passed");
    struct FilePathScope{AIFilePathSuite* previous=sAIFilePath;~FilePathScope(){sAIFilePath=previous;}} pathScope;
    sAIFilePath=suites.get<AIFilePathSuite>(kAIFilePathSuite,kAIFilePathVersion);
    wchar_t temporary[MAX_PATH]{};const auto tempLength=GetTempPathW(MAX_PATH,temporary);require(tempLength&&tempLength<MAX_PATH,"Cannot locate test artifact directory");
    const auto savedPath=std::wstring(temporary)+L"IllustratorTableTool-Frameless-"+std::to_wstring(GetTickCount64())+L".ai";
    ai::FilePath saved(ai::UnicodeString(reinterpret_cast<const ASUnicode*>(savedPath.c_str())));
    traceStage("frameless.save.start");checked(document->WriteDocumentWithOptions(saved,kAINativeFileFormat,kFileFormatWrite|kFileFormatSuppressUI,nullptr,false,nullptr));
    traceStage("frameless.save.passed");
    AIArtHandle copy=nullptr;checked(art->DuplicateArt(native.group,kPlaceAboveAll,nullptr,&copy));auditVectorTable(suites,copy);
    auto tables=adapter.discoverTables();require(tables.size()==2,"Copy not recognized");
    auto copied=std::find_if(tables.begin(),tables.end(),[&](const NativeTable& t){return t.group==copy;});require(copied!=tables.end(),"Copy references missing");
    adapter.applyStructure(*copied,table::applyCommand(copied->model,{0,0,0,1},table::Command::Split));auditVectorTable(suites,copied->group);
    adapter.synchronize(native);require(native.model.cells().size()==3,"Copy changed original");
    auto dictionaries=suites.get<AIDictionarySuite>(kAIDictionarySuite,kAIDictionaryVersion);AIDictionaryRef damaged=nullptr;checked(art->GetDictionary(copy,&damaged));
    const auto error=dictionaries->SetStringEntry(damaged,dictionaries->Key("IllustratorTableTool.Model.v3"),"invalid-test-entry");dictionaries->Release(damaged);checked(error);
    std::vector<RejectedTable> rejected;auto healthy=adapter.discoverTables(&rejected);require(healthy.size()==1&&rejected.size()==1,"Damaged table was not isolated");
    auto staleCopy=*copied;checked(art->DisposeArt(copy));bool refused=false;try{adapter.synchronize(staleCopy);}catch(...){refused=true;}require(refused,"Deleted object not refused");
    traceStage("frameless.copy.isolation.passed");
    table::CellStyle style;style.fontSize=20;style.fill={0.15,0.4,0.8};adapter.applyStyle(native,{0,0,0,1},style);
    adapter.applyStructure(native,table::applyCommand(native.model,{0,0,0,1},table::Command::Split));adapter.synchronize(native);
    require(native.model.cellById(first).formatting.runs.front().fontSize==24&&native.model.cellAt(0,1).formatting.paragraphs.front().alignment==2,"Split did not restore model formats");audit("frameless.split.zeroText.passed");
    for(const auto command:{table::Command::InsertRowBelow,table::Command::InsertColumnRight,table::Command::DeleteRows,table::Command::DeleteColumns}){
        adapter.applyStructure(native,table::applyCommand(native.model,{1,1,1,1},command));adapter.synchronize(native);auditVectorTable(suites,native.group);
        require(native.model.cellById(first).text==u"AAA BBB 中","Track edit changed surviving content");
    }
    traceStage("frameless.tracks.zeroText.passed");
    for(const auto flag:{kArtLocked,kArtHidden}){
        checked(art->SetArtUserAttr(native.group,flag,flag));refused=false;try{adapter.synchronize(native);}catch(...){refused=true;}
        checked(art->SetArtUserAttr(native.group,flag,0));require(refused,"Locked or hidden table not refused");
    }
    auto stale=native;checked(document->SetDocumentModified(false));checked(documents->Close(created));if(createdDocument)*createdDocument=nullptr;
    refused=false;try{adapter.synchronize(stale);}catch(...){refused=true;}require(refused,"Closed document not refused");
    checked(documents->OpenNoHistory(saved,kAIUnknownColorModel,kDialogOff,false,&created));if(createdDocument)*createdDocument=created;
    tables=adapter.discoverTables();require(tables.size()==1&&tables.front().model.cells().size()==3,"Saved table not restored");native=tables.front();
    audit("frameless.reopen.zeroText.passed");
    adapter.applyStructure(native,table::applyCommand(native.model,{0,0,0,1},table::Command::Split));adapter.synchronize(native);
    require(native.model.cellById(first).text==u"AAA BBB 中"&&native.model.cellAt(0,1).text==u"中文 O8","Reopened split lost text");audit("frameless.reopen.split.passed");
    traceStage("frameless.all.passed");if(interactive)MessageBoxW(nullptr,L"本轮矢量表格和模型保存检查通过。\n中文输入法、侧栏和撤销仍需单独操作确认。",L"开发验收",MB_OK|MB_ICONINFORMATION);
}
bool hasAutoHostAcceptanceRequest(){const auto path=requestPath();return !path.empty()&&GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;}
bool runAutoHostAcceptance(Suites& suites){
    const auto path=requestPath();if(path.empty()||!DeleteFileW(path.c_str()))return false;
    auto documents=suites.get<AIDocumentListSuite>(kAIDocumentListSuite,kAIDocumentListVersion);ai::int32 count=0;checked(documents->Count(&count));
    if(count!=0){traceStage("frameless.auto.refused.existingDocuments");return false;}AIDocumentHandle own=nullptr;
    try{runHostAcceptance(suites,false,&own);}catch(const std::exception& error){traceStage("frameless.auto.failed");traceStage(error.what());}
    catch(ASErr error){traceStage("frameless.auto.SDK.failed");traceStage(std::to_string(error).c_str());}catch(...){traceStage("frameless.auto.failed.unknown");}
    if(own){checked(documents->Activate(own,true));checked(suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion)->SetDocumentModified(false));checked(documents->Close(own));}
    checked(documents->Count(&count));traceStage("frameless.auto.finished");return count==0;
}
