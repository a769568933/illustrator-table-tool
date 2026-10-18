#pragma once
#include <windows.h>
#include "Suites.hpp"
#include "AIPanel.h"
#include "AIMenu.h"
#include <array>
#include <string>
#include <functional>
#include "table/TableModel.hpp"

class Sidebar {
    SPBasicSuite* basic=nullptr;
    SPPluginRef plugin=nullptr;
    AIPanelRef panel=nullptr;
    HWND window=nullptr,status=nullptr;
    WNDPROC original=nullptr;
    HFONT font=nullptr;
    std::array<AIMenuItemHandle,14> commands{};
    std::array<HWND,14> buttons{};
    HWND fontChoice=nullptr,sizeField=nullptr,sizeApply=nullptr,transparent=nullptr;
    std::array<HWND,6> alignButtons{};
    std::array<HWND,3> styleLabels{};
    std::vector<std::u16string> families;
    std::function<void(const table::CellStylePatch&)> styleHandler;
    bool busy=false,selectionEnabled=false;
    std::function<bool()> beforeInvoke;
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    void invoke(int command);
    void layout();
    void enableControls();
    void invokeStyle(const table::CellStylePatch&);
public:
    void create(SPBasicSuite*,SPPluginRef,Suites&,const std::array<AIMenuItemHandle,14>&);
    void show(Suites&);
    void destroy(Suites&);
    void update(bool selected,const std::wstring& message);
    void updateStyle(const table::TableModel&,table::CellRange);
    void setStyleHandler(std::function<void(const table::CellStylePatch&)> callback){styleHandler=std::move(callback);}
    void setBeforeInvoke(std::function<bool()> callback){beforeInvoke=std::move(callback);}
};
