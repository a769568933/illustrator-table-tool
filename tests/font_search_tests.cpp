#include "plugin/FontSearch.hpp"
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
void expect(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
}
int main(){try{
    const auto module=GetModuleHandleW(nullptr);
    auto parent=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,-32000,-32000,1,1,nullptr,nullptr,module,nullptr);
    auto query=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL,0,0,100,20,parent,nullptr,module,nullptr);
    auto combo=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWNLIST,0,20,150,200,parent,nullptr,module,nullptr);
    expect(parent&&query&&combo,"font search fixture created");
    FontSearch picker;picker.attach(query,combo,{u"宋体",u"Arial",u"思源宋体",u"Noto Sans"});picker.select(u"宋体");
    expect(SendMessageW(combo,CB_GETCOUNT,0,0)==4&&picker.selected()==u"宋体","initial list retains current font");
    SetWindowTextW(query,L"宋");SendMessageW(query,EM_SETSEL,1,1);picker.refresh();
    expect(SendMessageW(combo,CB_GETCOUNT,0,0)==2,"Chinese substring filters font names");
    SendMessageW(combo,CB_SETCURSEL,1,0);expect(picker.selected()==u"思源宋体","filtered result maps to actual font instead of old list index");
    wchar_t text[16]{};GetWindowTextW(query,text,16);DWORD first=0,last=0;SendMessageW(query,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    expect(std::wstring(text)==L"宋"&&first==1&&last==1,"filtering preserves search edit buffer and caret");
    picker.select(u"思源宋体");SetWindowTextW(query,L"aRiAl");picker.refresh();
    expect(SendMessageW(combo,CB_GETCOUNT,0,0)==1&&!picker.selected(),"English search ignores case without applying the first match");
    SendMessageW(combo,CB_SETCURSEL,0,0);expect(picker.selected()==u"Arial","explicit result selection resolves correct font");
    SetWindowTextW(query,L"No such font 123");picker.refresh();SendMessageW(combo,CB_SETCURSEL,0,0);
    expect(!picker.selected(),"no-result placeholder cannot be applied as a font");
    SetWindowTextW(query,L"");picker.refresh();expect(SendMessageW(combo,CB_GETCOUNT,0,0)==4&&picker.selected()==u"思源宋体","clearing search restores full catalog and current model font");
    picker.select(std::nullopt);expect(!picker.selected(),"mixed font selection stays unspecified");
    picker.reset();expect(!picker.selected(),"detached search has no stale control reference");DestroyWindow(parent);
    std::cout<<checks<<" font search checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
