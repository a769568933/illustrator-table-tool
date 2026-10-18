#include "table/EditSessionPolicy.hpp"
#include <algorithm>

namespace table {
bool BoundEditSession::matches(EditBinding current)const{
    return binding.document&&binding.group&&binding.document==current.document&&binding.group==current.group&&binding.generation==current.generation;
}
void BoundEditSession::commit(const EditChange& change,const std::function<void(const TableModel&)>& publish){
    if(change.changed)publish(change.model);
    value=change.session;
}
bool mayAppendTyping(bool plainInput,bool batchOpen,bool standalone,std::int32_t batchToken,std::int32_t previousToken,bool ownTag){
    return plainInput&&batchOpen&&standalone&&batchToken>0&&batchToken==previousToken&&ownTag;
}
EditSession snapEditSelection(EditSession session,const std::vector<std::uint32_t>& boundaries){
    if(boundaries.empty()||boundaries.front()!=0||!std::is_sorted(boundaries.begin(),boundaries.end())||
       std::adjacent_find(boundaries.begin(),boundaries.end())!=boundaries.end())throw ValidationError("Invalid caret boundaries");
    auto ceiling=[&](std::uint32_t position){const auto stop=std::lower_bound(boundaries.begin(),boundaries.end(),position);return stop==boundaries.end()?boundaries.back():*stop;};
    auto floor=[&](std::uint32_t position){const auto stop=std::upper_bound(boundaries.begin(),boundaries.end(),position);return stop==boundaries.begin()?boundaries.front():*(stop-1);};
    if(session.anchor==session.caret)session.anchor=session.caret=ceiling(session.caret);
    else if(session.anchor<session.caret){session.anchor=floor(session.anchor);session.caret=ceiling(session.caret);}
    else{session.anchor=ceiling(session.anchor);session.caret=floor(session.caret);}
    return session;
}
}
