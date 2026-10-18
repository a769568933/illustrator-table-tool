#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include "TextLayout.hpp"
#include <utility>

namespace table::text_detail {
template<class T> class ComRef {
    T* value=nullptr;
public:
    ComRef()=default;
    explicit ComRef(T* owned):value(owned){}
    ComRef(const ComRef&)=delete;
    ComRef& operator=(const ComRef&)=delete;
    ComRef(ComRef&& other) noexcept:value(std::exchange(other.value,nullptr)){}
    ComRef& operator=(ComRef&& other) noexcept {if(this!=&other){reset();value=std::exchange(other.value,nullptr);}return *this;}
    ~ComRef(){reset();}
    T* get()const{return value;}
    T* operator->()const{return value;}
    T** put(){reset();return &value;}
    void reset(){if(value){value->Release();value=nullptr;}}
};
inline void check(HRESULT result){if(FAILED(result))throw ValidationError("Windows text layout failed ("+std::to_string(static_cast<long>(result))+")");}
inline constexpr double toDip(double pt){return pt*4.0/3.0;}
inline constexpr double toPoint(double dip){return dip*3.0/4.0;}
inline const WCHAR* wide(const std::u16string& text){static_assert(sizeof(WCHAR)==sizeof(char16_t));return reinterpret_cast<const WCHAR*>(text.c_str());}
struct OutlineBudget { std::uint32_t glyphs=0,segments=0; LayoutLimits limits; };
class ColorEffect final:public IUnknown {
    LONG references=1;
public:
    Color color;
    explicit ColorEffect(Color value):color(value){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(IsEqualIID(id,__uuidof(IUnknown))){*out=static_cast<IUnknown*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&references);}
    ULONG STDMETHODCALLTYPE Release() override{const auto left=InterlockedDecrement(&references);if(!left)delete this;return left;}
};
void collectOutlines(IDWriteTextLayout* layout,float originX,float originY,Color fallback,OutlineBudget& budget,std::vector<GlyphPath>& output);
std::vector<std::uint32_t> graphemeBoundaries(const std::u16string& text);
}
