/**
 @Copyright (c) 2019, chaoguo All rights reserved.
  生成日期   : 2005年3月26日
  最近修改   :
  功能描述   :多线程 压力测试工具
*/
#include "presscall.h"

#include <arpa/inet.h>
#include <limits.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cinttypes>
#include <string>

#include "tlib_cfg.h"
#include "user_func.h"

// 是否开始执行
volatile bool isStarted = false;
// 是否时间到
volatile bool isTimeEnd = false;
// 已运行时间(us)
uint64_t iUsecRuned = 0;
volatile int stopTimeout = 40960000;  // 最多等待40秒: 40960000us

// 总呼叫结果,主线程读写
struct TResult m_AllResultHistory;
// 上一周期总呼叫结果，用于统计单位时间结果
struct TResult m_AllResultLast;
// 每user呼叫结果组，每个线程写，主线程读
struct TResult* Results_Now = NULL;
// 线程组
pthread_t* threads_array = NULL;
// 线程ID
int* thread_ids = NULL;
int* thread_running = NULL;
// 单位时间结果的汇总值和临时变量
struct TResult m_ResultTmp;

TConfig g_Config;
pthread_attr_t attr;

void calculate(uint64_t iSec, uint64_t curTime, uint64_t lastTime);
void printSummary();
void wait_threads_exit();

/*****************************************************************************
 函 数 名  : thread_func
 功能描述  : 线程函数
*****************************************************************************/
void* thread_func(void* arg) {
  // TConfig* config = (TConfig*)arg;
  int iMyID = *static_cast<int*>(arg);
  thread_running[iMyID] = 1;
  // 初始化随机数产生器
  srand((unsigned)((iMyID + 1) * time(NULL)));

  // 每个线程启动前先等一个时间，各线程等待时间不同
  // 使请求均匀的分布，不然并发连接太多会有部分连接失败
  int iStartWaitus;
  if (g_Config.m_iThreadSleepUs / g_Config.m_iThreadNum > 0) {
    iStartWaitus = g_Config.m_iThreadSleepUs * iMyID / g_Config.m_iThreadNum;
  } else {
    iStartWaitus = 10000 * iMyID / g_Config.m_iThreadNum;
  }
  usSleep(iStartWaitus);

  CUserFunc* pUserFunc = NULL;
  try {
    pUserFunc = new CUserFunc(iMyID, &g_Config);
    pUserFunc->setTimeEnd(&isTimeEnd);
  } catch (...) {
    printf("ERR: thread %d can not run!\n", iMyID);
    delete pUserFunc;
    return NULL;
  }

  // 等待开始
  while (!isStarted) {
    usSleep(10000);  // 1000000=1s
  }

  int64_t llRspTimeUs = 0;  // DoOnce会返回负数，不能定义为无符号
  while (!isTimeEnd) {
    // 执行
    try {
      llRspTimeUs = pUserFunc->DoOnce();
    } catch (...) {
      llRspTimeUs = 0;
      printf("ERR: thread %d run error!\n", iMyID);
      throw;
    }

    // 上报单位时间结果, 用于周期性打印
    if (llRspTimeUs > 0) {
      Results_Now[iMyID].iAllReqNum++;
      Results_Now[iMyID].iOkResponseNum++;

      if (llRspTimeUs > g_Config.m_iTimeLevel3) {
        Results_Now[iMyID].m_iTimeL4Num++;
      } else if (llRspTimeUs > g_Config.m_iTimeLevel2) {
        Results_Now[iMyID].m_iTimeL3Num++;
      } else if (llRspTimeUs > g_Config.m_iTimeLevel1) {
        Results_Now[iMyID].m_iTimeL2Num++;
      } else {
        Results_Now[iMyID].m_iTimeL1Num++;
      }

      Results_Now[iMyID].dSumRspTimeUs += llRspTimeUs;
      if (Results_Now[iMyID].resetMaxTime) {
        Results_Now[iMyID].llMaxRspTimeUs = llRspTimeUs;
        Results_Now[iMyID].resetMaxTime = false;
      } else if (llRspTimeUs > Results_Now[iMyID].llMaxRspTimeUs) {
        Results_Now[iMyID].llMaxRspTimeUs = llRspTimeUs;
      }
    } else if (llRspTimeUs == -1) {
      Results_Now[iMyID].iAllReqNum++;
      Results_Now[iMyID].iBadResponseNum++;
    } else if (llRspTimeUs == 0) {
      Results_Now[iMyID].iAllReqNum++;
      Results_Now[iMyID].iNoResponseNum++;
    }
    // else{} // -2说明未建立连接，不统计

    if (g_Config.m_iThreadSleepUs > 0)
      usSleep(g_Config.m_iThreadSleepUs);
  }

  thread_running[iMyID] = 0;
  delete pUserFunc;
  return NULL;
}

