/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#include "tlib_cfg.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

int64_t getCurrentTimeUs() {
  timeval curTimeVal;
  gettimeofday(&curTimeVal, NULL);
  return curTimeVal.tv_sec * 1000000 + curTimeVal.tv_usec;
}

char* trim(char* str) {
  char* ps = str;
  while (*ps == ' ' || *ps == '\t' || *ps == '\r' || *ps == '\n') {
    ps++;
  }

  char* p = ps + strlen(ps) - 1;
  while ((*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') && p >= ps) {
    --p;
  }
  *(p + 1) = '\0';

  return ps;
}

static void _Cfg_InitDefault(va_list ap) {
  char *sParam, *sVal, *sDefault;
  double *pdVal, dDefault;
  int64_t *plVal, lDefault;
  int iType, *piVal, iDefault;
  int32_t lSize;

  sParam = va_arg(ap, char*);
  while (sParam != NULL) {
    iType = va_arg(ap, int);
    switch (iType) {
      case CFG_STRING:
        sVal = va_arg(ap, char*);
        sDefault = va_arg(ap, char*);
        lSize = va_arg(ap, int32_t);
        strncpy(sVal, sDefault, lSize - 1);
        sVal[lSize - 1] = 0;
        break;
      case CFG_INT64:
        plVal = va_arg(ap, int64_t*);
        lDefault = va_arg(ap, int64_t);
        *plVal = lDefault;
        break;
      case CFG_INT:
        piVal = va_arg(ap, int*);
        iDefault = va_arg(ap, int);
        *piVal = iDefault;
        break;
      case CFG_DOUBLE:
        pdVal = va_arg(ap, double*);
        dDefault = va_arg(ap, double);
        *pdVal = dDefault;
        break;
    }
    sParam = va_arg(ap, char*);
  }
}

static void _Cfg_SetVal(va_list ap, char* sP, char* sV) {
  char *sParam, *sVal = NULL;
  // char *sDefault;
  double *pdVal = NULL, dDefault;
  int64_t* plVal = NULL;
  // int64_t lDefault;
  int iType, *piVal = NULL, iDefault;
  int32_t lSize = 0;

  sParam = va_arg(ap, char*);
  while (sParam != NULL) {
    iType = va_arg(ap, int);
    switch (iType) {
      case CFG_STRING:
        sVal = va_arg(ap, char*);
        // sDefault = va_arg(ap, char*);
        va_arg(ap, char*);
        lSize = va_arg(ap, int32_t);
        break;
      case CFG_INT64:
        plVal = va_arg(ap, int64_t*);
        // lDefault = va_arg(ap, int64_t);
        va_arg(ap, int64_t);
        if (strcmp(sP, sParam) == 0) {
          *plVal = atoll(sV);
        }
        break;
      case CFG_INT:
        piVal = va_arg(ap, int*);
        iDefault = va_arg(ap, int);
        if (strcmp(sP, sParam) == 0) {
          *piVal = iDefault;
        }
        break;
      case CFG_DOUBLE:
        pdVal = va_arg(ap, double*);
        dDefault = va_arg(ap, double);
        *pdVal = dDefault;
        break;
    }

    if (strcmp(sP, sParam) == 0) {
      switch (iType) {
        case CFG_STRING:
          strncpy(sVal, sV, lSize - 1);
          sVal[lSize - 1] = 0;
          break;
        case CFG_INT64:
          *plVal = atoll(sV);
          break;
        case CFG_INT:
          *piVal = atoi(sV);
          break;
        case CFG_DOUBLE:
          *pdVal = atof(sV);
          break;
      }
      return;
    }

    sParam = va_arg(ap, char*);
  }
}

static int _Cfg_GetParamVal(char* sLine, char* sParam, char* sVal) {
  char* p = sLine;
  while (*p == ' ' || *p == '\t' || *p == '\n') {
    p++;
  }
  if (*p == '#' || *p == '\0') {
    return 1;
  }

  char* pe = p + 1;
  while (*pe != ' ' && *pe != '\t' && *pe != '\n') {
    pe++;
  }
  int paramLen = pe - p;
  strncpy(sParam, p, paramLen);
  *(sParam + paramLen) = '\0';

  pe = trim(pe);
  strncpy(sVal, pe, strlen(pe) + 1);

  return 0;
}

void TLib_Cfg_GetConfig(const char* sConfigFilePath, ...) {
  FILE* pstFile;
  char sLine[MAX_CONFIG_LINE_LEN], sParam[MAX_CONFIG_LINE_LEN], sVal[MAX_CONFIG_LINE_LEN];
  va_list ap = {};

  va_start(ap, sConfigFilePath);
  _Cfg_InitDefault(ap);
  va_end(ap);

  if ((pstFile = fopen(sConfigFilePath, "r")) == NULL) {
    return;
  }

  while (fgets(sLine, MAX_CONFIG_LINE_LEN, pstFile) != NULL) {
    if (_Cfg_GetParamVal(sLine, sParam, sVal) == 0) {
      va_start(ap, sConfigFilePath);
      _Cfg_SetVal(ap, sParam, sVal);
      va_end(ap);
      // printf("'%s=%s'\n", sParam, sVal);  // 打印从文件中获取的参数
    }
  }

  fclose(pstFile);
}

void TLib_Cfg_GetNVConfig(const char* sConfigFilePath, TNVStu* pNVStu) {
  FILE* pstFile;
  char sLine[MAX_CONFIG_LINE_LEN], sParam[MAX_CONFIG_LINE_LEN], sVal[MAX_CONFIG_LINE_LEN];

  if ((pstFile = fopen(sConfigFilePath, "r")) == NULL) {
    return;
  }

  int iLinePos = 0;
  while (1) {
    strncpy(sLine, "", 1);

    fgets(sLine, sizeof(sLine), pstFile);
    if (strcmp(sLine, "") != 0) {
      if (_Cfg_GetParamVal(sLine, sParam, sVal) == 0) {
        strncpy(pNVStu[iLinePos].szName, sParam, strlen(sParam) + 1);
        strncpy(pNVStu[iLinePos].szVal, sVal, strlen(sVal) + 1);
        iLinePos++;
      }
    }

    if (feof(pstFile)) {
      break;
    }
  }

  fclose(pstFile);
}
