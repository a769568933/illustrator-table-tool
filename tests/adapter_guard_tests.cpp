#include "DocumentAdapter.hpp"
#include <cstring>
#include <iostream>
#include "table/Serialization.hpp"

extern "C" {AIUnicodeStringSuite* sAIUnicodeString=nullptr;SPBlocksSuite* sSPBlocks=nullptr;}
namespace {
AIArtSuite art{};AIDocumentSuite document{};
AIPathSuite paths{};AIPathStyleSuite styles{};AIUndoSuite undo{};AIGroupSuite groups{};
AIDictionarySuite dictionary{};AILayerSuite layers{};AIColorConversionSuite conversion{};
int dictionaryReads=0;
AIDocumentHandle current=reinterpret_cast<AIDocumentHandle>(1);
AIErr AIAPI getDocument(AIDocumentHandle* value){*value=current;return kNoErr;}
AIBoolean AIAPI invalidArt(AIArtHandle,AIBoolean){return false;}
AIErr AIAPI readDictionary(AIArtHandle,AIDictionaryRef*) {++dictionaryReads;return kBadParameterErr;}
SPErr SPAPI acquire(const char* name,ai::int32,const void** result) {
#define SUITE(key,value) if(std::strcmp(name,key)==0){*result=&value;return kNoErr;}
    SUITE(kAIArtSuite,art) SUITE(kAIDocumentSuite,document) SUITE(kAIPathSuite,paths)
    SUITE(kAIPathStyleSuite,styles) SUITE(kAIUndoSuite,undo) SUITE(kAIGroupSuite,groups)
    SUITE(kAIDictionarySuite,dictionary) SUITE(kAILayerSuite,layers) SUITE(kAIColorConversionSuite,conversion)
#undef SUITE
    return kBadParameterErr;
}
SPErr SPAPI release(const char*,ai::int32){return kNoErr;}
int decodedGroups=0;
template<class T> T handle(std::uintptr_t value){return reinterpret_cast<T>(value);}
std::uintptr_t id(const void* value){return reinterpret_cast<std::uintptr_t>(value);}
AIErr AIAPI firstLayer(AILayerHandle* layer){*layer=handle<AILayerHandle>(1);return kNoErr;}
AIErr AIAPI nextLayer(AILayerHandle,AILayerHandle* layer){*layer=nullptr;return kNoErr;}
AIErr AIAPI rootArt(AILayerHandle,AIArtHandle* value){*value=handle<AIArtHandle>(10);return kNoErr;}
bool forbiddenChild=false;
AIErr AIAPI artType(AIArtHandle value,short* type){*type=id(value)==31?(forbiddenChild?kTextFrameArt:kPathArt):kGroupArt;return kNoErr;}
AIBoolean AIAPI liveArt(AIArtHandle,AIBoolean){return true;}
AIErr AIAPI hiddenChild(AIArtHandle value,ai::int32,ai::int32* flags){*flags=id(value)==31?kArtHidden:0;return kNoErr;}
AIBoolean AIAPI hasDictionary(AIArtHandle value){return id(value)!=10;}
AIErr AIAPI dictionaryFor(AIArtHandle value,AIDictionaryRef* result){*result=handle<AIDictionaryRef>(id(value));return kNoErr;}
ai::int32 AIAPI releaseDictionary(AIDictionaryRef){return 0;}
AIDictKey AIAPI key(const char* name){return reinterpret_cast<AIDictKey>(name);}
AIBoolean AIAPI known(ConstAIDictionaryRef,AIDictKey){return true;}
AIErr AIAPI children(AIArtHandle value,AIArtHandle* result){*result=handle<AIArtHandle>(id(value)==10?30:id(value)==30?31:0);return kNoErr;}
AIErr AIAPI sibling(AIArtHandle value,AIArtHandle* result){*result=handle<AIArtHandle>(id(value)==30?20:0);return kNoErr;}
AIErr AIAPI binary(ConstAIDictionaryRef dict,AIDictKey,void* value,std::size_t* size) {
    if(!value) ++decodedGroups;
    if(id(dict)==20) return kBadParameterErr; // Wrong model dictionary type.
    const auto bytes=table::serialize(table::TableModel::create(1,1,20,20));
    if(value) std::memcpy(value,bytes.data(),bytes.size());*size=bytes.size();return kNoErr;
}
AIErr AIAPI malformedRole(ConstAIDictionaryRef,AIDictKey,const char**){return kBadParameterErr;}
int creations=0,rollbacks=0;
ai::int32 protectedFlags=0;
std::uintptr_t protectedObject=105;
AIErr AIAPI protectedAttributes(AIArtHandle value,ai::int32,ai::int32* flags){*flags=id(value)==protectedObject?protectedFlags:0;return kNoErr;}
AIErr AIAPI nestedChildren(AIArtHandle value,AIArtHandle* result){*result=handle<AIArtHandle>(id(value)==3?103:id(value)==103?104:id(value)==104?105:id(value)==6?203:id(value)==203?204:id(value)==204?205:0);return kNoErr;}
AIErr AIAPI noSibling(AIArtHandle,AIArtHandle* result){*result=nullptr;return kNoErr;}
AIErr AIAPI nestedType(AIArtHandle value,short* type){*type=id(value)==104?kCompoundPathArt:id(value)==105?kPathArt:kGroupArt;return kNoErr;}
AIErr AIAPI noParent(AIArtHandle,AIArtHandle* result){*result=nullptr;return kNoErr;}
AIErr AIAPI failThirdArt(ai::int16,ai::int16,AIArtHandle,AIArtHandle* result){
    if(++creations==3)return kBadParameterErr;*result=handle<AIArtHandle>(1000+creations);return kNoErr;
}
AIErr AIAPI clipGroup(AIArtHandle,AIBoolean){return kNoErr;}
AIErr AIAPI rollback(){++rollbacks;return kNoErr;}
std::size_t treeStamp=10;
AIErr AIAPI stamp(AIArtHandle,AIArtTimeStampOptions option,std::size_t* result){
    if(option!=kAITimeStampMaxFromArtAndChildren)return kBadParameterErr;*result=treeStamp;return kNoErr;
}
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
    art.GetFirstArtOfLayer=rootArt;art.GetArtType=artType;art.HasDictionary=hasDictionary;art.GetDictionary=dictionaryFor;
    art.GetArtFirstChild=children;art.GetArtSibling=sibling;layers.GetFirstLayer=firstLayer;layers.GetNextLayer=nextLayer;
    dictionary.Release=releaseDictionary;dictionary.Key=key;dictionary.IsKnown=known;dictionary.GetBinaryEntry=binary;dictionary.GetStringEntry=malformedRole;
    try {
        const auto tables=adapter.discoverTables();
        if(!tables.empty()||decodedGroups!=2) {std::cerr<<"Malformed table prevented independent discovery\n";return 1;}
    } catch(...) {std::cerr<<"Malformed table decoding escaped discovery\n";return 1;}
    std::cout<<"Malformed model and role entries are isolated\n";
    art.ValidArt=liveArt;
    art.GetArtUserAttr=hiddenChild;
    try{auditVectorTable(suites,handle<AIArtHandle>(30));}catch(...){std::cerr<<"Vector-only artwork refused\n";return 1;}
    forbiddenChild=true;bool rejected=false;
    try{auditVectorTable(suites,handle<AIArtHandle>(30));}catch(...){rejected=true;}
    if(!rejected){std::cerr<<"Non-vector child escaped recursive audit\n";return 1;}
    std::cout<<"Recursive audit refuses native text child\n";
    forbiddenChild=false;art.GetArtParent=noParent;art.NewArt=failThirdArt;groups.SetGroupClipped=clipGroup;undo.UndoChanges=rollback;
    auto original=table::TableModel::create(1,1,100,40);const auto cell=original.cellAt(0,0).id;original.setText(cell,u"A");
    NativeTable cached{original};cached.document=current;cached.group=handle<AIArtHandle>(3);cached.frame=handle<AIArtHandle>(101);
    cached.cells.push_back({cell,handle<AIArtHandle>(102),handle<AIArtHandle>(103),table::layoutText({u"A",{},table::CellStyle{},100,40})});
    auto changed=original;changed.setText(cell,u"B");rejected=false;
    try{adapter.applyText(cached,changed);}catch(...){rejected=true;}
    if(!rejected||creations!=3||rollbacks!=1||cached.model.cellById(cell).text!=u"A"||cached.cells.front().content!=handle<AIArtHandle>(103)){
        std::cerr<<"SDK path failure published a partial table or skipped rollback\n";return 1;
    }
    std::cout<<"SDK path failure preserves published model and requests context rollback\n";
    art.GetArtUserAttr=protectedAttributes;art.GetArtFirstChild=nestedChildren;art.GetArtSibling=noSibling;art.GetArtType=nestedType;
    auto healthy=cached;healthy.group=handle<AIArtHandle>(6);healthy.cells.front().content=handle<AIArtHandle>(203);
    for(const auto target:{104u,105u})for(const auto flags:{kArtLocked,kArtHidden}){
        protectedObject=target;
        protectedFlags=flags;creations=0;rejected=false;
        try{adapter.applyText(cached,changed);}catch(...){rejected=true;}
        if(!rejected||creations||cached.model.cellById(cell).text!=u"A"||adapter.valid(cached)){
            std::cerr<<"Protected nested glyph was replaced or mutated\n";return 1;
        }
        if(!adapter.valid(healthy)){std::cerr<<"Protected table disabled healthy sibling\n";return 1;}
    }
    protectedFlags=0;if(!adapter.valid(cached)){std::cerr<<"Independent unprotected artwork was refused\n";return 1;}
    std::cout<<"Nested locked and hidden glyphs refuse mutation before artwork creation\n";
    art.GetArtTimeStamp=stamp;cached.publishedStamp=treeStamp;
    if(!adapter.matchesPublishedArtwork(cached)){std::cerr<<"Delayed notification for published artwork must preserve editing\n";return 1;}
    ++treeStamp;
    if(adapter.matchesPublishedArtwork(cached)){std::cerr<<"External descendant edit must invalidate published artwork\n";return 1;}
    cached.publishedStamp=treeStamp;protectedObject=105;protectedFlags=kArtLocked;
    if(adapter.matchesPublishedArtwork(cached)){std::cerr<<"Parent or descendant protection must end editing even with matching stamp\n";return 1;}
    protectedFlags=0;current=handle<AIDocumentHandle>(2);
    if(adapter.matchesPublishedArtwork(cached)){std::cerr<<"Document switch must invalidate even with matching stamp\n";return 1;}
    current=cached.document;cached.publishedStamp.reset();
    if(adapter.matchesPublishedArtwork(cached)){std::cerr<<"Unrecorded artwork revision cannot retain editing\n";return 1;}
    std::cout<<"Deferred self notifications preserve editing; external edits and stale documents invalidate\n";
}
