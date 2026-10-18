#include "HostCommand.hpp"
#include "AIContext.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
extern "C" {AIUnicodeStringSuite* sAIUnicodeString=nullptr;SPBlocksSuite* sSPBlocks=nullptr;}
namespace {
AIAppContextSuite context{};AIUnicodeStringSuite unicode{};SPBlocksSuite blocks{};
bool present=false;std::vector<bool> previous;
AIBoolean AIAPI exists(){return present;}
AIErr AIAPI push(SPPluginRef,AIAppContextHandle* out){previous.push_back(present);present=true;*out=reinterpret_cast<AIAppContextHandle>(previous.size());return kNoErr;}
AIErr AIAPI pop(AIAppContextHandle handle){if(reinterpret_cast<std::uintptr_t>(handle)!=previous.size())return kBadParameterErr;present=previous.back();previous.pop_back();return kNoErr;}
SPErr SPAPI acquire(const char* name,ai::int32,const void** result){
    if(!std::strcmp(name,kAIAppContextSuite))*result=&context;
    else if(!std::strcmp(name,kAIUnicodeStringSuite))*result=&unicode;
    else if(!std::strcmp(name,kSPBlocksSuite))*result=&blocks;
    else return kBadParameterErr;return kNoErr;
}
SPErr SPAPI release(const char*,ai::int32){return kNoErr;}
void expect(bool condition,const char* name){if(!condition)throw std::runtime_error(name);}
}
int main(){try{
    context.DoesAppContextExist=exists;context.PushAppContext=push;context.PopAppContext=pop;
    SPBasicSuite basic{};basic.AcquireSuite=acquire;basic.ReleaseSuite=release;const auto plugin=reinterpret_cast<SPPluginRef>(1);
    invokeHostCommand(&basic,plugin,[&](Suites&){
        expect(present&&hostCommandOwnsUndoContext(),"OS callback has own SDK context");
        invokeHostCommand(&basic,plugin,[&](Suites&){expect(present&&!hostCommandOwnsUndoContext(),"nested callback never changes undo kind");});
        expect(hostCommandOwnsUndoContext(),"outer command state restored");
    });
    expect(!present&&!sAIUnicodeString&&!sSPBlocks,"context and global suites released");
    bool caught=false;try{invokeHostCommand(&basic,plugin,[&](Suites&){throw std::runtime_error("failure");});}catch(const std::runtime_error&){caught=true;}
    expect(caught&&!present&&previous.empty(),"exception unwinds context");
    present=true;invokeHostCommand(&basic,plugin,[&](Suites&){expect(!hostCommandOwnsUndoContext(),"existing host context remains separate");});
    expect(present&&previous.empty(),"preexisting context preserved");
    std::cout<<"Host context, nested callbacks and exception cleanup passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
