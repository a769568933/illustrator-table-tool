#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>

// The search field is independent of the list: rebuilding results must never
// replace its edit buffer, caret or input-method composition.
class FontSearch {
    HWND search=nullptr,choice=nullptr;
    std::vector<std::u16string> catalog,visible;
    std::optional<std::u16string> current;
public:
    void attach(HWND query,HWND list,std::vector<std::u16string> names){search=query;choice=list;catalog=std::move(names);refresh();}
    void select(std::optional<std::u16string> value){
        current=std::move(value);int selected=-1;
        for(std::size_t i=0;i<visible.size();++i)if(current&&visible[i]==*current){selected=static_cast<int>(i);break;}
        if(choice)SendMessageW(choice,CB_SETCURSEL,selected,0);
    }
    void refresh(){
        if(!search||!choice)return;wchar_t text[257]{};GetWindowTextW(search,text,257);std::wstring query=text;
        const auto space=[](wchar_t c){return c==L' '||c==L'\t'||c==0x3000;};
        while(!query.empty()&&space(query.front()))query.erase(query.begin());while(!query.empty()&&space(query.back()))query.pop_back();
        visible.clear();SendMessageW(choice,WM_SETREDRAW,FALSE,0);SendMessageW(choice,CB_RESETCONTENT,0,0);
        for(const auto& name:catalog){
            const std::wstring label(name.begin(),name.end());
            if(!query.empty()&&FindStringOrdinal(FIND_FROMSTART,label.c_str(),static_cast<int>(label.size()),query.c_str(),static_cast<int>(query.size()),TRUE)<0)continue;
            SendMessageW(choice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));visible.push_back(name);
        }
        if(visible.empty())SendMessageW(choice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"未找到匹配字体"));
        select(current);SendMessageW(choice,WM_SETREDRAW,TRUE,0);InvalidateRect(choice,nullptr,TRUE);
    }
    std::optional<std::u16string> selected()const{
        if(!choice)return {};const auto index=SendMessageW(choice,CB_GETCURSEL,0,0);
        if(index<0||static_cast<std::size_t>(index)>=visible.size())return {};return visible[static_cast<std::size_t>(index)];
    }
    void reset(){search=choice=nullptr;catalog.clear();visible.clear();current.reset();}
};
