dnl -------------------------------------------------------- -*- autoconf -*-
dnl Licensed to the Apache Software Foundation (ASF) under one or more
dnl contributor license agreements.  See the NOTICE file distributed with
dnl this work for additional information regarding copyright ownership.
dnl The ASF licenses this file to You under the Apache License, Version 2.0
dnl (the "License"); you may not use this file except in compliance with
dnl the License.  You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl Unless required by applicable law or agreed to in writing, software
dnl distributed under the License is distributed on an "AS IS" BASIS,
dnl WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
dnl See the License for the specific language governing permissions and
dnl limitations under the License.

dnl
dnl crypto.m4 Trafficserver's Crypto autoconf macros
dnl

dnl
dnl TS_CHECK_CRYPTO: look for crypto libraries and headers
dnl
AC_DEFUN([TS_CHECK_CRYPTO], [
  AC_CHECK_LIB([crypt], [crypt], [AC_SUBST([LIBCRYPT],["-lcrypt"])])

  AX_CHECK_OPENSSL([
    enable_crypto=yes
  ], [
    AC_ERROR(failed to find OpenSSL)
    enable_crypto=no
  ])

  if test "x${enable_crypto}" = "xyes"; then
    TS_ADDTO(LDFLAGS, [$OPENSSL_LDFLAGS])
    TS_ADDTO(CPPFLAGS, [$OPENSSL_INCLUDES])
    TS_ADDTO(CPPFLAGS, [-DOPENSSL_NO_SSL_INTERN])
  fi

  dnl add checks for other varieties of ssl here
])

dnl
dnl Check OpenSSL Version
dnl
AC_DEFUN([TS_CHECK_CRYPTO_VERSION], [
  AC_MSG_CHECKING([OpenSSL version])
  AC_TRY_RUN([
#include <openssl/opensslv.h>
int main() {
  if (OPENSSL_VERSION_NUMBER < 0x1000200fL) {
    return 1;
  }
  return 0;
}
],
  [AC_MSG_RESULT([ok])],
  [AC_MSG_FAILURE([requires an OpenSSL version 1.0.2 or greater])])
])

dnl
dnl Since OpenSSL 1.1.0
dnl
AC_DEFUN([TS_CHECK_CRYPTO_ASYNC], [
  enable_tls_async=yes
  _async_saved_LIBS=$LIBS

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_FUNCS(SSL_get_all_async_fds ASYNC_init_thread,
    [], [enable_tls_async=no]
  )
  LIBS=$_async_saved_LIBS

  AC_MSG_CHECKING(whether to enable ASYNC job openssl support)
  AC_MSG_RESULT([$enable_tls_async])
  TS_ARG_ENABLE_VAR([use], [tls-async])
  AC_SUBST(use_tls_async)
])

