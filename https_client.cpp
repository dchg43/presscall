/**
 @Copyright (c) 2019, chaoguo All rights reserved.
*/
#include "https_client.h"

#include <errno.h>
#include <pthread.h>
#include <unistd.h>

static SSL_CTX* ctx = NULL;
// OpenSSL非多线程安全，需要进行多线程保护的初始化
static pthread_mutex_t* lock_cs = NULL;

// thread id 回调函数，由openssl回调，向 openssl 库提供当前线程号
uint64_t pthreads_thread_id(void) {
  return pthread_self();
}

// locking 回调函数，由openssl库回调，向 openssl 库 提供 lock/unlock，或更详细些
// read lock 或 write lock 的功能 Locking callback. The type, file and line
// arguments are ignored. The file and line may be used to identify the site of
// the call in the OpenSSL library for diagnostic purposes if required.
void pthreads_locking_callback(int mode, int type, const char* file, int line) {
  if (mode & CRYPTO_LOCK) {
    pthread_mutex_lock(&(lock_cs[type]));
  } else {
    pthread_mutex_unlock(&(lock_cs[type]));
  }
}

void ShowCerts(SSL* ssl) {
  X509* cert = NULL;
  char* line = NULL;

  cert = SSL_get_peer_certificate(ssl);
  if (cert != NULL) {
    printf("Digital certificate information:\n");
    line = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
    printf("Certificate: %s\n", line);
    free(line);
    line = X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0);
    printf("Issuer: %s\n", line);
    free(line);
    X509_free(cert);
  } else {
    printf("No certificate information！\n");
  }
}

void initSSL(TConfig* g_Config) {
#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L
#  ifndef SSL_load_error_strings
#    define SSL_load_error_strings() \
      OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL)
#  endif
#  ifndef OpenSSL_add_all_algorithms
#    define OpenSSL_add_all_algorithms() \
      OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL)
#  endif
#endif
  // https://www.jianshu.com/p/61dba20d6e66
  SSL_library_init();        // SSL 库初始化
  SSL_load_error_strings();  // 提供将错误号解析为字符串的功能
  // 设置协议
  const SSL_METHOD* method = NULL;
  if (strcasecmp(g_Config->m_tlsProtocol, "tls1") == 0) {
    method = TLSv1_client_method();
  } else if (strcasecmp(g_Config->m_tlsProtocol, "tls1_1") == 0) {
    method = TLSv1_1_client_method();
  } else if (strcasecmp(g_Config->m_tlsProtocol, "tls1_2") == 0) {
    method = TLSv1_2_client_method();
  } else if (strcasecmp(g_Config->m_tlsProtocol, "tls1_3") == 0) {
    method = SSLv3_client_method();
  } else {
    method = SSLv23_client_method();  // openssl 1.1中同TLS_client_method(新增)
  }
  // 初始化ssl
  ctx = SSL_CTX_new(method);
  // 设置支持的算法
  if (strcmp(g_Config->m_tlsCiphers, "") == 0 ||
      SSL_CTX_set_cipher_list(ctx, g_Config->m_tlsCiphers) != 1) {
    OpenSSL_add_all_algorithms();  // 设置失败则支持所有算法
  }

  if (strlen(g_Config->m_caCert) > 0) {
    if (access(g_Config->m_caCert, R_OK) != -1) {
      // 设置CA证书，用于验证服务端证书
      SSL_CTX_load_verify_locations(ctx, g_Config->m_caCert, NULL);
      // 设置是否验证服务端证书
      // SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
      // SSL_CTX_set_verify_depth(ctx, 0);
      SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
      SSL_CTX_set_verify_depth(ctx, 0);
      SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
      SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);
      // SSL_CTX_set_default_passwd_cb_userdata(ctx,
      //         const_cast<void*>(static_cast<const void*>("12345")));
    } else {
      printf("CA file not exists or cannot read: %s\n", g_Config->m_caCert);
    }
  }

  if (strlen(g_Config->m_clientCert) > 0) {
    if (access(g_Config->m_clientCert, R_OK) != -1) {
      // 客户端证书，双向认证时需要
      SSL_CTX_use_certificate_file(ctx, g_Config->m_clientCert, SSL_FILETYPE_PEM);

      if (strlen(g_Config->m_clientKey) > 0) {
        if (access(g_Config->m_clientKey, R_OK) != -1) {
          // 客户端密钥文件，双向认证时需要
          SSL_CTX_use_PrivateKey_file(ctx, g_Config->m_clientKey, SSL_FILETYPE_PEM);
          // 验证密钥是否与证书一致
          SSL_CTX_check_private_key(ctx);
        } else {
          printf("ClientKey file not exists or cannot read: %s\n", g_Config->m_clientKey);
        }
      }
    } else {
      printf("ClientCert file not exists or cannot read: %s\n", g_Config->m_clientCert);
    }
  }

  // OpenSSL非多线程安全，需要进行多线程保护的初始化
  lock_cs = reinterpret_cast<pthread_mutex_t*>(
      OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t)));
  if (!lock_cs) {
    /* Nothing we can do about this...void function! */
    return;
  }
  // lock 数组初始化
  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    pthread_mutex_init(&(lock_cs[i]), NULL);
  }
  CRYPTO_set_id_callback((uint64_t(*)())pthreads_thread_id);
  CRYPTO_set_locking_callback(pthreads_locking_callback);
}

