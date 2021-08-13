/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#include "http_client.h"

#include <unistd.h>

int HttpClient::build_buffer(char* m_pszSendBuff, TConfig* g_Config) {
  return VirtualClient::build_http_buffer(m_pszSendBuff, g_Config);
}

int HttpClient::checkResponse(const char* m_pszRecvBuff, int64_t unDataLen, int lastRead) {
  return VirtualClient::checkHttpResponse(m_pszRecvBuff, unDataLen, lastRead);
}

bool HttpClient::connectServer(TConfig* g_Config) {
  m_Config = g_Config;
  bool superResult = VirtualClient::connectServer(g_Config);
  if (!superResult) {
    return false;
  }
  return isConnect();
}

void HttpClient::disconnect() {
  VirtualClient::disconnect();
}

bool HttpClient::isConnect() {
  return VirtualClient::isConnect();
}

bool HttpClient::reconnect() {
  disconnect();
  return connectServer(m_Config);
}

int HttpClient::readonce(char* pBuff, int64_t iLen) {
  return read(getSocket(), pBuff, iLen);
}

int HttpClient::writeonce(char* pBuff, int64_t iLen) {
  return write(getSocket(), pBuff, iLen);
}

/** 返回0，没有读完；>0读完 */
int64_t HttpClient::isReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                                   int64_t& iPkgTheoryLen) {
  return VirtualClient::httpReadComplete(pData, unDataLen, iReceivLenInBuff, iPkgTheoryLen);
}
