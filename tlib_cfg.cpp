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
  char* p = str + strlen(str) - 1;
  while (p >= str && (*p <= ' ')) {
    --p;
  }
  *(p + 1) = '\0';

  p = str;
  while (*p > '\0' && *p <= ' ') {
    p++;
  }

  return p;
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
        strncpy(sVal, sDefault, lSize);
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
  char *sParam, *sVal;
  double* pdVal;
  int64_t* plVal;
  int iType, *piVal;
  int32_t lSize;

  sParam = va_arg(ap, char*);
  while (sParam != NULL) {
    iType = va_arg(ap, int);
    if (strcmp(sP, sParam) == 0) {
      switch (iType) {
        case CFG_STRING:
          sVal = va_arg(ap, char*);
          va_arg(ap, char*);
          lSize = va_arg(ap, int32_t);
          strncpy(sVal, sV, lSize);
          break;
        case CFG_INT64:
          plVal = va_arg(ap, int64_t*);
          va_arg(ap, int64_t);
          *plVal = atoll(sV);
          break;
        case CFG_INT:
          piVal = va_arg(ap, int*);
          va_arg(ap, int);
          *piVal = atoi(sV);
          break;
        case CFG_DOUBLE:
          pdVal = va_arg(ap, double*);
          va_arg(ap, double);
          *pdVal = atof(sV);
          break;
      }
      return;
    } else {
      switch (iType) {
        case CFG_STRING:
          va_arg(ap, char*);
          va_arg(ap, char*);
          va_arg(ap, int32_t);
          break;
        case CFG_INT64:
          va_arg(ap, int64_t*);
          va_arg(ap, int64_t);
          break;
        case CFG_INT:
          va_arg(ap, int*);
          va_arg(ap, int);
          break;
        case CFG_DOUBLE:
          va_arg(ap, double*);
          va_arg(ap, double);
          break;
      }
    }
    sParam = va_arg(ap, char*);
  }
}

static char* _Cfg_GetParamVal(char* sParam) {
  if (*sParam == '#' || *sParam == '\0') {
    return NULL;
  }

  char* pEnd = sParam + strlen(sParam);
  char* sVal = sParam;
  while (*sVal != ' ' && *sVal != '\t' && *sVal != '\n' && *sVal != '\0') {
    sVal++;
  }
  *sVal = '\0';

  if (sVal < pEnd) {
    sVal++;
    while (*sVal == ' ' || *sVal == '\t' || *sVal == '\n') {
      sVal++;
    }
  }

  return sVal;
}

void TLib_Cfg_GetConfig(const char* sConfigFilePath, ...) {
  FILE* pstFile;
  if ((pstFile = fopen(sConfigFilePath, "r")) == NULL) {
    return;
  }

  va_list ap = {};
  va_start(ap, sConfigFilePath);
  _Cfg_InitDefault(ap);
  va_end(ap);

  char sLine[MAX_CONFIG_LINE_LEN];
  char *sParam, *sVal;
  while (fgets(sLine, MAX_CONFIG_LINE_LEN, pstFile) != NULL) {
    sParam = trim(sLine);
    if ((sVal = _Cfg_GetParamVal(sParam)) != NULL) {
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
  if ((pstFile = fopen(sConfigFilePath, "r")) == NULL) {
    return;
  }

  char sLine[MAX_CONFIG_LINE_LEN];
  char *sParam, *sVal;
  int iLinePos = 0;
  while (fgets(sLine, MAX_CONFIG_LINE_LEN, pstFile) != NULL) {
    sParam = trim(sLine);
    if ((sVal = _Cfg_GetParamVal(sParam)) != NULL) {
      strncpy(pNVStu[iLinePos].szName, sParam, strlen(sParam) + 1);
      strncpy(pNVStu[iLinePos].szVal, sVal, strlen(sVal) + 1);
      iLinePos++;
    }
  }

  fclose(pstFile);
}
