#include "HostCommand.hpp"
#include "AIContext.h"
#include <exception>

namespace {thread_local bool ownsUndo=false;}
bool hostCommandOwnsUndoContext(){return ownsUndo;}
void invokeHostCommand(SPBasicSuite* basic,SPPluginRef plugin,const std::function<void(Suites&)>& operation){
    Suites suites(basic);sSPBlocks=suites.get<SPBlocksSuite>(kSPBlocksSuite,kSPBlocksSuiteVersion);
    sAIUnicodeString=suites.get<AIUnicodeStringSuite>(kAIUnicodeStringSuite,kAIUnicodeStringVersion);
    auto context=suites.get<AIAppContextSuite>(kAIAppContextSuite,kAIAppContextVersion);
    const bool previous=ownsUndo,standalone=!context->DoesAppContextExist();AIAppContextHandle scope=nullptr;
    checked(context->PushAppContext(plugin,&scope));ownsUndo=standalone;
    std::exception_ptr failure;try{operation(suites);}catch(...){failure=std::current_exception();}
    const auto close=context->PopAppContext(scope);ownsUndo=previous;
    if(failure)std::rethrow_exception(failure);checked(close);
}
