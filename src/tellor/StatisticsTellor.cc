#include "StatisticsTellor.h"
#include "Utils.h"
#include "utilities_js.hpp"

template <>
double ShareStatsDay<ShareTellor>::getShareReward(const ShareTellor &share) {
  return 5.0;
}

template class ShareStatsDay<ShareTellor>;