/*****************************************************************************
 函 数 名  : thread_func
 功能描述  : 线程函数
*****************************************************************************/
void* child_func(void* arg) {
  // int iMyID = *static_cast<int*>(arg);
  // 栈大小最小值16384，即16KB，默认8MB
  int ret, realThreadNum = 0;
  isStarted = true;
  // 创建线程
  for (int i = 0; i < g_Config.m_iThreadNum; i++) {
    thread_ids[i] = i;
    ret = pthread_create(&threads_array[i], &attr, thread_func, &thread_ids[i]);
    if (ret != 0) {
      threads_array[i] = 0;
      if (g_Config.m_iPrintError) {
        char temp[64];
        snprintf(temp, sizeof(temp), "create thread failed :%d[%d]\n", ret, i);
        fwrite(temp, strlen(temp), 1, g_Config.errLogOut);
      }
      // exit(3);
    } else {
      realThreadNum++;
    }
    // 防止线程数设置过大，而内存不足，导致一直卡在创建线程无法退出
    if (isTimeEnd) {
      g_Config.m_iThreadNum = i + 1;
      break;
    }
  }
  // isStarted = true;

  printf("Start finished, %d threads running, will %sprint error message ...\n", realThreadNum,
         g_Config.m_iPrintError ? "" : "not ");
  printf(
      "\nTIME      OK/NO/BAD/ALL=PERCENT           TPS     AvgTime(ms)  MaxTime(ms) "
      " <%3dms  <%3dms  <%3dms  >%3dms\n",
      g_Config.m_iTimeLevel1 / 1000, g_Config.m_iTimeLevel2 / 1000, g_Config.m_iTimeLevel3 / 1000,
      g_Config.m_iTimeLevel3 / 1000);
  fflush(stdout);

  return NULL;
}

