/**
  @Copyright (c) 2019, chaoguo All rights reserved.
*/
#ifndef PRESSCALL_ALL_IN_ONE_HTTP_CLIENT_H_
#define PRESSCALL_ALL_IN_ONE_HTTP_CLIENT_H_

#include "virtual_client.h"

void build_http_buffer(TConfig* g_Config);

class HttpClient : public VirtualClient {
 public:
  HttpClient() {
    m_Config = NULL;
  }
  ~HttpClient() {
    disconnect();
  }

  int build_buffer(char* m_pszSendBuff, TConfig* g_Config);
  bool connectServer(TConfig* g_Config);
  void disconnect();
  bool reconnect();
  bool isConnect();
  int64_t isReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                         int64_t iPkgTheoryLen);
  int checkResponse(const char* recvData, int64_t unDataLen, int lastRead);
  int readonce(char* pBuff, int64_t iLen);
  int writeonce(char* pBuff, int64_t iLen);

 private:
  TConfig* m_Config;
};

#endif  // PRESSCALL_ALL_IN_ONE_HTTP_CLIENT_H_
