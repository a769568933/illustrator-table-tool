#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "table/RichText.hpp"
#include <functional>

enum class InputKind {Insert,Paste,Key,Composition,Commit,Cancel,FocusLost,Blink,Error};
enum InputModifier:std::uint32_t {InputShift=1,InputControl=2,InputAlt=4};
struct InputEvent {InputKind kind;std::u16string text;std::uint32_t key=0,modifiers=0,compositionStart=0,compositionLength=0;};
struct CompositionUpdate {std::u16string preedit,result;std::uint32_t caret=0;bool resultAvailable=false;};
using CompositionReader=std::function<CompositionUpdate(HWND,LPARAM)>;
std::u16string decodeClipboardText(HGLOBAL memory);
class InputWindow {
    HWND window=nullptr,owner=nullptr;HINSTANCE module=nullptr;
    std::function<void(const InputEvent&)> callback;CompositionReader reader;
    bool compositionActive=false,commitPending=false;std::uint64_t commitSerial=0;std::u16string duplicateChars;char16_t pendingHigh=0;
    bool focusRequested=false;
    void emit(const InputEvent& event);
    void character(char16_t value);
    LRESULT message(UINT message,WPARAM wParam,LPARAM lParam);
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
public:
    InputWindow()=default;
    InputWindow(const InputWindow&)=delete;
    ~InputWindow(){destroy();}
    void create(HWND owner,std::function<void(const InputEvent&)> callback,CompositionReader reader={});
    HWND handle()const{return window;}
    bool composing()const{return compositionActive;}
    void focus();
    void blur();
    void setCaretScreen(POINT point);
    bool requestCompositionCommit();
    void cancelComposition();
    void copyText(std::u16string_view text);
    void pasteText();
    void destroy();
};
