#include "EditorOverlay.hpp"
#include "text/TextLayoutInternal.hpp"
#include <wincodec.h>
#include <algorithm>
#include <cmath>

using namespace table::text_detail;
namespace {
struct Apartment {
    HRESULT status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Apartment(){if(FAILED(status)&&status!=RPC_E_CHANGED_MODE)check(status);}
    ~Apartment(){if(SUCCEEDED(status))CoUninitialize();}
};
D2D1_POINT_2F point(table::Point p){return {static_cast<FLOAT>(p.x),static_cast<FLOAT>(p.y)};}
D2D1_RECT_F rectangle(table::Rect r){return {static_cast<FLOAT>(r.left),static_cast<FLOAT>(r.top),static_cast<FLOAT>(r.right),static_cast<FLOAT>(r.bottom)};}
}
OverlayBitmap renderEditorOverlay(const OverlayFrame& frame){
    if(!frame.layout||!frame.width||!frame.height||frame.width>16384||frame.height>16384||static_cast<std::uint64_t>(frame.width)*frame.height>16*1024*1024||
        !std::isfinite(frame.scaleX)||!std::isfinite(frame.scaleY)||frame.scaleX<=0||frame.scaleY<=0||!std::isfinite(frame.offsetPixels.x)||!std::isfinite(frame.offsetPixels.y))
        throw table::ValidationError("Invalid editor preview dimensions");
    Apartment apartment;ComRef<IWICImagingFactory> imaging;
    check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IWICImagingFactory),reinterpret_cast<void**>(imaging.put())));
    ComRef<IWICBitmap> bitmap;check(imaging->CreateBitmap(frame.width,frame.height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.put()));
    ComRef<ID2D1Factory> factory;check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory),nullptr,reinterpret_cast<void**>(factory.put())));
    auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    ComRef<ID2D1RenderTarget> target;check(factory->CreateWicBitmapRenderTarget(bitmap.get(),properties,target.put()));
    ComRef<ID2D1SolidColorBrush> brush;check(target->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1),brush.put()));
    target->BeginDraw();target->Clear(frame.backgroundEnabled?D2D1::ColorF(static_cast<FLOAT>(frame.background.red),static_cast<FLOAT>(frame.background.green),static_cast<FLOAT>(frame.background.blue),1):D2D1::ColorF(0,0,0,0));
    target->SetTransform(D2D1::Matrix3x2F(static_cast<FLOAT>(frame.scaleX),0,0,static_cast<FLOAT>(frame.scaleY),static_cast<FLOAT>(frame.offsetPixels.x),static_cast<FLOAT>(frame.offsetPixels.y)));
    brush->SetColor(D2D1::ColorF(0.62f,0.78f,1.f,1.f));
    for(const auto& rect:frame.layout->selection(frame.anchor,frame.caret))target->FillRectangle(rectangle(rect),brush.get());
    for(const auto& glyph:frame.layout->outlines()){
        ComRef<ID2D1PathGeometry> geometry;check(factory->CreatePathGeometry(geometry.put()));ComRef<ID2D1GeometrySink> sink;check(geometry->Open(sink.put()));
        sink->SetFillMode(glyph.evenOdd?D2D1_FILL_MODE_ALTERNATE:D2D1_FILL_MODE_WINDING);
        for(const auto& contour:glyph.contours){
            sink->BeginFigure(point(contour.start),D2D1_FIGURE_BEGIN_FILLED);
            for(const auto& segment:contour.segments){if(segment.cubic)sink->AddBezier(D2D1::BezierSegment(point(segment.control1),point(segment.control2),point(segment.end)));else sink->AddLine(point(segment.end));}
            sink->EndFigure(contour.closed?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);
        }
        check(sink->Close());brush->SetColor(D2D1::ColorF(static_cast<FLOAT>(glyph.color.red),static_cast<FLOAT>(glyph.color.green),static_cast<FLOAT>(glyph.color.blue),1));
        target->FillGeometry(geometry.get(),brush.get());
    }
    brush->SetColor(D2D1::ColorF(0.05f,0.35f,1.f,1.f));
    if(frame.composition){for(const auto& rect:frame.layout->selection(frame.composition->first,frame.composition->second))
        target->DrawLine({static_cast<FLOAT>(rect.left),static_cast<FLOAT>(rect.bottom)},{static_cast<FLOAT>(rect.right),static_cast<FLOAT>(rect.bottom)},brush.get(),static_cast<FLOAT>(1/frame.scaleY));}
    if(frame.showCaret){auto rect=frame.layout->caret(frame.caret);rect.right=rect.left+std::max(0.75,1/frame.scaleX);target->FillRectangle(rectangle(rect),brush.get());}
    check(target->EndDraw());
    WICRect bounds{0,0,static_cast<INT>(frame.width),static_cast<INT>(frame.height)};ComRef<IWICBitmapLock> lock;check(bitmap->Lock(&bounds,WICBitmapLockRead,lock.put()));
    UINT stride=0,bytes=0;BYTE* data=nullptr;check(lock->GetStride(&stride));check(lock->GetDataPointer(&bytes,&data));
    if(!data||static_cast<std::uint64_t>(stride)*frame.height>bytes||stride<4*frame.width)throw table::ValidationError("Invalid preview pixel buffer");
    OverlayBitmap result{frame.width,frame.height,std::vector<std::uint8_t>(static_cast<std::size_t>(frame.width)*frame.height*3)};
    for(std::uint32_t y=0;y<frame.height;++y)for(std::uint32_t x=0;x<frame.width;++x){const auto source=data+static_cast<std::size_t>(y)*stride+4*x;const auto destination=3*(static_cast<std::size_t>(y)*frame.width+x);
        result.rgb[destination]=source[2];result.rgb[destination+1]=source[1];result.rgb[destination+2]=source[0];}
    lock.reset();
    if(!frame.backgroundEnabled){
        ComRef<IStream> stream;check(CreateStreamOnHGlobal(nullptr,TRUE,stream.put()));
        ComRef<IWICBitmapEncoder> encoder;check(imaging->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.put()));check(encoder->Initialize(stream.get(),WICBitmapEncoderNoCache));
        ComRef<IWICBitmapFrameEncode> encoded;check(encoder->CreateNewFrame(encoded.put(),nullptr));check(encoded->Initialize(nullptr));check(encoded->SetSize(frame.width,frame.height));
        auto format=GUID_WICPixelFormat32bppBGRA;check(encoded->SetPixelFormat(&format));if(!IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA))throw table::ValidationError("Preview encoder lost alpha support");
        ComRef<IWICFormatConverter> converter;check(imaging->CreateFormatConverter(converter.put()));
        check(converter->Initialize(bitmap.get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));check(encoded->WriteSource(converter.get(),nullptr));check(encoded->Commit());check(encoder->Commit());
        STATSTG info{};check(stream->Stat(&info,STATFLAG_NONAME));if(info.cbSize.QuadPart>128*1024*1024)throw table::ValidationError("Encoded preview exceeds limit");
        result.png.resize(static_cast<std::size_t>(info.cbSize.QuadPart));LARGE_INTEGER zero{};check(stream->Seek(zero,STREAM_SEEK_SET,nullptr));ULONG read=0;
        check(stream->Read(result.png.data(),static_cast<ULONG>(result.png.size()),&read));if(read!=result.png.size())throw table::ValidationError("Incomplete preview image");
    }
    return result;
}
