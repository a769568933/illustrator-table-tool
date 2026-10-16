#pragma once
#include "table/TableModel.hpp"
#include <optional>

std::optional<table::TableModel> showCreateTableDialog();
struct FontChoice { std::u16string name, label; };
std::optional<table::CellStyle> showCellStyleDialog(const table::CellStyle& initial,const std::vector<FontChoice>& fonts);
