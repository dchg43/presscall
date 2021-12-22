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

int verify_callback(int preverify, X509_STORE_CTX* x509_ctx) {
  return preverify;
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

void show_certs(SSL* ssl) {
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
  // https://www.jianshu.com/p/61dba20d6e66
#ifndef OPENSSL_VERSION_NUMBER
#  define OPENSSL_VERSION_NUMBER 0x10000000L
#endif

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
  SSL_library_init();  // SSL 库初始化
  OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);

  // 初始化ssl
  const SSL_METHOD* method = SSLv23_client_method();  // openssl 1.1中同TLS_client_method(新增)
  ctx = SSL_CTX_new(method);

  // 设置协议版本
  int min_version = 0;
  int max_version = 0;
  if (strcasecmp(g_Config->m_tlsProtocol, "tls1") == 0) {
    min_version = TLS1_VERSION;
    max_version = TLS1_VERSION;
  } else if (strcasecmp(g_Config->m_tlsProtocol, "tls1_1") == 0) {
    min_version = TLS1_1_VERSION;
    max_version = TLS1_1_VERSION;
  } else if (strcasecmp(g_Config->m_tlsProtocol, "tls1_2") == 0) {
    min_version = TLS1_2_VERSION;
    max_version = TLS1_2_VERSION;
  } else if (strcasecmp(g_Config->m_tlsProtocol, "tls1_3") == 0) {
    min_version = TLS1_3_VERSION;
    max_version = TLS1_3_VERSION;
  } else if (strcasecmp(g_Config->m_tlsProtocol, "ssl3") == 0) {
    min_version = SSL3_VERSION;
    max_version = SSL3_VERSION;
  }
  if (min_version != 0 &&
      (SSL_CTX_ctrl(ctx, SSL_CTRL_SET_MIN_PROTO_VERSION, min_version, NULL) == 0 ||
       SSL_CTX_ctrl(ctx, SSL_CTRL_SET_MAX_PROTO_VERSION, max_version, NULL) == 0)) {
    printf("Set ssl version failed: %s\n", strerror(errno));
  }

  // 设置支持的算法
  if (strcmp(g_Config->m_tlsCiphers, "") != 0 &&
      SSL_CTX_set_cipher_list(ctx, g_Config->m_tlsCiphers) != 1) {
    printf("Set ssl cipher failed: %s\n", strerror(errno));
  }

#else
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
  } else if (strcasecmp(g_Config->m_tlsProtocol, "ssl3") == 0) {
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
#endif

  if (strlen(g_Config->m_caCert) > 0) {
    // 设置CA证书，用于验证服务端证书
    if (SSL_CTX_load_verify_locations(ctx, g_Config->m_caCert, NULL)) {
      // 设置需要验证服务端证书
      SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verify_callback);
      SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT);
      // SSL_CTX_set_default_passwd_cb_userdata(ctx,
      //         const_cast<void*>(static_cast<const void*>("12345"))); // 设置证书passwd
      // SSL_CTX_set_verify_depth(ctx, 0); // depth需要根据证书实际来设置，不设置采用自适应
      // SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY); // 如果使用了多路复用机制最好不用
      // SSL_CTX_set_client_CA_list(ctx, SSL_load_client_CA_file(g_Config->m_caCert));
      // SSL_CTX_set_default_verify_paths(ctx);
    } else {
      printf("CA file not exists or cannot read: %s\n", g_Config->m_caCert);
      // 设置不验证服务端证书
      SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }
  } else {
    // 设置不验证服务端证书
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
  }

  if (strlen(g_Config->m_clientCert) > 0 && strlen(g_Config->m_clientKey) > 0) {
    // 客户端证书，双向认证时需要
    if (SSL_CTX_use_certificate_file(ctx, g_Config->m_clientCert, SSL_FILETYPE_PEM)) {
      // 客户端密钥文件，双向认证时需要
      if (SSL_CTX_use_PrivateKey_file(ctx, g_Config->m_clientKey, SSL_FILETYPE_PEM)) {
        // 验证密钥是否与证书一致
        if (!SSL_CTX_check_private_key(ctx)) {
          printf("client certificate check failed, cert: %s, key: %s\n", g_Config->m_clientCert,
                 g_Config->m_clientKey);
        }
      } else {
        printf("ClientKey file not exists or cannot read: %s\n", g_Config->m_clientKey);
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
    snprintf(m_szErrMsg, sizeof(m_szErrMsg), "ssl verify failed: %s[%s:%d]", strerror(errno),
             __FILE__, __LINE__);
    disconnect();
    return false;
  }

  // printf("Connected with encryption: %s:%s\n", SSL_get_version(ssl), SSL_get_cipher(ssl));
  // show_certs(ssl);

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
  return VirtualClient::reconnect();
}

int HttpsClient::readonce(char* pBuff, int64_t iLen) {
  if (ssl != NULL) {
    return SSL_read(ssl, pBuff, iLen);
  } else {
    return 0;
  }
}

int HttpsClient::writeonce(char* pBuff, int64_t iLen) {
  if (ssl != NULL) {
    return SSL_write(ssl, pBuff, iLen);
  } else {
    return 0;
  }
}

/** 返回0，读完；>0没有读完 */
int64_t HttpsClient::isReadComplete(const char* pData, int64_t unDataLen, int64_t iReceivLenInBuff,
                                    int64_t iPkgTheoryLen) {
  return VirtualClient::httpReadComplete(pData, unDataLen, iReceivLenInBuff, iPkgTheoryLen);
}
