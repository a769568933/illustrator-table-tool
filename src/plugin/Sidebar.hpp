#pragma once
#include <windows.h>
#include "Suites.hpp"
#include "AIPanel.h"
#include "AIMenu.h"
#include <array>
#include <string>

class Sidebar {
    SPBasicSuite* basic=nullptr;
    SPPluginRef plugin=nullptr;
    AIPanelRef panel=nullptr;
    HWND window=nullptr,status=nullptr;
    WNDPROC original=nullptr;
    HFONT font=nullptr;
    std::array<AIMenuItemHandle,12> commands{};
    std::array<HWND,12> buttons{};
    bool busy=false,selectionEnabled=false;
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    void invoke(int command);
    void layout();
public:
    void create(SPBasicSuite*,SPPluginRef,Suites&,const std::array<AIMenuItemHandle,12>&);
    void show(Suites&);
    void destroy(Suites&);
    void update(bool selected,const std::wstring& message);
};
