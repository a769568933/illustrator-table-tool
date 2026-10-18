#include "TextLayoutInternal.hpp"
#include "table/RichText.hpp"
#include <algorithm>
#include <cmath>

namespace table {
using namespace text_detail;
namespace {
ComRef<IDWriteFactory> factory(){
    ComRef<IDWriteFactory> result;
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(result.put())));
    return result;
}
std::u16string familyName(IDWriteFontCollection* fonts,UINT32 index){
    ComRef<IDWriteFontFamily> family;check(fonts->GetFontFamily(index,family.put()));
    ComRef<IDWriteLocalizedStrings> names;check(family->GetFamilyNames(names.put()));
    UINT32 selected=0;BOOL found=FALSE;check(names->FindLocaleName(L"zh-cn",&selected,&found));
    if(!found){check(names->FindLocaleName(L"en-us",&selected,&found));if(!found)selected=0;}
    UINT32 length=0;check(names->GetStringLength(selected,&length));if(length>256)throw ValidationError("Font family name exceeds limit");
    std::vector<WCHAR> buffer(length+1);check(names->GetString(selected,buffer.data(),length+1));
    return {reinterpret_cast<const char16_t*>(buffer.data()),length};
}
std::u16string resolveFamily(IDWriteFontCollection* fonts,const std::u16string& requested,std::vector<std::u16string>& substitutions){
    auto exists=[&](const std::u16string& name){UINT32 index=0;BOOL found=FALSE;check(fonts->FindFamilyName(wide(name),&index,&found));return found!=FALSE;};
    if(!requested.empty()&&exists(requested))return requested;
    std::u16string fallback=u"Microsoft YaHei";
    if(!exists(fallback)){fallback=u"Segoe UI";if(!exists(fallback)){if(!fonts->GetFontFamilyCount())throw ValidationError("No system fonts available");fallback=familyName(fonts,0);}}
    if(!requested.empty()){
        auto description=requested+u" -> "+fallback;
        if(std::find(substitutions.begin(),substitutions.end(),description)==substitutions.end())substitutions.push_back(std::move(description));
    }
    return fallback;
}
struct Paragraph {
    ComRef<IDWriteTextLayout> layout;
    std::uint32_t start=0,length=0;
    double y=0,height=0;
};
class WindowsLayout final:public TextLayout {
    std::vector<Paragraph> paragraphs;
    std::vector<std::uint32_t> stops{0};
    std::vector<GlyphPath> paths;
    std::vector<std::u16string> substitutions;
    std::uint32_t textLength=0;
    double padding=0;
    bool overflow=false;
    void requireStop(std::uint32_t index)const{if(!std::binary_search(stops.begin(),stops.end(),index))throw ValidationError("Position splits a text cluster");}
    const Paragraph& at(std::uint32_t position)const{
        for(auto it=paragraphs.rbegin();it!=paragraphs.rend();++it)if(position>=it->start)return *it;
        return paragraphs.front();
    }
public:
    explicit WindowsLayout(const LayoutRequest& request){
        validateStyle(request.style);validateText(request.style.font);validateFormatting(request.text,request.formatting);
        if(!std::isfinite(request.widthPt)||!std::isfinite(request.heightPt)||request.widthPt<=0||request.heightPt<=0||
            request.text.size()>request.limits.maxTextUnits||request.limits.maxTextUnits>32768||request.limits.maxGlyphs>32768||request.limits.maxSegments>250000)
            throw ValidationError("Text layout dimensions or budget exceeded");
        textLength=static_cast<std::uint32_t>(request.text.size());padding=request.style.padding;
        const double width=request.widthPt-2*padding,height=request.heightPt-2*padding;
        if(!request.text.empty()&&(width<=0||height<=0))overflow=true;
        auto engine=factory();ComRef<IDWriteFontCollection> fonts;check(engine->GetSystemFontCollection(fonts.put(),FALSE));
        const auto baseFamily=resolveFamily(fonts.get(),request.style.font,substitutions);
        const auto format=materializeFormatting(request.text,request.formatting,request.style);
        OutlineBudget budget{0,0,request.limits};double y=0;std::size_t paragraphIndex=0;
        for(std::uint32_t start=0;;){
            const auto newline=request.text.find(u'\n',start);
            const auto end=newline==std::u16string::npos?textLength:static_cast<std::uint32_t>(newline);
            Paragraph paragraph;paragraph.start=start;paragraph.length=end-start;paragraph.y=y;
            ComRef<IDWriteTextFormat> base;
            check(engine->CreateTextFormat(wide(baseFamily),fonts.get(),DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,
                static_cast<float>(toDip(request.style.fontSize)),L"zh-cn",base.put()));
            const int alignment=format.paragraphs.at(paragraphIndex++).alignment;
            check(base->SetTextAlignment(alignment==1?DWRITE_TEXT_ALIGNMENT_CENTER:alignment==2?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING));
            check(base->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
            check(engine->CreateTextLayout(wide(request.text)+start,paragraph.length,base.get(),static_cast<float>(toDip(std::max(0.01,width))),
                static_cast<float>(toDip(std::max(0.01,height))),paragraph.layout.put()));
            for(const auto& run:format.runs){
                const auto first=std::max(run.start,start),last=std::min(run.start+run.length,end);if(first>=last)continue;
                DWRITE_TEXT_RANGE range{first-start,last-first};const auto family=resolveFamily(fonts.get(),run.font,substitutions);
                check(paragraph.layout->SetFontFamilyName(wide(family),range));
                check(paragraph.layout->SetFontSize(static_cast<float>(toDip(run.fontSize)),range));
                ComRef<ColorEffect> effect(new ColorEffect(run.color));check(paragraph.layout->SetDrawingEffect(effect.get(),range));
            }
            DWRITE_TEXT_METRICS metrics{};check(paragraph.layout->GetMetrics(&metrics));
            paragraph.height=toPoint(metrics.height);
            if(paragraph.height<=0)paragraph.height=request.style.fontSize*1.3;
            overflow=overflow||toPoint(metrics.widthIncludingTrailingWhitespace)>width+0.01;
            UINT32 count=0;const auto status=paragraph.layout->GetClusterMetrics(nullptr,0,&count);
            if(FAILED(status)&&status!=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))check(status);
            if(count>paragraph.length)throw ValidationError("Invalid layout cluster count");
            std::vector<DWRITE_CLUSTER_METRICS> clusters(count);
            if(count)check(paragraph.layout->GetClusterMetrics(clusters.data(),count,&count));
            auto position=start;
            for(const auto& cluster:clusters){
                if(!cluster.length||cluster.length>end-position)throw ValidationError("Invalid text cluster");
                position+=cluster.length;if(stops.back()!=position)stops.push_back(position);
            }
            if(position!=end)throw ValidationError("Incomplete text cluster mapping");
            collectOutlines(paragraph.layout.get(),static_cast<float>(toDip(padding)),static_cast<float>(toDip(padding+y)),request.style.text,budget,paths);
            y+=paragraph.height;paragraphs.push_back(std::move(paragraph));
            if(newline==std::u16string::npos)break;
            start=end+1;if(stops.back()!=start)stops.push_back(start);
        }
        overflow=!request.text.empty()&&(overflow||y>height+0.01);
        const auto graphemes=graphemeBoundaries(request.text);
        stops.erase(std::remove_if(stops.begin(),stops.end(),[&](auto p){return !std::binary_search(graphemes.begin(),graphemes.end(),p);}),stops.end());
    }
    std::uint32_t hitTest(Point point)const override{
        if(!std::isfinite(point.x)||!std::isfinite(point.y))throw ValidationError("Invalid text hit point");
        const Paragraph* paragraph=&paragraphs.back();
        for(const auto& candidate:paragraphs)if(point.y<padding+candidate.y+candidate.height){paragraph=&candidate;break;}
        BOOL trailing=FALSE,inside=FALSE;DWRITE_HIT_TEST_METRICS metrics{};
        check(paragraph->layout->HitTestPoint(static_cast<float>(toDip(point.x-padding)),static_cast<float>(toDip(point.y-padding-paragraph->y)),&trailing,&inside,&metrics));
        auto position=paragraph->start+metrics.textPosition+(trailing?metrics.length:0);
        position=std::min(position,paragraph->start+paragraph->length);
        auto boundary=std::lower_bound(stops.begin(),stops.end(),position);return boundary==stops.end()?stops.back():*boundary;
    }
    Rect caret(std::uint32_t position)const override{
        requireStop(position);const auto& paragraph=at(position);FLOAT x=0,y=0;DWRITE_HIT_TEST_METRICS metrics{};
        check(paragraph.layout->HitTestTextPosition(position-paragraph.start,FALSE,&x,&y,&metrics));
        const auto top=padding+paragraph.y+toPoint(y),left=padding+toPoint(x);
        return {left,top,left+0.75,top+(metrics.height>0?toPoint(metrics.height):paragraph.height)};
    }
    std::vector<Rect> selection(std::uint32_t first,std::uint32_t last)const override{
        if(first>last)std::swap(first,last);requireStop(first);requireStop(last);std::vector<Rect> result;
        if(first==last)return result;
        for(const auto& p:paragraphs){
            const auto begin=std::max(first,p.start),end=std::min(last,p.start+p.length);
            if(begin<end){
                UINT32 count=0;const auto status=p.layout->HitTestTextRange(begin-p.start,end-begin,0,0,nullptr,0,&count);
                if(FAILED(status)&&status!=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))check(status);
                if(count>textLength+1)throw ValidationError("Too many text selection regions");
                std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                if(count)check(p.layout->HitTestTextRange(begin-p.start,end-begin,0,0,metrics.data(),count,&count));
                for(const auto& metric:metrics)result.push_back({padding+toPoint(metric.left),padding+p.y+toPoint(metric.top),
                    padding+toPoint(metric.left+metric.width),padding+p.y+toPoint(metric.top+metric.height)});
            }
            const auto newline=p.start+p.length;
            if(newline<textLength&&first<=newline&&last>newline){auto marker=caret(newline);marker.right=marker.left+3;result.push_back(marker);}
        }
        return result;
    }
    const std::vector<std::uint32_t>& boundaries()const override{return stops;}
    const std::vector<GlyphPath>& outlines()const override{return paths;}
    const std::vector<std::u16string>& fontSubstitutions()const override{return substitutions;}
    bool overflows()const override{return overflow;}
};
}
std::shared_ptr<const TextLayout> layoutText(const LayoutRequest& request){return std::make_shared<WindowsLayout>(request);}
std::vector<std::u16string> installedFontFamilies(){
    auto engine=factory();ComRef<IDWriteFontCollection> fonts;check(engine->GetSystemFontCollection(fonts.put(),FALSE));
    const auto count=fonts->GetFontFamilyCount();if(count>4096)throw ValidationError("Too many system font families");
    std::vector<std::u16string> result;for(UINT32 i=0;i<count;++i)result.push_back(familyName(fonts.get(),i));
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
}
