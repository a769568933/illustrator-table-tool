#include "table/EditSessionPolicy.hpp"

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
}
