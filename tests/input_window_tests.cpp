#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "plugin/InputWindow.hpp"
#include "plugin/EditorOverlay.hpp"
#include <imm.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace table;
namespace {
int checks=0;
void expect(bool condition,const char* name){++checks;if(!condition)throw std::runtime_error(name);}
BOOL CALLBACK noEditChild(HWND child,LPARAM state){
    wchar_t name[128]{};GetClassNameW(child,name,128);
    if(_wcsicmp(name,L"EDIT")==0||wcsstr(name,L"RichEdit")) *reinterpret_cast<bool*>(state)=false;return TRUE;
}
void custom_window_has_no_edit_children(){
    InputWindow input;input.create(nullptr,[](const InputEvent&){});
    wchar_t name[128]{};GetClassNameW(input.handle(),name,128);
    expect(std::wstring(name)==L"IllustratorTableTool.Input.v3","custom input window class");
    bool custom=true;EnumChildWindows(input.handle(),noEditChild,reinterpret_cast<LPARAM>(&custom));expect(custom,"no EDIT or RichEdit children");
    const auto module=reinterpret_cast<HINSTANCE>(GetClassLongPtrW(input.handle(),GCLP_HMODULE));
    input.destroy();expect(input.handle()==nullptr,"input window released");
    WNDCLASSEXW registered{};registered.cbSize=sizeof(registered);
    expect(!GetClassInfoExW(module,L"IllustratorTableTool.Input.v3",&registered),"input class released before plugin unload");
}
void ime_commit_once(){
    std::vector<InputEvent> events;CompositionUpdate update;
    InputWindow input;input.create(nullptr,[&](const InputEvent& e){events.push_back(e);},[&](HWND,LPARAM){return update;});
    SendMessageW(input.handle(),WM_IME_STARTCOMPOSITION,0,0);
    update={u"zhong",u"",5,false};SendMessageW(input.handle(),WM_IME_COMPOSITION,0,GCS_COMPSTR);
    expect(input.composing(),"composition remains transient");
    expect(!input.requestCompositionCommit(),"pending IME prevents structural action");
    update={u"",u"中",0,true};SendMessageW(input.handle(),WM_IME_COMPOSITION,0,GCS_RESULTSTR);
    SendMessageW(input.handle(),WM_IME_ENDCOMPOSITION,0,0);SendMessageW(input.handle(),WM_CHAR,u'中',0);
    std::u16string commits;for(const auto& e:events)if(e.kind==InputKind::Commit||e.kind==InputKind::Insert)commits+=e.text;
    expect(commits==u"中","IME result not duplicated by WM_CHAR");
    SendMessageW(input.handle(),WM_KEYDOWN,'A',0);SendMessageW(input.handle(),WM_CHAR,u'a',0);
    expect(events.back().kind==InputKind::Insert&&events.back().text==u"a","next physical key remains available");
}
void ime_cancel_keeps_model(){
    std::vector<InputEvent> events;InputWindow input;
    input.create(nullptr,[&](const InputEvent& e){events.push_back(e);},[](HWND,LPARAM){return CompositionUpdate{u"wen",u"",3,false};});
    SendMessageW(input.handle(),WM_IME_STARTCOMPOSITION,0,0);SendMessageW(input.handle(),WM_IME_COMPOSITION,0,GCS_COMPSTR);
    input.cancelComposition();expect(!input.composing(),"cancel clears preedit");
    int inserted=0;for(const auto& e:events)if(e.kind==InputKind::Commit||e.kind==InputKind::Insert)++inserted;
    expect(inserted==0,"cancel does not commit text");
}
void surrogate_input_is_atomic(){
    std::vector<InputEvent> events;InputWindow input;input.create(nullptr,[&](const InputEvent& e){events.push_back(e);});
    SendMessageW(input.handle(),WM_CHAR,0xd83d,0);expect(events.empty(),"high surrogate not inserted alone");
    SendMessageW(input.handle(),WM_CHAR,0xde00,0);
    expect(events.size()==1&&events.front().text==u"\U0001F600","surrogate pair emitted as one insertion");
}
void focus_survives_host_callback_restore(){
    HWND owner=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,-32000,-32000,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    expect(owner!=nullptr,"test owner created");
    {
        InputWindow input;input.create(owner,[](const InputEvent&){});
        auto dispatch=[&]{MSG message{};while(PeekMessageW(&message,input.handle(),0,0,PM_REMOVE))DispatchMessageW(&message);};
        input.focus();SetFocus(owner);dispatch();
        expect(GetFocus()==input.handle(),"editor receives focus after host restores its window at callback exit");
        input.focus();input.blur();dispatch();
        expect(GetFocus()!=input.handle(),"cancelled focus request cannot reclaim focus after exit");
    }
    DestroyWindow(owner);
}
void editor_keys_precede_host_accelerators(){
    HWND owner=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,-32000,-32000,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    expect(owner!=nullptr,"accelerator test owner created");
    ACCEL keys[]={{FVIRTKEY,'V',1},{FVIRTKEY,VK_TAB,2},{FVIRTKEY,VK_ESCAPE,3},{FVIRTKEY|FCONTROL,'Z',4}};
    HACCEL accelerators=CreateAcceleratorTableW(keys,4);
    expect(accelerators!=nullptr,"host accelerator fixture created");
    BYTE previous[256]{};GetKeyboardState(previous);BYTE plain[256]{};SetKeyboardState(plain);
    {
        int hostCommands=0;std::vector<InputEvent> events;InputWindow input;
        input.create(owner,[&](const InputEvent& e){events.push_back(e);if(e.kind==InputKind::Key&&e.key==VK_ESCAPE)input.blur();});
        auto pump=[&]{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(TranslateAcceleratorW(owner,accelerators,&message))++hostCommands;
            else {TranslateMessage(&message);DispatchMessageW(&message);}
        }};
        input.focus();pump();expect(GetFocus()==input.handle(),"accelerator test editor focused");
        events.clear();PostMessageW(input.handle(),WM_KEYDOWN,'V',1);
        MSG peek{};expect(PeekMessageW(&peek,input.handle(),WM_KEYDOWN,WM_KEYDOWN,PM_NOREMOVE)&&peek.message==WM_KEYDOWN,"peek without removal leaves keyboard message intact");
        expect(events.empty(),"peek without removal cannot insert text or invoke editor commands");
        PostMessageW(input.handle(),WM_KEYUP,'V',1L<<31);pump();
        std::u16string inserted;for(const auto& e:events)if(e.kind==InputKind::Insert)inserted+=e.text;
        expect(hostCommands==0&&inserted==u"v","letter typed into editor before host tool accelerator");
        PostMessageW(input.handle(),WM_KEYDOWN,VK_TAB,1);pump();
        expect(hostCommands==0&&events.back().kind==InputKind::Key&&events.back().key==VK_TAB,"Tab stays in table editor");
        plain[VK_CONTROL]=0x80;SetKeyboardState(plain);events.clear();
        PostMessageW(input.handle(),WM_KEYDOWN,'Z',1);pump();
        bool undo=false;for(const auto& e:events)if(e.kind==InputKind::Key&&e.key=='Z'&&(e.modifiers&InputControl))undo=true;
        expect(hostCommands==0&&undo,"Ctrl Z reaches table editor once before host undo accelerator");
        plain[VK_CONTROL]=0;SetKeyboardState(plain);
        PostMessageW(input.handle(),WM_KEYDOWN,VK_ESCAPE,1);pump();
        expect(hostCommands==0&&GetFocus()!=input.handle(),"Escape exits editor before host accelerator");
        PostMessageW(owner,WM_KEYDOWN,'V',1);pump();
        expect(hostCommands==1,"host shortcuts resume after editor blur");
        input.focus();pump();SetFocus(owner);PostMessageW(owner,WM_KEYDOWN,'V',1);pump();
        expect(hostCommands==2,"unrelated focused window keeps host shortcuts");
        input.destroy();PostMessageW(owner,WM_KEYDOWN,'V',1);pump();
        expect(hostCommands==3,"destroyed input leaves no keyboard interception");
    }
    SetKeyboardState(previous);DestroyAcceleratorTable(accelerators);DestroyWindow(owner);
}
void clipboard_unlock_on_failure(){
    // Own memory only: tests must not replace the user's system clipboard.
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,8);expect(memory!=nullptr,"clipboard fixture allocated");
    auto data=static_cast<char16_t*>(GlobalLock(memory));for(int i=0;i<4;++i)data[i]=u'A';GlobalUnlock(memory);
    bool refused=false;try{decodeClipboardText(memory);}catch(const ValidationError&){refused=true;}
    expect(refused,"unterminated clipboard data rejected");expect((GlobalFlags(memory)&GMEM_LOCKCOUNT)==0,"clipboard memory unlocked on failure");GlobalFree(memory);
}
void pt_dpi_roundtrip(){
    CellStyle style;style.padding=1;
    auto layout=layoutText({u"",{},style,60,30});
    for(double zoom:{0.25,1.,4.})for(double dpi:{1.,1.5,2.}){
        OverlayFrame frame;frame.layout=layout;frame.width=600;frame.height=300;frame.scaleX=frame.scaleY=zoom*dpi;frame.offsetPixels={10,5};
        auto bitmap=renderEditorOverlay(frame);
        const auto caret=layout->caret(0);const int x=static_cast<int>(std::floor(10+caret.left*frame.scaleX));
        const int y=static_cast<int>(std::floor(5+(caret.top+1)*frame.scaleY));bool blue=false;
        for(int dx=-1;dx<=2;++dx){const auto pixel=3*(static_cast<std::size_t>(y)*bitmap.width+x+dx);if(bitmap.rgb.at(pixel+2)>bitmap.rgb.at(pixel)+40)blue=true;}
        expect(blue,"caret scales and aligns in actual rendered pixel buffer");
    }
    OverlayFrame invalid;invalid.layout=layout;invalid.width=20000;invalid.height=20000;bool refused=false;
    try{renderEditorOverlay(invalid);}catch(const ValidationError&){refused=true;}expect(refused,"overlay allocation bounded");
}
}
int main(){try{
    custom_window_has_no_edit_children();ime_commit_once();ime_cancel_keeps_model();surrogate_input_is_atomic();focus_survives_host_callback_restore();editor_keys_precede_host_accelerators();clipboard_unlock_on_failure();pt_dpi_roundtrip();
    std::cout<<checks<<" input and preview checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
