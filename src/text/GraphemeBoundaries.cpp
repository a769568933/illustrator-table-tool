#include "TextLayoutInternal.hpp"
#include "table/RichText.hpp"

namespace table::text_detail {
namespace {
// Windows exposes the stable ICU C ABI. Resolve only from the system directory;
// no bundled or project-local DLL may intercept text segmentation.
class SystemIcu {
    HMODULE module=nullptr;
    template<class T> T function(const char* name){
        const auto address=GetProcAddress(module,name);
        if(!address)throw ValidationError("Windows ICU character iterator unavailable");
        return reinterpret_cast<T>(address);
    }
public:
    using Open=void* (__cdecl*)(std::int32_t,const char*,const char16_t*,std::int32_t,std::int32_t*);
    using Next=std::int32_t (__cdecl*)(void*);
    using Close=void (__cdecl*)(void*);
    Open open=nullptr;Next next=nullptr;Close close=nullptr;
    SystemIcu(){
        module=LoadLibraryExW(L"icu.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)throw ValidationError("Windows 10 1903 or newer ICU runtime required");
        try{open=function<Open>("ubrk_open");next=function<Next>("ubrk_next");close=function<Close>("ubrk_close");}
        catch(...){FreeLibrary(module);module=nullptr;throw;}
    }
    ~SystemIcu(){if(module)FreeLibrary(module);}
    SystemIcu(const SystemIcu&)=delete;
};
struct Apartment {
    HRESULT status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Apartment(){if(FAILED(status)&&status!=RPC_E_CHANGED_MODE)check(status);}
    ~Apartment(){if(SUCCEEDED(status))CoUninitialize();}
};
}
std::vector<std::uint32_t> graphemeBoundaries(const std::u16string& text){
    validateText(text);Apartment apartment;static SystemIcu icu;
    std::int32_t error=0;
    auto iterator=icu.open(0,"zh_CN",text.data(),static_cast<std::int32_t>(text.size()),&error);
    if(error>0||!iterator){if(iterator)icu.close(iterator);throw ValidationError("Cannot segment Unicode characters");}
    std::vector<std::uint32_t> result;
    try{
        result.push_back(0);
        for(auto end=icu.next(iterator);end!=-1;end=icu.next(iterator)){
            if(end<0||static_cast<std::size_t>(end)>text.size()||static_cast<std::uint32_t>(end)<=result.back())throw ValidationError("Invalid Unicode character boundary");
            result.push_back(static_cast<std::uint32_t>(end));
        }
        if(result.back()!=text.size())throw ValidationError("Incomplete Unicode character boundaries");
    }catch(...){icu.close(iterator);throw;}
    icu.close(iterator);return result;
}
}