dnl
dnl Since OpenSSL 1.1.1
dnl
AC_DEFUN([TS_CHECK_CRYPTO_HELLO_CB], [
  _hello_saved_LIBS=$LIBS
  enable_hello_cb=yes

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_HEADERS(openssl/ssl.h openssl/ts.h)
  AC_CHECK_HEADERS(openssl/tls1.h, [], [],
[ #if HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#endif ])

  AC_MSG_CHECKING([for SSL_CTX_set_client_hello_cb])
  AC_LINK_IFELSE(
  [
    AC_LANG_PROGRAM([[
#if HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#if HAVE_OPENSSL_TLS1_H
#include <openssl/tls1.h>
#endif
      ]],
      [[SSL_CTX_set_client_hello_cb(NULL, NULL, NULL);]])
  ],
  [
    AC_MSG_RESULT([yes])
  ],
  [
    AC_MSG_RESULT([no])
    enable_hello_cb=no
  ])

  LIBS=$_hello_saved_LIBS

  AC_MSG_CHECKING(whether to enable TLS client hello callback support)
  AC_MSG_RESULT([$enable_hello_cb])
  TS_ARG_ENABLE_VAR([use], [hello-cb])
  AC_SUBST(use_hello_cb)
])

dnl
dnl Since OpenSSL 1.1.0
dnl
AC_DEFUN([TS_CHECK_CRYPTO_SET_RBIO], [
  _rbio_saved_LIBS=$LIBS
  enable_set_rbio=yes

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_MSG_CHECKING([for SSL_set0_rbio])
  AC_LINK_IFELSE(
  [
    AC_LANG_PROGRAM([[
#if HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#if HAVE_OPENSSL_TLS1_H
#include <openssl/tls1.h>
#endif
      ]],
      [[SSL_set0_rbio(NULL, NULL);]])
  ],
  [
    AC_MSG_RESULT([yes])
  ],
  [
    AC_MSG_RESULT([no])
    enable_set_rbio=no
  ])

  LIBS=$_rbio_saved_LIBS

  AC_MSG_CHECKING(whether to enable set rbio)
  AC_MSG_RESULT([$enable_set_rbio])
  TS_ARG_ENABLE_VAR([use], [set-rbio])
  AC_SUBST(use_set_rbio)
])

dnl
dnl Since OpenSSL 1.1.0
dnl
AC_DEFUN([TS_CHECK_CRYPTO_DH_GET_2048_256], [
  _dh_saved_LIBS=$LIBS
  enable_dh_get_2048_256=yes

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_MSG_CHECKING([for DH_get_2048_256])
  AC_LINK_IFELSE(
  [
    AC_LANG_PROGRAM([[
#include<openssl/dh.h>
      ]],
      [[DH_get_2048_256();]])
  ],
  [
    AC_MSG_RESULT([yes])
  ],
  [
    AC_MSG_RESULT([no])
    enable_dh_get_2048_256=no
  ])

  LIBS=$_dh_saved_LIBS

  AC_MSG_CHECKING(whether to enable DH_get_2048_256)
  AC_MSG_RESULT([$enable_dh_get_2048_256])
  TS_ARG_ENABLE_VAR([use], [dh_get_2048_256])
  AC_SUBST(use_dh_get_2048_256)
])

AC_DEFUN([TS_CHECK_CRYPTO_HKDF], [
  enable_hkdf=no
  _hkdf_saved_LIBS=$LIBS
  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_MSG_CHECKING([for EVP_PKEY_CTX_hkdf_mode])
  AC_LINK_IFELSE(
  [
    AC_LANG_PROGRAM([[
#include <openssl/kdf.h>
    ]],
    [[
#ifndef EVP_PKEY_CTX_hkdf_mode
# error no EVP_PKEY_CTX_hkdf_mode support
#endif
    ]])
  ],
  [
    AC_MSG_RESULT([yes])
    enable_hkdf=yes
  ],
  [
    AC_MSG_RESULT([no])
  ])
  AC_CHECK_FUNC(HKDF_extract, [
    enable_hkdf=yes
  ], [])
  LIBS=$_hkdf_saved_LIBS
  TS_ARG_ENABLE_VAR([use], [hkdf])
  AC_SUBST(use_hkdf)
])

AC_DEFUN([TS_CHECK_CRYPTO_TLS13], [
  enable_tls13=yes
  _tls13_saved_LIBS=$LIBS
  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_MSG_CHECKING([whether TLS 1.3 is supported])
  AC_LINK_IFELSE(
  [
    AC_LANG_PROGRAM([[
#include <openssl/ssl.h>
    ]],
    [[
#ifndef TLS1_3_VERSION
# error no TLS1_3 support
#endif
#ifdef OPENSSL_NO_TLS1_3
# error no TLS1_3 support
#endif
    ]])
  ],
  [
    AC_MSG_RESULT([yes])
  ],
  [
    AC_MSG_RESULT([no])
    enable_tls13=no
  ])
  LIBS=$_tls13_saved_LIBS
  TS_ARG_ENABLE_VAR([use], [tls13])
  AC_SUBST(use_tls13)
])

dnl
dnl Since OpenSSL 1.1.0
dnl
AC_DEFUN([TS_CHECK_CRYPTO_OCSP], [
  _ocsp_saved_LIBS=$LIBS

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_HEADERS(openssl/ocsp.h, [ocsp_have_headers=1], [enable_tls_ocsp=no])

  if test "$ocsp_have_headers" == "1"; then
    AC_CHECK_FUNCS(OCSP_sendreq_new OCSP_REQ_CTX_add1_header OCSP_REQ_CTX_set1_req, [enable_tls_ocsp=yes], [enable_tls_ocsp=no])

    LIBS=$_ocsp_saved_LIBS
  fi

  AC_MSG_CHECKING(whether OCSP is supported)
  AC_MSG_RESULT([$enable_tls_ocsp])
  TS_ARG_ENABLE_VAR([use], [tls-ocsp])
  AC_SUBST(use_tls_ocsp)
])

dnl
dnl Since OpenSSL 1.1.1
dnl
AC_DEFUN([TS_CHECK_CRYPTO_SET_CIPHERSUITES], [
  _set_ciphersuites_saved_LIBS=$LIBS

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_HEADERS(openssl/ssl.h)
  AC_CHECK_FUNCS(SSL_CTX_set_ciphersuites, [enable_tls_set_ciphersuites=yes], [enable_tls_set_ciphersuites=no])

  LIBS=$_set_ciphersuites_saved_LIBS

  AC_MSG_CHECKING(whether to enable TLSv1.3 ciphersuites configuration is supported)
  AC_MSG_RESULT([$enable_tls_set_ciphersuites])
  TS_ARG_ENABLE_VAR([use], [tls-set-ciphersuites])
  AC_SUBST(use_tls_set_ciphersuites)
])

dnl
dnl Since OpenSSL 1.1.1
dnl
AC_DEFUN([TS_CHECK_EARLY_DATA], [
  _set_ciphersuites_saved_LIBS=$LIBS

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_HEADERS(openssl/ssl.h)
  AC_CHECK_FUNCS(
    SSL_set_max_early_data,
    [
      has_tls_early_data=1
      early_data_check=yes
    ],
    [
      has_tls_early_data=0
      early_data_check=no
    ]
  )

  LIBS=$_set_ciphersuites_saved_LIBS

  AC_MSG_CHECKING([for OpenSSL early data support])
  AC_MSG_RESULT([$early_data_check])

  AC_SUBST(has_tls_early_data)
])

dnl
dnl Since OpenSSL 1.1.1
dnl
dnl SSL_CTX_set_tlsext_ticket_key_evp_cb function is for OpenSSL 3.0
dnl SSL_CTX_set_tlsext_ticket_key_cb macro is for OpenSSL 1.1.1
dnl SSL_CTX_set_tlsext_ticket_key_cb function is for BoringSSL
AC_DEFUN([TS_CHECK_SESSION_TICKET], [
  _set_ssl_ctx_set_tlsext_ticket_key_evp_cb_saved_LIBS=$LIBS

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_HEADERS(openssl/ssl.h)
  session_ticket_check=no
  has_tls_session_ticket=0
  AC_MSG_CHECKING([for SSL_CTX_set_tlsext_ticket_key_cb macro])
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[#include <openssl/ssl.h>]],
                     [[
                     #ifndef SSL_CTX_set_tlsext_ticket_key_cb
                     #error
                     #endif
                     ]])
    ],
    [
      AC_DEFINE(HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_CB, 1, [Whether SSL_CTX_set_tlsext_ticket_key_cb is available])
      session_ticket_check=yes
      has_tls_session_ticket=1
    ],
    []
  )
  AC_MSG_RESULT([$session_ticket_check])
  AC_CHECK_FUNCS(
    SSL_CTX_set_tlsext_ticket_key_evp_cb SSL_CTX_set_tlsext_ticket_key_cb,
    [
      session_ticket_check=yes
      has_tls_session_ticket=1
    ],
    []
  )

  LIBS=$_set_ssl_ctx_set_tlsext_ticket_key_evp_cb_saved_LIBS

  AC_MSG_CHECKING([for session ticket support])
  AC_MSG_RESULT([$session_ticket_check])

  AC_SUBST(has_tls_session_ticket)
])
