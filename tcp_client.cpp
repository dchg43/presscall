/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#include "tcp_client.h"

#include <unistd.h>

int TcpClient::build_buffer(char* m_pszSendBuff, TConfig* g_Config) {
  return VirtualClient::build_tcp_buffer(m_pszSendBuff, g_Config);
}

int TcpClient::checkResponse(const char* m_pszRecvBuff, int64_t unDataLen, int lastRead) {
  return VirtualClient::checktcpResponse(unDataLen, lastRead);
}

bool TcpClient::connectServer(TConfig* g_Config) {
  m_Config = g_Config;
  bool superResult = VirtualClient::connectServer(g_Config);
  if (!superResult) {
    return false;
  }
  return isConnect();
}

void TcpClient::disconnect() {
  VirtualClient::disconnect();
}

bool TcpClient::isConnect() {
  return VirtualClient::isConnect();
}

bool TcpClient::reconnect() {
  disconnect();
  return connectServer(m_Config);
}

int TcpClient::readonce(char* pBuff, int64_t iLen) {
  return read(getSocket(), pBuff, iLen);
}

int TcpClient::writeonce(char* pBuff, int64_t iLen) {
  return write(getSocket(), pBuff, iLen);
}

/** 返回0，没有读完；>0读完 */
int64_t TcpClient::isReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                                  int64_t& iPkgTheoryLen) {
  return VirtualClient::tcpReadComplete(pData, unDataLen, iReceivLenInBuff, iPkgTheoryLen);
}
