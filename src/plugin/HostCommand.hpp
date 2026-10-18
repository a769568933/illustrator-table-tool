#pragma once
#include "Suites.hpp"
#include <functional>
void invokeHostCommand(SPBasicSuite* basic,SPPluginRef plugin,const std::function<void(Suites&)>& operation);
bool hostCommandOwnsUndoContext();
