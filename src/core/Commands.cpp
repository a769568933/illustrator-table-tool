#include "table/Commands.hpp"
#include <algorithm>
#include <set>

namespace table {
TableModel applyCommand(const TableModel& source,CellRange selection,Command command) {
    if(selection.firstRow>selection.lastRow) std::swap(selection.firstRow,selection.lastRow);
    if(selection.firstColumn>selection.lastColumn) std::swap(selection.firstColumn,selection.lastColumn);
    source.cellAt(selection.firstRow,selection.firstColumn);source.cellAt(selection.lastRow,selection.lastColumn);
    auto model=source;
    switch(command) {
    case Command::Merge:model.merge(selection);break;
    case Command::Split: {
        std::set<CellId> ids;
        for(int r=selection.firstRow;r<=selection.lastRow;++r)
            for(int c=selection.firstColumn;c<=selection.lastColumn;++c) ids.insert(model.cellAt(r,c).id);
        for(auto id:ids) model.split(id);break;
    }
    case Command::InsertRowAbove:model.insertRows(selection.firstRow,1);break;
    case Command::InsertRowBelow:model.insertRows(selection.lastRow+1,1);break;
    case Command::InsertColumnLeft:model.insertColumns(selection.firstColumn,1);break;
    case Command::InsertColumnRight:model.insertColumns(selection.lastColumn+1,1);break;
    case Command::DeleteRows:model.eraseRows(selection.firstRow,selection.lastRow-selection.firstRow+1);break;
    case Command::DeleteColumns:model.eraseColumns(selection.firstColumn,selection.lastColumn-selection.firstColumn+1);break;
    case Command::DistributeRows:model.distributeTracks(false,selection.firstRow,selection.lastRow-selection.firstRow+1);break;
    case Command::DistributeColumns:model.distributeTracks(true,selection.firstColumn,selection.lastColumn-selection.firstColumn+1);break;
    default:throw ValidationError("Unknown table command");
    }
    return model;
}
}
