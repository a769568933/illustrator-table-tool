#include "table/RichText.hpp"
#include <algorithm>

namespace table {
namespace {
bool high(char16_t c){return c>=0xd800&&c<=0xdbff;}
bool low(char16_t c){return c>=0xdc00&&c<=0xdfff;}
bool same(const TextRun& a,const TextRun& b) {
    return a.font==b.font&&a.fontSize==b.fontSize&&a.color.red==b.color.red&&a.color.green==b.color.green&&a.color.blue==b.color.blue;
}
void pushRun(std::vector<TextRun>& runs,TextRun value) {
    if(!value.length) return;
    if(!runs.empty()&&runs.back().start+runs.back().length==value.start&&same(runs.back(),value)) runs.back().length+=value.length;
    else runs.push_back(std::move(value));
    if(runs.size()>maximumFormatRuns) throw ValidationError("Too many text format ranges");
}
void pushParagraph(std::vector<ParagraphRun>& runs,ParagraphRun value) {
    if(!runs.empty()&&runs.back().start==value.start) runs.back()=value;
    else runs.push_back(value);
    if(runs.size()>maximumFormatRuns) throw ValidationError("Too many paragraphs");
}
TextRun baseRun(const CellStyle& style,std::uint32_t start,std::uint32_t length) {
    return {start,length,style.font,style.fontSize,style.text};
}
}
void validateText(std::u16string_view text) {
    if(text.size()>maximumTextBytes) throw ValidationError("Text exceeds limit");
    std::size_t bytes=0;
    for(std::size_t i=0;i<text.size();++i) {
        const auto c=text[i];
        if(c==0) throw ValidationError("Embedded NUL is not supported");
        if(high(c)) {
            if(i+1>=text.size()||!low(text[++i])) throw ValidationError("Invalid UTF-16 surrogate pair");
            bytes+=4;
        } else if(low(c)) throw ValidationError("Invalid UTF-16 surrogate pair");
        else bytes+=c<0x80?1:c<0x800?2:3;
        if(bytes>maximumTextBytes) throw ValidationError("Text exceeds limit");
    }
}
bool isTextBoundary(std::u16string_view text,std::uint32_t position) {
    return position<=text.size()&&(position==0||position==text.size()||!high(text[position-1])||!low(text[position]));
}
void validateFormatting(std::u16string_view text,const TextFormatting& format) {
    validateText(text);
    if(format.runs.size()>maximumFormatRuns||format.runs.size()>text.size()||format.paragraphs.size()>maximumFormatRuns||format.paragraphs.size()>text.size()+1)
        throw ValidationError("Too many text format ranges");
    std::uint32_t end=0;
    for(const auto& run:format.runs) {
        if(!run.length||run.start<end||run.start>text.size()||run.length>text.size()-run.start||
            !isTextBoundary(text,run.start)||!isTextBoundary(text,run.start+run.length)) throw ValidationError("Invalid character format range");
        CellStyle style;style.font=run.font;style.fontSize=run.fontSize;style.text=run.color;validateStyle(style);validateText(run.font);
        end=run.start+run.length;
    }
    std::uint32_t previous=0;bool first=true;
    for(const auto& p:format.paragraphs) {
        if(p.start>text.size()||(!first&&p.start<=previous)||(p.start>0&&text[p.start-1]!=u'\n')||p.alignment<0||p.alignment>2)
            throw ValidationError("Invalid paragraph format range");
        previous=p.start;first=false;
    }
}
TextFormatting materializeFormatting(std::u16string_view text,const TextFormatting& format,const CellStyle& fallback) {
    validateStyle(fallback);validateFormatting(text,format);
    TextFormatting result;std::uint32_t offset=0;
    for(auto run:format.runs) {
        if(run.start>offset) pushRun(result.runs,baseRun(fallback,offset,run.start-offset));
        if(run.font.empty()) run.font=fallback.font;
        offset=run.start+run.length;pushRun(result.runs,std::move(run));
    }
    if(offset<text.size()) pushRun(result.runs,baseRun(fallback,offset,static_cast<std::uint32_t>(text.size()-offset)));
    std::size_t index=0;
    for(std::uint32_t start=0;;) {
        int alignment=fallback.alignment;
        if(index<format.paragraphs.size()&&format.paragraphs[index].start==start) alignment=format.paragraphs[index++].alignment;
        pushParagraph(result.paragraphs,{start,alignment});
        const auto next=text.find(u'\n',start);if(next==std::u16string_view::npos) break;
        start=static_cast<std::uint32_t>(next+1);
    }
    return result;
}
void appendFormattedText(std::u16string& destination,TextFormatting& target,std::u16string_view source,
    const TextFormatting& sourceFormatting,const CellStyle& fallback,bool separator) {
    auto format=materializeFormatting(source,sourceFormatting,fallback);
    auto offset=static_cast<std::uint32_t>(destination.size());
    if(separator) {
        auto run=target.runs.empty()?baseRun(fallback,offset,1):target.runs.back();run.start=offset;run.length=1;
        pushRun(target.runs,std::move(run));destination+=u'\n';++offset;
    }
    destination.append(source);validateText(destination);
    for(auto run:format.runs) {run.start+=offset;pushRun(target.runs,std::move(run));}
    for(auto p:format.paragraphs) {p.start+=offset;pushParagraph(target.paragraphs,p);}
}
std::string encodeUtf8(std::u16string_view value) {
    validateText(value);
    std::string result;
    for(std::size_t i=0;i<value.size();++i) {
        std::uint32_t cp=value[i];
        if(cp>=0xd800&&cp<=0xdbff) {
            if(++i>=value.size()||value[i]<0xdc00||value[i]>0xdfff) throw ValidationError("Invalid Unicode encoding");
            cp=0x10000+((cp-0xd800)<<10)+(value[i]-0xdc00);
        } else if(cp>=0xdc00&&cp<=0xdfff) throw ValidationError("Invalid Unicode encoding");
        if(cp<0x80) result+=static_cast<char>(cp);
        else if(cp<0x800) {result+=static_cast<char>(0xc0|(cp>>6));result+=static_cast<char>(0x80|(cp&63));}
        else if(cp<0x10000) {result+=static_cast<char>(0xe0|(cp>>12));result+=static_cast<char>(0x80|((cp>>6)&63));result+=static_cast<char>(0x80|(cp&63));}
        else {result+=static_cast<char>(0xf0|(cp>>18));result+=static_cast<char>(0x80|((cp>>12)&63));result+=static_cast<char>(0x80|((cp>>6)&63));result+=static_cast<char>(0x80|(cp&63));}
        if(result.size()>maximumTextBytes) throw ValidationError("Invalid Unicode encoding");
    }
    return result;
}
std::u16string decodeUtf8(std::string_view value) {
    if(value.size()>maximumTextBytes) throw ValidationError("Text exceeds limit");
    std::u16string result;
    for(std::size_t i=0;i<value.size();) {
        auto first=static_cast<unsigned char>(value[i++]);
        std::uint32_t cp=first; int count=0; std::uint32_t minimum=0;
        if(first<0x80) {}
        else if(first>=0xc2&&first<=0xdf) {cp=first&31;count=1;minimum=0x80;}
        else if(first>=0xe0&&first<=0xef) {cp=first&15;count=2;minimum=0x800;}
        else if(first>=0xf0&&first<=0xf4) {cp=first&7;count=3;minimum=0x10000;}
        else throw ValidationError("Invalid Unicode encoding");
        for(int n=0;n<count;++n) {
            if(i>=value.size()) throw ValidationError("Invalid Unicode encoding");
            auto b=static_cast<unsigned char>(value[i++]); if((b&0xc0)!=0x80) throw ValidationError("Invalid Unicode encoding"); cp=(cp<<6)|(b&63);
        }
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) throw ValidationError("Invalid Unicode encoding");
        if(cp<0x10000) result+=static_cast<char16_t>(cp);
        else {cp-=0x10000;result+=static_cast<char16_t>(0xd800+(cp>>10));result+=static_cast<char16_t>(0xdc00+(cp&1023));}
    }
    validateText(result);
    return result;
}

}
