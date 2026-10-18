#include <windows.h>
#include "Suites.hpp"
#include "DocumentAdapter.hpp"
#include "Dialogs.hpp"
#include "Sidebar.hpp"
#include "Trace.hpp"
#include "EditController.hpp"
#include "HostCommand.hpp"
#ifdef TABLE_HOST_ACCEPTANCE
#include "HostAcceptance.hpp"
#include "AITimer.h"
#include "AIApplication.h"
#endif
#include "SPInterf.h"
#include "AIPlugin.h"
#include "AIMenu.h"
#include "AIMenuGroups.h"
#include "AITool.h"
#include "AIToolNames.h"
#include "AIEvent.h"
#include "AINotifier.h"
#include "AIRuntime.h"
#include "SPAccess.h"
#include "AIAnnotator.h"
#include "AIAnnotatorDrawer.h"
#include "AIDocumentView.h"
#include "table/Interaction.hpp"
#include "table/Commands.hpp"
#include <cstring>
#include <exception>
#include <cmath>
#include <array>
#include <algorithm>

extern "C" { AIUnicodeStringSuite* sAIUnicodeString=nullptr; SPBlocksSuite* sSPBlocks=nullptr; }
namespace {
bool is(const char* value,const char* expected){ return std::strcmp(value,expected)==0; }
struct State {
    AIMenuItemHandle parent=nullptr, about=nullptr, create=nullptr, edit=nullptr;
    AIMenuItemHandle showSidebar=nullptr;
#ifdef TABLE_HOST_ACCEPTANCE
    AIMenuItemHandle acceptance=nullptr;
    AITimerHandle acceptanceTimer=nullptr;
#endif
    std::array<AIMenuItemHandle,12> commands{};
    AIToolHandle tool=nullptr;
    AIAnnotatorHandle annotator=nullptr;
    bool placing=false, selectedTool=false;
    std::optional<table::TableModel> pending;
    std::vector<NativeTable> tables;
    std::optional<std::size_t> active;
    table::Interaction interaction;
    DWORD lastDown=0;
    table::CellId lastCell=0;
    table::Point lastPoint;
    Sidebar sidebar;
    EditController editor;
    std::uint64_t generation=1;
    SPBasicSuite* basic=nullptr;
    SPPluginRef plugin=nullptr;
    SPAccessRef access=nullptr;
    AINotifierHandle started=nullptr,shutdown=nullptr;
    AINotifierHandle documentSwitch=nullptr,documentClose=nullptr,artChanged=nullptr;
    AINotifierHandle viewChanged=nullptr;
    bool mutating=false;
};
struct MutationScope {
    State& state;bool previous;
    explicit MutationScope(State& value):state(value),previous(value.mutating){state.mutating=true;}
    ~MutationScope(){state.mutating=previous;}
};
void showSidebar(State& state,Suites& suites) {
    std::array<AIMenuItemHandle,14> items{state.create,state.edit};
    std::copy(state.commands.begin(),state.commands.end(),items.begin()+2);
    state.sidebar.create(state.basic,state.plugin,suites,items);state.sidebar.show(suites);
}
void updateSidebar(State& state,Suites& suites) {
    if(!state.active||*state.active>=state.tables.size()||!state.interaction.selection()) {
        state.sidebar.update(false,state.placing?L"请在画布单击放置新表格。":L"请先使用表格工具选择单元格。\nShift 多选；双击编辑文字。");return;
    }
    auto& native=state.tables[*state.active];DocumentAdapter adapter(suites);
    if(!adapter.valid(native)) {state.sidebar.update(false,L"当前表格不可编辑。\n请检查当前文档、锁定和隐藏状态。");return;}
    const auto range=*state.interaction.selection();
    auto message=std::to_wstring(native.model.rows())+L" 行 × "+std::to_wstring(native.model.columns())+L" 列\n选区：行 "+
        std::to_wstring(range.firstRow+1)+L"–"+std::to_wstring(range.lastRow+1)+L"，列 "+std::to_wstring(range.firstColumn+1)+L"–"+std::to_wstring(range.lastColumn+1);
    if(state.editor.editing())message+=L"\n格内编辑中，Esc 退出，Tab 切格。";
    state.sidebar.update(true,message);
}
void invalidate(Suites& suites) {
    // Invalidate only on interaction events; never during the draw callback.
    AIRect screen{-32000,-32000,32000,32000};
    checked(suites.get<AIAnnotatorSuite>(kAIAnnotatorSuite,kAIAnnotatorVersion)->InvalAnnotationRect(nullptr,&screen));
}
void startup(SPMessageData* data,Suites& suites,State& state) {
    traceStage("plugin.start.0.2.2");
    state.basic=data->basic;state.plugin=data->self;
    state.editor.configure(state.basic,state.plugin,EditorHost{
        [&state]()->NativeTable* {return state.active&&*state.active<state.tables.size()?&state.tables[*state.active]:nullptr;},
        [&state](){return state.generation;},
        [&state](){invokeHostCommand(state.basic,state.plugin,[](Suites& nested){invalidate(nested);});},
        [&state](const std::wstring& message){state.sidebar.update(state.active.has_value(),message);},
        [&state](const std::function<void()>& operation){MutationScope scope(state);operation();},
        [&state](table::CellId id){if(state.active&&*state.active<state.tables.size()){
            state.interaction.selectCell(state.tables[*state.active].model,id);
            invokeHostCommand(state.basic,state.plugin,[&state](Suites& nested){updateSidebar(state,nested);});
        }}
    });
    state.sidebar.setBeforeInvoke([&state](){return state.editor.beforeStructure();});
    auto menus=suites.get<AIMenuSuite>(kAIMenuSuite,kAIMenuVersion);
    AIPlatformAddMenuItemDataUS parent;parent.groupName=kObjectUtilsMenuGroup;parent.itemText=ai::UnicodeString::FromUTF8("画布表格");
    checked(menus->AddMenuItem(data->self,"IllustratorTableTool.Menu",&parent,0,&state.parent));
    AIMenuGroup group=nullptr;checked(menus->AddMenuGroupAsSubMenu("IllustratorTableTool.Commands",0,state.parent,&group));
    auto add=[&](const char* key,const char* title,AIMenuItemHandle* handle) {
        AIPlatformAddMenuItemDataUS item; item.groupName="IllustratorTableTool.Commands";
        item.itemText=ai::UnicodeString::FromUTF8(title);
        checked(menus->AddMenuItem(data->self,key,&item,0,handle));
    };
    add("IllustratorTableTool.About","关于画布表格…",&state.about);
    add("IllustratorTableTool.Create","新建表格…",&state.create);
    add("IllustratorTableTool.Edit","选择和调整单元格",&state.edit);
#ifdef TABLE_HOST_ACCEPTANCE
    add("IllustratorTableTool.Acceptance","运行开发验收（新测试文档）",&state.acceptance);
    auto timers=suites.get<AITimerSuite>(kAITimerSuite,kAITimerVersion);
    checked(timers->AddTimer(data->self,"IllustratorTableTool.AcceptanceTimer",5*kTicksPerSecond,&state.acceptanceTimer));
    checked(timers->SetTimerActive(state.acceptanceTimer,false));
#endif
    const char* names[]={"合并单元格","拆分单元格","在上方插入行","在下方插入行","在左侧插入列","在右侧插入列","删除所选行","删除所选列","转换为普通对象","单元格样式…","平均行高","平均列宽"};
    for(std::size_t i=0;i<state.commands.size();++i) {
        auto key="IllustratorTableTool.Command"+std::to_string(i);add(key.c_str(),names[i],&state.commands[i]);
    }
    AIPlatformAddMenuItemDataUS sidebarMenu;sidebarMenu.groupName=kOtherPalettesMenuGroup;sidebarMenu.itemText=ai::UnicodeString::FromUTF8("画布表格");
    checked(menus->AddMenuItem(data->self,"IllustratorTableTool.Sidebar.Menu",&sidebarMenu,0,&state.showSidebar));
    auto notifiers=suites.get<AINotifierSuite>(kAINotifierSuite,kAINotifierVersion);
    checked(notifiers->AddNotifier(data->self,"IllustratorTableTool.Started",kAIApplicationStartedNotifier,&state.started));
    checked(notifiers->AddNotifier(data->self,"IllustratorTableTool.Shutdown",kAIApplicationShutdownNotifier,&state.shutdown));
    checked(notifiers->AddNotifier(data->self,"IllustratorTableTool.DocumentSwitch",kAIActiveDocumentWindowAboutToBeChangedNotifier,&state.documentSwitch));
    checked(notifiers->AddNotifier(data->self,"IllustratorTableTool.DocumentClose",kAIDocumentAboutToCloseNotifier,&state.documentClose));
    checked(notifiers->AddNotifier(data->self,"IllustratorTableTool.ArtChanged",kAIArtObjectsChangedNotifier,&state.artChanged));
    checked(notifiers->AddNotifier(data->self,"IllustratorTableTool.ViewChanged",kAIDocumentViewChangedNotifier,&state.viewChanged));
    checked(suites.get<SPAccessSuite>(kSPAccessSuite,kSPAccessSuiteVersion)->AcquirePlugin(data->self,&state.access));
    AIAddToolData tool;
    tool.title=ai::UnicodeString::FromUTF8("画布表格工具");
    tool.tooltip=ai::UnicodeString::FromUTF8("单击选格；Shift 多选；拖动边线；双击编辑文字");
    tool.sameGroupAs=kNoTool; tool.sameToolsetAs=kNoTool;
    tool.normalIconResID=tool.darkIconResID=16001; tool.iconType=ai::IconType::kSVG;
    checked(suites.get<AIToolSuite>(kAIToolSuite,kAIToolVersion)->AddTool(data->self,"IllustratorTableTool.Tool",tool,kToolWantsToTrackCursorOption,&state.tool));
    auto annotations=suites.get<AIAnnotatorSuite>(kAIAnnotatorSuite,kAIAnnotatorVersion);
    checked(annotations->AddAnnotator(data->self,"IllustratorTableTool.Selection",&state.annotator));
    checked(annotations->SetAnnotatorActive(state.annotator,true));
}
table::Point local(const NativeTable& t,AIRealPoint p){ return {(p.h-t.origin.h)/t.scaleX,(t.origin.v-p.v)/t.scaleY}; }
void toolEvent(State& state,Suites& suites,const char* selector,AIToolMessage* message) {
    if(message->tool!=state.tool) return;
    auto annotations=suites.get<AIAnnotatorSuite>(kAIAnnotatorSuite,kAIAnnotatorVersion);
    if(is(selector,kSelectorAISelectTool)) { state.selectedTool=true; showSidebar(state,suites); invalidate(suites); return; }
    if(is(selector,kSelectorAIDeselectTool)) {
        if(state.editor.editing())traceStage("edit.exit.tool-deselected");
        state.editor.end(false);
        state.selectedTool=false; state.interaction.cancel(); state.placing=false; invalidate(suites); return;
    }
    DocumentAdapter adapter(suites);
    auto view=suites.get<AIDocumentViewSuite>(kAIDocumentViewSuite,kAIDocumentViewVersion);
    AIReal zoom=1; checked(view->GetDocumentViewZoom(nullptr,&zoom));
    if((GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0) {
        state.interaction.cancel(); state.placing=false; invalidate(suites); return;
    }
    if(is(selector,kSelectorAIToolMouseDown)) {
        const DWORD eventTime=static_cast<DWORD>(GetMessageTime());
        if(state.editor.editing()&&!state.editor.beforeStructure())return;
        if(state.placing&&state.pending) {
            auto model=*state.pending;
            state.tables.push_back(adapter.createTable(model,message->cursor));
            state.active=state.tables.size()-1; state.interaction.clearSelection(); state.placing=false;state.pending.reset();
            invalidate(suites); return;
        }
        auto oldGroup=state.active&&*state.active<state.tables.size() ? state.tables[*state.active].group : nullptr;
        std::vector<RejectedTable> rejected;
        state.tables=adapter.discoverTables(&rejected);
        state.active.reset();
        for(std::size_t i=state.tables.size();i>0;--i) {
            const auto& table=state.tables[i-1];
            if(!adapter.valid(table)) continue;
            if(table::Geometry::hitTest(table.model,local(table,message->cursor),zoom,3,table.scaleX,table.scaleY).kind!=table::HitKind::Outside) { state.active=i-1; break; }
        }
        if(!state.active) {
            state.editor.end(false);
            state.interaction.cancel();state.interaction.clearSelection();invalidate(suites);
            for(const auto& item:rejected) {
                if(message->cursor.h>=item.bounds.left&&message->cursor.h<=item.bounds.right&&
                   message->cursor.v>=item.bounds.bottom&&message->cursor.v<=item.bounds.top) {
                    state.sidebar.update(false,item.reason);
                    MessageBoxW(nullptr,item.reason.c_str(),L"画布表格",MB_OK|MB_ICONINFORMATION);break;
                }
            }
            return;
        }
        auto& table=state.tables[*state.active];
        if(oldGroup!=table.group) {state.editor.end(false);state.interaction.clearSelection();state.lastCell=0;}
        auto point=local(table,message->cursor);
        auto hit=table::Geometry::hitTest(table.model,point,zoom,3,table.scaleX,table.scaleY);
        const bool shift=message->event&&(message->event->modifiers&aiEventModifiers_shiftKey)!=0;
        const bool hostDouble=message->event&&(message->event->modifiers&aiEventModifiers_doubleClick)!=0;
        if(hit.kind==table::HitKind::Cell&&table::shouldBeginCellEdit(hit.cell,state.lastCell,oldGroup==table.group,hostDouble,shift,
            eventTime-state.lastDown,GetDoubleClickTime(),table::Geometry::screenDistance(point,state.lastPoint,zoom,table.scaleX,table.scaleY))) {
            state.lastCell=0; state.interaction.cancel();
            state.editor.begin(table,hit.cell,point);
            return;
        }
        state.lastDown=eventTime; state.lastCell=hit.cell; state.lastPoint=point;
        if(!shift&&hit.kind==table::HitKind::Cell&&state.editor.pointer(table,hit.cell,point,false)){invalidate(suites);return;}
        if(state.editor.editing())state.editor.end(false);
        state.interaction.pointerDown(table.model,point,shift,zoom,table.scaleX,table.scaleY);
        invalidate(suites);
    } else if(is(selector,kSelectorAIToolMouseDrag) || is(selector,kSelectorAIToolMouseUp)) {
        if(is(selector,kSelectorAIToolMouseDrag))state.lastCell=0;
        if(!state.active || *state.active>=state.tables.size() || !adapter.valid(state.tables[*state.active])) {
            state.interaction.cancel(); state.active.reset(); invalidate(suites); return;
        }
        auto& table=state.tables[*state.active];
        if(state.editor.editing()){
            const auto point=local(table,message->cursor);const auto hit=table::Geometry::hitTest(table.model,point,zoom,0,table.scaleX,table.scaleY);
            if(hit.kind==table::HitKind::Cell)state.editor.pointer(table,hit.cell,point,true);
            invalidate(suites);return;
        }
        state.interaction.pointerMove(local(table,message->cursor));
        if(is(selector,kSelectorAIToolMouseUp)) {
            auto result=state.interaction.pointerUp();
            if(result) adapter.applyGeometry(table,*result);
        }
        invalidate(suites);
    } else if(is(selector,kSelectorAITrackToolCursor)) {
        LPCTSTR cursor=state.placing ? IDC_CROSS : IDC_ARROW;
        if(state.active && *state.active<state.tables.size() && adapter.valid(state.tables[*state.active])) {
            const auto& table=state.tables[*state.active];
            auto hit=table::Geometry::hitTest(table.model,local(table,message->cursor),zoom,3,table.scaleX,table.scaleY);
            if(hit.edge && hit.edge->track>0) cursor=hit.edge->vertical ? IDC_SIZEWE : IDC_SIZENS;
        }
        SetCursor(LoadCursor(nullptr,cursor));
    }
    (void)annotations;
}
void draw(State& state,Suites& suites,AIAnnotatorMessage* message) {
    if(state.mutating||!state.selectedTool || !state.active || *state.active>=state.tables.size() || !message->drawer) return;
    auto& table=state.tables[*state.active];
    DocumentAdapter adapter(suites); if(!adapter.valid(table)) return;
    auto views=suites.get<AIDocumentViewSuite>(kAIDocumentViewSuite,kAIDocumentViewVersion);
    auto drawer=suites.get<AIAnnotatorDrawerSuite>(kAIAnnotatorDrawerSuite,kAIAnnotatorDrawerVersion);
    AIRGBColor blue; blue.red=9000; blue.green=32000; blue.blue=65535;
    drawer->SetColor(message->drawer,blue); drawer->SetLineWidth(message->drawer,1.5);
    auto point=[&](table::Point p) {
        AIRealPoint artwork=table.transform().world(p);
        AIPoint screen{}; checked(views->ArtworkPointToViewPoint(message->view,&artwork,&screen)); return screen;
    };
    if(state.interaction.preview()) {
        for(const auto& edge : table::Geometry::visibleEdges(*state.interaction.preview()))
            checked(drawer->DrawLine(message->drawer,point(edge.from),point(edge.to)));
    } else if(state.interaction.selection()) {
        auto range=*state.interaction.selection();
        auto first=table::Geometry::cellBounds(table.model,table.model.cellAt(range.firstRow,range.firstColumn).id);
        auto last=table::Geometry::cellBounds(table.model,table.model.cellAt(range.lastRow,range.lastColumn).id);
        auto a=point({first.left,first.top}),b=point({last.right,last.bottom});
        AIRect rect{a.h,a.v,b.h,b.v}; checked(drawer->DrawRect(message->drawer,rect,false));
    }
    if(!state.interaction.dragging()) {
        AIRGBColor red;red.red=65535;red.green=9000;red.blue=9000;drawer->SetColor(message->drawer,red);
        for(const auto& cell:table.cells) if(adapter.overflows(cell)) {
            auto bounds=table::Geometry::cellBounds(table.model,cell.id);auto p=point({bounds.right,bounds.bottom});
            AIRect marker{static_cast<short>(p.h-7),static_cast<short>(p.v-7),p.h,p.v};
            checked(drawer->DrawRect(message->drawer,marker,false));
            AIPoint a{static_cast<short>(p.h-6),static_cast<short>(p.v-4)},b{static_cast<short>(p.h-1),static_cast<short>(p.v-4)};
            checked(drawer->DrawLine(message->drawer,a,b));
            a={static_cast<short>(p.h-4),static_cast<short>(p.v-6)};b={static_cast<short>(p.h-4),static_cast<short>(p.v-1)};
            checked(drawer->DrawLine(message->drawer,a,b));
        }
    }
    state.editor.draw(suites,message);
}
}
extern "C" ASAPI ASErr PluginMain(char* caller,char* selector,void* message) {
    if(!caller||!selector||!message) return kBadParameterErr;
    auto* data=static_cast<SPMessageData*>(message); if(!data->basic) return kBadParameterErr;
    try {
        Suites suites(data->basic);
        sSPBlocks=suites.get<SPBlocksSuite>(kSPBlocksSuite,kSPBlocksSuiteVersion);
        sAIUnicodeString=suites.get<AIUnicodeStringSuite>(kAIUnicodeStringSuite,kAIUnicodeStringVersion);
        if(is(caller,kSPInterfaceCaller)) {
            if(is(selector,kSPInterfaceStartupSelector)) {
                auto* state=new State; data->globals=state;
                try { startup(data,suites,*state); } catch(...) {
                    state->editor.shutdown();
                    state->sidebar.destroy(suites);
                    if(state->access) suites.get<SPAccessSuite>(kSPAccessSuite,kSPAccessSuiteVersion)->ReleasePlugin(state->access);
                    delete state;data->globals=nullptr;throw;
                }
            } else if(is(selector,kSPInterfaceShutdownSelector)) {auto* state=static_cast<State*>(data->globals);if(state)state->editor.shutdown();delete state;data->globals=nullptr; }
            return kNoErr;
        }
        auto* state=static_cast<State*>(data->globals); if(!state) return kNoErr;
#ifdef TABLE_HOST_ACCEPTANCE
        if(is(caller,kCallerAITimer)&&is(selector,kSelectorAIGoTimer)&&static_cast<AITimerMessage*>(message)->timer==state->acceptanceTimer) {
            checked(suites.get<AITimerSuite>(kAITimerSuite,kAITimerVersion)->SetTimerActive(state->acceptanceTimer,false));
            bool quit=false;
            {MutationScope operation(*state);quit=runAutoHostAcceptance(suites);}
            if(quit) return suites.get<AIApplicationSuite>(kAIApplicationSuite,kAIApplicationVersion)->Quit();
            return kNoErr;
        }
#endif
        if(is(caller,kCallerAINotify)&&is(selector,kSelectorAINotify)) {
            auto notifier=static_cast<AINotifierMessage*>(message)->notifier;
            if(notifier==state->started) {
                showSidebar(*state,suites);
#ifdef TABLE_HOST_ACCEPTANCE
                if(hasAutoHostAcceptanceRequest()) checked(suites.get<AITimerSuite>(kAITimerSuite,kAITimerVersion)->SetTimerActive(state->acceptanceTimer,true));
#endif
            }
            else if((notifier==state->documentSwitch||notifier==state->documentClose)&&!state->mutating) {
                if(state->editor.editing())traceStage(notifier==state->documentSwitch?"edit.exit.document-switch":"edit.exit.document-close");
                state->editor.end(false);++state->generation;
                state->interaction.cancel();state->interaction.clearSelection();state->active.reset();
                state->tables.clear();state->placing=false;state->pending.reset();state->lastCell=0;
                state->sidebar.update(false,L"文档已切换或关闭，请重新选择表格。");
            } else if(notifier==state->artChanged&&!state->mutating) {
                if(state->editor.editing())traceStage("edit.exit.art-changed");
                state->editor.end(false);++state->generation;
                // Keep valid cached tables through native text edits; clear deleted/foreign handles.
                if(state->active&&*state->active<state->tables.size()) {
                    DocumentAdapter adapter(suites);
                    if(!adapter.valid(state->tables[*state->active])) {
                        state->interaction.cancel();state->interaction.clearSelection();state->active.reset();state->tables.clear();
                        state->sidebar.update(false,L"所选表格已删除或不可编辑，请重新选择。");
                    }
                }
            }
            else if(notifier==state->viewChanged&&!state->mutating) {state->editor.refreshView();invalidate(suites);}
            else if(notifier==state->shutdown) {
#ifdef TABLE_HOST_ACCEPTANCE
                checked(suites.get<AITimerSuite>(kAITimerSuite,kAITimerVersion)->SetTimerActive(state->acceptanceTimer,false));
#endif
                state->editor.shutdown();state->sidebar.destroy(suites);
                if(state->access) {checked(suites.get<SPAccessSuite>(kSPAccessSuite,kSPAccessSuiteVersion)->ReleasePlugin(state->access));state->access=nullptr;}
            }
            return kNoErr;
        }
        if(is(caller,kCallerAIMenu)&&is(selector,kSelectorAIGoMenuItem)) {
            MutationScope operation(*state);
            auto item=static_cast<AIMenuMessage*>(message)->menuItem;
            if(item!=state->showSidebar&&item!=state->about){if(!state->editor.beforeStructure())return kNoErr;state->editor.end(false);}
#ifdef TABLE_HOST_ACCEPTANCE
            if(item==state->acceptance) {
                state->active.reset();state->interaction.clearSelection();state->tables.clear();
                try {runHostAcceptance(suites);}
                catch(const std::exception& error) {traceStage(error.what());MessageBoxA(nullptr,error.what(),"Host acceptance failed",MB_OK|MB_ICONERROR);}
                catch(ASErr error) {traceStage("acceptance.SDK.error");MessageBoxW(nullptr,L"原生验收遇到 SDK 错误，请查看阶段日志。",L"开发验收",MB_OK|MB_ICONERROR);return error;}
                return kNoErr;
            }
#endif
            if(item==state->showSidebar) {showSidebar(*state,suites);updateSidebar(*state,suites);return kNoErr;}
            if(item==state->about) MessageBoxW(nullptr,L"AI 画布表格工具 · 开发版\n单击选格，Shift 多选，拖动边线，双击编辑文字。\n通过“窗口 → 画布表格”打开侧边栏。",L"AI 画布表格工具",MB_OK|MB_ICONINFORMATION);
            else if(item==state->create||item==state->edit) {
                AIDocumentHandle document=nullptr; checked(suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion)->GetDocument(&document));
                if(!document) {MessageBoxW(nullptr,L"请先新建或打开 Illustrator 文档。",L"画布表格",MB_OK);return kNoErr;}
                std::optional<table::TableModel> pending;
                if(item==state->create) {pending=showCreateTableDialog();if(!pending) return kNoErr;}
                DocumentAdapter adapter(suites);
                state->tables=adapter.discoverTables();state->active.reset();state->interaction.clearSelection();
                checked(suites.get<AIToolSuite>(kAIToolSuite,kAIToolVersion)->SetSelectedTool(state->tool));
                state->pending=std::move(pending);state->placing=state->pending.has_value();
            } else {
                auto command=std::find(state->commands.begin(),state->commands.end(),item);
                if(command==state->commands.end()) return kNoErr;
                if(!state->active || *state->active>=state->tables.size() || !state->interaction.selection()) {
                    MessageBoxW(nullptr,L"请先使用表格工具选择单元格。按住 Shift 可选择矩形区域。",L"选择表格区域",MB_OK);return kNoErr;
                }
                traceStage("command.synchronize.start");
                DocumentAdapter adapter(suites);auto& table=state->tables[*state->active];adapter.synchronize(table);
                traceStage("command.synchronize.done");
                auto selection=*state->interaction.selection();auto model=table.model;
                const auto index=static_cast<int>(command-state->commands.begin());
                if(index==9) {
                    auto initial=model.cellAt(selection.firstRow,selection.firstColumn).style;
                    auto fonts=adapter.installedFonts();auto style=showCellStyleDialog(initial,fonts);if(!style) return kNoErr;
                    checked(suites.get<AIToolSuite>(kAIToolSuite,kAIToolVersion)->SetSelectedTool(state->tool));
                    adapter.applyStyle(table,selection,*style);invalidate(suites);updateSidebar(*state,suites);return kNoErr;
                }
                if(index<8) model=table::applyCommand(model,selection,static_cast<table::Command>(index));
                if(index==10)model=table::applyCommand(model,selection,table::Command::DistributeRows);
                if(index==11)model=table::applyCommand(model,selection,table::Command::DistributeColumns);
                traceStage("command.model.done");
                checked(suites.get<AIToolSuite>(kAIToolSuite,kAIToolVersion)->SetSelectedTool(state->tool));
                traceStage("command.tool.selected");
                if(index==8) {adapter.detach(table);state->active.reset();}
                else {
                    std::optional<TrackDeletion> deletion;
                    if(index==6) deletion=TrackDeletion{false,selection.firstRow,selection.lastRow-selection.firstRow+1};
                    if(index==7) deletion=TrackDeletion{true,selection.firstColumn,selection.lastColumn-selection.firstColumn+1};
                    adapter.applyStructure(table,model,deletion);
                }
                traceStage("command.artwork.done");
                if(index<10)state->interaction.clearSelection();invalidate(suites);
            }
        } else if(is(caller,kCallerAITool)) {MutationScope operation(*state);toolEvent(*state,suites,selector,static_cast<AIToolMessage*>(message));}
        else if(is(caller,kCallerAIAnnotation)&&is(selector,kSelectorAIDrawAnnotation)) draw(*state,suites,static_cast<AIAnnotatorMessage*>(message));
        if(!is(caller,kCallerAIAnnotation)) updateSidebar(*state,suites);
        return kNoErr;
    } catch(const table::ValidationError& error) {
        traceStage(error.what());
        if(is(caller,kCallerAIMenu)) MessageBoxW(nullptr,L"无法执行此操作。请检查选区是否完整、是否会删除整张表，以及表格是否旋转、锁定或缺少对象。",L"检查表格和选区",MB_OK|MB_ICONINFORMATION);
        return kBadParameterErr;
    }
    catch(ASErr error) { return error; }
    catch(const std::exception&) {return kCantHappenErr;}
    catch(...) {return kCantHappenErr;}
}
