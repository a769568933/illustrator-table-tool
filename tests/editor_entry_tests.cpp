#include "EditController.hpp"
#include "AIContext.h"
#include "AIDocumentView.h"
#include "AIAnnotatorDrawer.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
extern "C" {AIUnicodeStringSuite* sAIUnicodeString=nullptr;SPBlocksSuite* sSPBlocks=nullptr;}
namespace {
AIAppContextSuite context{};AIUnicodeStringSuite unicode{};SPBlocksSuite blocks{};
AIArtSuite art{};AIPathSuite paths{};AIPathStyleSuite styles{};AIGroupSuite groups{};
AIDocumentSuite document{};AIUndoSuite undo{};AIDictionarySuite dictionaries{};AILayerSuite layers{};
AIColorConversionSuite colors{};AIDocumentViewSuite views{};AIAnnotatorDrawerSuite drawer{};
HWND owner=nullptr;int images=0,contexts=0;
std::vector<ai::uint8> lastPNG;
template<class T>T handle(std::uintptr_t value){return reinterpret_cast<T>(value);}
std::uintptr_t id(const void* value){return reinterpret_cast<std::uintptr_t>(value);}
AIBoolean AIAPI exists(){return contexts>0;}
AIErr AIAPI push(SPPluginRef,AIAppContextHandle* out){++contexts;*out=handle<AIAppContextHandle>(contexts);return kNoErr;}
AIErr AIAPI pop(AIAppContextHandle){--contexts;return kNoErr;}
AIErr AIAPI appWindow(AIWindowRef* result){*result=owner;return kNoErr;}
AIErr AIAPI currentDocument(AIDocumentHandle* result){*result=handle<AIDocumentHandle>(100);return kNoErr;}
AIErr AIAPI stamp(AIArtHandle,AIArtTimeStampOptions,std::size_t* result){*result=10;return kNoErr;}
AIBoolean AIAPI live(AIArtHandle object,AIBoolean){return object!=nullptr;}
AIErr AIAPI attributes(AIArtHandle,ai::int32,ai::int32* flags){*flags=0;return kNoErr;}
AIErr AIAPI parent(AIArtHandle,AIArtHandle* result){*result=nullptr;return kNoErr;}
AIErr AIAPI type(AIArtHandle object,short* result){*result=id(object)==1||id(object)==4?kGroupArt:kPathArt;return kNoErr;}
AIErr AIAPI child(AIArtHandle object,AIArtHandle* result){*result=id(object)==1?handle<AIArtHandle>(2):nullptr;return kNoErr;}
AIErr AIAPI sibling(AIArtHandle object,AIArtHandle* result){*result=id(object)==2||id(object)==3?handle<AIArtHandle>(id(object)+1):nullptr;return kNoErr;}
AIErr AIAPI convert(AIDocumentViewHandle,const AIRealPoint* point,AIRealPoint* result){*result={100+point->h,700-point->v};return kNoErr;}
AIErr AIAPI artworkBounds(AIDocumentViewHandle,AIRealRect* result){*result={0,600,800,0};return kNoErr;}
AIErr AIAPI viewArea(AIDocumentViewHandle,AIRealRect* result){*result={0,0,800,600};return kNoErr;}
AIErr AIAPI image(AIAnnotatorDrawer*,const ai::uint8* pixels,ai::uint32 width,ai::uint32 height,ai::int16,AIBoolean,const AIPoint&,AIBoolean){
    if(!pixels||width==0||height==0)return kBadParameterErr;++images;return kNoErr;
}
AIErr AIAPI png(AIAnnotatorDrawer*,const ai::uint8* data,ai::uint32 size,const AIPoint&){
    if(size<8||std::memcmp(data,"\x89PNG\r\n\x1a\n",8))return kBadParameterErr;lastPNG.assign(data,data+size);++images;return kNoErr;
}
SPErr SPAPI acquire(const char* name,ai::int32,const void** result){
#define SUITE(key,value) if(!std::strcmp(name,key)){*result=&value;return kNoErr;}
    SUITE(kAIAppContextSuite,context) SUITE(kAIUnicodeStringSuite,unicode) SUITE(kSPBlocksSuite,blocks)
    SUITE(kAIArtSuite,art) SUITE(kAIPathSuite,paths) SUITE(kAIPathStyleSuite,styles) SUITE(kAIGroupSuite,groups)
    SUITE(kAIDocumentSuite,document) SUITE(kAIUndoSuite,undo) SUITE(kAIDictionarySuite,dictionaries) SUITE(kAILayerSuite,layers)
    SUITE(kAIColorConversionSuite,colors) SUITE(kAIDocumentViewSuite,views) SUITE(kAIAnnotatorDrawerSuite,drawer)
#undef SUITE
    return kBadParameterErr;
}
SPErr SPAPI release(const char*,ai::int32){return kNoErr;}
}
int main(){try{
    context.DoesAppContextExist=exists;context.PushAppContext=push;context.PopAppContext=pop;context.GetPlatformAppWindow=appWindow;
    art.ValidArt=live;art.GetArtUserAttr=attributes;art.GetArtParent=parent;art.GetArtType=type;art.GetArtFirstChild=child;art.GetArtSibling=sibling;art.GetArtTimeStamp=stamp;
    document.GetDocument=currentDocument;views.FixedArtworkPointToViewPoint=convert;views.GetDocumentViewBounds=artworkBounds;views.GetDocumentViewVisibleArea=viewArea;drawer.DrawImage=image;drawer.DrawPNGImageUnscaled=png;
    SPBasicSuite basic{};basic.AcquireSuite=acquire;basic.ReleaseSuite=release;
    owner=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,-32000,-32000,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!owner)throw std::runtime_error("fixture window unavailable");
    {
        auto model=table::TableModel::create(1,1,100,80);const auto cell=model.cellAt(0,0).id;model.setText(cell,u"old words on two lines");
        NativeTable native{model};native.document=handle<AIDocumentHandle>(100);native.group=handle<AIArtHandle>(1);native.frame=handle<AIArtHandle>(2);native.origin={0,400};native.publishedStamp=10;
        native.cells.push_back({cell,handle<AIArtHandle>(3),handle<AIArtHandle>(4),table::layoutText({model.cellById(cell).text,{},table::CellStyle{},100,80})});
        EditController editor;editor.configure(&basic,handle<SPPluginRef>(1),EditorHost{
            [&](){return &native;},[]{return std::uint64_t(1);},[]{},[](const std::wstring&){},[](const std::function<void()>& operation){operation();},[](table::CellId){}
        });
        editor.begin(native,cell,{10,10});
        AIAnnotatorMessage message{};message.drawer=handle<AIAnnotatorDrawer*>(1);Suites suites(&basic);editor.draw(suites,&message);
        if(!editor.editing()||images!=1)throw std::runtime_error("editor begin must publish a visible caret preview using artwork-coordinate bounds");
        editor.handle({InputKind::Key,{},'A',InputControl});editor.draw(suites,&message);const auto published=lastPNG;
        editor.handle({InputKind::Composition,u"replacement that reflows",0,0,2,24});editor.draw(suites,&message);
        if(lastPNG!=published||native.model.cellById(cell).text!=u"old words on two lines")throw std::runtime_error("Transparent composition must keep published glyphs unchanged and delegate preedit to IME UI");
        editor.handle({InputKind::Cancel,{}});editor.draw(suites,&message);
        if(lastPNG!=published||!editor.editing())throw std::runtime_error("Cancel keeps transparent text and selection intact");
        editor.end(false);editor.shutdown();
    }
    DestroyWindow(owner);owner=nullptr;
    if(contexts)throw std::runtime_error("SDK callback context leaked");
    std::cout<<"Editor entry renders visible preview and releases custom input state\n";
}catch(const std::exception& error){if(owner)DestroyWindow(owner);std::cerr<<error.what()<<'\n';return 1;}}
