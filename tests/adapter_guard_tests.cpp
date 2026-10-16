#include "DocumentAdapter.hpp"
#include <cstring>
#include <iostream>

extern "C" {AIUnicodeStringSuite* sAIUnicodeString=nullptr;SPBlocksSuite* sSPBlocks=nullptr;}
namespace {
AIArtSuite art{};AIDocumentSuite document{};
AIPathSuite paths{};AIPathStyleSuite styles{};AITextFrameSuite text{};AIUndoSuite undo{};
AIDictionarySuite dictionary{};AILayerSuite layers{};AIFontSuite fonts{};AIATEPaintSuite paint{};AIColorConversionSuite conversion{};
ATE::TextRangeSuite ranges{};ATE::CharFeaturesSuite features{};ATE::StorySuite stories{};
ATE::FontSuite ateFonts{};ATE::ApplicationPaintSuite paints{};ATE::ParaFeaturesSuite paragraphs{};ATE::TextFrameSuite frames{};
int dictionaryReads=0;
AIDocumentHandle current=reinterpret_cast<AIDocumentHandle>(1);
AIErr AIAPI getDocument(AIDocumentHandle* value){*value=current;return kNoErr;}
AIBoolean AIAPI invalidArt(AIArtHandle,AIBoolean){return false;}
AIErr AIAPI readDictionary(AIArtHandle,AIDictionaryRef*) {++dictionaryReads;return kBadParameterErr;}
SPErr SPAPI acquire(const char* name,ai::int32,const void** result) {
#define SUITE(key,value) if(std::strcmp(name,key)==0){*result=&value;return kNoErr;}
    SUITE(kAIArtSuite,art) SUITE(kAIDocumentSuite,document) SUITE(kAIPathSuite,paths)
    SUITE(kAIPathStyleSuite,styles) SUITE(kAITextFrameSuite,text) SUITE(kAIUndoSuite,undo)
    SUITE(kAIDictionarySuite,dictionary) SUITE(kAILayerSuite,layers) SUITE(kAIFontSuite,fonts)
    SUITE(kAIATEPaintSuite,paint) SUITE(kAIColorConversionSuite,conversion)
    SUITE(kTextRangeSuite,ranges) SUITE(kCharFeaturesSuite,features) SUITE(kStorySuite,stories)
    SUITE(kFontSuite,ateFonts) SUITE(kApplicationPaintSuite,paints) SUITE(kParaFeaturesSuite,paragraphs) SUITE(kTextFrameSuite,frames)
#undef SUITE
    return kBadParameterErr;
}
SPErr SPAPI release(const char*,ai::int32){return kNoErr;}
}
int main() {
    document.GetDocument=getDocument;art.ValidArt=invalidArt;art.GetDictionary=readDictionary;
    SPBasicSuite basic{};basic.AcquireSuite=acquire;basic.ReleaseSuite=release;Suites suites(&basic);DocumentAdapter adapter(suites);
    for(bool otherDocument:{true,false}) {
        NativeTable table{table::TableModel::create(1,1,20,20)};
        table.document=otherDocument?reinterpret_cast<AIDocumentHandle>(2):current;
        table.group=reinterpret_cast<AIArtHandle>(3);dictionaryReads=0;bool refused=false;
        try{adapter.synchronize(table);}catch(...){refused=true;}
        if(!refused||dictionaryReads) {
            std::cerr<<"Unsafe table synchronization reached a stale or foreign artwork dictionary\n";return 1;
        }
    }
    std::cout<<"Closed-document and invalid-art guards passed\n";
}
