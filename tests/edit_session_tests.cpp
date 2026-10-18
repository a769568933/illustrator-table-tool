#include "table/EditSessionPolicy.hpp"
#include <iostream>
#include <stdexcept>
using namespace table;
namespace {int checks=0;void expect(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}}
int main(){try{
    BoundEditSession session({10,20,1},{1,0,0,0});
    expect(session.matches({10,20,1}),"current session valid");
    expect(!session.matches({11,20,1})&&!session.matches({10,21,1})&&!session.matches({10,20,2}),"foreign or stale input session refused");
    expect(!BoundEditSession({0,0,0},{}).matches({0,0,0}),"null document session refused");
    auto model=TableModel::create(1,1,100,40);auto changed=replaceSelection(model,{1,0,0,0},u"中");
    bool failed=false;try{session.commit(changed,[](const TableModel&){throw ValidationError("SDK failed");});}catch(const ValidationError&){failed=true;}
    expect(failed&&session.selection().caret==0,"failed publication keeps original selection");
    std::u16string published;session.commit(changed,[&](const TableModel& m){published=m.cellById(1).text;});
    expect(published==u"中"&&session.selection().caret==1,"selection accepted after successful publication");
    expect(mayAppendTyping(true,true,true,3,3,true),"same own typing batch can append");
    expect(!mayAppendTyping(true,true,true,3,4,true),"new transaction breaks typing batch");
    expect(!mayAppendTyping(true,true,true,3,3,false),"other plugin transaction never appended");
    expect(!mayAppendTyping(true,true,false,3,3,true),"nested context never retagged");
    expect(!mayAppendTyping(false,true,true,3,3,true),"paste and IME commits have own undo");
    expect(!mayAppendTyping(true,false,true,3,3,true),"navigation breaks batch");
    std::cout<<checks<<" session policy checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
