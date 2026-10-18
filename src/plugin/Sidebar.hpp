#pragma once
#include <windows.h>
#include "Suites.hpp"
#include "AIPanel.h"
#include "AIMenu.h"
#include <array>
#include <string>
#include <functional>

class Sidebar {
    SPBasicSuite* basic=nullptr;
    SPPluginRef plugin=nullptr;
    AIPanelRef panel=nullptr;
    HWND window=nullptr,status=nullptr;
    WNDPROC original=nullptr;
    HFONT font=nullptr;
    std::array<AIMenuItemHandle,14> commands{};
    std::array<HWND,14> buttons{};
    bool busy=false,selectionEnabled=false;
    std::function<bool()> beforeInvoke;
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    void invoke(int command);
    void layout();
public:
    void create(SPBasicSuite*,SPPluginRef,Suites&,const std::array<AIMenuItemHandle,14>&);
    void show(Suites&);
    void destroy(Suites&);
    void update(bool selected,const std::wstring& message);
    void setBeforeInvoke(std::function<bool()> callback){beforeInvoke=std::move(callback);}
};