void initConfig(int argc, char** argv) {
  char rootpath[strlen(argv[0]) - strlen(strrchr(argv[0], '/')) + 2];
  memcpy(rootpath, argv[0], sizeof(rootpath) - 1);
  rootpath[sizeof(rootpath) - 1] = '\0';

  memset(&g_Config, 0, sizeof(g_Config));
  // 获取参数
  char path[strlen(argv[0]) + strlen(CFGFILE)];
  char errLogPath[120];
  snprintf(path, sizeof(path), "%s%s", rootpath, CFGFILE);
  TLib_Cfg_GetConfig(
      path,  // config file path
      "Host", CFG_STRING, g_Config.m_szDestIp, "127.0.0.1", sizeof(g_Config.m_szDestIp),  // Host
      "HttpMethod", CFG_STRING, g_Config.m_szMethod, "GET", sizeof(g_Config.m_szMethod),  // Method
      "Port", CFG_INT, &(g_Config.m_iDestPort), 80,                                       // Port
      "ThreadNum", CFG_INT, &(g_Config.m_iThreadNum), 1,             // Thread number
      "ThreadSleepMs", CFG_INT, &(g_Config.m_iThreadSleepUs), 0,     // sleep time
      "RunDuration", CFG_INT, &(g_Config.m_iRunDuration), 5,         // run time
      "SampleSecs", CFG_INT, &(g_Config.m_iSampleUs), 5,             //
      "TestMode", CFG_INT, &(g_Config.m_test_mode), 1,               //
      "LongConnection", CFG_INT, &(g_Config.m_iLongConn), 1,         //
      "PrintError", CFG_STRING, errLogPath, "", sizeof(errLogPath),  //
      "useDiffPort", CFG_INT, &(g_Config.m_iUseDiffPort), 0,         //
      "MsgTimeout", CFG_INT, &(g_Config.m_iTimeout), 60000000,       //
      "MsgLen", CFG_INT, &(g_Config.m_iLen), 0,                      //
      "GetFile", CFG_STRING, &(g_Config.m_pszGetFile), "", sizeof(g_Config.m_pszGetFile),  //
      "GetFileLen", CFG_INT, &(g_Config.m_iRecvLen), RECV_MAX_LEN - MAX_HEADER_LEN,        //
      "Domain", CFG_STRING, &(g_Config.m_pszHost), "", sizeof(g_Config.m_pszHost),         //

      "SockAddress", CFG_STRING, &(g_Config.m_szSockAddr), "", sizeof(g_Config.m_szSockAddr),     //
      "SockInterface", CFG_STRING, &(g_Config.m_szSockIf), "", sizeof(g_Config.m_szSockIf),       //
      "CaCert", CFG_STRING, &(g_Config.m_caCert), "", sizeof(g_Config.m_caCert),                  //
      "ClientCert", CFG_STRING, &(g_Config.m_clientCert), "", sizeof(g_Config.m_clientCert),      //
      "ClientKey", CFG_STRING, &(g_Config.m_clientKey), "", sizeof(g_Config.m_clientKey),         //
      "ssl_protocol", CFG_STRING, &(g_Config.m_tlsProtocol), "", sizeof(g_Config.m_tlsProtocol),  //
      "ssl_ciphers", CFG_STRING, &(g_Config.m_tlsCiphers), "", sizeof(g_Config.m_tlsCiphers),     //
      "RspTimeLevel1", CFG_INT, &(g_Config.m_iTimeLevel1), 10,                                    //
      "RspTimeLevel2", CFG_INT, &(g_Config.m_iTimeLevel2), 100,                                   //
      "RspTimeLevel3", CFG_INT, &(g_Config.m_iTimeLevel3), 1000, NULL);

  char tmp[256];
  if (g_Config.m_caCert[0] != '/') {
    memcpy(tmp, g_Config.m_caCert, sizeof(g_Config.m_caCert));
    snprintf(g_Config.m_caCert, sizeof(g_Config.m_caCert), "%s%s", rootpath, tmp);
  }
  if (g_Config.m_clientCert[0] != '/') {
    memcpy(tmp, g_Config.m_clientCert, sizeof(g_Config.m_clientCert));
    snprintf(g_Config.m_clientCert, sizeof(g_Config.m_clientCert), "%s%s", rootpath, tmp);
  }
  if (g_Config.m_clientKey[0] != '/') {
    memcpy(tmp, g_Config.m_clientKey, sizeof(g_Config.m_clientKey));
    snprintf(g_Config.m_clientKey, sizeof(g_Config.m_clientKey), "%s%s", rootpath, tmp);
  }

  /* 根据arg参数个数来判断通过命令行传入的值，每个case都不需要break语句。
     其他参数使用配置文件的值 */
  switch (argc) {
    case 9:
      g_Config.m_iTimeLevel3 = atoi(argv[8]);
    case 8:
      g_Config.m_iTimeLevel2 = atoi(argv[7]);
    case 7:
      g_Config.m_iTimeLevel1 = atoi(argv[6]);
    case 6:
      g_Config.m_iSampleUs = atoi(argv[5]);
    case 5:
      g_Config.m_iRunDuration = atoi(argv[4]);
    case 4:
      g_Config.m_iThreadNum = atoi(argv[3]);
    case 3:
      strncpy(g_Config.m_szDestIp, argv[1], strlen(argv[1]) + 1);
      g_Config.m_iDestPort = atoi(argv[2]);
    case 1:  // 防止执行default
      break;
    default:  // 至少需要2个参数
      printf("presscall ip port [th_num] [run_sec] [sample_sec] [time_l1] [time_l2] [time_l3]\n");
      printf("example:\n");
      printf("presscall 127.0.0.1 8080\n");
      fflush(stdout);
      exit(1);
  }

  if (strcmp(errLogPath, "0") == 0) {
    g_Config.m_iPrintError = 0;
    g_Config.errLogOut = stdout;
  } else if (strcasecmp(errLogPath, "stderr") == 0 || strcmp(errLogPath, "1") == 0) {
    g_Config.m_iPrintError = 1;
    g_Config.errLogOut = stderr;
  } else if (strcasecmp(errLogPath, "stdout") == 0) {
    g_Config.m_iPrintError = 1;
    g_Config.errLogOut = stdout;
  } else {
    g_Config.m_iPrintError = 1;
    g_Config.errLogOut = fopen(errLogPath, "wb");
    if (g_Config.errLogOut == NULL) {
      g_Config.errLogOut = stderr;
    }
  }
}

