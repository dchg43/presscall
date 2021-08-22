/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#ifndef PRESSCALL_ALL_IN_ONE_TLIB_CFG_H_
#define PRESSCALL_ALL_IN_ONE_TLIB_CFG_H_

#include <stdint.h>
#include <stdio.h>

#define CFGFILE "./conf.cfg"
#define MAX_CONFIG_LINE_LEN 256
#define CFG_STRING (int)1
#define CFG_INT (int)2
#define CFG_INT64 (int)3
#define CFG_DOUBLE (int)4
#define SEND_MAX_LEN 8181    // large_client_header_buffers默认8K
#define RECV_MAX_LEN 2524    // MSS + MAX_HEADER_LEN
#define MAX_HEADER_LEN 1024  // client_header_buffer_size默认1024

// #define countof(x) (sizeof(x) / sizeof(x[0]))
#define tmin(x, y) ((x) < (y) ? (x) : (y))

enum tmode { EN_CS = 0, EN_HTTP = 1, EN_HTTPS = 2 };

typedef struct {
  // char* m_pszSendBuff;
  // char* m_pszRecvBuff;
  // struct sockaddr* server;

  char m_szDestIp[48];   // Host
  char m_szMethod[16];   // Method
  int m_iDestPort;       // Port
  int m_iThreadNum;      // ThreadNum
  int m_iThreadSleepUs;  // ThreadSleepMs
  int m_test_mode;       // TestMode

  // close timeout. 设置linger的超时时间，0非正常关闭连接
  int m_iLingerTime;   // LingerTime
  int m_iLongConn;     // LongConnection
  int m_iPrintError;   // PrintError
  int m_iUseDiffPort;  // useDiffPort
  int m_iLen;          // MsgLen
  int m_iRecvLen;      // GetFileLen
  int m_iTimeLevel1;   // RspTimeLevel1
  int m_iTimeLevel2;   // RspTimeLevel2
  int m_iTimeLevel3;   // RspTimeLevel3
  FILE* errLogOut;     // error log out file
  struct sockaddr* sockAddr;
  int64_t m_iRunDuration;  // RunDuration
  int64_t m_iCallNumbers;  // CallNumbers
  int64_t m_iSampleUs;     // SampleSecs

  // 超时时间机制：
  // 1.初始超时时间设置为MsgTimeout
  // 2.取近段时间连接成功的最大时间maxTime的2倍maxTimeout作为后续超时时间
  // 3.超时时间1分钟更新一次，使用上一分钟的maxTimeout
  // 4.maxTimeout最大不超过70秒
  int64_t m_iTimeout;  // MsgTimeout

  char m_pszGetFile[256];  // GetFile
  char m_pszHost[128];     // Domain
  char m_szSockAddr[48];

  char m_tlsProtocol[16];  // ssl_protocol
  char m_caCert[256];      // CaCert
  char m_clientCert[256];  // ClientCert
  char m_clientKey[256];   // ClientKey
  char m_tlsCiphers[512];  // ssl_ciphers
} TConfig;

typedef struct {
  char szName[MAX_CONFIG_LINE_LEN];
  char szVal[MAX_CONFIG_LINE_LEN];
} TNVStu;

void TLib_Cfg_GetConfig(const char* sConfigFilePath, ...);
void TLib_Cfg_GetNVConfig(const char* sConfigFilePath, TNVStu* pNVStu);

int64_t getCurrentTimeUs();

#endif  // PRESSCALL_ALL_IN_ONE_TLIB_CFG_H_
