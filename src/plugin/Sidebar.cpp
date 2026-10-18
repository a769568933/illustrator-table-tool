#include "Sidebar.hpp"
#include "AIContext.h"
#include "SPInterf.h"
#include "Trace.hpp"
#include "text/TextLayout.hpp"
#include "table/RichText.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {
constexpr wchar_t property[]=L"IllustratorTableTool.Sidebar";
const wchar_t* labels[]={L"新建表格…",L"表格选择工具",L"合并单元格",L"拆分单元格",
    L"上方插入行",L"下方插入行",L"左侧插入列",L"右侧插入列",L"删除所选行",L"删除所选列",L"转换普通对象",L"单元格样式…",L"平均行高",L"平均列宽"};
}
void Sidebar::create(SPBasicSuite* value,SPPluginRef self,Suites& suites,const std::array<AIMenuItemHandle,14>& items) {
    if(panel) return;basic=value;plugin=self;commands=items;traceStage("panel.create.start");
    auto panels=suites.get<AIPanelSuite>(kAIPanelSuite,kAIPanelVersion);
    AISize minimum{280,610};checked(panels->Create(plugin,ai::UnicodeString::FromUTF8("IllustratorTableTool.Sidebar"),
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
        auto control=[&](const wchar_t* kind,const wchar_t* text,int id,DWORD style){
            auto child=CreateWindowExW(0,kind,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),module,nullptr);
            if(!child)throw static_cast<ASErr>(kCantHappenErr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return child;
        };
        styleLabels[0]=control(L"STATIC",L"文字与单元格",-1,SS_LEFT);
        styleLabels[1]=control(L"STATIC",L"字体",-1,SS_LEFT);styleLabels[2]=control(L"STATIC",L"字号",-1,SS_LEFT);
        fontChoice=control(L"COMBOBOX",L"",23201,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        families=table::installedFontFamilies();families.insert(families.begin(),u"");
        for(const auto& family:families){const std::wstring label=family.empty()?L"默认字体":std::wstring(family.begin(),family.end());SendMessageW(fontChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        SendMessageW(fontChoice,CB_SETCURSEL,0,0);
        sizeField=control(L"EDIT",L"12",23202,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);SendMessageW(sizeField,EM_SETLIMITTEXT,16,0);
        sizeApply=control(L"BUTTON",L"应用字号",23203,BS_PUSHBUTTON|WS_TABSTOP);
        const wchar_t* alignment[]={L"左对齐",L"水平居中",L"右对齐",L"顶部对齐",L"垂直居中",L"底部对齐"};
        for(std::size_t i=0;i<alignButtons.size();++i)alignButtons[i]=control(L"BUTTON",alignment[i],23210+static_cast<int>(i),BS_AUTOCHECKBOX|BS_PUSHLIKE|WS_TABSTOP);
        transparent=control(L"BUTTON",L"透明背景（无填充）",23220,BS_AUTO3STATE|WS_TABSTOP);
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
        MoveWindow(buttons[i],12+column*(half+gap),82+row*39,half,31,TRUE);
    }
    MoveWindow(styleLabels[0],12,363,width,20,TRUE);MoveWindow(styleLabels[1],12,393,34,20,TRUE);
    MoveWindow(fontChoice,50,389,width-38,230,TRUE);MoveWindow(styleLabels[2],12,429,34,20,TRUE);
    MoveWindow(sizeField,50,425,68,24,TRUE);MoveWindow(sizeApply,126,425,width-114,25,TRUE);
    const int third=(width-12)/3;
    for(std::size_t i=0;i<alignButtons.size();++i)MoveWindow(alignButtons[i],12+static_cast<int>(i%3)*(third+6),466+static_cast<int>(i/3)*39,third,31,TRUE);
    MoveWindow(transparent,12,552,width,25,TRUE);
}
void Sidebar::show(Suites& suites) {
    if(panel) {traceStage("panel.show.start");checked(suites.get<AIPanelSuite>(kAIPanelSuite,kAIPanelVersion)->Show(panel,true));traceStage("panel.show.done");}
}
void Sidebar::update(bool selected,const std::wstring& message) {
    selectionEnabled=selected;if(!window) return;SetWindowTextW(status,message.c_str());
    enableControls();
}
void Sidebar::enableControls(){
    for(std::size_t i=0;i<buttons.size();++i) EnableWindow(buttons[i],!busy&&(i<2||selectionEnabled));
    for(auto control:{fontChoice,sizeField,sizeApply,transparent})if(control)EnableWindow(control,!busy&&selectionEnabled);
    for(auto control:alignButtons)if(control)EnableWindow(control,!busy&&selectionEnabled);
}
void Sidebar::updateStyle(const table::TableModel& model,table::CellRange range){
    if(!window)return;const auto& anchor=model.cellAt(range.firstRow,range.firstColumn);auto first=anchor.style;
    const auto initial=table::materializeFormatting(anchor.text,anchor.formatting,anchor.style);
    if(!initial.runs.empty()){first.font=initial.runs.front().font;first.fontSize=initial.runs.front().fontSize;}
    if(!initial.paragraphs.empty())first.alignment=initial.paragraphs.front().alignment;
    bool sameFont=true,sameSize=true,sameHorizontal=true,sameVertical=true,sameFill=true;
    for(const auto& cell:model.cells())if(cell.row<=range.lastRow&&cell.column<=range.lastColumn&&cell.row+cell.rowSpan>range.firstRow&&cell.column+cell.columnSpan>range.firstColumn){
        const auto format=table::materializeFormatting(cell.text,cell.formatting,cell.style);
        if(format.runs.empty()){sameFont=sameFont&&cell.style.font==first.font;sameSize=sameSize&&cell.style.fontSize==first.fontSize;}
        for(const auto& run:format.runs){sameFont=sameFont&&run.font==first.font;sameSize=sameSize&&run.fontSize==first.fontSize;}
        for(const auto& paragraph:format.paragraphs)sameHorizontal=sameHorizontal&&paragraph.alignment==first.alignment;
        sameVertical=sameVertical&&cell.style.verticalAlignment==first.verticalAlignment;sameFill=sameFill&&cell.style.fillEnabled==first.fillEnabled;
    }
    auto found=std::find(families.begin(),families.end(),first.font);
    SendMessageW(fontChoice,CB_SETCURSEL,sameFont&&found!=families.end()?static_cast<WPARAM>(found-families.begin()):static_cast<WPARAM>(-1),0);
    wchar_t size[32]{};if(sameSize)std::swprintf(size,32,L"%.6g",first.fontSize);SetWindowTextW(sizeField,size);
    for(std::size_t i=0;i<alignButtons.size();++i)SendMessageW(alignButtons[i],BM_SETCHECK,(i<3?sameHorizontal&&first.alignment==static_cast<int>(i):sameVertical&&first.verticalAlignment==static_cast<int>(i)-3)?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(transparent,BM_SETCHECK,sameFill?(first.fillEnabled?BST_UNCHECKED:BST_CHECKED):BST_INDETERMINATE,0);
}
void Sidebar::invokeStyle(const table::CellStylePatch& patch){
    if(busy||!selectionEnabled||!styleHandler)return;busy=true;enableControls();
    try{if(!beforeInvoke||beforeInvoke())styleHandler(patch);}catch(...){SetWindowTextW(status,L"样式未修改，请检查选区和字号（1–1000 点）。");}
    busy=false;enableControls();
}
void Sidebar::invoke(int command) {
    if(busy||command<0||command>=static_cast<int>(commands.size())) return;busy=true;
    enableControls();
    try {
        if(beforeInvoke&&!beforeInvoke()) {busy=false;enableControls();return;}
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
    enableControls();
}
LRESULT CALLBACK Sidebar::procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* self=static_cast<Sidebar*>(GetPropW(window,property));if(!self) return DefWindowProcW(window,message,wParam,lParam);
    if(message==WM_SIZE) self->layout();
    if(message==WM_COMMAND){
        const auto id=LOWORD(wParam),notification=HIWORD(wParam);table::CellStylePatch patch;
        if(id==23201&&notification==CBN_SELENDOK){const auto selected=SendMessageW(self->fontChoice,CB_GETCURSEL,0,0);if(selected>=0&&static_cast<std::size_t>(selected)<self->families.size()){patch.font=self->families[static_cast<std::size_t>(selected)];self->invokeStyle(patch);}return 0;}
        if(notification==BN_CLICKED&&id==23203){wchar_t value[32]{};GetWindowTextW(self->sizeField,value,32);wchar_t* end=nullptr;const double size=std::wcstod(value,&end);
            if(end==value||*end||!std::isfinite(size)||size<1||size>1000){SetWindowTextW(self->status,L"字号请输入 1–1000 点之间的数字。");return 0;}patch.fontSize=size;self->invokeStyle(patch);return 0;}
        if(notification==BN_CLICKED&&id>=23210&&id<23216){if(id<23213)patch.alignment=id-23210;else patch.verticalAlignment=id-23213;self->invokeStyle(patch);return 0;}
        if(notification==BN_CLICKED&&id==23220){patch.fillEnabled=SendMessageW(self->transparent,BM_GETCHECK,0,0)!=BST_CHECKED;self->invokeStyle(patch);return 0;}
    }
    if(message==WM_COMMAND&&HIWORD(wParam)==BN_CLICKED) {
        const int id=LOWORD(wParam)-23000;
        if(id>=0&&id<static_cast<int>(self->commands.size())) {self->invoke(id);return 0;}
    }
    return CallWindowProcW(self->original,window,message,wParam,lParam);
}
void Sidebar::destroy(Suites& suites) {
    beforeInvoke={};styleHandler={};
    if(window&&original) {SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));RemovePropW(window,property);original=nullptr;}
    for(auto& button:buttons) {if(button&&IsWindow(button)) DestroyWindow(button);button=nullptr;}
    for(auto control:{fontChoice,sizeField,sizeApply,transparent})if(control&&IsWindow(control))DestroyWindow(control);
    fontChoice=sizeField=sizeApply=transparent=nullptr;
    for(auto& control:alignButtons){if(control&&IsWindow(control))DestroyWindow(control);control=nullptr;}
    for(auto& control:styleLabels){if(control&&IsWindow(control))DestroyWindow(control);control=nullptr;}families.clear();
    if(status&&IsWindow(status)) DestroyWindow(status);status=nullptr;
    if(panel) {suites.get<AIPanelSuite>(kAIPanelSuite,kAIPanelVersion)->Destroy(panel);panel=nullptr;}
    window=nullptr;if(font) {DeleteObject(font);font=nullptr;}
}