void normally_config() {
  if (g_Config.m_iLen > SEND_MAX_LEN - MAX_HEADER_LEN) {
    printf("WARN: msgLen is too big, please make sure not have 400 or 414 response\n");
    fflush(stdout);
    // exit(2);
  }

  if (g_Config.m_pszGetFile[0] != '/') {
    g_Config.m_pszGetFile[strlen(g_Config.m_pszGetFile) + 1] = '\0';
    for (int i = strlen(g_Config.m_pszGetFile); i > 0; i--) {
      g_Config.m_pszGetFile[i] = g_Config.m_pszGetFile[i - 1];
    }
    g_Config.m_pszGetFile[0] = '/';
  }

  if (g_Config.m_pszHost == NULL || strlen(g_Config.m_pszHost) == 0) {
    if (strstr(g_Config.m_szDestIp, ":")) {
      snprintf(g_Config.m_pszHost, sizeof(g_Config.m_pszHost), "[%s]:%d", g_Config.m_szDestIp,
               g_Config.m_iDestPort);
    } else {
      snprintf(g_Config.m_pszHost, sizeof(g_Config.m_pszHost), "%s:%d", g_Config.m_szDestIp,
               g_Config.m_iDestPort);
    }
  }

  if (strcasecmp(g_Config.m_szMethod, "POST") == 0) {
    memcpy(g_Config.m_szMethod, "POST", 5);
  } else {
    memcpy(g_Config.m_szMethod, "GET", 4);
  }

  if (strstr(g_Config.m_szDestIp, ":")) {
    printf("%s %s://[%s]:%d%s, Domain: %s\n", g_Config.m_szMethod,
           g_Config.m_test_mode == 1 ? "http" : (g_Config.m_test_mode == 2 ? "https" : "tcp"),
           g_Config.m_szDestIp, g_Config.m_iDestPort, g_Config.m_pszGetFile, g_Config.m_pszHost);
  } else {
    printf("%s %s://%s:%d%s, Domain: %s\n", g_Config.m_szMethod,
           g_Config.m_test_mode == 1 ? "http" : (g_Config.m_test_mode == 2 ? "https" : "tcp"),
           g_Config.m_szDestIp, g_Config.m_iDestPort, g_Config.m_pszGetFile, g_Config.m_pszHost);
  }
  printf("LongConnection %d  MsgLen %d  every %dms  duration %lus level(ms):%d,%d,%d\n",
         g_Config.m_iLongConn, g_Config.m_iLen, g_Config.m_iThreadSleepUs, g_Config.m_iRunDuration,
         g_Config.m_iTimeLevel1, g_Config.m_iTimeLevel2, g_Config.m_iTimeLevel3);

  g_Config.m_iSampleUs = g_Config.m_iSampleUs * 1000000;
  g_Config.m_iThreadSleepUs = g_Config.m_iThreadSleepUs * 1000;
  g_Config.m_iTimeLevel1 = g_Config.m_iTimeLevel1 * 1000;
  g_Config.m_iTimeLevel2 = g_Config.m_iTimeLevel2 * 1000;
  g_Config.m_iTimeLevel3 = g_Config.m_iTimeLevel3 * 1000;
  if (g_Config.m_iRecvLen <= 0) {
    g_Config.m_iRecvLen = RECV_MAX_LEN;
  } else {
    g_Config.m_iRecvLen = g_Config.m_iRecvLen + MAX_HEADER_LEN;
  }
  if (g_Config.m_iThreadNum <= 0) {
    g_Config.m_iThreadNum = 1;
  }
  if (g_Config.m_iLen < 0) {
    g_Config.m_iLen = 0;
  }
}

