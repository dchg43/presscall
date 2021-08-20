/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#ifndef PRESSCALL_ALL_IN_ONE_HTTPS_CLIENT_H_
#define PRESSCALL_ALL_IN_ONE_HTTPS_CLIENT_H_

#include <openssl/ssl.h>
#include <string.h>

#include "virtual_client.h"

void initSSL(TConfig* g_Config);
void cleanupSSL();
void build_https_buffer(TConfig* g_Config);

class HttpsClient : public VirtualClient {
 public:
  HttpsClient() {
    memset(m_szErrMsg, 0, sizeof(m_szErrMsg));
    ssl = NULL;
  }
  ~HttpsClient() {
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
  SSL* ssl;
  TConfig* m_Config;
};

#endif  // PRESSCALL_ALL_IN_ONE_HTTPS_CLIENT_H_
