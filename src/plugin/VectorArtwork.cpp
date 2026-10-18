#include "VectorArtwork.hpp"
#include <cmath>
#include <set>

VectorArtwork::VectorArtwork(Suites& suites):
 art(suites.get<AIArtSuite>(kAIArtSuite,kAIArtVersion)),paths(suites.get<AIPathSuite>(kAIPathSuite,kAIPathVersion)),
 styles(suites.get<AIPathStyleSuite>(kAIPathStyleSuite,kAIPathStyleVersion)),groups(suites.get<AIGroupSuite>(kAIGroupSuite,kAIGroupVersion)),
 document(suites.get<AIDocumentSuite>(kAIDocumentSuite,kAIDocumentVersion)),conversion(suites.get<AIColorConversionSuite>(kAIColorConversionSuite,kAIColorConversionVersion)){}
AIArtHandle VectorArtwork::path(AIArtHandle parent){AIArtHandle result=nullptr;checked(art->NewArt(kPathArt,kPlaceInsideOnTop,parent,&result));return result;}
void VectorArtwork::rectangle(AIArtHandle path,table::Rect r,const ArtworkTransform& transform){
    const table::Point points[]={{r.left,r.top},{r.right,r.top},{r.right,r.bottom},{r.left,r.bottom}};AIPathSegment segments[4]{};
    for(int i=0;i<4;++i)segments[i].p=segments[i].in=segments[i].out=transform.world(points[i]);
    checked(paths->SetPathSegmentCount(path,4));checked(paths->SetPathSegments(path,0,4,segments));checked(paths->SetPathClosed(path,true));
}
void VectorArtwork::line(AIArtHandle path,const table::Edge& edge,const ArtworkTransform& transform){
    AIPathSegment segments[2]{};segments[0].p=segments[0].in=segments[0].out=transform.world(edge.from);
    segments[1].p=segments[1].in=segments[1].out=transform.world(edge.to);
    checked(paths->SetPathSegmentCount(path,2));checked(paths->SetPathSegments(path,0,2,segments));checked(paths->SetPathClosed(path,false));
}
AIColor VectorArtwork::color(const table::Color& value){
    ai::int16 mode=kDocUnknownColor;checked(document->GetDocumentColorModel(&mode));AIColor result;result.Init();
    if(mode==kDocRGBColor){result.kind=kThreeColor;result.c.rgb.red=value.red;result.c.rgb.green=value.green;result.c.rgb.blue=value.blue;}
    else {
        result.kind=kFourColor;
        if(value.red==value.green&&value.green==value.blue){result.c.f.cyan=result.c.f.magenta=result.c.f.yellow=0;result.c.f.black=1-value.red;}
        else {
            AIFloatSampleComponent rgb[3]={static_cast<AIFloatSampleComponent>(value.red),static_cast<AIFloatSampleComponent>(value.green),static_cast<AIFloatSampleComponent>(value.blue)},cmyk[4]{};
            AIColorConvertOptions options;AIBoolean inGamut=false;
            checked(conversion->ConvertSampleColor(kAIRGBColorSpace,rgb,kAICMYKColorSpace,cmyk,options,&inGamut));
            result.c.f.cyan=cmyk[0];result.c.f.magenta=cmyk[1];result.c.f.yellow=cmyk[2];result.c.f.black=cmyk[3];
        }
    }
    return result;
}
void VectorArtwork::paint(AIArtHandle path,const table::Color& fill,bool filled,const table::Color& stroke,double width,bool evenOdd){
    AIPathStyle style;style.Init();style.fillPaint=filled;style.strokePaint=width>0;style.fill.color=color(fill);
    style.stroke.color=color(stroke);style.stroke.width=static_cast<AIReal>(width);style.stroke.miterLimit=4;style.evenodd=evenOdd;
    checked(styles->SetPathStyle(path,&style));
}
void VectorArtwork::contour(AIArtHandle parent,const table::GlyphContour& contour,const ArtworkTransform& transform,table::Point offset){
    if(!contour.closed||contour.segments.empty()||contour.segments.size()>32765)throw table::ValidationError("Unsupported glyph contour");
    auto point=[&](table::Point p){return transform.world({p.x+offset.x,p.y+offset.y});};
    auto equal=[](table::Point a,table::Point b){return std::abs(a.x-b.x)<1e-7&&std::abs(a.y-b.y)<1e-7;};
    AIPathSegment initial{};initial.p=initial.in=initial.out=point(contour.start);std::vector<AIPathSegment> nodes{initial};
    for(std::size_t i=0;i<contour.segments.size();++i){
        const auto& segment=contour.segments[i];auto& previous=nodes.back();
        if(segment.cubic)previous.out=point(segment.control1);
        if(i+1==contour.segments.size()&&equal(segment.end,contour.start)){
            nodes.front().in=segment.cubic?point(segment.control2):nodes.front().p;
        }else{
            AIPathSegment node{};node.p=node.in=node.out=point(segment.end);if(segment.cubic)node.in=point(segment.control2);nodes.push_back(node);
        }
    }
    if(nodes.size()<2)throw table::ValidationError("Empty glyph contour");
    auto object=path(parent);const auto count=static_cast<ai::int16>(nodes.size());
    checked(paths->SetPathSegmentCount(object,count));checked(paths->SetPathSegments(object,0,count,nodes.data()));checked(paths->SetPathClosed(object,true));
}
AIArtHandle VectorArtwork::content(AIArtHandle parent,table::Rect cell,const ArtworkTransform& transform,const table::TextLayout& layout){
    AIArtHandle group=nullptr;checked(art->NewArt(kGroupArt,kPlaceInsideOnTop,parent,&group));checked(groups->SetGroupClipped(group,true));
    for(const auto& glyph:layout.outlines()){
        AIArtHandle compound=nullptr;checked(art->NewArt(kCompoundPathArt,kPlaceInsideOnTop,group,&compound));
        for(const auto& shape:glyph.contours)contour(compound,shape,transform,{cell.left,cell.top});
        paint(compound,glyph.color,true,{},0,glyph.evenOdd);
    }
    const auto clip=path(group);rectangle(clip,cell,transform);paint(clip,{},false);
    checked(art->SetArtUserAttr(clip,kArtIsClipMask,kArtIsClipMask));return group;
}
void auditVectorTable(Suites& suites,AIArtHandle group){
    auto art=suites.get<AIArtSuite>(kAIArtSuite,kAIArtVersion);std::vector<AIArtHandle> stack{group};std::set<AIArtHandle> seen;
    while(!stack.empty()){
        const auto item=stack.back();stack.pop_back();
        if(seen.size()>=100000||!seen.insert(item).second||!item||!art->ValidArt(item,false))throw table::ValidationError("Invalid or excessive table artwork");
        short type=0;checked(art->GetArtType(item,&type));
        if(type!=kGroupArt&&type!=kPathArt&&type!=kCompoundPathArt)throw table::ValidationError("Table contains a non-vector artwork type");
        if(type==kPathArt)continue;
        AIArtHandle child=nullptr;checked(art->GetArtFirstChild(item,&child));
        while(child){
            stack.push_back(child);if(stack.size()>100000)throw table::ValidationError("Excessive table artwork");
            AIArtHandle next=nullptr;checked(art->GetArtSibling(child,&next));child=next;
        }
    }
}