void normally_ip() {
  g_Config.sockAddr = NULL;
  if (strlen(g_Config.m_szSockAddr) > 0) {
    std::string str_szSockAddr = g_Config.m_szSockAddr;
    if (str_szSockAddr.find(":", 0) != std::string::npos) {
      struct addrinfo addrCriteria;
      memset(&addrCriteria, 0, sizeof(addrCriteria));
      addrCriteria.ai_family = AF_UNSPEC;
      addrCriteria.ai_socktype = SOCK_STREAM;
      addrCriteria.ai_flags = AI_PASSIVE;
      addrCriteria.ai_protocol = IPPROTO_TCP;
      struct addrinfo* addrList;
      int retVal = getaddrinfo((const char*)&g_Config.m_szSockAddr, NULL, &addrCriteria, &addrList);
      if (retVal != 0) {
        printf("Get local address error: %s\n", gai_strerror(retVal));
        fflush(stdout);
        exit(2);
      }
      if (addrList != NULL) {
        g_Config.sockAddr = (struct sockaddr*)(addrList->ai_addr);
        // char addrBuffer[INET6_ADDRSTRLEN];
        // inet_ntop(AF_INET6, &((struct sockaddr_in6*)(addrList->ai_addr))->sin6_addr, addrBuffer,
        // sizeof (addrBuffer)); printf("%s\n", addrBuffer); printf("%u\n", ntohs(((struct
        // sockaddr_in6*)(addrList->ai_addr))->sin6_port)); addrList = addrList->ai_next;
      } else {
        struct in6_addr m_v6_src;
        if (inet_pton(AF_INET6, g_Config.m_szSockAddr, &m_v6_src) <= 0) {
          printf("V6Host is incorrect: %s\n", g_Config.m_szSockAddr);
          fflush(stdout);
          exit(2);
        }
        struct sockaddr_in6* client6 = new sockaddr_in6();
        client6->sin6_family = AF_INET6;
        client6->sin6_port = htons(0);
        client6->sin6_scope_id = 0;
        memcpy(&client6->sin6_addr, (struct in6_addr*)&m_v6_src, sizeof(m_v6_src));
        g_Config.sockAddr = (struct sockaddr*)client6;
      }
    } else {
      int m_iSrcIp = inet_addr(g_Config.m_szSockAddr);
      struct sockaddr_in* client4 = new sockaddr_in();
      client4->sin_family = AF_INET;
      client4->sin_port = htons(0);
      client4->sin_addr.s_addr = m_iSrcIp;
      g_Config.sockAddr = (struct sockaddr*)client4;
    }
  }
}

extern "C" void sigPIPE(int nSignal) {
  // char temp[64];
  // snprintf(temp, sizeof(temp), "\nGet signal: %d, do nothing.\n", nSignal);
  // fwrite(temp, strlen(temp), 1, g_Config.errLogOut);
}

extern "C" void sigQUIT(int nSignal) {
  if (!isTimeEnd) {
    isTimeEnd = true;
    g_Config.m_iThreadSleepUs = 0;
  }
  stopTimeout /= 2;

  char temp[64];
  snprintf(temp, sizeof(temp), "\nGet signal: %d, quit threads ...\n", nSignal);
  fwrite(temp, strlen(temp), 1, g_Config.errLogOut);
}

