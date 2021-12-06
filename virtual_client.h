/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#ifndef PRESSCALL_ALL_IN_ONE_VIRTUAL_CLIENT_H_
#define PRESSCALL_ALL_IN_ONE_VIRTUAL_CLIENT_H_

#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "tlib_cfg.h"

struct LINGER {
  u_short l_onoff;
  u_short l_linger;
};

class VirtualClient {
 public:
  VirtualClient() {
    memset(m_szErrMsg, 0, sizeof(m_szErrMsg));
    m_isocket = -1;
    isTimeEnd = NULL;
    cookies = NULL;
    server = NULL;
  }
  virtual ~VirtualClient() {
    if (cookies != NULL) {
      delete[] cookies;
    }
    if (server != NULL) {
      delete server;
    }
  }

  virtual bool connectServer(TConfig* g_Config) = 0;
  virtual void disconnect() = 0;
  virtual bool isConnect() = 0;
  virtual int build_buffer(char* m_pszSendBuff, TConfig* g_Config) = 0;
  virtual bool reconnect() = 0;
  /** 返回0，没有读完；>0读完 */
  virtual int64_t isReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                                 int64_t iPkgTheoryLen) = 0;
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
    if (cookies != NULL) {
      delete[] cookies;
    }
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
  void setServer(char* ip, int port) {
    if (server != NULL) {
      delete server;
    }

    if (strstr(ip, ":") != NULL) {
      struct sockaddr_in6* server6 = new sockaddr_in6();
      server6->sin6_family = AF_INET6;
      server6->sin6_port = htons(port);
      if (inet_pton(AF_INET6, ip, &server6->sin6_addr) <= 0) {
        printf("V6Host is incorrect: %s\n", ip);
        fflush(stdout);
        exit(2);
      }
      server = (struct sockaddr*)server6;
    } else {
      struct sockaddr_in* server4 = new sockaddr_in();
      server4->sin_family = AF_INET;
      server4->sin_port = htons(port);
      server4->sin_addr.s_addr = inet_addr(ip);
      server = (struct sockaddr*)server4;
    }
  }

  /** 0:响应为空 1:成功 2:有cookie -1:失败，响应不是200 */
  int tcpRead(char* pBuff, int64_t iLen);
  /** 返回写成功的大小 */
  int tcpWrite(char* pBuff, int64_t iLen);

  int build_http_buffer(char* m_pszSendBuff, TConfig* g_Config);
  int build_tcp_buffer(char* m_pszSendBuff, TConfig* g_Config);
  int64_t httpReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                           int64_t iPkgTheoryLen);
  int64_t tcpReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                          int64_t iPkgTheoryLen);
  int checkHttpResponse(const char* recvData, int64_t unDataLen, int lastRead);
  int checktcpResponse(int64_t unDataLen, int lastRead);

 protected:
  char m_szErrMsg[256];

 private:
  int m_isocket;
  volatile bool* isTimeEnd;
  char* cookies;
  struct sockaddr* server;
  VirtualClient(const VirtualClient&) {
    /* do not create copies */
  }
  VirtualClient& operator=(const VirtualClient&) {
    /* do not create copies */
    return *this;
  }
};

#endif  // PRESSCALL_ALL_IN_ONE_VIRTUAL_CLIENT_H_
