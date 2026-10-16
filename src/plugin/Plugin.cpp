#include <windows.h>
#include "Suites.hpp"
#include "DocumentAdapter.hpp"
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
#include <cstring>
#include <exception>
#include <cmath>

extern "C" { AIUnicodeStringSuite* sAIUnicodeString=nullptr; SPBlocksSuite* sSPBlocks=nullptr; }
namespace {
bool is(const char* value,const char* expected){ return std::strcmp(value,expected)==0; }
struct State {
    AIMenuItemHandle about=nullptr, create=nullptr, edit=nullptr;
    AIToolHandle tool=nullptr;
    AIAnnotatorHandle annotator=nullptr;
    bool placing=false, selectedTool=false;
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
    auto add=[&](const char* key,const char* title,AIMenuItemHandle* handle) {
        AIPlatformAddMenuItemDataUS item; item.groupName=kObjectUtilsMenuGroup;
        item.itemText=ai::UnicodeString::FromUTF8(title);
        checked(menus->AddMenuItem(data->self,key,&item,0,handle));
    };
    add("IllustratorTableTool.About","表格工具：关于…",&state.about);
    add("IllustratorTableTool.Create","表格工具：新建 2×2 测试表格…",&state.create);
    add("IllustratorTableTool.Edit","表格工具：选择和调整单元格",&state.edit);
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
table::Point local(const NativeTable& t,AIRealPoint p){ return {p.x-t.origin.x,t.origin.y-p.y}; }
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
        if(state.placing) {
            auto model=table::TableModel::create(2,2,table::mm(80),table::mm(32));
            for(const auto& cell : model.cells()) model.setText(cell.id,u"双击编辑文字");
            state.tables.push_back(adapter.createTable(model,message->cursor));
            state.active=state.tables.size()-1; state.interaction.clearSelection(); state.placing=false;
            invalidate(suites); return;
        }
        auto oldActive=state.active;
        state.active.reset();
        for(std::size_t i=state.tables.size();i>0;--i) {
            const auto& table=state.tables[i-1];
            if(!adapter.valid(table)) continue;
            if(table::Geometry::hitTest(table.model,local(table,message->cursor),zoom,3).kind!=table::HitKind::Outside) { state.active=i-1; break; }
        }
        if(!state.active) { state.interaction.cancel(); state.interaction.clearSelection(); invalidate(suites); return; }
        if(oldActive!=state.active) { state.interaction.clearSelection(); state.lastCell=0; }
        auto& table=state.tables[*state.active];
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
        LPCWSTR cursor=state.placing ? IDC_CROSS : IDC_ARROW;
        if(state.active && *state.active<state.tables.size() && adapter.valid(state.tables[*state.active])) {
            const auto& table=state.tables[*state.active];
            auto hit=table::Geometry::hitTest(table.model,local(table,message->cursor),zoom,3);
            if(hit.edge && hit.edge->track>0) cursor=hit.edge->vertical ? IDC_SIZEWE : IDC_SIZENS;
        }
        SetCursor(LoadCursorW(nullptr,cursor));
    }
    (void)annotations;
}
void draw(State& state,Suites& suites,AIAnnotatorMessage* message) {
    if(!state.selectedTool || !state.active || *state.active>=state.tables.size() || !message->drawer) return;
    auto& table=state.tables[*state.active];
    DocumentAdapter adapter(suites); if(!adapter.valid(table)) return;
    auto views=suites.get<AIDocumentViewSuite>(kAIDocumentViewSuite,kAIDocumentViewVersion);
    auto drawer=suites.get<AIAnnotatorDrawerSuite>(kAIAnnotatorDrawerSuite,kAIAnnotatorDrawerVersion);
    AIRGBColor blue; blue.red=9000; blue.green=32000; blue.blue=65535;
    drawer->SetColor(message->drawer,blue); drawer->SetLineWidth(message->drawer,1.5);
    auto point=[&](table::Point p) {
        AIRealPoint artwork{static_cast<AIReal>(table.origin.x+p.x),static_cast<AIReal>(table.origin.y-p.y)};
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
            if(item==state->about) MessageBoxW(nullptr,L"AI 画布表格工具 · 交互验证版\n2×2 表格、选择、拖线和原生文字接入测试。\n保存重开、合并拆分及样式仍在开发。",L"AI 画布表格工具",MB_OK|MB_ICONINFORMATION);
            else if(item==state->create||item==state->edit) {
                AIDocumentHandle document=nullptr; checked(suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion)->GetDocument(&document));
                if(!document) {MessageBoxW(nullptr,L"请先新建或打开 Illustrator 文档。",L"画布表格",MB_OK);return kNoErr;}
                checked(suites.get<AIToolSuite>(kAIToolSuite,kAIToolVersion)->SetSelectedTool(state->tool));
                state->placing=item==state->create;
            }
        } else if(is(caller,kCallerAITool)) toolEvent(*state,suites,selector,static_cast<AIToolMessage*>(message));
        else if(is(caller,kCallerAIAnnotation)&&is(selector,kSelectorAIDrawAnnotation)) draw(*state,suites,static_cast<AIAnnotatorMessage*>(message));
        return kNoErr;
    } catch(ASErr error) { return error; }
    catch(const std::exception&) {return kCantHappenErr;}
    catch(...) {return kCantHappenErr;}
}
