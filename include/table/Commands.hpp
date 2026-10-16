#pragma once
#include "TableModel.hpp"

namespace table {
enum class Command { Merge, Split, InsertRowAbove, InsertRowBelow, InsertColumnLeft,
    InsertColumnRight, DeleteRows, DeleteColumns };
TableModel applyCommand(const TableModel& source,CellRange selection,Command command);
}
