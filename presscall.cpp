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

#include "tlib_cfg.h"
#include "user_func.h"

// 是否开始执行
volatile bool isStarted = false;
// 是否时间到
volatile bool isTimeEnd = false;
// 已运行时间(us)
int64_t iUsecRuned = 0;
volatile int stopTimeout = 40960000;  // 最多等待40秒: 40960000us

// 总呼叫结果,主线程读写
struct TResult m_AllResultHistory;
// 上一周期总呼叫结果，用于统计单位时间结果
struct TResult m_AllResultLast;
// 每user呼叫结果组，每个线程写，主线程读
struct TResult* Results_Now = NULL;
// 线程组
struct ThreadArray* threads_array = NULL;
int* thread_running = NULL;
// 单位时间结果的汇总值和临时变量
struct TResult m_ResultTmp;
// 存放配置文件路径
char config_path_dir[MAX_PATH_LEN];

TConfig g_Config;
pthread_attr_t attr;

void report_result(int64_t llRspTimeUs, int iMyID);
void thread_clean_func(void* arg);
void calculate(int64_t iSec, int64_t curTime, int64_t lastTime);
void printSummary();
void wait_threads_exit();
int sleepAndCheck(int64_t sleepEndTimeUs, int64_t curTime);

/*****************************************************************************
 函 数 名  : thread_func
 功能描述  : 线程函数
*****************************************************************************/
void* thread_func(void* arg) {
  // TConfig* config = (TConfig*)arg;
  int iMyID = *static_cast<int*>(arg);
  if (threads_array[iMyID].call_numbers == 0) {
    thread_running[iMyID] = 0;
    return NULL;
  }

  thread_running[iMyID] = 1;
  pthread_cleanup_push(thread_clean_func, &iMyID);
  // 初始化随机数产生器
  srand((unsigned)((iMyID + 1) * time(NULL)));

  // 每个线程启动前先等一个时间，各线程等待时间不同
  // 使请求均匀的分布，不然并发连接太多会有部分连接失败
  int iStartWaitus;
  if (g_Config.m_iThreadSleepUs > 0) {
    // 需要先除再乘，不然可能会越界，整数上限2147483647
    if (INT_MAX / g_Config.m_iThreadSleepUs < iMyID) {
      iStartWaitus = g_Config.m_iThreadSleepUs / g_Config.m_iThreadNum * iMyID;
    } else {
      iStartWaitus = g_Config.m_iThreadSleepUs * iMyID / g_Config.m_iThreadNum;
    }
  } else {
    iStartWaitus = 10000 * iMyID / g_Config.m_iThreadNum;
  }
  usSleep(iStartWaitus);

  CUserFunc* pUserFunc = NULL;
  try {
    pUserFunc = new CUserFunc(iMyID, &g_Config);
    pUserFunc->setTimeEnd(&isTimeEnd);
  } catch (char* err) {
    printf("ERR: thread %d run error: %s\n", iMyID, err);
    free(err);
    delete pUserFunc;
    thread_running[iMyID] = 0;
    return NULL;
  } catch (...) {
    printf("ERR: thread %d can not run!\n", iMyID);
    delete pUserFunc;
    thread_running[iMyID] = 0;
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
    } catch (char* err) {
      llRspTimeUs = 0;
      printf("ERR: thread %d run error: %s\n", iMyID, err);
      free(err);
      usSleep(100000);
    } catch (...) {
      llRspTimeUs = 0;
      printf("ERR: thread %d run error!\n", iMyID);
      usSleep(100000);
    }
    report_result(llRspTimeUs, iMyID);

    if (threads_array[iMyID].call_numbers >= 0) {
      if (llRspTimeUs >= -1) {  // -2说明未建立连接，不统计
        threads_array[iMyID].call_numbers--;
        if (threads_array[iMyID].call_numbers <= 0) {
          break;
        }
      }
    }
    if (g_Config.m_iThreadSleepUs > 0) {
      usSleep(g_Config.m_iThreadSleepUs);
    }
  }

  delete pUserFunc;
  pthread_cleanup_pop(1);
  return NULL;
}

/** 上报单位时间结果, 用于周期性打印 */
void report_result(int64_t llRspTimeUs, int iMyID) {
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
    Results_Now[iMyID].iBadRespNum++;
  } else if (llRspTimeUs == 0) {
    Results_Now[iMyID].iAllReqNum++;
    Results_Now[iMyID].iNoResponseNum++;
  } else {
    if (!isTimeEnd) {  // 结束不认为是失败
      // Results_Now[iMyID].iAllReqNum++;  // -2说明未建立连接，不统计
      Results_Now[iMyID].iConnFailNum++;
    }
  }
}

