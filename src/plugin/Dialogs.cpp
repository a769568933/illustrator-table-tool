#include <windows.h>
#include "Dialogs.hpp"
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <stdexcept>
#include <commdlg.h>
#include <algorithm>

namespace {
HINSTANCE instance=nullptr;
struct CreateState {std::optional<table::TableModel> value;};
double number(HWND window,int id) {
    wchar_t buffer[80]{};GetDlgItemTextW(window,id,buffer,80);
    wchar_t* end=nullptr;double result=std::wcstod(buffer,&end);
    if(end==buffer||!std::isfinite(result)) throw table::ValidationError("Invalid number");
    while(*end&&std::iswspace(*end)) ++end;
    if(*end) throw table::ValidationError("Invalid number suffix");return result;
}
INT_PTR CALLBACK createProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_INITDIALOG) {
        SetWindowLongPtrW(window,DWLP_USER,lParam);
        SetDlgItemTextW(window,20101,L"4");SetDlgItemTextW(window,20102,L"4");
        SetDlgItemTextW(window,20103,L"120");SetDlgItemTextW(window,20104,L"60");return TRUE;
    }
    if(message!=WM_COMMAND) return FALSE;
    if(LOWORD(wParam)==IDCANCEL) {EndDialog(window,IDCANCEL);return TRUE;}
    if(LOWORD(wParam)==IDOK) {
        try {
            auto rows=number(window,20101),columns=number(window,20102);
            if(std::floor(rows)!=rows||std::floor(columns)!=columns||rows<1||rows>100||columns<1||columns>50) throw table::ValidationError("Invalid count");
            auto model=table::TableModel::create(static_cast<int>(rows),static_cast<int>(columns),table::mm(number(window,20103)),table::mm(number(window,20104)));
            reinterpret_cast<CreateState*>(GetWindowLongPtrW(window,DWLP_USER))->value=std::move(model);
            EndDialog(window,IDOK);
        } catch(const std::exception&) {
            MessageBoxW(window,L"请输入有效数字：1–100 行、1–50 列，总格数不超过 2000。\n每行高度和每列宽度至少为 2 毫米。",L"检查表格参数",MB_OK|MB_ICONINFORMATION);
        }
        return TRUE;
    }
    return FALSE;
}
}
extern "C" BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) instance=module;return TRUE;
}
std::optional<table::TableModel> showCreateTableDialog() {
    CreateState state;
    if(DialogBoxParamW(instance,MAKEINTRESOURCEW(20001),GetActiveWindow(),createProc,reinterpret_cast<LPARAM>(&state))==-1)
        throw std::runtime_error("Unable to open table dialog");
    return state.value;
}
namespace {
struct StyleState {
    table::CellStyle value;const std::vector<FontChoice>* fonts;bool accepted=false;
};
void field(HWND window,int id,double value) {
    wchar_t buffer[64]{};std::swprintf(buffer,64,L"%.6g",value);SetDlgItemTextW(window,id,buffer);
}
COLORREF rgb(const table::Color& value) {
    return RGB(static_cast<int>(value.red*255+0.5),static_cast<int>(value.green*255+0.5),static_cast<int>(value.blue*255+0.5));
}
void colorLabel(HWND window,int id,const table::Color& color) {
    const auto value=rgb(color);wchar_t label[32]{};
    std::swprintf(label,32,L"#%02X%02X%02X  选择…",GetRValue(value),GetGValue(value),GetBValue(value));SetDlgItemTextW(window,id,label);
}
INT_PTR CALLBACK styleProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* state=reinterpret_cast<StyleState*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        state=reinterpret_cast<StyleState*>(lParam);SetWindowLongPtrW(window,DWLP_USER,lParam);
        field(window,20201,state->value.fontSize);field(window,20202,state->value.borderWidth);field(window,20203,state->value.padding/table::mm(1));
        auto font=GetDlgItem(window,20204);SendMessageW(font,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"保留当前字体"));
        int selected=0;
        for(std::size_t i=0;i<state->fonts->size();++i) {
            const auto& choice=state->fonts->at(i);
            std::wstring label(choice.label.begin(),choice.label.end());
            SendMessageW(font,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if(choice.name==state->value.font) selected=static_cast<int>(i)+1;
        }
        SendMessageW(font,CB_SETCURSEL,selected,0);
        auto align=GetDlgItem(window,20205);
        for(auto label:{L"左对齐",L"居中",L"右对齐"}) SendMessageW(align,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(align,CB_SETCURSEL,state->value.alignment,0);
        colorLabel(window,20206,state->value.fill);colorLabel(window,20207,state->value.border);colorLabel(window,20208,state->value.text);return TRUE;
    }
    if(message!=WM_COMMAND||!state) return FALSE;
    const int id=LOWORD(wParam);
    if(id==IDCANCEL) {EndDialog(window,IDCANCEL);return TRUE;}
    if(id>=20206&&id<=20208) {
        auto* value=id==20206 ? &state->value.fill : id==20207 ? &state->value.border : &state->value.text;
        COLORREF custom[16]{};CHOOSECOLORW choose{};choose.lStructSize=sizeof(choose);choose.hwndOwner=window;
        choose.rgbResult=rgb(*value);choose.lpCustColors=custom;choose.Flags=CC_FULLOPEN|CC_RGBINIT;
        if(ChooseColorW(&choose)) {
            *value={GetRValue(choose.rgbResult)/255.0,GetGValue(choose.rgbResult)/255.0,GetBValue(choose.rgbResult)/255.0};colorLabel(window,id,*value);
        }
        return TRUE;
    }
    if(id==IDOK) {
        try {
            auto value=state->value;value.fontSize=number(window,20201);value.borderWidth=number(window,20202);value.padding=table::mm(number(window,20203));
            const auto selected=SendDlgItemMessageW(window,20204,CB_GETCURSEL,0,0);
            value.font=selected>0 ? state->fonts->at(static_cast<std::size_t>(selected)-1).name : u"";
            value.alignment=static_cast<int>(SendDlgItemMessageW(window,20205,CB_GETCURSEL,0,0));table::validateStyle(value);
            state->value=std::move(value);state->accepted=true;EndDialog(window,IDOK);
        } catch(const std::exception&) {
            MessageBoxW(window,L"字号范围 1–1000 点；边框粗细 0–100 点；内边距 0–100 毫米。\n内边距超过单元格尺寸时会自动限制，保证文字框仍然有效。",L"检查样式参数",MB_OK|MB_ICONINFORMATION);
        }
        return TRUE;
    }
    return FALSE;
}
}
std::optional<table::CellStyle> showCellStyleDialog(const table::CellStyle& initial,const std::vector<FontChoice>& fonts) {
    StyleState state{initial,&fonts};
    if(DialogBoxParamW(instance,MAKEINTRESOURCEW(20002),GetActiveWindow(),styleProc,reinterpret_cast<LPARAM>(&state))==-1)
        throw std::runtime_error("Unable to open cell style dialog");
    return state.accepted ? std::optional<table::CellStyle>{state.value} : std::nullopt;
}
