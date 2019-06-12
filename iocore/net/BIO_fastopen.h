/** @file
 *
 *  OpenSSL socket BIO that does TCP Fast Open.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <openssl/bio.h>

// Return a BIO_METHOD for a socket BIO that implements TCP Fast Open.
const BIO_METHOD *BIO_s_fastopen();

// OpenSSL 1.0.2h has BIO_set_conn_ip(), but master has BIO_set_conn_address(). Use
// the API from master since it makes more sense.
#if !defined(BIO_set_conn_address)
#define BIO_set_conn_address(b, addr) BIO_ctrl(b, BIO_C_SET_CONNECT, 2, (char *)addr)
#endif