int main(int argc, char** argv) {
  initConfig(argc, argv);
  normally_config();
  normally_ip();
  initAhead(&g_Config);

  // ----信号处理
  for (int i = 1; i <= NSIG; i++) {
    // SIG_DFL默认，SIG_IGN忽略信号
    signal(i, SIG_DFL);
  }
  signal(SIGHUP, sigQUIT);   // 1 终端关闭
  signal(SIGINT, sigQUIT);   // 2 ctrl+C
  signal(SIGPIPE, sigPIPE);  // 13 管道写错误,连接异常断开,不退出
  signal(SIGQUIT, sigQUIT);  // kill -3
  signal(SIGKILL, sigQUIT);  // kill -9
  signal(SIGTERM, sigQUIT);  // kill -15

  // ----数据初始化
  Results_Now = new TResult[g_Config.m_iThreadNum];
  threads_array = new pthread_t[g_Config.m_iThreadNum];
  thread_ids = new int[g_Config.m_iThreadNum];
  thread_running = new int[g_Config.m_iThreadNum];
  for (int i = 0; i < g_Config.m_iThreadNum; i++) {
    memset(&Results_Now[i], 0, sizeof(TResult));
  }

  // 栈大小最小值16384，即16KB，默认8MB
  int ret, stacksize = 16384;
  // 初始化线程属性
  pthread_attr_init(&attr);
  // 设置线程栈大小
  pthread_attr_setstacksize(&attr, stacksize);
  // 创建线程
  int child_id = g_Config.m_iThreadNum;
  pthread_t tidp;
  ret = pthread_create(&tidp, &attr, child_func, &child_id);
  if (ret != 0) {
    if (g_Config.m_iPrintError) {
      char temp[64];
      snprintf(temp, sizeof(temp), "create main thread failed :%d\n", ret);
      fwrite(temp, strlen(temp), 1, g_Config.errLogOut);
    }
  }

  int64_t runStartTime, curTime, sleepEndTimeUs, runEndTime;
  curTime = runStartTime = sleepEndTimeUs = getCurrentTimeUs();
  if (g_Config.m_iRunDuration == 0) {
    runEndTime = LLONG_MAX;
  } else {
    runEndTime = curTime + g_Config.m_iRunDuration * 1000000;
  }

  // 持续运行
  iUsecRuned = 0;
  while (sleepEndTimeUs < runEndTime && !isTimeEnd) {
    sleepEndTimeUs += g_Config.m_iSampleUs;
    curTime = getCurrentTimeUs();
    if (sleepEndTimeUs > curTime) {
      if (usSleep(sleepEndTimeUs - curTime) < 0) {
        // sleep被打断
        sleepEndTimeUs = getCurrentTimeUs();
      }
    } else {
      // 执行时间超过sleep time
      sleepEndTimeUs = curTime;
    }
    iUsecRuned = sleepEndTimeUs - runStartTime;
    calculate(iUsecRuned, sleepEndTimeUs, g_Config.m_iSampleUs);
  }

  if (!isTimeEnd) {
    // 通知线程结束
    isTimeEnd = true;
    g_Config.m_iThreadSleepUs = 0;
  }

  // 等待线程结束
  wait_threads_exit();

  // 释放所有申请的内存
  if (g_Config.errLogOut != NULL) {
    fclose(g_Config.errLogOut);
  }
  destroyEnd(&g_Config);
  pthread_attr_destroy(&attr); /* 不再使用线程属性，将其销毁 */
  delete[] threads_array;
  delete[] thread_ids;
  delete[] thread_running;
  delete[] Results_Now;
  exit(0);
}

