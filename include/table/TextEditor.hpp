#pragma once
#include "TableModel.hpp"
#include <string_view>

namespace table {
struct EditSession { CellId cell=0; std::uint32_t anchor=0,caret=0; std::uint64_t batch=0; };
struct EditChange { TableModel model; EditSession session; bool changed=false,addedRow=false; };
EditChange replaceSelection(const TableModel& model,const EditSession& session,std::u16string_view text);
EditChange eraseCluster(const TableModel& model,const EditSession& session,const std::vector<std::uint32_t>& boundaries,bool backward);
EditChange navigateCell(const TableModel& model,const EditSession& session,bool backward);
}
