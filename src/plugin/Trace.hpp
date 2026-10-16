#pragma once
#include <windows.h>
#include <cstring>

// Development diagnostics record operation stages only; never document text.
inline void traceStage(const char* stage) {
    wchar_t path[MAX_PATH]{};const DWORD length=GetTempPathW(MAX_PATH,path);
    constexpr wchar_t name[]=L"IllustratorTableTool.log";
    if(length==0||length+sizeof(name)/sizeof(wchar_t)>=MAX_PATH) return;
    std::memcpy(path+length,name,sizeof(name));
    HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return;
    DWORD written=0;WriteFile(file,stage,static_cast<DWORD>(std::strlen(stage)),&written,nullptr);WriteFile(file,"\r\n",2,&written,nullptr);CloseHandle(file);
}
