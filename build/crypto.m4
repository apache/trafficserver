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
  AC_SEARCH_LIBS([crypt], [crypt], [AC_SUBST([LIBCRYPT],["-lcrypt"])])

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

AC_DEFUN([TS_CHECK_CRYPTO_EC_KEYS], [
  _eckeys_saved_LIBS=$LIBS
  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_HEADERS(openssl/ec.h)
  AC_CHECK_FUNCS(EC_KEY_new_by_curve_name, [enable_tls_eckey=yes], [enable_tls_eckey=no])
  LIBS=$_eckeys_saved_LIBS

  AC_MSG_CHECKING(whether EC keys are supported)
  AC_MSG_RESULT([$enable_tls_eckey])
  TS_ARG_ENABLE_VAR([use], [tls-eckey])
  AC_SUBST(use_tls_eckey)
])

AC_DEFUN([TS_CHECK_CRYPTO_NEXTPROTONEG], [
  enable_tls_npn=yes
  _npn_saved_LIBS=$LIBS
  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_FUNCS(SSL_CTX_set_next_protos_advertised_cb SSL_CTX_set_next_proto_select_cb SSL_select_next_proto SSL_get0_next_proto_negotiated,
    [], [enable_tls_npn=no]
  )
  LIBS=$_npn_saved_LIBS

  AC_MSG_CHECKING(whether to enable Next Protocol Negotiation TLS extension support)
  AC_MSG_RESULT([$enable_tls_npn])
  TS_ARG_ENABLE_VAR([use], [tls-npn])
  AC_SUBST(use_tls_npn)
])

AC_DEFUN([TS_CHECK_CRYPTO_ALPN], [
  enable_tls_alpn=yes
  _alpn_saved_LIBS=$LIBS
  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_FUNCS(SSL_CTX_set_alpn_protos SSL_CTX_set_alpn_select_cb SSL_get0_alpn_selected SSL_select_next_proto,
    [], [enable_tls_alpn=no]
  )
  LIBS=$_alpn_saved_LIBS

  AC_MSG_CHECKING(whether to enable Application Layer Protocol Negotiation TLS extension support)
  AC_MSG_RESULT([$enable_tls_alpn])
  TS_ARG_ENABLE_VAR([use], [tls-alpn])
  AC_SUBST(use_tls_alpn)
])

AC_DEFUN([TS_CHECK_CRYPTO_SNI], [
  _sni_saved_LIBS=$LIBS
  enable_tls_sni=yes

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_CHECK_HEADERS(openssl/ssl.h openssl/ts.h)
  AC_CHECK_HEADERS(openssl/tls1.h, [], [], 
[ #if HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#endif ])

  # We are looking for SSL_CTX_set_tlsext_servername_callback, but it's a
  # macro, so AC_CHECK_FUNCS is not going to do the business.
  AC_MSG_CHECKING([for SSL_CTX_set_tlsext_servername_callback])
  AC_COMPILE_IFELSE(
  [
    AC_LANG_PROGRAM([[
#if HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#if HAVE_OPENSSL_TLS1_H
#include <openssl/tls1.h>
#endif
      ]],
      [[SSL_CTX_set_tlsext_servername_callback(NULL, NULL);]])
  ],
  [
    AC_MSG_RESULT([yes])
  ],
  [
    AC_MSG_RESULT([no])
    enable_tls_sni=no
  ])

  AC_CHECK_FUNCS(SSL_get_servername, [], [enable_tls_sni=no])

  LIBS=$_sni_saved_LIBS

  AC_MSG_CHECKING(whether to enable ServerNameIndication TLS extension support)
  AC_MSG_RESULT([$enable_tls_sni])
  TS_ARG_ENABLE_VAR([use], [tls-sni])
  AC_SUBST(use_tls_sni)
])

AC_DEFUN([TS_CHECK_CRYPTO_CERT_CB], [
  _cert_saved_LIBS=$LIBS
  enable_cert_cb=yes

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_MSG_CHECKING([for SSL_CTX_set_cert_cb])
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
      [[SSL_CTX_set_cert_cb(NULL, NULL, NULL);]])
  ],
  [
    AC_MSG_RESULT([yes])
  ],
  [
    AC_MSG_RESULT([no])
    enable_cert_cb=no
  ])

  LIBS=$_cert_saved_LIBS

  AC_MSG_CHECKING(whether to enable TLS certificate callback support)
  AC_MSG_RESULT([$enable_cert_cb])
  TS_ARG_ENABLE_VAR([use], [cert-cb])
  AC_SUBST(use_cert_cb)
])

AC_DEFUN([TS_CHECK_CRYPTO_SET_RBIO], [
  _rbio_saved_LIBS=$LIBS
  enable_set_rbio=yes

  TS_ADDTO(LIBS, [$OPENSSL_LIBS])
  AC_MSG_CHECKING([for SSL_set_rbio])
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
      [[SSL_set_rbio(NULL, NULL);]])
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
