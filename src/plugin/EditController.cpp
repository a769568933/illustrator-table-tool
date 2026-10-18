#include "EditController.hpp"
#include "HostCommand.hpp"
#include "Trace.hpp"
#include "AIContext.h"
#include "AIDocumentView.h"
#include "AIAnnotatorDrawer.h"
#include "AIActionManager.h"
#include "AIDocumentAction.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const NativeCell& nativeCell(const NativeTable& native,table::CellId id){
    const auto found=std::find_if(native.cells.begin(),native.cells.end(),[&](const NativeCell& cell){return cell.id==id;});
    if(found==native.cells.end()||!found->layout)throw table::ValidationError("Missing cell layout");return *found;
}
table::EditBinding identity(const NativeTable& native,std::uint64_t generation){return {reinterpret_cast<std::uintptr_t>(native.document),reinterpret_cast<std::uintptr_t>(native.group),generation};}
std::uint32_t nearestStop(const table::TextLayout& layout,std::uint32_t position){const auto& stops=layout.boundaries();auto found=std::lower_bound(stops.begin(),stops.end(),position);return found==stops.end()?stops.back():*found;}
}
void EditController::configure(SPBasicSuite* value,SPPluginRef self,EditorHost callbacks){basic=value;plugin=self;host=std::move(callbacks);}
NativeTable* EditController::current(Suites& suites){
    if(!session)return nullptr;auto native=host.active();
    if(!native||!session->matches(identity(*native,host.generation()))||!DocumentAdapter(suites).matchesPublishedArtwork(*native))throw table::ValidationError("Text session is stale");
    native->model.cellById(session->selection().cell);return native;
}
void EditController::begin(NativeTable& native,table::CellId id,table::Point point){
    traceStage("edit.begin");
    if(session&&!beforeStructure())return;
    invokeHostCommand(basic,plugin,[&](Suites& suites){
        DocumentAdapter adapter(suites);if(!adapter.valid(native))throw table::ValidationError("Table cannot be edited");
        AIWindowRef application=nullptr;checked(suites.get<AIAppContextSuite>(kAIAppContextSuite,kAIAppContextVersion)->GetPlatformAppWindow(&application));owner=application;
        if(!input.handle())input.create(owner,[this](const InputEvent& event){handle(event);});
        traceStage("edit.window.ready");
        const auto bounds=table::Geometry::cellBounds(native.model,id);const auto& cell=nativeCell(native,id);
        const auto caret=cell.layout->hitTest({point.x-bounds.left,point.y-bounds.top});
        session.emplace(identity(native,host.generation()),table::EditSession{id,caret,caret,0});
        preediting=false;preedit.clear();batchOpen=false;inputFailure=false;caretVisible=true;
        AIRealPoint viewPoint{};const auto artwork=native.transform().world(point);
        checked(suites.get<AIDocumentViewSuite>(kAIDocumentViewSuite,kAIDocumentViewVersion)->FixedArtworkPointToViewPoint(nullptr,&artwork,&viewPoint));
        POINT cursor{};if(!GetCursorPos(&cursor))throw table::ValidationError("Cannot locate editor caret");if(owner)ScreenToClient(owner,&cursor);
        viewToOwner={cursor.x-viewPoint.h,cursor.y-viewPoint.v};
        host.selectCell(id);rebuild(suites);traceStage(overlay.rgb.empty()?"edit.preview.empty":"edit.preview.ready");input.focus();traceStage("edit.focus.queued");host.invalidate();
    });
}
bool EditController::pointer(NativeTable& native,table::CellId id,table::Point point,bool extend){
    if(!session||session->selection().cell!=id)return false;
    if(!beforeStructure())return true;
    invokeHostCommand(basic,plugin,[&](Suites& suites){
        current(suites);const auto bounds=table::Geometry::cellBounds(native.model,id);auto selection=session->selection();
        selection.caret=nativeCell(native,id).layout->hitTest({point.x-bounds.left,point.y-bounds.top});if(!extend)selection.anchor=selection.caret;
        session->select(selection);batchOpen=false;caretVisible=true;rebuild(suites);input.focus();host.invalidate();
    });return true;
}
bool EditController::beforeStructure(){
    if(!session)return true;
    if(inputFailure){host.status(L"上次输入未能写入。请保留原文，按 Esc 退出后重新选择单元格。");return false;}
    if(!input.requestCompositionCommit()){host.status(L"请先在输入法中确认当前文字，或按 Esc 取消组字，再进行表格操作。");return false;}
    return !inputFailure;
}
void EditController::end(bool commitComposition){
    if(session)traceStage("edit.session.end");
    if(commitComposition&&!beforeStructure())return;
    session.reset();preediting=false;preedit.clear();batchOpen=false;inputFailure=false;overlay={};input.cancelComposition();input.blur();if(host.invalidate)host.invalidate();
}
void EditController::apply(Suites& suites,const table::EditChange& change,bool plainInput){
    auto native=current(suites);if(!native)return;
    auto undo=suites.get<AIUndoSuite>(kAIUndoSuite,kAIUndoVersion);
    const auto tag=ai::UnicodeString::FromUTF8("IllustratorTableTool.Typing.v3");ai::int32 previousToken=0;ai::UnicodeString previousTag;
    const bool standalone=hostCommandOwnsUndoContext();bool ownTag=false;
    if(standalone&&batchOpen&&undo->GetNthTransactionTagUS(-1,previousTag,&previousToken)==kNoErr)ownTag=previousTag==tag;
    const bool append=table::mayAppendTyping(plainInput,batchOpen,standalone,batchToken,previousToken,ownTag);
    if(change.changed&&standalone){
        checked(undo->SetKind(append?kAIAppendUndoContext:kAIStandardUndoContext));
        if(!append){if(batchToken==std::numeric_limits<std::int32_t>::max())throw table::ValidationError("Input batch limit");++batchToken;}
    }
    session->commit(change,[&](const table::TableModel& model){
        host.mutation([&]{DocumentAdapter adapter(suites);if(change.addedRow)adapter.applyStructure(*native,model);else adapter.applyText(*native,model);});
    });
    session->select(table::snapEditSelection(session->selection(),nativeCell(*native,session->selection().cell).layout->boundaries()));
    if(change.changed&&standalone&&!append)checked(undo->SetTagUS(tag,batchToken));
    batchOpen=plainInput&&standalone;preediting=false;preedit.clear();caretVisible=true;host.selectCell(session->selection().cell);
}
void EditController::key(Suites& suites,const InputEvent& event){
    auto native=current(suites);if(!native)return;auto selection=session->selection();const auto& cell=native->model.cellById(selection.cell);const auto& layout=*nativeCell(*native,selection.cell).layout;
    const bool control=(event.modifiers&InputControl)!=0,shift=(event.modifiers&InputShift)!=0;
    const bool navigation=event.key==VK_LEFT||event.key==VK_RIGHT||event.key==VK_UP||event.key==VK_DOWN||event.key==VK_HOME||event.key==VK_END||event.key==VK_TAB||event.key==VK_RETURN||event.key==VK_BACK||event.key==VK_DELETE||event.key==VK_ESCAPE;
    if(!control&&!navigation)return;
    batchOpen=false;
    if(control){
        if(event.key=='A'){selection.anchor=0;selection.caret=static_cast<std::uint32_t>(cell.text.size());session->select(selection);return;}
        if(event.key=='C'||event.key=='X'){
            const auto first=std::min(selection.anchor,selection.caret),last=std::max(selection.anchor,selection.caret);
            if(first!=last){input.copyText(std::u16string_view(cell.text).substr(first,last-first));if(event.key=='X')apply(suites,table::replaceSelection(native->model,selection,u""),false);}return;
        }
        if(event.key=='V'){input.pasteText();return;}
        if(event.key=='Z'||event.key=='Y'){
            host.mutation([&]{auto undo=suites.get<AIUndoSuite>(kAIUndoSuite,kAIUndoVersion);
                checked(event.key=='Y'||shift?undo->MultiRedoTransaction(native->document,1):undo->MultiUndoTransaction(native->document,1));
                DocumentAdapter(suites).synchronize(*native);});
            auto found=std::find_if(native->model.cells().begin(),native->model.cells().end(),[&](const table::Cell& c){return c.id==selection.cell;});
            if(found==native->model.cells().end())selection.cell=native->model.cellAt(0,0).id;
            const auto& updated=*nativeCell(*native,selection.cell).layout;selection.anchor=selection.caret=nearestStop(updated,std::min(selection.caret,static_cast<std::uint32_t>(native->model.cellById(selection.cell).text.size())));
            session->select(selection);host.selectCell(selection.cell);return;
        }
        if(event.key=='S'){if(!beforeStructure())return;checked(suites.get<AIActionManagerSuite>(kAIActionManagerSuite,kAIActionManagerVersion)->PlayActionEvent(kAISaveDocumentAction,kDialogOn,nullptr));return;}
    }
    if(event.key==VK_ESCAPE){end(false);return;}
    if(event.key==VK_TAB){apply(suites,table::navigateCell(native->model,selection,shift),false);return;}
    if(event.key==VK_RETURN){apply(suites,table::replaceSelection(native->model,selection,u"\n"),false);return;}
    if(event.key==VK_BACK||event.key==VK_DELETE){apply(suites,table::eraseCluster(native->model,selection,layout.boundaries(),event.key==VK_BACK),false);return;}
    const auto& stops=layout.boundaries();auto found=std::lower_bound(stops.begin(),stops.end(),selection.caret);auto position=selection.caret;
    if(event.key==VK_LEFT){if(!shift&&selection.anchor!=selection.caret)position=std::min(selection.anchor,selection.caret);else if(found!=stops.begin())position=*(found-1);}
    else if(event.key==VK_RIGHT){if(!shift&&selection.anchor!=selection.caret)position=std::max(selection.anchor,selection.caret);else if(found+1!=stops.end())position=*(found+1);}
    else if(event.key==VK_UP||event.key==VK_DOWN){const auto caret=layout.caret(position);const auto height=caret.bottom-caret.top;
        position=layout.hitTest({caret.left,(caret.top+caret.bottom)/2+(event.key==VK_UP?-height:height)});}
    else if(event.key==VK_HOME||event.key==VK_END){
        if(control)position=event.key==VK_HOME?0:static_cast<std::uint32_t>(cell.text.size());
        else {const auto caret=layout.caret(position);double best=event.key==VK_HOME?std::numeric_limits<double>::max():-std::numeric_limits<double>::max();
            for(auto stop:stops){const auto candidate=layout.caret(stop);if(std::abs(candidate.top-caret.top)>0.01)continue;
                if((event.key==VK_HOME&&candidate.left<best)||(event.key==VK_END&&candidate.left>best)){best=candidate.left;position=stop;}}
        }
    }else return;
    selection.caret=position;if(!shift)selection.anchor=position;session->select(selection);
}
void EditController::handleWithContext(Suites& suites,const InputEvent& event){
    if(!session)return;
    if(event.kind==InputKind::Key&&event.key==VK_ESCAPE){end(false);return;}
    if(inputFailure&&event.kind!=InputKind::Error&&event.kind!=InputKind::FocusLost)return;
    current(suites);
    switch(event.kind){
    case InputKind::Insert:case InputKind::Paste:case InputKind::Commit:
        apply(suites,table::replaceSelection(host.active()->model,session->selection(),event.text),event.kind==InputKind::Insert);break;
    case InputKind::Composition:batchOpen=false;preediting=true;preedit=event.text;preeditCaret=event.compositionStart;caretVisible=true;break;
    case InputKind::Cancel:preediting=false;preedit.clear();batchOpen=false;break;
    case InputKind::FocusLost:batchOpen=false;caretVisible=false;break;
    case InputKind::Blink:if(GetFocus()!=input.handle())return;caretVisible=!caretVisible;break;
    case InputKind::Error:inputFailure=true;host.status(std::wstring(reinterpret_cast<const wchar_t*>(event.text.c_str())));return;
    case InputKind::Key:key(suites,event);caretVisible=true;break;
    }
    if(session){rebuild(suites);host.invalidate();}
}
void EditController::handle(const InputEvent& event){
    if(!session||!basic)return;
    try{host.mutation([&]{invokeHostCommand(basic,plugin,[&](Suites& suites){handleWithContext(suites,event);});});}
    catch(const std::exception& error){traceStage("edit.callback.failed");traceStage(error.what());inputFailure=true;batchOpen=false;host.status(L"操作未完成。请按 Esc 退出编辑后重新选择单元格。");}
    catch(...){traceStage("edit.callback.sdk-failed");inputFailure=true;batchOpen=false;host.status(L"操作未完成。请按 Esc 退出编辑后重新选择单元格。");}
}
void EditController::rebuild(Suites& suites){
    auto native=current(suites);if(!native)return;const auto selection=session->selection();const auto bounds=table::Geometry::cellBounds(native->model,selection.cell);
    auto layout=nativeCell(*native,selection.cell).layout;auto display=selection;
    std::optional<std::pair<std::uint32_t,std::uint32_t>> composition;
    if(preediting){auto preview=table::replaceSelection(native->model,selection,preedit);const auto& cell=preview.model.cellById(selection.cell);
        layout=table::layoutText({cell.text,cell.formatting,cell.style,bounds.right-bounds.left,bounds.bottom-bounds.top});
        const auto first=std::min(selection.anchor,selection.caret);
        const auto marked=table::snapEditSelection({selection.cell,first,first+static_cast<std::uint32_t>(preedit.size()),0},layout->boundaries());
        composition={{marked.anchor,marked.caret}};
        display.anchor=display.caret=nearestStop(*layout,first+std::min(preeditCaret,static_cast<std::uint32_t>(preedit.size())));
    }
    auto views=suites.get<AIDocumentViewSuite>(kAIDocumentViewSuite,kAIDocumentViewVersion);
    auto convert=[&](table::Point point){auto artwork=native->transform().world(point);AIRealPoint view{};checked(views->FixedArtworkPointToViewPoint(nullptr,&artwork,&view));return table::Point{view.h,view.v};};
    const auto top=convert({bounds.left,bounds.top}),bottom=convert({bounds.right,bounds.bottom});
    AIRealRect visible{};checked(views->GetDocumentViewBounds(nullptr,&visible));AIRealPoint viewFirst{},viewLast{};AIRealPoint a{visible.left,visible.top},b{visible.right,visible.bottom};
    checked(views->FixedArtworkPointToViewPoint(nullptr,&a,&viewFirst));checked(views->FixedArtworkPointToViewPoint(nullptr,&b,&viewLast));
    const double left=std::max(top.x,static_cast<double>(std::min(viewFirst.h,viewLast.h))),right=std::min(bottom.x,static_cast<double>(std::max(viewFirst.h,viewLast.h)));
    const double upper=std::max(top.y,static_cast<double>(std::min(viewFirst.v,viewLast.v))),lower=std::min(bottom.y,static_cast<double>(std::max(viewFirst.v,viewLast.v)));
    if(right<=left||lower<=upper){overlay={};return;}
    const auto x=std::floor(left),y=std::floor(upper);OverlayFrame frame;frame.layout=layout;
    frame.width=static_cast<std::uint32_t>(std::ceil(right)-x);frame.height=static_cast<std::uint32_t>(std::ceil(lower)-y);
    frame.scaleX=(bottom.x-top.x)/(bounds.right-bounds.left);frame.scaleY=(bottom.y-top.y)/(bounds.bottom-bounds.top);
    frame.offsetPixels={top.x-x,top.y-y};frame.background=native->model.cellById(selection.cell).style.fill;frame.backgroundEnabled=native->model.cellById(selection.cell).style.fillEnabled;frame.anchor=display.anchor;frame.caret=display.caret;frame.composition=composition;frame.showCaret=caretVisible;
    overlay=renderEditorOverlay(frame);overlayOrigin={static_cast<short>(x),static_cast<short>(y)};
    const auto caret=layout->caret(display.caret);POINT candidate{static_cast<LONG>(std::lround(top.x+caret.left*frame.scaleX+viewToOwner.x)),static_cast<LONG>(std::lround(top.y+caret.bottom*frame.scaleY+viewToOwner.y))};
    if(owner)ClientToScreen(owner,&candidate);input.setCaretScreen(candidate);
    if(!layout->fontSubstitutions().empty())host.status(L"当前字体不可用，已使用系统字体显示。可在单元格样式中选择已安装字体。");
}
void EditController::refreshView(){
    if(!session||refreshing)return;refreshing=true;
    try{invokeHostCommand(basic,plugin,[&](Suites& suites){rebuild(suites);});}catch(...){overlay={};batchOpen=false;}
    refreshing=false;
}
void EditController::draw(Suites& suites,AIAnnotatorMessage* message){
    if(!session||overlay.rgb.empty()||!message->drawer)return;
    auto drawer=suites.get<AIAnnotatorDrawerSuite>(kAIAnnotatorDrawerSuite,kAIAnnotatorDrawerVersion);
    if(!overlay.png.empty())checked(drawer->DrawPNGImageUnscaled(message->drawer,overlay.png.data(),static_cast<ai::uint32>(overlay.png.size()),overlayOrigin));
    else checked(drawer->DrawImage(message->drawer,overlay.rgb.data(),overlay.width,overlay.height,kRGBColorSpace,true,overlayOrigin,false));
}
void EditController::shutdown(){session.reset();host={};input.destroy();overlay={};basic=nullptr;plugin=nullptr;}
