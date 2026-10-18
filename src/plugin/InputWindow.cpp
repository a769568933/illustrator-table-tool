#include "InputWindow.hpp"
#include <imm.h>
#include <algorithm>

namespace {
constexpr wchar_t className[]=L"IllustratorTableTool.Input.v3";
std::uint32_t modifiers(){return ((GetKeyState(VK_SHIFT)&0x8000)?InputShift:0)|((GetKeyState(VK_CONTROL)&0x8000)?InputControl:0)|((GetKeyState(VK_MENU)&0x8000)?InputAlt:0);}
struct ImeContext {HWND window;HIMC value;explicit ImeContext(HWND h):window(h),value(ImmGetContext(h)){}~ImeContext(){if(value)ImmReleaseContext(window,value);}};
struct ClipboardScope {explicit ClipboardScope(HWND h){if(!OpenClipboard(h))throw table::ValidationError("Clipboard is busy");}~ClipboardScope(){CloseClipboard();}};
std::u16string compositionString(HIMC context,DWORD kind){
    const LONG bytes=ImmGetCompositionStringW(context,kind,nullptr,0);
    if(bytes<0||bytes%2||bytes>2*static_cast<LONG>(table::maximumTextBytes))throw table::ValidationError("Invalid input method buffer");
    std::u16string value(static_cast<std::size_t>(bytes)/2,0);
    if(bytes&&ImmGetCompositionStringW(context,kind,value.data(),static_cast<DWORD>(bytes))!=bytes)throw table::ValidationError("Input method buffer changed");
    table::validateText(value);return value;
}
CompositionUpdate readComposition(HWND window,LPARAM flags){
    ImeContext context(window);if(!context.value)throw table::ValidationError("Input method context unavailable");CompositionUpdate update;
    if(flags&GCS_RESULTSTR){update.result=compositionString(context.value,GCS_RESULTSTR);update.resultAvailable=true;}
    if(flags&GCS_COMPSTR){update.preedit=compositionString(context.value,GCS_COMPSTR);auto caret=ImmGetCompositionStringW(context.value,GCS_CURSORPOS,nullptr,0);
        update.caret=static_cast<std::uint32_t>(std::clamp<LONG>(caret,0,static_cast<LONG>(update.preedit.size())));}
    return update;
}
}
std::u16string decodeClipboardText(HGLOBAL memory){
    const auto bytes=GlobalSize(memory);if(bytes<2||bytes%2||bytes>2*(table::maximumTextBytes+1))throw table::ValidationError("Invalid clipboard size");
    const auto value=static_cast<const char16_t*>(GlobalLock(memory));if(!value)throw table::ValidationError("Clipboard cannot be read");
    try{const auto count=bytes/2;const auto end=std::find(value,value+count,char16_t(0));if(end==value+count)throw table::ValidationError("Clipboard text is not terminated");
        std::u16string result(value,end);table::validateText(result);GlobalUnlock(memory);return result;
    }catch(...){GlobalUnlock(memory);throw;}
}
void InputWindow::create(HWND parent,std::function<void(const InputEvent&)> target,CompositionReader source){
    if(window)return;owner=parent;callback=std::move(target);reader=source?std::move(source):CompositionReader(readComposition);
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&procedure),&module))throw table::ValidationError("Cannot locate input module");
    WNDCLASSEXW definition{};definition.cbSize=sizeof(definition);definition.hInstance=module;definition.lpfnWndProc=procedure;definition.lpszClassName=className;
    if(!RegisterClassExW(&definition)){
        WNDCLASSEXW existing{};existing.cbSize=sizeof(existing);
        if(GetLastError()!=ERROR_CLASS_ALREADY_EXISTS||!GetClassInfoExW(module,className,&existing)||existing.lpfnWndProc!=procedure)throw table::ValidationError("Cannot register input window");
    }
    window=CreateWindowExW(WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,className,L"",parent?WS_CHILD:WS_POPUP,0,0,1,1,parent,nullptr,module,this);
    if(!window)throw table::ValidationError("Cannot create input window");
    ImmAssociateContextEx(window,nullptr,IACE_DEFAULT);SetTimer(window,1,500,nullptr);
}
void InputWindow::emit(const InputEvent& event){if(callback)callback(event);}
void InputWindow::focus(){if(window){ShowWindow(window,SW_SHOWNOACTIVATE);SetFocus(window);}}
void InputWindow::blur(){if(window){ShowWindow(window,SW_HIDE);if(GetFocus()==window&&owner)SetFocus(owner);}}
void InputWindow::setCaretScreen(POINT point){
    if(!window)return;if(owner)ScreenToClient(owner,&point);
    SetWindowPos(window,nullptr,point.x,point.y,1,1,SWP_NOZORDER|SWP_NOACTIVATE);
    ImeContext context(window);if(context.value){CANDIDATEFORM candidate{};candidate.dwStyle=CFS_CANDIDATEPOS;candidate.ptCurrentPos={0,18};ImmSetCandidateWindow(context.value,&candidate);
        COMPOSITIONFORM composition{};composition.dwStyle=CFS_POINT;composition.ptCurrentPos={0,0};ImmSetCompositionWindow(context.value,&composition);}
}
bool InputWindow::requestCompositionCommit(){
    if(!compositionActive)return true;ImeContext context(window);if(!context.value)return false;
    const auto before=commitSerial;commitPending=true;const auto completed=ImmNotifyIME(context.value,NI_COMPOSITIONSTR,CPS_COMPLETE,0);commitPending=false;
    return completed&&!compositionActive&&commitSerial!=before;
}
void InputWindow::cancelComposition(){
    const bool pending=compositionActive;compositionActive=false;pendingHigh=0;duplicateChars.clear();
    if(window){ImeContext context(window);if(context.value)ImmNotifyIME(context.value,NI_COMPOSITIONSTR,CPS_CANCEL,0);}
    if(pending)emit({InputKind::Cancel,{}});
}
void InputWindow::copyText(std::u16string_view text){
    table::validateText(text);ClipboardScope clipboard(window);const auto bytes=(text.size()+1)*sizeof(char16_t);
    const auto memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!memory)throw table::ValidationError("Cannot allocate clipboard text");
    const auto destination=static_cast<char16_t*>(GlobalLock(memory));if(!destination){GlobalFree(memory);throw table::ValidationError("Cannot write clipboard text");}
    std::copy(text.begin(),text.end(),destination);destination[text.size()]=0;GlobalUnlock(memory);
    if(!EmptyClipboard()||!SetClipboardData(CF_UNICODETEXT,memory)){GlobalFree(memory);throw table::ValidationError("Cannot set clipboard text");}
}
void InputWindow::pasteText(){ClipboardScope clipboard(window);auto memory=GetClipboardData(CF_UNICODETEXT);if(memory)emit({InputKind::Paste,decodeClipboardText(memory)});}
void InputWindow::character(char16_t value){
    if(!duplicateChars.empty()){if(duplicateChars.front()==value){duplicateChars.erase(duplicateChars.begin());return;}duplicateChars.clear();}
    if(value<0x20||value==0x7f)return;
    if(value>=0xd800&&value<=0xdbff){pendingHigh=value;return;}
    if(value>=0xdc00&&value<=0xdfff){if(!pendingHigh)throw table::ValidationError("Invalid input surrogate");std::u16string pair{pendingHigh,value};pendingHigh=0;emit({InputKind::Insert,std::move(pair)});return;}
    pendingHigh=0;emit({InputKind::Insert,std::u16string(1,value)});
}
LRESULT InputWindow::message(UINT message,WPARAM wParam,LPARAM lParam){
    switch(message){
    case WM_GETDLGCODE:return DLGC_WANTCHARS|DLGC_WANTARROWS|DLGC_WANTTAB|DLGC_WANTALLKEYS;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(window,&paint);EndPaint(window,&paint);return 0;}
    case WM_IME_STARTCOMPOSITION:compositionActive=true;duplicateChars.clear();pendingHigh=0;return 0;
    case WM_IME_COMPOSITION:{
        const auto update=reader(window,lParam);
        if(update.resultAvailable){table::validateText(update.result);duplicateChars=update.result;compositionActive=false;++commitSerial;emit({InputKind::Commit,update.result});}
        if(lParam&GCS_COMPSTR){table::validateText(update.preedit);compositionActive=true;emit({InputKind::Composition,update.preedit,0,0,update.caret,static_cast<std::uint32_t>(update.preedit.size())});}
        return 0;
    }
    case WM_IME_ENDCOMPOSITION:if(compositionActive&&!commitPending){compositionActive=false;emit({InputKind::Cancel,{}});}return 0;
    case WM_IME_CHAR:return 0;
    case WM_CHAR:if(!compositionActive)character(static_cast<char16_t>(wParam));return 0;
    case WM_UNICHAR:
        if(wParam==UNICODE_NOCHAR)return TRUE;
        if(wParam>0x10ffff)throw table::ValidationError("Invalid input code point");
        if(wParam>0xffff){const auto cp=static_cast<std::uint32_t>(wParam)-0x10000;character(static_cast<char16_t>(0xd800+(cp>>10)));character(static_cast<char16_t>(0xdc00+(cp&1023)));}
        else character(static_cast<char16_t>(wParam));return 0;
    case WM_KEYDOWN:{
        duplicateChars.clear();const auto mods=modifiers();
        if(compositionActive){if(wParam==VK_ESCAPE){cancelComposition();return 0;}return DefWindowProcW(window,message,wParam,lParam);}
        emit({InputKind::Key,{},static_cast<std::uint32_t>(wParam),mods});return 0;
    }
    case WM_PASTE:pasteText();return 0;
    case WM_KILLFOCUS:pendingHigh=0;emit({InputKind::FocusLost,{}});return 0;
    case WM_TIMER:if(wParam==1)emit({InputKind::Blink,{}});return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
LRESULT CALLBACK InputWindow::procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    auto self=reinterpret_cast<InputWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<InputWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wParam,lParam);
    if(message==WM_NCDESTROY){SetWindowLongPtrW(window,GWLP_USERDATA,0);self->window=nullptr;return DefWindowProcW(window,message,wParam,lParam);}
    try{return self->message(message,wParam,lParam);}catch(...){try{self->emit({InputKind::Error,u"输入未完成，请重新选择单元格后重试。"});}catch(...){}return 0;}
}
void InputWindow::destroy(){
    callback={};
    if(window){const auto original=window;cancelComposition();KillTimer(original,1);if(GetFocus()==original&&owner)SetFocus(owner);DestroyWindow(original);window=nullptr;}
    if(module&&UnregisterClassW(className,module))module=nullptr;
}