/*****************************************************************************
 函 数 名  : calculate
 功能描述  : 采样函数
*****************************************************************************/
void calculate(uint64_t iUsec, uint64_t curTime, uint64_t lastTime) {
  if (lastTime <= 0) {
    printf("Run duration time error: %lu\n", lastTime);
    return;
  }
  // 备份上次汇总结果并清空
  memcpy(&m_AllResultLast, &m_AllResultHistory, sizeof(TResult));
  memset(&m_AllResultHistory, 0, sizeof(TResult));
  // 汇总收集总结果
  for (int i = 0; i < g_Config.m_iThreadNum; i++) {
    // 此处memcpy为非原子拷贝，可能出现周期计数比实际少，但不影响最终结果
    // 之所以拷贝到m_ResultTmp不直接用Results_Now，是因为会出现all和ok不一致导致不是100%
    memcpy(&m_ResultTmp, (const void*)&Results_Now[i], sizeof(TResult));
    if (!Results_Now[i].resetMaxTime) {
      if (m_ResultTmp.llMaxRspTimeUs > m_AllResultHistory.llMaxRspTimeUs)
        m_AllResultHistory.llMaxRspTimeUs = m_ResultTmp.llMaxRspTimeUs;
      Results_Now[i].resetMaxTime = true;
    }

    // m_AllResultHistory.ullRspTimeUs += (m_ResultTmp.ullRspTimeUs + 500) / 1000;
    m_AllResultHistory.dSumRspTimeUs += m_ResultTmp.dSumRspTimeUs;
    m_AllResultHistory.iAllReqNum += m_ResultTmp.iAllReqNum;
    m_AllResultHistory.iOkResponseNum += m_ResultTmp.iOkResponseNum;
    m_AllResultHistory.iNoResponseNum += m_ResultTmp.iNoResponseNum;
    m_AllResultHistory.iBadResponseNum += m_ResultTmp.iBadResponseNum;
    m_AllResultHistory.m_iTimeL1Num += m_ResultTmp.m_iTimeL1Num;
    m_AllResultHistory.m_iTimeL2Num += m_ResultTmp.m_iTimeL2Num;
    m_AllResultHistory.m_iTimeL3Num += m_ResultTmp.m_iTimeL3Num;
    m_AllResultHistory.m_iTimeL4Num += m_ResultTmp.m_iTimeL4Num;
  }

  // 计算本周期结果
  m_ResultTmp.iAllReqNum = m_AllResultHistory.iAllReqNum - m_AllResultLast.iAllReqNum;
  m_ResultTmp.iOkResponseNum = m_AllResultHistory.iOkResponseNum - m_AllResultLast.iOkResponseNum;
  m_ResultTmp.iNoResponseNum = m_AllResultHistory.iNoResponseNum - m_AllResultLast.iNoResponseNum;
  m_ResultTmp.iBadResponseNum =
      m_AllResultHistory.iBadResponseNum - m_AllResultLast.iBadResponseNum;
  m_ResultTmp.dSumRspTimeUs = m_AllResultHistory.dSumRspTimeUs - m_AllResultLast.dSumRspTimeUs;

  m_ResultTmp.llMaxRspTimeUs = m_AllResultHistory.llMaxRspTimeUs;
  if (m_AllResultLast.llMaxRspTimeUs > m_AllResultHistory.llMaxRspTimeUs)
    m_AllResultHistory.llMaxRspTimeUs = m_AllResultLast.llMaxRspTimeUs;

  m_ResultTmp.m_iTimeL1Num = m_AllResultHistory.m_iTimeL1Num - m_AllResultLast.m_iTimeL1Num;
  m_ResultTmp.m_iTimeL2Num = m_AllResultHistory.m_iTimeL2Num - m_AllResultLast.m_iTimeL2Num;
  m_ResultTmp.m_iTimeL3Num = m_AllResultHistory.m_iTimeL3Num - m_AllResultLast.m_iTimeL3Num;
  m_ResultTmp.m_iTimeL4Num = m_AllResultHistory.m_iTimeL4Num - m_AllResultLast.m_iTimeL4Num;

  // 打印本周期结果
  char percentStr[32];
  snprintf(
      percentStr, sizeof(percentStr), "%lu/%lu/%lu/%lu=%.2f%%", m_ResultTmp.iOkResponseNum,
      m_ResultTmp.iNoResponseNum, m_ResultTmp.iBadResponseNum, m_ResultTmp.iAllReqNum,
      m_ResultTmp.iAllReqNum > 0 ? m_ResultTmp.iOkResponseNum * 100.0 / m_ResultTmp.iAllReqNum : 0);
  time_t time = curTime / 1000000;
  struct tm* p_tm_time = localtime(&time);
  printf("%02d:%02d:%02d  %-28s% 10.2f% 11.3fms% 11.3fms %7lu %7lu %7lu %7lu\n", p_tm_time->tm_hour,
         p_tm_time->tm_min, p_tm_time->tm_sec, percentStr,
         m_ResultTmp.iOkResponseNum / (lastTime / 1000000.0),
         m_ResultTmp.iOkResponseNum > 0
             ? m_ResultTmp.dSumRspTimeUs / 1000.0 / m_ResultTmp.iOkResponseNum
             : 0,
         m_ResultTmp.llMaxRspTimeUs / 1000.0, m_ResultTmp.m_iTimeL1Num, m_ResultTmp.m_iTimeL2Num,
         m_ResultTmp.m_iTimeL3Num, m_ResultTmp.m_iTimeL4Num);
  fflush(stdout);
}

