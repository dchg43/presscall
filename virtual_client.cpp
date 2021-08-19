/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#include "virtual_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int VirtualClient::build_http_buffer(char* m_pszSendBuff, TConfig* g_Config) {
  char* tmp = new char[g_Config->m_iLen + 1];

  const char CCH[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_,.";
  // srand((unsigned)time(NULL));
  int i;
  for (i = 0; i < g_Config->m_iLen; i++) {
    tmp[i] = CCH[rand() % (sizeof(CCH) - 1)];
  }
  tmp[i] = '\0';

  /* Build a http request. */
  snprintf(m_pszSendBuff, g_Config->m_iLen + MAX_HEADER_LEN,
           "%s %s HTTP/1.1\r\n"
           "Accept: */*\r\n"
           "Connection: %s\r\n"
           "debug_uri: 600\r\n"
           "scf-etag: presscall etag\r\n"
           "Host: %s\r\n",
           g_Config->m_szMethod, g_Config->m_pszGetFile,
           (g_Config->m_iLongConn) ? "keep-alive" : "close", g_Config->m_pszHost);
  if (cookies != NULL) {
    snprintf(m_pszSendBuff + strlen(m_pszSendBuff),
             g_Config->m_iLen + MAX_HEADER_LEN - strlen(m_pszSendBuff), "Cookie: %s;\r\n", cookies);
  }
  if (strcasecmp(g_Config->m_szMethod, "POST") == 0) {
    snprintf(m_pszSendBuff + strlen(m_pszSendBuff),
             g_Config->m_iLen + MAX_HEADER_LEN - strlen(m_pszSendBuff),
             "Content-Length: %d\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "\r\n"
             "%s",
             g_Config->m_iLen, tmp);
  } else {
    snprintf(m_pszSendBuff + strlen(m_pszSendBuff),
             g_Config->m_iLen + MAX_HEADER_LEN - strlen(m_pszSendBuff),
             "XXXSize: %d\r\n"
             "XXXLine: %s\r\n"
             "\r\n",
             g_Config->m_iLen, tmp);
  }

  delete[] tmp;
  return strlen(m_pszSendBuff);
}

