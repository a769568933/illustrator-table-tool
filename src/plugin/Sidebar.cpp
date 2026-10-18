#include "Sidebar.hpp"
#include "AIContext.h"
#include "SPInterf.h"
#include "Trace.hpp"
#include <algorithm>

namespace {
constexpr wchar_t property[]=L"IllustratorTableTool.Sidebar";
const wchar_t* labels[]={L"新建表格…",L"表格选择工具",L"合并单元格",L"拆分单元格",
    L"上方插入行",L"下方插入行",L"左侧插入列",L"右侧插入列",L"删除所选行",L"删除所选列",L"转换普通对象",L"单元格样式…",L"平均行高",L"平均列宽"};
}
void Sidebar::create(SPBasicSuite* value,SPPluginRef self,Suites& suites,const std::array<AIMenuItemHandle,14>& items) {
    if(panel) return;basic=value;plugin=self;commands=items;traceStage("panel.create.start");
    auto panels=suites.get<AIPanelSuite>(kAIPanelSuite,kAIPanelVersion);
    AISize minimum{260,470};checked(panels->Create(plugin,ai::UnicodeString::FromUTF8("IllustratorTableTool.Sidebar"),
        ai::UnicodeString::FromUTF8("画布表格"),1,minimum,true,nullptr,this,panel));
    traceStage("panel.create.done");
    try {
        checked(panels->SetPreferredSizes(panel,minimum));
        if(panels->SetSVGIconResourceID(panel,16001,16001)) traceStage("panel.icon.unavailable");
        checked(panels->GetPlatformWindow(panel,window));if(!window) throw static_cast<ASErr>(kCantHappenErr);
        font=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        auto module=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window,GWLP_HINSTANCE));
        status=CreateWindowExW(0,L"STATIC",L"在画布选格；Shift 选择矩形区域。",WS_CHILD|WS_VISIBLE|SS_LEFT,12,12,236,54,window,nullptr,module,nullptr);
        SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        for(std::size_t i=0;i<buttons.size();++i) {
            buttons[i]=CreateWindowExW(0,L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,
                window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(23000+i)),module,nullptr);
            if(!buttons[i]) throw static_cast<ASErr>(kCantHappenErr);
            SendMessageW(buttons[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        }
        if(!SetPropW(window,property,this)) throw static_cast<ASErr>(kCantHappenErr);
        original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&Sidebar::procedure)));
        if(!original) throw static_cast<ASErr>(kCantHappenErr);
        layout();update(false,L"请先选择单元格。\n单击选格，Shift 多选，双击编辑文字。");traceStage("panel.controls.ready");
    } catch(...) {destroy(suites);throw;}
}
void Sidebar::layout() {
    RECT bounds{};GetClientRect(window,&bounds);const int width=std::max(120,static_cast<int>(bounds.right)-24);
    MoveWindow(status,12,12,width,64,TRUE);
    const int gap=8,half=(width-gap)/2;
    for(std::size_t i=0;i<buttons.size();++i) {
        const int row=static_cast<int>(i/2),column=static_cast<int>(i%2);
        MoveWindow(buttons[i],12+column*(half+gap),82+row*49,half,35,TRUE);
    }
}
void Sidebar::show(Suites& suites) {
    if(panel) {traceStage("panel.show.start");checked(suites.get<AIPanelSuite>(kAIPanelSuite,kAIPanelVersion)->Show(panel,true));traceStage("panel.show.done");}
}
void Sidebar::update(bool selected,const std::wstring& message) {
    selectionEnabled=selected;if(!window) return;SetWindowTextW(status,message.c_str());
    for(std::size_t i=0;i<buttons.size();++i) EnableWindow(buttons[i],!busy&&(i<2||selected));
}
void Sidebar::invoke(int command) {
    if(busy||command<0||command>=static_cast<int>(commands.size())) return;busy=true;
    for(auto button:buttons) EnableWindow(button,FALSE);
    try {
        if(beforeInvoke&&!beforeInvoke()) {busy=false;for(std::size_t i=0;i<buttons.size();++i)EnableWindow(buttons[i],i<2||selectionEnabled);return;}
        Suites suites(basic);sSPBlocks=suites.get<SPBlocksSuite>(kSPBlocksSuite,kSPBlocksSuiteVersion);
        sAIUnicodeString=suites.get<AIUnicodeStringSuite>(kAIUnicodeStringSuite,kAIUnicodeStringVersion);
        auto context=suites.get<AIAppContextSuite>(kAIAppContextSuite,kAIAppContextVersion);
        AIAppContextHandle scope=nullptr;checked(context->PushAppContext(plugin,&scope));
        try {
            auto interfaceSuite=suites.get<SPInterfaceSuite>(kSPInterfaceSuite,kSPInterfaceSuiteVersion);
            AIMenuMessage message{};checked(interfaceSuite->SetupMessageData(plugin,&message.d));message.menuItem=commands[command];
            SPErr outcome=kNoErr;
            const auto sent=interfaceSuite->SendMessage(plugin,kCallerAIMenu,kSelectorAIGoMenuItem,&message,&outcome);
            interfaceSuite->EmptyMessageData(plugin,&message.d);checked(sent);
            if(outcome) SetWindowTextW(status,L"操作未完成，请检查选区和表格状态。");
        } catch(...) {context->PopAppContext(scope);throw;}
        checked(context->PopAppContext(scope));
    } catch(...) {SetWindowTextW(status,L"操作未完成。请重新选择表格后重试。");}
    busy=false;
    for(std::size_t i=0;i<buttons.size();++i) EnableWindow(buttons[i],i<2||selectionEnabled);
}
LRESULT CALLBACK Sidebar::procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* self=static_cast<Sidebar*>(GetPropW(window,property));if(!self) return DefWindowProcW(window,message,wParam,lParam);
    if(message==WM_SIZE) self->layout();
    if(message==WM_COMMAND&&HIWORD(wParam)==BN_CLICKED) {
        const int id=LOWORD(wParam)-23000;
        if(id>=0&&id<static_cast<int>(self->commands.size())) {self->invoke(id);return 0;}
    }
    return CallWindowProcW(self->original,window,message,wParam,lParam);
}
void Sidebar::destroy(Suites& suites) {
    beforeInvoke={};
    if(window&&original) {SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));RemovePropW(window,property);original=nullptr;}
    for(auto& button:buttons) {if(button&&IsWindow(button)) DestroyWindow(button);button=nullptr;}
    if(status&&IsWindow(status)) DestroyWindow(status);status=nullptr;
    if(panel) {suites.get<AIPanelSuite>(kAIPanelSuite,kAIPanelVersion)->Destroy(panel);panel=nullptr;}
    window=nullptr;if(font) {DeleteObject(font);font=nullptr;}
}