/**
 用于线程结束时处理线程标识
 */
void thread_clean_func(void* arg) {
  int threadID = *static_cast<int*>(arg);
  thread_running[threadID] = 0;
}

void initConfigPath(int argc, char** argv) {
  // 获取参数
  int maxPathLen = sizeof(config_path_dir) - strlen(CFGFILE) - 1;

  if (*(argv[0]) == '/') {
    memcpy(config_path_dir, argv[0], strlen(argv[0]) + 1);
  } else {
    char* ptr = getcwd(config_path_dir, maxPathLen);
    if (ptr == NULL) {
      int ret = readlink("/proc/self/exe", config_path_dir, maxPathLen);
      if (ret < 0 || ret >= maxPathLen) {  // 等于maxPathLen说明长度超过了PATH_MAX
        // 出错了，结束程序
        printf("get conf file path failed\n");
        fflush(stdout);
        exit(1);
      }
      config_path_dir[ret] = '\0';  // readlink返回字符串不是以\0结尾
    } else {
      strncat(config_path_dir, "/", 2);  // getcwd获取的路径最后没有'/'
      if (strlen(argv[0]) > 1 && argv[0][0] == '.' && argv[0][1] == '/') {
        strncat(config_path_dir, argv[0] + 2, strlen(argv[0]) + 1 - 2);
      } else {
        strncat(config_path_dir, argv[0], strlen(argv[0]) + 1);
      }
    }
  }
  // 去掉可执行文件名，得到目录
  char* ptr = strrchr(config_path_dir, '/');
  if (ptr != NULL) {
    *(ptr + 1) = '\0';
  } else {
    memcpy(config_path_dir, "/", 2);
  }

  // int rootPathLen = strlen(config_path_dir);
  strncat(config_path_dir, CFGFILE, strlen(CFGFILE) + 1);
  printf("Config file: %s\n", config_path_dir);
}

void initConfig(TConfig* t_Config) {
  memset(t_Config, 0, sizeof(*t_Config));
  char errLogPath[256];
  TLib_Cfg_GetConfig(
      config_path_dir,  // config file path
      "Host", CFG_STRING, &t_Config->m_szDestIp, "127.0.0.1", sizeof(t_Config->m_szDestIp),  // Host
      "HttpMethod", CFG_STRING, &t_Config->m_szMethod, "GET",
      sizeof(t_Config->m_szMethod),                                   // Method
      "Port", CFG_INT, &t_Config->m_iDestPort, 80,                    // Port
      "ThreadNum", CFG_INT, &t_Config->m_iThreadNum, 1,               // Thread number
      "ThreadSleepMs", CFG_INT, &t_Config->m_iThreadSleepUs, 0,       // sleep time
      "RunDuration", CFG_INT64, &t_Config->m_iRunDuration, 0LL,       // run time
      "CallNumbers", CFG_INT64, &t_Config->m_iCallNumbers, 0LL,       // Call Numbers
      "SampleSecs", CFG_INT64, &t_Config->m_iSampleUs, 1LL,           //
      "TestMode", CFG_INT, &t_Config->m_test_mode, 1,                 //
      "LongConnection", CFG_INT, &t_Config->m_iLongConn, 0,           //
      "PrintError", CFG_STRING, &errLogPath, "", sizeof(errLogPath),  //
      "useDiffPort", CFG_INT, &t_Config->m_iUseDiffPort, 0,           //
      "MsgTimeout", CFG_INT64, &t_Config->m_iTimeout, 60000000LL,     //
      "LingerTime", CFG_INT, &t_Config->m_iLingerTime, 1,             //
      "MsgLen", CFG_INT, &t_Config->m_iLen, 0,                        //
      "GetFile", CFG_STRING, &t_Config->m_pszGetFile, "", sizeof(t_Config->m_pszGetFile),  //
      "GetFileLen", CFG_INT, &t_Config->m_iRecvLen, RECV_MAX_LEN - MAX_HEADER_LEN,         //
      "Domain", CFG_STRING, &t_Config->m_pszHost, "", sizeof(t_Config->m_pszHost),         //

      "SockAddress", CFG_STRING, &t_Config->m_szSockAddr, "", sizeof(t_Config->m_szSockAddr),  //
      "CaCert", CFG_STRING, &t_Config->m_caCert, "", sizeof(t_Config->m_caCert),               //
      "ClientCert", CFG_STRING, &t_Config->m_clientCert, "", sizeof(t_Config->m_clientCert),   //
      "ClientKey", CFG_STRING, &t_Config->m_clientKey, "", sizeof(t_Config->m_clientKey),      //
      "ssl_protocol", CFG_STRING, &t_Config->m_tlsProtocol, "",
      sizeof(t_Config->m_tlsProtocol),                                                         //
      "ssl_ciphers", CFG_STRING, &t_Config->m_tlsCiphers, "", sizeof(t_Config->m_tlsCiphers),  //
      "RspTimeLevel1", CFG_INT, &t_Config->m_iTimeLevel1, 10,                                  //
      "RspTimeLevel2", CFG_INT, &t_Config->m_iTimeLevel2, 100,                                 //
      "RspTimeLevel3", CFG_INT, &t_Config->m_iTimeLevel3, 1000,                                //
      // "double_example", CFG_DOUBLE, &test, 1000.123D,
      NULL);

  if (strcmp(errLogPath, "0") == 0) {
    t_Config->m_iPrintError = 0;
    t_Config->errLogOut = stdout;
  } else if (strcasecmp(errLogPath, "stderr") == 0 || strcmp(errLogPath, "1") == 0) {
    t_Config->m_iPrintError = 1;
    t_Config->errLogOut = stderr;
  } else if (strcasecmp(errLogPath, "stdout") == 0) {
    t_Config->m_iPrintError = 1;
    t_Config->errLogOut = stdout;
  } else {
    t_Config->m_iPrintError = 1;
    t_Config->errLogOut = fopen(errLogPath, "ab");
    if (t_Config->errLogOut == NULL) {
      t_Config->errLogOut = stderr;
    }
  }
}