int VirtualClient::build_tcp_buffer(char* m_pszSendBuff, TConfig* g_Config) {
  char* tmp = m_pszSendBuff;
  int total_len = g_Config->m_iLen + 8;

  *tmp = htonl(0x4E534153);
  *(tmp + sizeof(int)) = htonl(total_len);

  tmp += 8;

  const char CCH[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_,.";
  // srand((unsigned)time(NULL));
  int i;
  for (i = 0; i < g_Config->m_iLen; i++) {
    tmp[i] = CCH[rand() % (sizeof(CCH) - 1)];
  }
  tmp[i] = '\0';

  return total_len;
}

/** 返回0，没有读完；>0读完 */
int64_t VirtualClient::httpReadComplete(const char* pData, int64_t unDataLen,
                                        int64_t iReceivLenInBuff, int64_t& iPkgTheoryLen) {
  if (iPkgTheoryLen < 0) {
    // no head, or head not enough
    const char* pHeadEnd = strstr(pData, "\r\n\r\n");
    // 因为buff没有加\0结束符，查找到的内容有可能超过已读取的长度
    if ((pHeadEnd == NULL) || (pHeadEnd - pData + 4 > iReceivLenInBuff)) {
      return 0;
    }

    pHeadEnd += 4;  // +len("\r\n\r\n")
    int64_t iHeadLen = pHeadEnd - pData;

    const char* pContentLength = strstr(pData, "Content-Length:");
    if ((pContentLength == NULL) || (pContentLength > pHeadEnd)) {
      const char* pTransfer = strstr(pData, "Transfer-Encoding:");
      // no body, return head
      if ((pTransfer == NULL) || (pTransfer > pHeadEnd)) {
        iPkgTheoryLen = iHeadLen;
        return iHeadLen;
      }

      // chunked model
      const char* pchunkEnd = pData + iReceivLenInBuff - 10;
      pchunkEnd = strstr(pchunkEnd, "\r\n0\r\n\r\n");
      // 因为buff没有加\0结束符，查找到的内容有可能超过已读取的长度
      if (pchunkEnd == NULL || pchunkEnd - pData + 7 > iReceivLenInBuff) {
        return 0;
      }
      return unDataLen;
    }

    // Content-Length model
    int64_t iBodyLen = unDataLen - iHeadLen;
    int64_t iContentLength = atoll(pContentLength + 15);  // +len("Content-Length:")
    if (iBodyLen < iContentLength) {
      iPkgTheoryLen = iHeadLen + iContentLength;
      return 0;
    } else {
      return unDataLen;
    }
  } else {
    if (unDataLen < iPkgTheoryLen) {
      return 0;
    } else {
      return unDataLen;
    }
  }
}

/** 返回0，没有读完；>0读完 */
int64_t VirtualClient::tcpReadComplete(const char* pData, int64_t unDataLen,
                                       int64_t iReceivLenInBuff, int64_t& iPkgTheoryLen) {
  if (unDataLen < static_cast<int>(sizeof(int)) * 2)
    return 0;

  // 没有长度头，认为已经读完
  int64_t iMsgTag = ntohl(*pData);
  if (iMsgTag != 0x4E534153) {  // SASN ,equ(=) fast than memcmp
    return unDataLen;
  }

  // 有长度头，根据头判断是否读完
  int64_t iMsgLen = ntohl(*pData + 1);
  iPkgTheoryLen = iMsgLen;

  if (iPkgTheoryLen <= unDataLen) {
    return iPkgTheoryLen;
  }

  return 0;
}

int VirtualClient::checkHttpResponse(const char* m_pszRecvBuff, int64_t unDataLen, int lastRead) {
  if (unDataLen == 0) {
    return 0;
  }
  const char* p = strchr(m_pszRecvBuff, ' ');
  // 检查http响应码是否是200/300(202都可以，小于400)
  if (p && strstr(m_pszRecvBuff, "HTTP") && atoi(p + 1) < 400) {
    const char* pCookie = strstr(m_pszRecvBuff, "Set-Cookie: ");
    if (pCookie) {
      pCookie = pCookie + strlen("Set-Cookie: ");
      const char* pCookieEnd = strchr(pCookie, ';');
      if (pCookieEnd) {
        char* localcookies = getCookie();
        if (localcookies == NULL)
          localcookies = new char[pCookieEnd - pCookie + 1];
        memcpy(localcookies, pCookie, pCookieEnd - pCookie);
        localcookies[pCookieEnd - pCookie] = '\0';
        setCookie(localcookies);
        return 2;
      }
    }
    return 1;
  } else {
    getErrMsg()[0] = '\0';
    return -1;
  }
}

int VirtualClient::checktcpResponse(int64_t unDataLen, int lastRead) {
  if (unDataLen == 0) {
    return 0;
  } else if (lastRead >= 0) {
    return 1;
  } else {
    getErrMsg()[0] = '\0';
    return -1;
  }
}

bool VirtualClient::connectServer(TConfig* g_Config) {
  if (m_isocket >= 0) {
    return true;
  }

  disconnect();

  // create
  if ((m_isocket = socket(server->sa_family, SOCK_STREAM, 0)) < 0) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "socket: %s[%d]", strerror(errno), __LINE__);
    return false;
  }

  // l_onoff为0。则马上关闭socket(graceful)，closesocket马上返回。并尽量在后台将内核发送缓冲区的数据发出去。这种情况正常四次挥手，但是会time_wait。
  // l_onoff非0，l_linger为0。closesocket马上返回(abortive)，连接重置，发送RST到对端，并且丢弃内核发送缓冲区中的数据。这种情况非正常四次挥手，不会time_wait。
  // l_onoff非0，l_linger非0。这种情况又分为阻塞和非阻塞。
  //   对于阻塞socket，则延迟l_linger秒关闭socket，直到发完数据或超时。超时则连接重置，发送RST到对端(abortive)，发完则是正常关闭(graceful)。会time_wait
  //   对于非阻塞socket，如果closesocket不能立即完成，则马上返回错误WSAEWOULDBLOCK。
  struct linger m_sLinger;
  m_sLinger.l_onoff = 1;
  m_sLinger.l_linger = g_Config->m_iLingerTime;
  int res = setsockopt(m_isocket, SOL_SOCKET, SO_LINGER, &m_sLinger, sizeof(linger));
  if (res < 0) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "set SO_LINGER: %s[%d]", strerror(errno), __LINE__);
  }

  // 设置地址重用
  int option_on = 1;
  res = setsockopt(m_isocket, SOL_SOCKET, SO_REUSEADDR, &option_on, sizeof(option_on));
  if (res < 0) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "set SO_REUSEADDR: %s[%d]", strerror(errno), __LINE__);
  }

  struct timeval tv;
  tv.tv_sec = g_Config->m_iTimeout / 1000000;
  tv.tv_usec = g_Config->m_iTimeout % 1000000;
  // 设置发送超时
  res = setsockopt(m_isocket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(struct timeval));
  if (res < 0) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "set SO_SNDTIMEO: %s[%d]", strerror(errno), __LINE__);
  }
  // 设置接收超时
  res = setsockopt(m_isocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval));
  if (res < 0) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "set SO_RCVTIMEO: %s[%d]", strerror(errno), __LINE__);
  }

  // 设置为非阻塞模式
  // int iFlag = fcntl(m_isocket, F_GETFL) | O_NONBLOCK;
  // fcntl(m_isocket, F_SETFL, iFlag);

  /* Bind to address */
  if (g_Config->sockAddr != NULL) {
    if (bind(m_isocket, g_Config->sockAddr, sizeof(struct sockaddr_in6)) < 0) {
      snprintf(m_szErrMsg, sizeof(m_szErrMsg), "bind failed: %s, %s[%d]", g_Config->m_szSockAddr,
               strerror(errno), __LINE__);
      disconnect();
      return false;
    }
  }

  /* Bind to device */
  // if (strlen(g_Config->m_szSockIf) > 0) {
  //   struct ifreq if_bind;
  //   strncpy(if_bind.ifr_name, g_Config->m_szSockIf, sizeof(if_bind.ifr_name));
  //   if (setsockopt(m_isocket, SOL_SOCKET, SO_BINDTODEVICE, &if_bind, sizeof(if_bind)) < 0) {
  //     snprintf(m_szErrMsg, sizeof(m_szErrMsg), "bind failed: %s, %s[%d]", if_bind.ifr_name,
  //              strerror(errno), __LINE__);
  //     disconnect();
  //     return false;
  //   }
  // }

  int serverOsize = 0;
  if (server->sa_family == AF_INET) {
    serverOsize = sizeof(struct sockaddr_in);
  } else {
    serverOsize = sizeof(struct sockaddr_in6);
  }

  // connect
  int iRet = connect(m_isocket, server, serverOsize);
  if ((iRet != 0)) {
    // if((errno != EWOULDBLOCK) && (errno != EINPROGRESS))
    // { // 当前是阻塞模式，errno为EINPROGRESS表示连接超时
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "connect failed: %s[%d]", strerror(errno), __LINE__);
    disconnect();
    return false;
    // }

    // select wait
    //        fd_set WriteSet;
    //        FD_ZERO(&WriteSet);
    //        FD_SET(m_isocket,&WriteSet);

    // iRet = select(FD_SETSIZE,NULL,&WriteSet,NULL,&tv);
    // 这儿传入tv会导致线程数不能超过1024，否则会出现段错误
    //        iRet = select(FD_SETSIZE,NULL,&WriteSet,NULL,NULL);
    //        if(iRet <= 0 || !FD_ISSET(m_isocket, &WriteSet))
    //        {
    //            disconnect();
    //            snprintf(m_szErrMsg,sizeof(m_szErrMsg),"connect select
    //            timeout!ret=%d[%d]",iRet,__LINE__); return false;
    //        }
  }

  // getscokopt check
  char iSockErr = '\0';
  socklen_t iSockErrLen = sizeof(iSockErr);
  if (getsockopt(m_isocket, SOL_SOCKET, SO_ERROR, &iSockErr, &iSockErrLen)) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "connect getsockopt failed: %s[%d]", strerror(errno),
             __LINE__);
    disconnect();
    return false;
  }

  if (iSockErr != 0) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "connect getsockopt failed! iSockErr: %d, %s[%d]",
             iSockErr, strerror(errno), __LINE__);
    disconnect();
    return false;
  }

  int flags = 1;
  res = setsockopt(m_isocket, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));
  if (res < 0) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "set TCP_NODELAY: %s[%d]", strerror(errno), __LINE__);
  }

  // 取消非阻塞模式
  // iFlag &= ~O_NONBLOCK;
  // fcntl(m_isocket, F_SETFL, iFlag);

  return true;
}

