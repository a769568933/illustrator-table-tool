#include <windows.h>
#include "Dialogs.hpp"
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <stdexcept>

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