void initConfigCmd(TConfig* t_Config, int argc, char** argv) {
  /* 根据arg参数个数来判断通过命令行传入的值，每个case都不需要break语句。
     其他参数使用配置文件的值 */
  switch (argc) {
    case 9:
      t_Config->m_iTimeLevel3 = atoi(argv[8]);
    case 8:
      t_Config->m_iTimeLevel2 = atoi(argv[7]);
    case 7:
      t_Config->m_iTimeLevel1 = atoi(argv[6]);
    case 6:
      t_Config->m_iSampleUs = atoi(argv[5]);
    case 5:
      t_Config->m_iRunDuration = atoi(argv[4]);
    case 4:
      t_Config->m_iThreadNum = atoi(argv[3]);
    case 3:
      strncpy(t_Config->m_szDestIp, argv[1], strlen(argv[1]) + 1);
      t_Config->m_iDestPort = atoi(argv[2]);
    case 1:  // 防止执行default
      break;
    default:  // 至少需要2个参数
      printf(
          "presscall ip port [thread_num] [run_second] [print_interval] [time_l1] [time_l2] "
          "[time_l3]\n");
      printf("example:\n");
      printf("presscall 127.0.0.1 8080\n");
      fflush(stdout);
      exit(1);
  }
}

void normally_config(TConfig* t_Config) {
  if (t_Config->m_iLen > SEND_MAX_LEN) {
    printf("WARN: msgLen is too big, please make sure not have 400 or 414 response\n");
    fflush(stdout);
    // exit(2);
  }

  char* ptr = strrchr(config_path_dir, '/');
  char* workDir;
  if (ptr != NULL) {
    workDir = new char[ptr - config_path_dir + 1];
    memcpy(workDir, config_path_dir, ptr - config_path_dir);
    workDir[ptr - config_path_dir] = '\0';
  } else {
    workDir = new char[2];
    memcpy(workDir, ".", 2);
  }

  if (strlen(t_Config->m_caCert) > 0 && t_Config->m_caCert[0] != '/' &&
      strlen(t_Config->m_caCert) + strlen(workDir) < sizeof(t_Config->m_caCert)) {
    memmove(t_Config->m_caCert + strlen(workDir) + 1, t_Config->m_caCert,
            strlen(t_Config->m_caCert) + 1);
    memcpy(t_Config->m_caCert, workDir, strlen(workDir));
    t_Config->m_caCert[strlen(workDir)] = '/';
  }
  if (strlen(t_Config->m_clientCert) > 0 && t_Config->m_clientCert[0] != '/' &&
      strlen(t_Config->m_clientCert) + strlen(workDir) < sizeof(t_Config->m_clientCert)) {
    memmove(t_Config->m_clientCert + strlen(workDir) + 1, t_Config->m_clientCert,
            strlen(t_Config->m_clientCert) + 1);
    memcpy(t_Config->m_clientCert, workDir, strlen(workDir));
    t_Config->m_clientCert[strlen(workDir)] = '/';
  }
  if (strlen(t_Config->m_clientKey) > 0) {
    if (t_Config->m_clientKey[0] != '/' &&
        strlen(t_Config->m_clientKey) + strlen(workDir) < sizeof(t_Config->m_clientKey)) {
      memmove(t_Config->m_clientKey + strlen(workDir) + 1, t_Config->m_clientKey,
              strlen(t_Config->m_clientKey) + 1);
      memcpy(t_Config->m_clientKey, workDir, strlen(workDir));
      t_Config->m_clientKey[strlen(workDir)] = '/';
    }
  } else if (strlen(t_Config->m_clientCert) > 0) {
    memcpy(t_Config->m_clientKey, t_Config->m_clientCert, strlen(t_Config->m_clientCert) + 1);
  }
  delete[] workDir;

  if (t_Config->m_pszGetFile[0] != '/') {
    t_Config->m_pszGetFile[strlen(t_Config->m_pszGetFile) + 1] = '\0';
    for (int i = strlen(t_Config->m_pszGetFile); i > 0; i--) {
      t_Config->m_pszGetFile[i] = t_Config->m_pszGetFile[i - 1];
    }
    t_Config->m_pszGetFile[0] = '/';
  }

  if (strlen(t_Config->m_pszHost) == 0) {
    if (strstr(t_Config->m_szDestIp, ":")) {
      snprintf(t_Config->m_pszHost, sizeof(t_Config->m_pszHost), "[%s]:%d", t_Config->m_szDestIp,
               t_Config->m_iDestPort);
    } else {
      snprintf(t_Config->m_pszHost, sizeof(t_Config->m_pszHost), "%s:%d", t_Config->m_szDestIp,
               t_Config->m_iDestPort);
    }
  }

  char tmpPort[16];
  if (t_Config->m_iUseDiffPort == 0 || t_Config->m_iThreadNum <= 1) {
    snprintf(tmpPort, sizeof(tmpPort), "%d", t_Config->m_iDestPort);
  } else {
    snprintf(tmpPort, sizeof(tmpPort), "[%d-%d]", t_Config->m_iDestPort,
             t_Config->m_iDestPort + t_Config->m_iThreadNum - 1);
  }

  if (strstr(t_Config->m_szDestIp, ":") != NULL) {
    printf("%s %s://[%s]:%s%s, Domain: %s\n", t_Config->m_szMethod,
           t_Config->m_test_mode == 1 ? "http" : (t_Config->m_test_mode == 2 ? "https" : "tcp"),
           t_Config->m_szDestIp, tmpPort, t_Config->m_pszGetFile, t_Config->m_pszHost);
  } else {
    printf("%s %s://%s:%s%s, Domain: %s\n", t_Config->m_szMethod,
           t_Config->m_test_mode == 1 ? "http" : (t_Config->m_test_mode == 2 ? "https" : "tcp"),
           t_Config->m_szDestIp, tmpPort, t_Config->m_pszGetFile, t_Config->m_pszHost);
  }
  printf("LongConnection %d  MsgLen %d  every %dms  duration %lus level(ms):%d,%d,%d\n",
         t_Config->m_iLongConn, t_Config->m_iLen, t_Config->m_iThreadSleepUs,
         t_Config->m_iRunDuration, t_Config->m_iTimeLevel1, t_Config->m_iTimeLevel2,
         t_Config->m_iTimeLevel3);
  fflush(stdout);

  if (t_Config->m_iSampleUs > 0 && LLONG_MAX / 1000000 > t_Config->m_iSampleUs) {
    t_Config->m_iSampleUs = t_Config->m_iSampleUs * 1000000;
  } else {
    t_Config->m_iSampleUs = 1000000;
  }
  t_Config->m_iThreadSleepUs = t_Config->m_iThreadSleepUs * 1000;
  t_Config->m_iTimeLevel1 = t_Config->m_iTimeLevel1 * 1000;
  t_Config->m_iTimeLevel2 = t_Config->m_iTimeLevel2 * 1000;
  t_Config->m_iTimeLevel3 = t_Config->m_iTimeLevel3 * 1000;
  if (t_Config->m_iRecvLen <= 0) {
    t_Config->m_iRecvLen = RECV_MAX_LEN;
  } else {
    t_Config->m_iRecvLen = t_Config->m_iRecvLen + MAX_HEADER_LEN;
  }
  if (t_Config->m_iThreadNum <= 0) {
    t_Config->m_iThreadNum = 1;
  }
  if (t_Config->m_iLen < 0) {
    t_Config->m_iLen = 0;
  }
}