void wait_threads_exit() {
  // 发送信号，使线程停止sleep
  int i;
  for (i = 0; i < g_Config.m_iThreadNum; i++) {
    if (thread_running[i] > 0) {
      pthread_kill(threads_array[i], SIGPIPE);  // kill -13
    }
  }

  // 等待线程结束或超时
  int waitTime = 10000;  // 10ms
  for (i = 0; i < g_Config.m_iThreadNum; i++) {
    // 如果线程存在，继续等待。ESRCH: 线程不存在，EINVAL：信号不合法
    if (thread_running[i] > 0) {
      i--;  // 继续等该线程
      if (waitTime > stopTimeout) {
        break;
      }
      usSleep(waitTime);
      waitTime *= 2;
    }
    // }
  }

  // 终止线程，防止线程挂死。这个属于异常操作，可能产生core
  bool hasPrinted = false;
  int left = i;
  try {
    for (; i < g_Config.m_iThreadNum; i++) {
      if (thread_running[i] > 0) {
        if (!hasPrinted) {
          char temp[64] = "WARN: stop timeout, kill thread\n";
          fwrite(temp, strlen(temp), 1, g_Config.errLogOut);
          // 打印汇总结果
          printSummary();
          hasPrinted = true;
        }

        // 线程中没有中断的情况下会导致cancel不生效，改用kill
        // pthread_cancel(threads_array[i]);
        pthread_kill(threads_array[i], SIGKILL);  // kill -9
      }
    }
  } catch (...) {
    char temp[64] = "ERR: pthread_cancel failed\n";
    fwrite(temp, strlen(temp), 1, g_Config.errLogOut);
    fflush(stdout);
  }

  // 等待线程结束
  for (; left < g_Config.m_iThreadNum; left++) {
    void* joinError = NULL;
    if (thread_running[left] > 0 && pthread_join(threads_array[left], &joinError) != 0) {
      left--;  // 继续等该线程
      if (g_Config.m_iPrintError) {
        char temp[150];
        snprintf(temp, sizeof(temp), "Wait thread to stop failed:%d, error:%s\n", left,
                 reinterpret_cast<char*>(joinError));
        fwrite(temp, strlen(temp), 1, g_Config.errLogOut);
      }
      usSleep(10000);  // sleep 10ms
    }
  }

  if (!hasPrinted) {
    // 打印汇总结果
    printSummary();
  }
}

/*****************************************************************************
 函 数 名  : printSummary
 功能描述  : 退出函数，退出时做全局统计
*****************************************************************************/
void printSummary() {
  // 打印总结果
  printf("\n");

  if (iUsecRuned > 0) {
    printf("TPS: %.2f\n", m_AllResultHistory.iOkResponseNum / (iUsecRuned / 1000000.0));
  }

  float percent = 0;
  if (m_AllResultHistory.iAllReqNum > 0) {
    percent = m_AllResultHistory.iOkResponseNum * 1.0 / m_AllResultHistory.iAllReqNum;
  }
  printf("Success percent: %.2f%%\n", percent * 100);

  double averageResponseTime = 0;
  if (m_AllResultHistory.iOkResponseNum > 0) {
    averageResponseTime =
        m_AllResultHistory.dSumRspTimeUs / 1000.0 / m_AllResultHistory.iOkResponseNum;
  }
  printf("Max response time: %.3fms\n", m_AllResultHistory.llMaxRspTimeUs / 1000.0);
  printf("Average response time: %.3fms\n", averageResponseTime);

  printf("Running time(hour:min:sec): %" PRIu64 ":%02" PRIu64 ":%06.3f\n", iUsecRuned / 3600000000,
         (iUsecRuned / 60000000) % 60, (iUsecRuned % 60000000) / 1000000.0);

  // All request需要把关闭期间的数据汇总起来
  memset(&m_AllResultHistory, 0, sizeof(TResult));
  for (int i = 0; i < g_Config.m_iThreadNum; i++) {
    m_AllResultHistory.iAllReqNum += Results_Now[i].iAllReqNum;
    m_AllResultHistory.iOkResponseNum += Results_Now[i].iOkResponseNum;
    m_AllResultHistory.iNoResponseNum += Results_Now[i].iNoResponseNum;
    m_AllResultHistory.iBadResponseNum += Results_Now[i].iBadResponseNum;
  }

  printf("All request : %lu\n", m_AllResultHistory.iAllReqNum);
  printf("OK response : %lu\n", m_AllResultHistory.iOkResponseNum);
  printf("Bad response: %lu\n", m_AllResultHistory.iBadResponseNum);
  printf("No response : %lu\n", m_AllResultHistory.iNoResponseNum);
  fflush(stdout);
}
