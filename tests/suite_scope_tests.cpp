#include "Suites.hpp"
#include <iostream>

extern "C" { AIUnicodeStringSuite* sAIUnicodeString=nullptr;SPBlocksSuite* sSPBlocks=nullptr; }
int main() {
    SPBasicSuite basic{};AIUnicodeStringSuite outerUnicode{},innerUnicode{};SPBlocksSuite outerBlocks{},innerBlocks{};
    {
        Suites outer(&basic);sAIUnicodeString=&outerUnicode;sSPBlocks=&outerBlocks;
        {Suites inner(&basic);sAIUnicodeString=&innerUnicode;sSPBlocks=&innerBlocks;}
        if(sAIUnicodeString!=&outerUnicode||sSPBlocks!=&outerBlocks) {
            std::cerr<<"Nested SDK callback destroyed outer suite globals\n";return 1;
        }
    }
    if(sAIUnicodeString||sSPBlocks) {std::cerr<<"Root suite globals leaked\n";return 1;}
    std::cout<<"Nested SDK suite scope checks passed\n";
}
