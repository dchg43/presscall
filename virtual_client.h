/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#ifndef PRESSCALL_ALL_IN_ONE_VIRTUAL_CLIENT_H_
#define PRESSCALL_ALL_IN_ONE_VIRTUAL_CLIENT_H_

#include <fcntl.h>

#include "tlib_cfg.h"

typedef struct {
  u_short l_onoff;
  u_short l_linger;
} LINGER;

class VirtualClient {
 public:
  VirtualClient() {
    m_isocket = -1;
    cookies = NULL;
  }
  virtual ~VirtualClient() {}

  virtual bool connectServer(TConfig* g_Config) = 0;
  virtual void disconnect() = 0;
  virtual bool isConnect() = 0;
  virtual int build_buffer(char* m_pszSendBuff, TConfig* g_Config) = 0;
  virtual bool reconnect() = 0;
  /** 返回0，没有读完；>0读完 */
  virtual int64_t isReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                                 int64_t& iPkgTheoryLen) = 0;
  /** 0:无响应 1:成功 -1:失败 */
  virtual int checkResponse(const char* recvData, int64_t unDataLen, int lastRead) = 0;
  virtual int readonce(char* pBuff, int64_t iLen) = 0;
  virtual int writeonce(char* pBuff, int64_t iLen) = 0;

  char* getErrMsg() {
    return m_szErrMsg;
  }
  char* getCookie() {
    return cookies;
  }
  void setCookie(char* setCookies) {
    cookies = setCookies;
  }
  void setTimeEnd(volatile bool* timeEnd) {
    isTimeEnd = timeEnd;
  }
  volatile bool* hasTimeEnd() {
    return isTimeEnd;
  }
  int getSocket() {
    return m_isocket;
  }
  void setServer(struct sockaddr* clientServer) {
    server = clientServer;
  }

  /** 0:响应为空 1:成功 2:有cookie -1:失败，响应不是200 */
  int tcpRead(char* pBuff, int64_t iLen);
  /** 返回写成功的大小 */
  int tcpWrite(char* pBuff, int64_t iLen);

  int build_http_buffer(char* m_pszSendBuff, TConfig* g_Config);
  int build_tcp_buffer(char* m_pszSendBuff, TConfig* g_Config);
  int64_t httpReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                           int64_t& iPkgTheoryLen);
  int64_t tcpReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                          int64_t& iPkgTheoryLen);
  int checkHttpResponse(const char* recvData, int64_t unDataLen, int lastRead);
  int checktcpResponse(int64_t unDataLen, int lastRead);

 protected:
  char m_szErrMsg[100];

 private:
  int m_isocket;
  volatile bool* isTimeEnd;
  char* cookies;
  struct sockaddr* server;
};

#endif  // PRESSCALL_ALL_IN_ONE_VIRTUAL_CLIENT_H_
