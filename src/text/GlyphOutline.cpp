#include "TextLayoutInternal.hpp"
#include <cmath>

namespace table::text_detail {
namespace {
class GeometrySink final:public IDWriteGeometrySink {
    LONG references=1;
    GlyphPath& path;OutlineBudget& budget;float x,y;
    HRESULT failure=S_OK;
    Point point(D2D1_POINT_2F p)const{
        if(!std::isfinite(p.x)||!std::isfinite(p.y))throw ValidationError("Invalid glyph contour");
        return {toPoint(p.x+x),toPoint(p.y+y)};
    }
    void count(){if(budget.segments>=budget.limits.maxSegments)throw ValidationError("Text curve budget exceeded");++budget.segments;}
    template<class F> void perform(F fn)noexcept{if(FAILED(failure))return;try{fn();}catch(...){failure=E_FAIL;}}
public:
    GeometrySink(GlyphPath& value,OutlineBudget& resources,float left,float top):path(value),budget(resources),x(left),y(top){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(IsEqualIID(id,__uuidof(IUnknown))||IsEqualIID(id,__uuidof(IDWriteGeometrySink))){*out=static_cast<IDWriteGeometrySink*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&references);}
    ULONG STDMETHODCALLTYPE Release()override{auto left=InterlockedDecrement(&references);if(!left)delete this;return left;}
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE mode)noexcept override{path.evenOdd=mode==D2D1_FILL_MODE_ALTERNATE;}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT)noexcept override{}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start,D2D1_FIGURE_BEGIN)noexcept override{perform([&]{count();path.contours.push_back({point(start),{},false});});}
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points,UINT countPoints)noexcept override{perform([&]{
        if(path.contours.empty())throw ValidationError("Missing glyph contour");
        for(UINT i=0;i<countPoints;++i){count();path.contours.back().segments.push_back({{}, {},point(points[i]),false});}
    });}
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* curves,UINT countCurves)noexcept override{perform([&]{
        if(path.contours.empty())throw ValidationError("Missing glyph contour");
        for(UINT i=0;i<countCurves;++i){count();path.contours.back().segments.push_back({point(curves[i].point1),point(curves[i].point2),point(curves[i].point3),true});}
    });}
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end)noexcept override{perform([&]{if(path.contours.empty())throw ValidationError("Missing glyph contour");path.contours.back().closed=end==D2D1_FIGURE_END_CLOSED;});}
    HRESULT STDMETHODCALLTYPE Close()noexcept override{return failure;}
};
class OutlineRenderer final:public IDWriteTextRenderer {
    LONG references=1;
    Color fallback;OutlineBudget& budget;std::vector<GlyphPath>& output;
public:
    std::string error;
    OutlineRenderer(Color color,OutlineBudget& limits,std::vector<GlyphPath>& paths):fallback(color),budget(limits),output(paths){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(IsEqualIID(id,__uuidof(IUnknown))||IsEqualIID(id,__uuidof(IDWritePixelSnapping))||IsEqualIID(id,__uuidof(IDWriteTextRenderer))){*out=static_cast<IDWriteTextRenderer*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&references);}
    ULONG STDMETHODCALLTYPE Release()override{auto left=InterlockedDecrement(&references);if(!left)delete this;return left;}
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,BOOL* disabled)override{if(!disabled)return E_POINTER;*disabled=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,DWRITE_MATRIX* transform)override{if(!transform)return E_POINTER;*transform={1,0,0,1,0,0};return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*,FLOAT* pixels)override{if(!pixels)return E_POINTER;*pixels=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,FLOAT x,FLOAT y,DWRITE_MEASURING_MODE,const DWRITE_GLYPH_RUN* run,const DWRITE_GLYPH_RUN_DESCRIPTION*,IUnknown* effect)override{
        try{
            if(!run||!run->fontFace||run->glyphCount>budget.limits.maxGlyphs-budget.glyphs)throw ValidationError("Text glyph budget exceeded");
            budget.glyphs+=run->glyphCount;
            for(UINT32 i=0;i<run->glyphCount;++i)if(run->glyphIndices[i]==0)throw ValidationError("A character has no available font glyph");
            GlyphPath path;path.color=effect?static_cast<ColorEffect*>(effect)->color:fallback;
            ComRef<GeometrySink> sink(new GeometrySink(path,budget,x,y));
            check(run->fontFace->GetGlyphRunOutline(run->fontEmSize,run->glyphIndices,run->glyphAdvances,run->glyphOffsets,run->glyphCount,run->isSideways,(run->bidiLevel&1)!=0,sink.get()));
            check(sink->Close());if(!path.contours.empty())output.push_back(std::move(path));return S_OK;
        }catch(const std::exception& e){error=e.what();return E_FAIL;}catch(...){error="Cannot generate glyph contours";return E_FAIL;}
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*,FLOAT,FLOAT,const DWRITE_UNDERLINE*,IUnknown*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,FLOAT,FLOAT,const DWRITE_STRIKETHROUGH*,IUnknown*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,FLOAT,FLOAT,IDWriteInlineObject*,BOOL,BOOL,IUnknown*)override{return E_NOTIMPL;}
};
}
void collectOutlines(IDWriteTextLayout* layout,float originX,float originY,Color fallback,OutlineBudget& budget,std::vector<GlyphPath>& output){
    ComRef<OutlineRenderer> renderer(new OutlineRenderer(fallback,budget,output));
    const auto result=layout->Draw(nullptr,renderer.get(),originX,originY);
    if(!renderer->error.empty())throw ValidationError(renderer->error);
    check(result);
}
}
