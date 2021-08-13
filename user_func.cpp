/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#include "user_func.h"

#include <arpa/inet.h>

#include <string>

#include "http_client.h"
#include "https_client.h"
#include "tcp_client.h"

int usSleep(int uSec) {
  // usleep(1000);
  struct timeval timeout;
  timeout.tv_sec = uSec / 1000000;
  timeout.tv_usec = uSec % 1000000;
  // select -1出错，0超时，1监听成功
  // int ret = 0;
  // do {
  //    ret = select(0, NULL, NULL, NULL, &timeout);
  // } while(ret<0 && errno==EINTR);
  // select需要响应ctrl+C中断，所以不判断返回值
  return select(0, NULL, NULL, NULL, &timeout);
}

void initAhead(TConfig* g_Config) {
  if (g_Config->m_test_mode == EN_HTTPS) {
    initSSL(g_Config);
  }
}

void destroyEnd(TConfig* g_Config) {
  if (g_Config->m_test_mode == EN_HTTPS) {
    cleanupSSL();
  }
}

CUserFunc::CUserFunc(int iMyID, TConfig* g_Config) {
  m_iMyID = iMyID;
  m_Config = g_Config;
  m_pszRecvBuff = new char[m_Config->m_iRecvLen + 2];
  m_pszRecvBuff[m_Config->m_iRecvLen] = '\0';

  if (m_Config->m_test_mode == EN_CS) {
    m_TcpCltSocket = new TcpClient();
  } else if (m_Config->m_test_mode == EN_HTTPS) {
    m_TcpCltSocket = new HttpsClient();
  } else {
    m_TcpCltSocket = new HttpClient();  // default send http message
  }

  m_pszSendBuff = new char[m_Config->m_iLen + MAX_HEADER_LEN + 1];
  m_lenSendBuff = m_TcpCltSocket->build_buffer(m_pszSendBuff, m_Config);

  // 初始化vip、vport
  int destPort = g_Config->m_iDestPort;
  if (g_Config->m_iUseDiffPort != 0) {
    destPort += iMyID;
  }

  std::string str_szDestIp = g_Config->m_szDestIp;
  if (str_szDestIp.find(":", 0) != std::string::npos) {
    struct in6_addr m_v6_dest;
    if (inet_pton(AF_INET6, g_Config->m_szDestIp, &m_v6_dest) <= 0) {
      printf("V6Host is incorrect: %s\n", g_Config->m_szDestIp);
      fflush(stdout);
      exit(2);
    }

    struct sockaddr_in6* server6 = new sockaddr_in6();
    server6->sin6_family = AF_INET6;
    server6->sin6_port = htons(destPort);
    memcpy(&server6->sin6_addr, (struct in6_addr*)&m_v6_dest, sizeof(m_v6_dest));
    m_TcpCltSocket->setServer((struct sockaddr*)server6);
  } else {
    int m_iDestIp = inet_addr(g_Config->m_szDestIp);

    struct sockaddr_in* server4 = new sockaddr_in();
    server4->sin_family = AF_INET;
    server4->sin_port = htons(destPort);
    server4->sin_addr.s_addr = m_iDestIp;
    m_TcpCltSocket->setServer((struct sockaddr*)server4);
  }
}

void CUserFunc::setTimeEnd(volatile bool* timeEnd) {
  m_TcpCltSocket->setTimeEnd(timeEnd);
}

CUserFunc::~CUserFunc() {
  delete m_TcpCltSocket;
  delete[] m_pszSendBuff;
  delete[] m_pszRecvBuff;
}

/** 执行一次请求。0:无响应 >=1:成功 -1:失败 -2:连接不成功 */
int64_t CUserFunc::DoOnce(void) {
  if (!m_TcpCltSocket->isConnect()) {
    if (m_TcpCltSocket->connectServer(m_Config) != true) {
      if (m_Config->m_iPrintError && !(*m_TcpCltSocket->hasTimeEnd())) {
        snprintf(
            m_pszRecvBuff, m_Config->m_iRecvLen, "connect to %s:%d failed: %s.\n",
            m_Config->m_szDestIp,
            m_Config->m_iUseDiffPort == 0 ? m_Config->m_iDestPort : m_Config->m_iDestPort + m_iMyID,
            m_TcpCltSocket->getErrMsg());
        fwrite(m_pszRecvBuff, strlen(m_pszRecvBuff), 1, m_Config->errLogOut);
      }
      usSleep(100000);  // 连接失败sleep 100ms, 防止反复重连
      return -2;
    }
  }
  int64_t tBegin = getCurrentTimeUs();

  if (m_lenSendBuff != m_TcpCltSocket->tcpWrite(m_pszSendBuff, m_lenSendBuff)) {
    if (m_Config->m_iPrintError && !(*m_TcpCltSocket->hasTimeEnd())) {
      snprintf(m_pszRecvBuff, m_Config->m_iRecvLen, "write error: %s.\n",
               m_TcpCltSocket->getErrMsg());
      fwrite(m_pszRecvBuff, strlen(m_pszRecvBuff), 1, m_Config->errLogOut);
    }
    return -1;
  }

  int result = m_TcpCltSocket->tcpRead(m_pszRecvBuff, m_Config->m_iRecvLen);

  int64_t llRspTimeUs = getCurrentTimeUs() - tBegin;

  if (!m_Config->m_iLongConn) {
    m_TcpCltSocket->disconnect();
  }

  if (result > 0) {
    if (result == 2) {
      if (m_Config->m_iPrintError) {
        snprintf(m_pszRecvBuff, m_Config->m_iRecvLen, "Cookie changed, new cookie: %s\n",
                 m_TcpCltSocket->getCookie());
        fwrite(m_pszRecvBuff, strlen(m_pszRecvBuff), 1, m_Config->errLogOut);
      }
      m_lenSendBuff = m_TcpCltSocket->build_buffer(m_pszSendBuff, m_Config);
    }
    return llRspTimeUs;
  } else if (result == -1 && m_Config->m_iPrintError) {
    //    && !(*m_TcpCltSocket->hasTimeEnd()) {
    int readlen = strlen(m_pszRecvBuff);
    if (readlen > 0) {
      int errlen = strlen(m_TcpCltSocket->getErrMsg());
      if (readlen + errlen + 2 > m_Config->m_iRecvLen) {
        snprintf(m_pszRecvBuff + readlen - errlen - 2, m_Config->m_iRecvLen, "\n%s\n",
                 m_TcpCltSocket->getErrMsg());
      } else {
        snprintf(m_pszRecvBuff + readlen, m_Config->m_iRecvLen - readlen, "\n%s\n",
                 m_TcpCltSocket->getErrMsg());
      }
      fwrite(m_pszRecvBuff, strlen(m_pszRecvBuff), 1, m_Config->errLogOut);
    } else {
      snprintf(m_pszRecvBuff, m_Config->m_iRecvLen, "read error: %s.\n",
               m_TcpCltSocket->getErrMsg());
      fwrite(m_pszRecvBuff, strlen(m_pszRecvBuff), 1, m_Config->errLogOut);
    }
  }
  return result;
}
