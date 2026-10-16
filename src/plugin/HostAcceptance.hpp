#pragma once
#include "Suites.hpp"
#include "AIDocument.h"
// Compiled only into the explicitly enabled development acceptance build.
void runHostAcceptance(Suites& suites,bool interactive=true,AIDocumentHandle* createdDocument=nullptr);
bool hasAutoHostAcceptanceRequest();
// Returns true only when no user document was open and all test documents were closed.
bool runAutoHostAcceptance(Suites& suites);