void normally_ip(TConfig* t_Config) {
  t_Config->sockAddr = NULL;
  if (strlen(t_Config->m_szSockAddr) > 0) {
    if (strstr(t_Config->m_szSockAddr, ":") != NULL) {
      struct addrinfo addrCriteria;
      memset(&addrCriteria, 0, sizeof(addrCriteria));
      addrCriteria.ai_family = AF_UNSPEC;
      addrCriteria.ai_socktype = SOCK_STREAM;
      addrCriteria.ai_flags = AI_PASSIVE;
      addrCriteria.ai_protocol = IPPROTO_TCP;
      struct addrinfo* addrList;
      int retVal =
          getaddrinfo((const char*)&t_Config->m_szSockAddr, NULL, &addrCriteria, &addrList);
      if (retVal != 0) {
        printf("Get local address error: %s\n", gai_strerror(retVal));
        fflush(stdout);
        exit(2);
      }

      struct sockaddr_in6* client6 = new sockaddr_in6();
      if (addrList != NULL) {
        memcpy(client6, (struct sockaddr*)(addrList->ai_addr), sizeof(struct sockaddr_in6));
        freeaddrinfo(addrList);
      } else {
        client6->sin6_family = AF_INET6;
        client6->sin6_port = htons(0);
        client6->sin6_scope_id = 0;
        if (inet_pton(AF_INET6, t_Config->m_szSockAddr, &client6->sin6_addr) <= 0) {
          printf("V6Host is incorrect: %s\n", t_Config->m_szSockAddr);
          fflush(stdout);
          exit(2);
        }
      }
      t_Config->sockAddr = (struct sockaddr*)client6;
    } else {
      struct sockaddr_in* client4 = new sockaddr_in();
      client4->sin_family = AF_INET;
      client4->sin_port = htons(0);
      client4->sin_addr.s_addr = inet_addr(t_Config->m_szSockAddr);
      t_Config->sockAddr = (struct sockaddr*)client4;
    }
  }
}

