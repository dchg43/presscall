/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#ifndef PRESSCALL_ALL_IN_ONE_PRESSCALL_H_
#define PRESSCALL_ALL_IN_ONE_PRESSCALL_H_

#include <pthread.h>
#include <stdint.h>

struct TResult {
  uint64_t iAllReqNum;
  uint64_t iOkResponseNum;
  uint64_t iNoResponseNum;
  uint64_t iBadResponseNum;
  __uint128_t dSumRspTimeUs;  // gcc4.6以下用double代替。不支持printf，可转为uint64_t再printf
  int64_t llMaxRspTimeUs;
  bool resetMaxTime;
  uint64_t m_iTimeL1Num;
  uint64_t m_iTimeL2Num;
  uint64_t m_iTimeL3Num;
  uint64_t m_iTimeL4Num;
};

struct ThreadArray {
  int thread_id;
  pthread_t thread_pid;
  volatile int64_t call_numbers;
};

#endif  // PRESSCALL_ALL_IN_ONE_PRESSCALL_H_
