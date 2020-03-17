#include "StatsHttpdTellor.h"

///////////////  template instantiation ///////////////
// Without this, some linking errors will issued.
// If you add a new derived class of Share, add it at the following.
template class WorkerShares<ShareTellor>;
template class StatsServerT<ShareTellor>;