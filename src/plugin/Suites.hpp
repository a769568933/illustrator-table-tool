#pragma once
#include "SPBasic.h"
#include "SPBlocks.h"
#include "AIUnicodeString.h"
#include "IAIUnicodeString.h"
#include <vector>

extern "C" { extern AIUnicodeStringSuite* sAIUnicodeString; extern SPBlocksSuite* sSPBlocks; }
inline void checked(ASErr error) { if(error) throw error; }
class Suites {
    SPBasicSuite* basic;
    AIUnicodeStringSuite* previousUnicode;
    SPBlocksSuite* previousBlocks;
    struct Acquired { const char* name; ai::int32 version; };
    std::vector<Acquired> acquired;
public:
    explicit Suites(SPBasicSuite* value) : basic(value),previousUnicode(sAIUnicodeString),previousBlocks(sSPBlocks) {}
    ~Suites() {
        for(auto it=acquired.rbegin();it!=acquired.rend();++it) basic->ReleaseSuite(it->name,it->version);
        sAIUnicodeString=previousUnicode; sSPBlocks=previousBlocks;
    }
    template<class T> T* get(const char* name,ai::int32 version) {
        const void* result=nullptr;
        checked(basic->AcquireSuite(name,version,&result));
        if(!result) throw static_cast<ASErr>(kCantHappenErr);
        acquired.push_back({name,version});
        return const_cast<T*>(static_cast<const T*>(result));
    }
};
