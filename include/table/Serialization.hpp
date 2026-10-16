#pragma once
#include "TableModel.hpp"
#include <string_view>

namespace table {
std::string serialize(const TableModel& model);
TableModel deserialize(std::string_view data);
}
