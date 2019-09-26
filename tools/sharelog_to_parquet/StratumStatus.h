#pragma once

//////////////////////////////// StratumStatus ////////////////////////////////
class StratumStatus {
public:
  enum {
    // make ACCEPT and SOLVED be two singular value,
    // so code bug is unlikely to make false ACCEPT shares

    // share reached the job target (but may not reached the network target)
    ACCEPT = 1798084231, // bin(01101011 00101100 10010110 10000111)

    // share reached the job target but the job is stale
    // if uncle block is allowed in the chain, share can be accept as this
    // status
    ACCEPT_STALE = 950395421, // bin(00111000 10100101 11100010 00011101)

    // share reached the network target
    SOLVED = 1422486894, // bin(‭01010100 11001001 01101101 01101110‬)

    // share reached the network target but the job is stale
    // if uncle block is allowed in the chain, share can be accept as this
    // status
    SOLVED_STALE = 1713984938, // bin(01100110 00101001 01010101 10101010)

    // share reached the network target but the correctness is not verified
    SOLVED_PRELIMINARY =
        1835617709, // // bin(01101101 01101001 01001101 10101101)

    REJECT_NO_REASON = 0,

    JOB_NOT_FOUND_OR_STALE = 21,
    DUPLICATE_SHARE = 22,
    LOW_DIFFICULTY = 23,
    UNAUTHORIZED = 24,
    NOT_SUBSCRIBED = 25,

    ILLEGAL_METHOD = 26,
    ILLEGAL_PARARMS = 27,
    IP_BANNED = 28,
    INVALID_USERNAME = 29,
    INTERNAL_ERROR = 30,
    TIME_TOO_OLD = 31,
    TIME_TOO_NEW = 32,
    ILLEGAL_VERMASK = 33,

    INVALID_SOLUTION = 34,
    WRONG_NONCE_PREFIX = 35,

    JOB_NOT_FOUND = 36,
    STALE_SHARE = 37,

    CLIENT_IS_NOT_SWITCHER = 400,

    UNKNOWN = 2147483647 // bin(01111111 11111111 11111111 11111111)
  };

  inline static bool isAccepted(int status) {
    return (status == ACCEPT) || (status == ACCEPT_STALE) ||
        (status == SOLVED) || (status == SOLVED_STALE);
  }

  inline static bool isAcceptedStale(int status) {
    return (status == ACCEPT_STALE) || (status == SOLVED_STALE);
  }

  inline static bool isRejectedStale(int status) {
    return (status == JOB_NOT_FOUND_OR_STALE) || (status == STALE_SHARE);
  }

  inline static bool isAnyStale(int status) {
    return isAcceptedStale(status) || isRejectedStale(status);
  }

  inline static bool isSolved(int status) {
    return (status == SOLVED) || (status == SOLVED_STALE) ||
        (status == SOLVED_PRELIMINARY);
  }
};
