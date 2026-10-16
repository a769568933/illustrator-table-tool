#pragma once
#include "Suites.hpp"
#include "AIArt.h"
#include "AIPath.h"
#include "AIPathStyle.h"
#include "AITextFrame.h"
#include "AIDocument.h"
#include "AIUndo.h"
#include "AIDictionary.h"
#include "AILayer.h"
#include "ATESuites.h"
#include "table/Geometry.hpp"

struct NativeCell { table::CellId id; AIArtHandle fill; AIArtHandle text; };
struct NativeTable {
    table::TableModel model;
    AIDocumentHandle document = nullptr;
    AIArtHandle group = nullptr;
    AIArtHandle frame = nullptr;
    AIRealPoint origin{};
    std::vector<NativeCell> cells;
    std::vector<AIArtHandle> edges;
};
class DocumentAdapter {
    AIArtSuite* art;
    AIPathSuite* paths;
    AIPathStyleSuite* styles;
    AITextFrameSuite* text;
    AIDocumentSuite* document;
    AIUndoSuite* undo;
    ATE::TextRangeSuite* ranges;
    ATE::CharFeaturesSuite* features;
    ATE::StorySuite* stories;
    AIDictionarySuite* dictionaries;
    AILayerSuite* layers;
    AIRealPoint world(const NativeTable& table, table::Point point) const;
    void rectangle(AIArtHandle path,const NativeTable& table,table::Rect bounds);
    void line(AIArtHandle path,const NativeTable& table,const table::Edge& edge);
    AIArtHandle newPath(AIArtHandle group);
    void setPaint(AIArtHandle path,bool fill,bool stroke,AIReal gray);
    void role(AIArtHandle art,const std::string& value);
    std::string role(AIArtHandle art) const;
    void writeModel(const NativeTable& table,const table::TableModel& model);
    NativeTable readTable(AIArtHandle group);
    void setText(AIArtHandle frame,const std::u16string& value);
    NativeCell createCell(const NativeTable& table,const table::Cell& cell);
public:
    explicit DocumentAdapter(Suites& suites);
    NativeTable createTable(const table::TableModel& model,AIRealPoint origin);
    bool valid(const NativeTable& table) const;
    void applyGeometry(NativeTable& table,const table::TableModel& model);
    void beginEdit(const NativeTable& table,table::CellId id);
    std::vector<NativeTable> discoverTables();
    void synchronize(NativeTable& table);
    void applyStructure(NativeTable& table,const table::TableModel& model);
    void detach(const NativeTable& table);
};
