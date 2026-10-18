#pragma once
#include "VectorArtwork.hpp"
#include "AIUndo.h"
#include "AIDictionary.h"
#include "AILayer.h"
#include "Dialogs.hpp"
#include "table/Geometry.hpp"
#include <map>

struct NativeCell { table::CellId id; AIArtHandle fill=nullptr,content=nullptr; std::shared_ptr<const table::TextLayout> layout; };
struct RejectedTable { AIRealRect bounds; std::wstring reason; };
struct TrackDeletion { bool vertical; int first; int count; };
struct NativeTable {
    table::TableModel model;
    AIDocumentHandle document=nullptr;
    AIArtHandle group=nullptr,frame=nullptr;
    AIRealPoint origin{};
    std::vector<NativeCell> cells;
    std::vector<AIArtHandle> edges;
    double scaleX=1,scaleY=1;
    std::optional<std::size_t> publishedStamp;
    ArtworkTransform transform()const{return {origin,scaleX,scaleY};}
};
class DocumentAdapter {
    Suites& suites;
    AIArtSuite* art;AIPathSuite* paths;AIDocumentSuite* document;AIUndoSuite* undo;AIDictionarySuite* dictionaries;AILayerSuite* layers;
    VectorArtwork vector;
    bool rootEditable(const NativeTable& table)const;
    void recordPublishedArtwork(NativeTable& table)const;
    void role(AIArtHandle object,const std::string& value);
    std::string role(AIArtHandle object)const;
    void writeModel(const NativeTable& table,const table::TableModel& model);
    NativeTable readTable(AIArtHandle group);
    std::map<table::CellId,std::shared_ptr<const table::TextLayout>> prepare(const table::TableModel& model,const NativeTable* previous);
    void styleEdges(const NativeTable& native,const table::TableModel& model);
    void apply(NativeTable& table,const table::TableModel& model,const char* label);
public:
    explicit DocumentAdapter(Suites& suites);
    NativeTable createTable(const table::TableModel& model,AIRealPoint origin);
    bool valid(const NativeTable& table)const;
    bool matchesPublishedArtwork(const NativeTable& table)const;
    void synchronize(NativeTable& table);
    std::vector<NativeTable> discoverTables(std::vector<RejectedTable>* rejected=nullptr);
    void applyGeometry(NativeTable& table,const table::TableModel& model);
    void applyStructure(NativeTable& table,const table::TableModel& model,std::optional<TrackDeletion> deletion=std::nullopt);
    void applyText(NativeTable& table,const table::TableModel& model);
    void applyStyle(NativeTable& table,table::CellRange range,const table::CellStyle& style);
    void applyStylePatch(NativeTable& table,table::CellRange range,const table::CellStylePatch& patch);
    bool overflows(const NativeCell& cell)const{return cell.layout&&cell.layout->overflows();}
    std::vector<FontChoice> installedFonts();
    void detach(const NativeTable& table);
};
