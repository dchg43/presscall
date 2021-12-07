/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#ifndef PRESSCALL_ALL_IN_ONE_PRESSCALL_H_
#define PRESSCALL_ALL_IN_ONE_PRESSCALL_H_

#include <pthread.h>
#include <stdint.h>

struct TResult {
  int64_t iAllReqNum;
  int64_t iOkResponseNum;
  int64_t iNoResponseNum;
  int64_t iBadRespNum;
  int64_t iConnFailNum;
  __int128_t dSumRspTimeUs;  // gcc4.6以下用double代替。不支持printf，可转为int64_t再printf
  int64_t llMaxRspTimeUs;
  bool resetMaxTime;
  int64_t m_iTimeL1Num;
  int64_t m_iTimeL2Num;
  int64_t m_iTimeL3Num;
  int64_t m_iTimeL4Num;
};

struct ThreadArray {
  int thread_id;
  pthread_t thread_pid;
  int64_t call_numbers;
};

#endif  // PRESSCALL_ALL_IN_ONE_PRESSCALL_H_
