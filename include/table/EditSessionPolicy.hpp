#pragma once
#include "TextEditor.hpp"
#include <functional>

namespace table {
struct EditBinding {std::uintptr_t document=0,group=0;std::uint64_t generation=0;};
class BoundEditSession {
    EditBinding binding;
    EditSession value;
public:
    BoundEditSession(EditBinding identity,EditSession session):binding(identity),value(session){}
    bool matches(EditBinding current)const;
    const EditSession& selection()const{return value;}
    void select(EditSession next){value=next;}
    void commit(const EditChange& change,const std::function<void(const TableModel&)>& publish);
};
bool mayAppendTyping(bool plainInput,bool batchOpen,bool standalone,std::int32_t batchToken,std::int32_t previousToken,bool ownTag);
EditSession snapEditSelection(EditSession session,const std::vector<std::uint32_t>& boundaries);
}
