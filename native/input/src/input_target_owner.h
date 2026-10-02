#pragma once
namespace hvinput {
// Caller has established GPU completion before retiring the old view.
// Failed creation outputs are not owned, even if the API writes a non-null value.
template<class Handle,class Destroy,class Create>
bool ReplaceTargetView(Handle& owned,Destroy destroy,Create create) {
 if(owned){destroy(owned);owned=Handle{};}
 Handle candidate{};
 if(!create(candidate))return false;
 owned=candidate;return true;
}
}
