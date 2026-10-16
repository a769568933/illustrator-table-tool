#include <windows.h>
#include "Suites.hpp"
#include "DocumentAdapter.hpp"
#include "Dialogs.hpp"
#include "SPInterf.h"
#include "AIPlugin.h"
#include "AIMenu.h"
#include "AIMenuGroups.h"
#include "AITool.h"
#include "AIToolNames.h"
#include "AIEvent.h"
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
    std::array<AIMenuItemHandle,10> commands{};
    AIToolHandle tool=nullptr;
    AIAnnotatorHandle annotator=nullptr;
    bool placing=false, selectedTool=false;
    std::optional<table::TableModel> pending;
    std::vector<NativeTable> tables;
    std::optional<std::size_t> active;
    table::Interaction interaction;
    ULONGLONG lastDown=0;
    table::CellId lastCell=0;
    table::Point lastPoint;
};
void invalidate(Suites& suites) {
    // Invalidate only on interaction events; never during the draw callback.
    AIRect screen{-32000,-32000,32000,32000};
    checked(suites.get<AIAnnotatorSuite>(kAIAnnotatorSuite,kAIAnnotatorVersion)->InvalAnnotationRect(nullptr,&screen));
}
void startup(SPMessageData* data,Suites& suites,State& state) {
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
    const char* names[]={"合并单元格","拆分单元格","在上方插入行","在下方插入行","在左侧插入列","在右侧插入列","删除所选行","删除所选列","转换为普通对象","单元格样式…"};
    for(std::size_t i=0;i<state.commands.size();++i) {
        auto key="IllustratorTableTool.Command"+std::to_string(i);add(key.c_str(),names[i],&state.commands[i]);
    }
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
table::Point local(const NativeTable& t,AIRealPoint p){ return {p.h-t.origin.h,t.origin.v-p.v}; }
void toolEvent(State& state,Suites& suites,const char* selector,AIToolMessage* message) {
    if(message->tool!=state.tool) return;
    auto annotations=suites.get<AIAnnotatorSuite>(kAIAnnotatorSuite,kAIAnnotatorVersion);
    if(is(selector,kSelectorAISelectTool)) { state.selectedTool=true; invalidate(suites); return; }
    if(is(selector,kSelectorAIDeselectTool)) {
        state.selectedTool=false; state.interaction.cancel(); state.placing=false; invalidate(suites); return;
    }
    DocumentAdapter adapter(suites);
    auto view=suites.get<AIDocumentViewSuite>(kAIDocumentViewSuite,kAIDocumentViewVersion);
    AIReal zoom=1; checked(view->GetDocumentViewZoom(nullptr,&zoom));
    if((GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0) {
        state.interaction.cancel(); state.placing=false; invalidate(suites); return;
    }
    if(is(selector,kSelectorAIToolMouseDown)) {
        if(state.placing&&state.pending) {
            auto model=*state.pending;
            state.tables.push_back(adapter.createTable(model,message->cursor));
            state.active=state.tables.size()-1; state.interaction.clearSelection(); state.placing=false;state.pending.reset();
            invalidate(suites); return;
        }
        auto oldGroup=state.active&&*state.active<state.tables.size() ? state.tables[*state.active].group : nullptr;
        state.tables=adapter.discoverTables();
        state.active.reset();
        for(std::size_t i=state.tables.size();i>0;--i) {
            const auto& table=state.tables[i-1];
            if(!adapter.valid(table)) continue;
            if(table::Geometry::hitTest(table.model,local(table,message->cursor),zoom,3).kind!=table::HitKind::Outside) { state.active=i-1; break; }
        }
        if(!state.active) { state.interaction.cancel(); state.interaction.clearSelection(); invalidate(suites); return; }
        auto& table=state.tables[*state.active];
        if(oldGroup!=table.group) {state.interaction.clearSelection();state.lastCell=0;}
        auto point=local(table,message->cursor);
        auto hit=table::Geometry::hitTest(table.model,point,zoom,3);
        auto now=GetTickCount64();
        if(hit.kind==table::HitKind::Cell && hit.cell==state.lastCell && now-state.lastDown<=GetDoubleClickTime() &&
            std::hypot(point.x-state.lastPoint.x,point.y-state.lastPoint.y)*zoom<=4) {
            state.lastCell=0; state.interaction.cancel();
            checked(suites.get<AIToolSuite>(kAIToolSuite,kAIToolVersion)->SetSelectedToolByName(kTypeTool));
            adapter.beginEdit(table,hit.cell); return;
        }
        state.lastDown=now; state.lastCell=hit.cell; state.lastPoint=point;
        bool shift=message->event && (message->event->modifiers&aiEventModifiers_shiftKey)!=0;
        state.interaction.pointerDown(table.model,point,shift,zoom);
        invalidate(suites);
    } else if(is(selector,kSelectorAIToolMouseDrag) || is(selector,kSelectorAIToolMouseUp)) {
        if(!state.active || *state.active>=state.tables.size() || !adapter.valid(state.tables[*state.active])) {
            state.interaction.cancel(); state.active.reset(); invalidate(suites); return;
        }
        auto& table=state.tables[*state.active];
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
            auto hit=table::Geometry::hitTest(table.model,local(table,message->cursor),zoom,3);
            if(hit.edge && hit.edge->track>0) cursor=hit.edge->vertical ? IDC_SIZEWE : IDC_SIZENS;
        }
        SetCursor(LoadCursor(nullptr,cursor));
    }
    (void)annotations;
}
void draw(State& state,Suites& suites,AIAnnotatorMessage* message) {
    if(!state.selectedTool || !state.active || *state.active>=state.tables.size() || !message->drawer) return;
    auto& table=state.tables[*state.active];
    DocumentAdapter adapter(suites); if(!adapter.valid(table)) return;
    if(!state.interaction.dragging()) adapter.synchronize(table);
    auto views=suites.get<AIDocumentViewSuite>(kAIDocumentViewSuite,kAIDocumentViewVersion);
    auto drawer=suites.get<AIAnnotatorDrawerSuite>(kAIAnnotatorDrawerSuite,kAIAnnotatorDrawerVersion);
    AIRGBColor blue; blue.red=9000; blue.green=32000; blue.blue=65535;
    drawer->SetColor(message->drawer,blue); drawer->SetLineWidth(message->drawer,1.5);
    auto point=[&](table::Point p) {
        AIRealPoint artwork{static_cast<AIReal>(table.origin.h+p.x),static_cast<AIReal>(table.origin.v-p.y)};
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
                try { startup(data,suites,*state); } catch(...) {delete state;data->globals=nullptr;throw;}
            } else if(is(selector,kSPInterfaceShutdownSelector)) { delete static_cast<State*>(data->globals);data->globals=nullptr; }
            return kNoErr;
        }
        auto* state=static_cast<State*>(data->globals); if(!state) return kNoErr;
        if(is(caller,kCallerAIMenu)&&is(selector,kSelectorAIGoMenuItem)) {
            auto item=static_cast<AIMenuMessage*>(message)->menuItem;
            if(item==state->about) MessageBoxW(nullptr,L"AI 画布表格工具 · 开发版\n单击选格，Shift 多选，拖动边线，双击编辑文字。\n通过“对象 → 画布表格”调整结构。",L"AI 画布表格工具",MB_OK|MB_ICONINFORMATION);
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
                DocumentAdapter adapter(suites);auto& table=state->tables[*state->active];adapter.synchronize(table);
                auto selection=*state->interaction.selection();auto model=table.model;
                const auto index=static_cast<int>(command-state->commands.begin());
                if(index==9) {
                    auto initial=model.cellAt(selection.firstRow,selection.firstColumn).style;
                    auto fonts=adapter.installedFonts();auto style=showCellStyleDialog(initial,fonts);if(!style) return kNoErr;
                    checked(suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion)->LoseTextFocus());
                    checked(suites.get<AIToolSuite>(kAIToolSuite,kAIToolVersion)->SetSelectedTool(state->tool));
                    adapter.applyStyle(table,selection,*style);invalidate(suites);return kNoErr;
                }
                if(index<8) model=table::applyCommand(model,selection,static_cast<table::Command>(index));
                checked(suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion)->LoseTextFocus());
                checked(suites.get<AIToolSuite>(kAIToolSuite,kAIToolVersion)->SetSelectedTool(state->tool));
                if(index==8) {adapter.detach(table);state->active.reset();}
                else adapter.applyStructure(table,model);
                state->interaction.clearSelection();invalidate(suites);
            }
        } else if(is(caller,kCallerAITool)) toolEvent(*state,suites,selector,static_cast<AIToolMessage*>(message));
        else if(is(caller,kCallerAIAnnotation)&&is(selector,kSelectorAIDrawAnnotation)) draw(*state,suites,static_cast<AIAnnotatorMessage*>(message));
        return kNoErr;
    } catch(const table::ValidationError&) {
        if(is(caller,kCallerAIMenu)) MessageBoxW(nullptr,L"无法执行此操作。请检查选区是否完整、是否会删除整张表，以及表格是否旋转、锁定或缺少对象。",L"检查表格和选区",MB_OK|MB_ICONINFORMATION);
        return kBadParameterErr;
    }
    catch(ASErr error) { return error; }
    catch(const std::exception&) {return kCantHappenErr;}
    catch(...) {return kCantHappenErr;}
}