void cleanupSSL() {
  SSL_CTX_free(ctx);
  // 清空 locking 回调函数
  // Deregister locking callback. No real need to deregister id callback.
  CRYPTO_set_locking_callback(NULL);
  // 销毁初始化时分配的 lock 数组
  // Release the lock array.
  for (int i = 0; i < CRYPTO_num_locks(); i++) {
    pthread_mutex_destroy(&(lock_cs[i]));
  }
  OPENSSL_free(lock_cs);
  lock_cs = NULL;
}

int HttpsClient::build_buffer(char* m_pszSendBuff, TConfig* g_Config) {
  return VirtualClient::build_http_buffer(m_pszSendBuff, g_Config);
}

int HttpsClient::checkResponse(const char* m_pszRecvBuff, int64_t unDataLen, int lastRead) {
  return VirtualClient::checkHttpResponse(m_pszRecvBuff, unDataLen, lastRead);
}

bool HttpsClient::connectServer(TConfig* g_Config) {
  if (ssl != NULL) {
    return true;
  }

  m_Config = g_Config;
  bool superResult = VirtualClient::connectServer(g_Config);
  if (!superResult) {
    return false;
  }

  /* 基于 ctx 产生一个新的 SSL */
  ssl = SSL_new(ctx);
  if (ssl == NULL) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "https new ssl failed: %s[%s:%d]", strerror(errno),
             __FILE__, __LINE__);
    disconnect();
    return false;
  }

  /* 关联socket */
  int retVal = SSL_set_fd(ssl, getSocket());
  if (retVal != 1) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "https set fd failed: %s[%s:%d]", strerror(errno),
             __FILE__, __LINE__);
    disconnect();
    return false;
  }

  /* 建立 SSL 连接 */
  SSL_set_tlsext_host_name(ssl, g_Config->m_pszHost);
  if (SSL_connect(ssl) < 0) {
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "ssl connect failed: %s[%s:%d]", strerror(errno),
             __FILE__, __LINE__);
    disconnect();
    return false;
  }
  // else
  // {
  //    printf("Connected with %s encryption\n", SSL_get_cipher(ssl));
  //    ShowCerts(ssl);
  // }

  return true;
}

void HttpsClient::disconnect() {
  if (ssl != NULL) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
    ssl = NULL;
  }
  VirtualClient::disconnect();
}

bool HttpsClient::isConnect() {
  if (!VirtualClient::isConnect()) {
    return false;
  }
  if (ssl == NULL) {
    disconnect();
    return false;
  }
  return true;
}

bool HttpsClient::reconnect() {
  disconnect();
  return connectServer(m_Config);
}

int HttpsClient::readonce(char* pBuff, int64_t iLen) {
  return SSL_read(ssl, pBuff, iLen);
}

int HttpsClient::writeonce(char* pBuff, int64_t iLen) {
  return SSL_write(ssl, pBuff, iLen);
}

/** 返回0，读完；>0没有读完 */
int64_t HttpsClient::isReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                                    int64_t iPkgTheoryLen) {
  return VirtualClient::httpReadComplete(pData, unDataLen, iReceivLenInBuff, iPkgTheoryLen);
}
