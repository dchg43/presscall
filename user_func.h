/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#ifndef PRESSCALL_ALL_IN_ONE_USER_FUNC_H_
#define PRESSCALL_ALL_IN_ONE_USER_FUNC_H_

#include "tlib_cfg.h"
#include "virtual_client.h"

class CUserFunc {
 public:
  CUserFunc(int iMyID, TConfig* g_Config);
  CUserFunc(const CUserFunc& src);
  ~CUserFunc();
  CUserFunc& operator=(const CUserFunc&) {
    return *this;
  }
  int64_t DoOnce(void);
  void setTimeEnd(volatile bool* timeEnd);

 private:
  int m_iMyID;
  int m_lenSendBuff;
  TConfig* m_Config;
  VirtualClient* m_TcpCltSocket;
  char* m_pszSendBuff;
  char* m_pszRecvBuff;
};

int usSleep(int uSec);
void initAhead(TConfig* g_Config);
void destroyEnd(TConfig* g_Config);

#endif  // PRESSCALL_ALL_IN_ONE_USER_FUNC_H_
