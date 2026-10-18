#pragma once
#include "DocumentAdapter.hpp"
#include "InputWindow.hpp"
#include "EditorOverlay.hpp"
#include "table/EditSessionPolicy.hpp"
#include "AIAnnotator.h"

struct EditorHost {
    std::function<NativeTable*()> active;
    std::function<std::uint64_t()> generation;
    std::function<void()> invalidate;
    std::function<void(const std::wstring&)> status;
    std::function<void(const std::function<void()>&)> mutation;
    std::function<void(table::CellId)> selectCell;
};
class EditController {
    SPBasicSuite* basic=nullptr;SPPluginRef plugin=nullptr;HWND owner=nullptr;
    EditorHost host;InputWindow input;
    std::optional<table::BoundEditSession> session;
    std::u16string preedit;std::uint32_t preeditCaret=0;
    bool preediting=false,batchOpen=false,caretVisible=true,inputFailure=false,refreshing=false;
    std::int32_t batchToken=0;
    table::Point viewToOwner{};
    OverlayBitmap overlay;AIPoint overlayOrigin{};
    NativeTable* current(Suites& suites);
    void apply(Suites& suites,const table::EditChange& change,bool plainInput);
    void key(Suites& suites,const InputEvent& event);
    void rebuild(Suites& suites);
    void handleWithContext(Suites& suites,const InputEvent& event);
public:
    void configure(SPBasicSuite*,SPPluginRef,EditorHost);
    bool editing()const{return session.has_value();}
    void begin(NativeTable& table,table::CellId id,table::Point point);
    bool pointer(NativeTable& table,table::CellId id,table::Point point,bool extend);
    void handle(const InputEvent& event);
    bool beforeStructure();
    void end(bool commitComposition);
    void refreshView();
    void draw(Suites& suites,AIAnnotatorMessage* message);
    void shutdown();
};