void VirtualClient::disconnect() {
  if (m_isocket >= 0) {
    int tmp_isocket = m_isocket;
    m_isocket = -1;

    //int result = shutdown(tmp_isocket, SHUT_RDWR);  // shutdown会出现TIME_WAIT
    //if (result != 0) {
      // close是否会有TIME_WAIT跟l_linger设置有关。也可以配置net.ipv4.tcp_max_tw_buckets限制TIME_WAIT数量
    close(tmp_isocket);
    //}
  }
}

bool VirtualClient::isConnect() {
  if (m_isocket < 0) {
    return false;
  }
  char iSockErr = '\0';
  socklen_t iSockErrLen = sizeof(iSockErr);
  if (getsockopt(m_isocket, SOL_SOCKET, SO_ERROR, &iSockErr, &iSockErrLen)) {
    disconnect();
    return false;
  }
  if (iSockErr != 0) {
    disconnect();
    return false;
  }
  return true;
}

int VirtualClient::tcpRead(char* pBuff, int64_t iBufLen) {
  int64_t iReceivLen = 0;
  int64_t iResponseLen = -1;
  int iOneReadLen = 0;
  int64_t iReceivLenInBuff = 0;
  do {
    if (iBufLen <= iReceivLen) {
      iOneReadLen = readonce(pBuff + MAX_HEADER_LEN,
                             tmin(iBufLen - MAX_HEADER_LEN, iResponseLen - iReceivLen));
      iReceivLenInBuff = MAX_HEADER_LEN + iOneReadLen;
      iReceivLen += iOneReadLen;
    } else {
      iOneReadLen = readonce(pBuff + iReceivLen, iBufLen - iReceivLen);
      iReceivLen = iReceivLenInBuff = iReceivLen + iOneReadLen;
    }

    if (iOneReadLen <= 0) {
      if (errno == EAGAIN) {  // 超时
        if (iReceivLen < iBufLen) {
          pBuff[iReceivLen] = '\0';
        }
        snprintf(m_szErrMsg, sizeof(m_szErrMsg), "read timeout, error: %s[%d]", strerror(errno),
                 __LINE__);
        disconnect();
        return -1;
      } else if (errno == EINTR || errno == EWOULDBLOCK) { /* 中断错误 可以继续读 */
        if (*isTimeEnd) {                                  // 结束不认为是失败
          if (iReceivLen == 0) {
            // 重新设置接收超时
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 250000;
            if (setsockopt(m_isocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval)) >= 0) {
              iReceivLen = iOneReadLen = readonce(pBuff, iBufLen);
            }
          }
          if (iReceivLen < iBufLen) {
            pBuff[iReceivLen] = '\0';
          }
          return checkResponse(pBuff, iReceivLen, iOneReadLen);
        }
        iOneReadLen = 0;
      } else {  // ECONNREST EPIPE
        if (iReceivLen < iBufLen) {
          pBuff[iReceivLen] = '\0';
        }
        snprintf(m_szErrMsg, sizeof(m_szErrMsg),
                 "link closed by remote host! read ret size=%d, error: %s[%d]", iOneReadLen,
                 strerror(errno), __LINE__);
        disconnect();
        return -1;
      }
    }
  } while (isReadComplete(pBuff, iReceivLen, iReceivLenInBuff, iResponseLen) == 0);
  if (iReceivLen < iBufLen) {
    pBuff[iReceivLen] = '\0';
  }

  return checkResponse(pBuff, iReceivLen, iOneReadLen);
}

int VirtualClient::tcpWrite(char* pBuff, int64_t iBufLen) {
  int iWriteLen = 0;
  while (iBufLen > 0) {
    /* 开始写 */
    iWriteLen = writeonce(pBuff, iBufLen);
    if (iWriteLen <= 0) {     /* 出错了 */
      if (errno == EAGAIN) {  // 超时
        snprintf(m_szErrMsg, sizeof(m_szErrMsg), "write timeout, error: %s[%d]", strerror(errno),
                 __LINE__);
        disconnect();
        break;
      } else if (errno == EINTR || errno == EWOULDBLOCK) { /* 中断错误 我们继续写 */
        if (*isTimeEnd) {                                  // 结束不认为是失败
          disconnect();
          break;
        }
        iWriteLen = 0;
      } else { /* 其他错误 没有办法,只好撤退了 */
        snprintf(m_szErrMsg, sizeof(m_szErrMsg),
                 "link close by remote host!write ret=%d, error: %s[%d]", iWriteLen,
                 strerror(errno), __LINE__);
        disconnect();
        break;
      }
    }

    iBufLen -= iWriteLen;
    pBuff += iWriteLen; /* 从剩下的地方继续写 */
  }

  return iWriteLen;
}