extern "C" void sigPIPE(int nSignal) {
  // fprintf(g_Config.errLogOut, "\nGet signal: %d, do nothing.\n", nSignal);
}

/*****************************************************************************
 函 数 名  : child_func
 功能描述  : 创建线程函数
*****************************************************************************/
void* child_func(void* arg) {
  int ret, realThreadNum = 0;
  int64_t callPerThread = -1;
  int64_t remain = -1;
  if (g_Config.m_iCallNumbers > 0) {
    callPerThread = g_Config.m_iCallNumbers / g_Config.m_iThreadNum;
    remain = g_Config.m_iCallNumbers - callPerThread * g_Config.m_iThreadNum;
  }
  isStarted = true;
  // 创建线程
  for (int i = 0; i < g_Config.m_iThreadNum; i++) {
    if ((int64_t)i < remain) {
      threads_array[i].call_numbers = callPerThread + 1;
    } else {
      if (callPerThread == 0L) {
        // 请求数为0，不需要起线程
        thread_running[i] = 0;
        continue;
      }
      threads_array[i].call_numbers = callPerThread;
    }

    threads_array[i].thread_id = i;
    ret = pthread_create(&threads_array[i].thread_pid, &attr, thread_func,
                         &threads_array[i].thread_id);
    if (ret != 0) {
      threads_array[i].thread_pid = 0;
      if (g_Config.m_iPrintError) {
        fprintf(g_Config.errLogOut, "create thread failed :%d[%d]\n", ret, i);
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

  time_t rawtime;
  struct tm p_tm_time;
  char str_time[20];
  time(&rawtime);
  localtime_r(&rawtime, &p_tm_time);
  strftime(str_time, sizeof(str_time), "%Y-%m-%d %H:%M:%S", &p_tm_time);

  printf("%s started, %d threads running, will %sprint error message ...\n", str_time,
         realThreadNum, g_Config.m_iPrintError ? "" : "not ");
  printf(
      "\nTIME      OK/NO/BAD/NOCONN/ALL=PERCENT      TPS     AvgTime(ms)  MaxTime(ms) "
      " <%3dms  <%3dms  <%3dms  >%3dms\n",
      g_Config.m_iTimeLevel1 / 1000, g_Config.m_iTimeLevel2 / 1000, g_Config.m_iTimeLevel3 / 1000,
      g_Config.m_iTimeLevel3 / 1000);
  fflush(stdout);

  int64_t runStartTime, curTime, sleepEndTimeUs, runEndTime;
  curTime = runStartTime = sleepEndTimeUs = getCurrentTimeUs();
  if (g_Config.m_iRunDuration <= 0 || g_Config.m_iRunDuration > (LLONG_MAX - curTime) / 1000000) {
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
      if (sleepAndCheck(sleepEndTimeUs, curTime) < 0) {
        // sleep被信号打断
        sleepEndTimeUs = getCurrentTimeUs();
      }
    } else {
      // 执行时间超过sleep time
      sleepEndTimeUs = curTime;
    }
    iUsecRuned = sleepEndTimeUs - runStartTime;
    calculate(iUsecRuned, sleepEndTimeUs, g_Config.m_iSampleUs);
  }

  return NULL;
}

int main(int argc, char** argv) {
  initConfigPath(argc, argv);
  initConfig(&g_Config);
  initConfigCmd(&g_Config, argc, argv);
  normally_config(&g_Config);
  normally_ip(&g_Config);
  initAhead(&g_Config);

  // 注册信号屏蔽集，子线程将继承该屏蔽集
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGHUP);  // 1 前台运行，终端关闭时退出；后台运行时，支持重新加载配置
  sigaddset(&mask, SIGINT);   // 2 Ctrl+C
  sigaddset(&mask, SIGQUIT);  // kill -3 Ctrl+\ dump内存
  // sigaddset(&mask, SIGTRAP);  // kill -5 调试开关
  sigaddset(&mask, SIGABRT);  // kill -6
  sigaddset(&mask, SIGKILL);  // kill -9
  sigaddset(&mask, SIGTERM);  // kill -15
  // sigaddset(&mask, SIGCONT);  // 18
  // sigaddset(&mask, SIGSTOP);  // 19
  // sigaddset(&mask, SIGTSTP);  // 20，Ctrl+Z，可捕获的stop信号. 可以用CONT信号继续运行
  int err = pthread_sigmask(SIG_BLOCK, &mask, NULL);
  if (err != 0) {
    printf("ERR: set sigmask failed: %s\n", strerror(err));
  }
  // 待忽略的信号。所有子线程继承父线程屏蔽集和动作，且所有线程共享修改，但忽略和屏蔽有不同：
  // 屏蔽是线程不会收到该信号，所以不会导致如sleep、receive等中断
  // 忽略线程会收到信号，只是没有动作
  // for (int i = 1; i <= NSIG; i++) {
  //   // SIG_DFL默认，SIG_IGN忽略信号
  //   signal(i, SIG_IGN);
  // }
  signal(SIGPIPE, sigPIPE);  // 13 该信号不能屏蔽，单独设置该信号处理函数

  // ----数据初始化
  Results_Now = new TResult[g_Config.m_iThreadNum];
  threads_array = new ThreadArray[g_Config.m_iThreadNum];
  thread_running = new int[g_Config.m_iThreadNum];
  for (int i = 0; i < g_Config.m_iThreadNum; i++) {
    memset(&Results_Now[i], 0, sizeof(TResult));
    memset(&threads_array[i], 0, sizeof(ThreadArray));
    thread_running[i] = 1;
  }

  // 修改栈大小可能导致莫名其妙的Segmentation fault，如果出现，可以改大
  // 栈大小最小值16384，即16KB，默认8MB
  int ret, stacksize = 16384;
  // 初始化线程属性
  pthread_attr_init(&attr);
  // 设置线程栈大小
  pthread_attr_setstacksize(&attr, stacksize);
  // 创建线程
  pthread_t tidp;
  ret = pthread_create(&tidp, &attr, child_func, &mask);
  if (ret != 0) {
    if (g_Config.m_iPrintError) {
      fprintf(g_Config.errLogOut, "create main thread failed :%d\n", ret);
    }
  }

  int sig;
  while (!isTimeEnd) {
    err = sigwait(&mask, &sig);
    if (err == 0) {
      switch (sig) {
        case SIGHUP:  // 1 前台运行，终端关闭时退出；后台运行时，支持重新加载配置
          if (getpgrp() != tcgetpgrp(STDIN_FILENO) || isatty(0) == 0) {
            // 1 后台运行时，支持重新加载配置
            printf("Reload config from file: %s\n", config_path_dir);
            TConfig tmp_Config;
            initConfig(&tmp_Config);
            normally_config(&tmp_Config);
            normally_ip(&tmp_Config);

            FILE* old = g_Config.errLogOut;
            memcpy(&g_Config, &tmp_Config, sizeof(g_Config));
            if (old != stdout && old != stderr) {
              fclose(old);
            }
            break;
          }
          // else 1 前台运行，终端关闭时退出
        case SIGINT:     // 2 Ctrl+C
        case SIGQUIT:    // kill -3 Ctrl+\ dump内存
        case SIGABRT:    // kill -6
        case SIGKILL:    // kill -9
        case SIGTERM: {  // kill -15
          isTimeEnd = true;
          g_Config.m_iThreadSleepUs = 0;
          stopTimeout /= 2;
          pthread_kill(tidp, SIGPIPE);  // kill -13
          fprintf(g_Config.errLogOut, "\nGet signal: %d, quit threads ...\n", sig);
          break;
        }
        default: {
          fprintf(g_Config.errLogOut, "\nGet signal: %d, ignore.\n", sig);
        }
      }
    }
  }

  // 等待线程结束
  while (pthread_join(tidp, NULL) != 0) {
    // do nothing
  }
  wait_threads_exit();

  // 释放所有申请的内存
  destroyEnd(&g_Config);
  fclose(g_Config.errLogOut);
  delete g_Config.sockAddr;
  pthread_attr_destroy(&attr); /* 不再使用线程属性，将其销毁 */
  delete[] threads_array;
  delete[] thread_running;
  delete[] Results_Now;
  return 0;
}

/*****************************************************************************
 功能描述  : sleep并同时检测子线程是否结束, -1未sleep完成即结束，0完成
*****************************************************************************/
int sleepAndCheck(int64_t sleepEndTimeUs, int64_t curTime) {
  if (g_Config.m_iCallNumbers <= 0) {
    if (usSleep(sleepEndTimeUs - curTime) < 0) {
      // sleep被打断
      return -1;
    }
    return 0;
  }
  while (curTime < sleepEndTimeUs) {
    if (usSleep(tmin(sleepEndTimeUs - curTime, 9000)) < 0) {  // 10000=10ms
      // sleep被打断
      return -1;
    }

    // 检测子进程是否全部退出
    bool allEnd = true;
    for (int i = 0; i < g_Config.m_iThreadNum; i++) {
      if (thread_running[i] > 0) {
        allEnd = false;
        break;
      }
    }
    if (allEnd || isTimeEnd) {
      isTimeEnd = true;
      return -1;
    }

    curTime = getCurrentTimeUs();
  }
  return 0;
}
/*****************************************************************************
 函 数 名  : calculate
 功能描述  : 采样函数
*****************************************************************************/
void calculate(int64_t iUsec, int64_t curTime, int64_t lastTime) {
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
    m_AllResultHistory.iBadRespNum += m_ResultTmp.iBadRespNum;
    m_AllResultHistory.iConnFailNum += m_ResultTmp.iConnFailNum;
    m_AllResultHistory.m_iTimeL1Num += m_ResultTmp.m_iTimeL1Num;
    m_AllResultHistory.m_iTimeL2Num += m_ResultTmp.m_iTimeL2Num;
    m_AllResultHistory.m_iTimeL3Num += m_ResultTmp.m_iTimeL3Num;
    m_AllResultHistory.m_iTimeL4Num += m_ResultTmp.m_iTimeL4Num;
  }

  // 计算本周期结果
  m_ResultTmp.iAllReqNum = m_AllResultHistory.iAllReqNum - m_AllResultLast.iAllReqNum;
  m_ResultTmp.iOkResponseNum = m_AllResultHistory.iOkResponseNum - m_AllResultLast.iOkResponseNum;
  m_ResultTmp.iNoResponseNum = m_AllResultHistory.iNoResponseNum - m_AllResultLast.iNoResponseNum;
  m_ResultTmp.iBadRespNum = m_AllResultHistory.iBadRespNum - m_AllResultLast.iBadRespNum;
  m_ResultTmp.iConnFailNum = m_AllResultHistory.iConnFailNum - m_AllResultLast.iConnFailNum;
  m_ResultTmp.dSumRspTimeUs = m_AllResultHistory.dSumRspTimeUs - m_AllResultLast.dSumRspTimeUs;

  m_ResultTmp.llMaxRspTimeUs = m_AllResultHistory.llMaxRspTimeUs;
  if (m_AllResultLast.llMaxRspTimeUs > m_AllResultHistory.llMaxRspTimeUs)
    m_AllResultHistory.llMaxRspTimeUs = m_AllResultLast.llMaxRspTimeUs;

  m_ResultTmp.m_iTimeL1Num = m_AllResultHistory.m_iTimeL1Num - m_AllResultLast.m_iTimeL1Num;
  m_ResultTmp.m_iTimeL2Num = m_AllResultHistory.m_iTimeL2Num - m_AllResultLast.m_iTimeL2Num;
  m_ResultTmp.m_iTimeL3Num = m_AllResultHistory.m_iTimeL3Num - m_AllResultLast.m_iTimeL3Num;
  m_ResultTmp.m_iTimeL4Num = m_AllResultHistory.m_iTimeL4Num - m_AllResultLast.m_iTimeL4Num;

  // 打印本周期结果
  char percentStr[64];
  snprintf(
      percentStr, sizeof(percentStr), "%lu/%lu/%lu/%lu/%lu=%.2f%%", m_ResultTmp.iOkResponseNum,
      m_ResultTmp.iNoResponseNum, m_ResultTmp.iBadRespNum, m_ResultTmp.iConnFailNum,
      m_ResultTmp.iAllReqNum,
      m_ResultTmp.iAllReqNum > 0 ? m_ResultTmp.iOkResponseNum * 100.0 / m_ResultTmp.iAllReqNum : 0);
  // localtime非多线程安全，localtime_r多线程安全但有锁的性能问题
  time_t time = curTime / 1000000;
  struct tm p_tm_time;
  localtime_r(&time, &p_tm_time);
  printf("%02d:%02d:%02d  %-30s% 10.2f% 11.3fms% 11.3fms %7lu %7lu %7lu %7lu\n", p_tm_time.tm_hour,
         p_tm_time.tm_min, p_tm_time.tm_sec, percentStr,
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
  for (int i = 0; i < g_Config.m_iThreadNum; i++) {
    if (thread_running[i] > 0) {
      pthread_kill(threads_array[i].thread_pid, SIGPIPE);  // kill -13
    }
  }

  // 等待线程结束或超时
  int waitTime = 10000;  // 10ms
  int i;
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
          fprintf(g_Config.errLogOut, "WARN: stop timeout, kill thread\n");
          // 打印汇总结果
          printSummary();
          hasPrinted = true;
        }

        // 线程中没有中断的情况下会导致cancel不生效，改用kill
        // pthread_cancel(threads_array[i].thread_pid);
        pthread_kill(threads_array[i].thread_pid, SIGKILL);  // kill -9
      }
    }
  } catch (...) {
    fprintf(g_Config.errLogOut, "ERR: pthread_cancel failed\n");
  }

  // 等待线程结束
  for (; left < g_Config.m_iThreadNum; left++) {
    void* joinError = NULL;
    if (thread_running[left] > 0 && pthread_join(threads_array[left].thread_pid, &joinError) != 0) {
      left--;  // 继续等该线程
      if (g_Config.m_iPrintError) {
        fprintf(g_Config.errLogOut, "Wait thread to stop failed:%d, error:%s\n", left,
                reinterpret_cast<char*>(joinError));
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

  double percent = 0.0;
  if (m_AllResultHistory.iAllReqNum > 0) {
    percent = m_AllResultHistory.iOkResponseNum * 100.0 / m_AllResultHistory.iAllReqNum;
  }
  printf("Success percent: %.2f%%\n", percent);

  double averageResponseTime = 0.0;
  if (m_AllResultHistory.iOkResponseNum > 0) {
    averageResponseTime =
        m_AllResultHistory.dSumRspTimeUs / 1000.0 / m_AllResultHistory.iOkResponseNum;
  }
  printf("Max response time: %.3fms\n", m_AllResultHistory.llMaxRspTimeUs / 1000.0);
  printf("Average response time: %.3fms\n", averageResponseTime);

  // need include <cinttypes> and add -std=c++0x to makefile
  // printf("Running time(hour:min:sec): %" PRIu64 ":%02" PRIu64 ":%06.3f\n", iUsecRuned /
  // 3600000000,
  //        (iUsecRuned / 60000000) % 60, (iUsecRuned % 60000000) / 1000000.0);

  printf("Running time(hour:min:sec): %lu:%02lu:%06.3f\n", iUsecRuned / 3600000000,
         (iUsecRuned / 60000000) % 60, (iUsecRuned % 60000000) / 1000000.0);

  // All request需要把关闭期间的数据汇总起来
  memset(&m_AllResultHistory, 0, sizeof(TResult));
  for (int i = 0; i < g_Config.m_iThreadNum; i++) {
    m_AllResultHistory.iAllReqNum += Results_Now[i].iAllReqNum;
    m_AllResultHistory.iOkResponseNum += Results_Now[i].iOkResponseNum;
    m_AllResultHistory.iNoResponseNum += Results_Now[i].iNoResponseNum;
    m_AllResultHistory.iBadRespNum += Results_Now[i].iBadRespNum;
    m_AllResultHistory.iNoResponseNum += Results_Now[i].iNoResponseNum;
    m_AllResultHistory.iConnFailNum += Results_Now[i].iConnFailNum;
  }

  printf("All request : %lu\n", m_AllResultHistory.iAllReqNum);
  printf("OK response : %lu\n", m_AllResultHistory.iOkResponseNum);
  printf("Bad response: %lu\n", m_AllResultHistory.iBadRespNum);
  printf("No response : %lu\n", m_AllResultHistory.iNoResponseNum);
  printf("Connect fail: %lu\n", m_AllResultHistory.iConnFailNum);
  fflush(stdout);
}
