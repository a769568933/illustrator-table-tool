#pragma once
#include "Suites.hpp"
#include "AIArt.h"
#include "AIPath.h"
#include "AIPathStyle.h"
#include "AIGroup.h"
#include "AIDocument.h"
#include "AIColorConversion.h"
#include "text/TextLayout.hpp"

struct ArtworkTransform {
    AIRealPoint origin{};double scaleX=1,scaleY=1;
    AIRealPoint world(table::Point p)const{return {static_cast<AIReal>(origin.h+p.x*scaleX),static_cast<AIReal>(origin.v-p.y*scaleY)};}
};
class VectorArtwork {
    AIArtSuite* art;AIPathSuite* paths;AIPathStyleSuite* styles;AIGroupSuite* groups;
    AIDocumentSuite* document;AIColorConversionSuite* conversion;
    void contour(AIArtHandle parent,const table::GlyphContour& contour,const ArtworkTransform& transform,table::Point offset);
public:
    explicit VectorArtwork(Suites& suites);
    AIArtHandle path(AIArtHandle parent);
    void rectangle(AIArtHandle path,table::Rect bounds,const ArtworkTransform& transform);
    void line(AIArtHandle path,const table::Edge& edge,const ArtworkTransform& transform);
    AIColor color(const table::Color& color);
    void paint(AIArtHandle path,const table::Color& fill,bool filled,const table::Color& stroke={},double width=0,bool evenOdd=false);
    AIArtHandle content(AIArtHandle parent,table::Rect cell,const ArtworkTransform& transform,const table::TextLayout& layout);
};
// Examines every child, including hidden children; never filters by selection.
void auditVectorTable(Suites& suites,AIArtHandle group);
