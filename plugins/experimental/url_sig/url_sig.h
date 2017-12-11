/** @file
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#ifndef URL_SIG_H_
#define URL_SIG_H_

/* in the query string that we add to sign the url: */
#define CIP_QSTRING "C" /* C=24.0.33.12 designates the client IP address */
#define EXP_QSTRING "E" /* E=1356128799 means expires at (seconds since Unix epoch) */
#define ALG_QSTRING "A" /* A=1 means hashing algorithm 1 */
#define KIN_QSTRING "K" /* K=3 means use key number 3 */
#define PAR_QSTRING \
  "P" /* P=1110 means use parts 0, 1 and 2 (and no more) for the hashing of the url after removing the 'http://' */
      /* and making the parts by doing a split("/") */
#define SIG_QSTRING                                                                                           \
  "S" /* S=9e2828d570a4bee3c964f698b0985ee58b9f6b64 means 9e2828d570a4bee3c964f698b0985ee58b9f6b64 is the sig \
         This one has to be the last one of the string */

#define CIP_STRLEN 20
#define EXP_STRLEN 16
#define PAR_STRLEN 16
#define MAX_PARTS 32
#define MAX_SEGMENTS 64

#define MAX_HTTP_REQUEST_SIZE 8192 //

#define MAX_SIG_SIZE 20
#define SHA1_SIG_SIZE 20
#define MD5_SIG_SIZE 16

#define MAX_REQ_LEN 8192
#define MAX_KEY_LEN 256
#define MAX_KEY_NUM 16
#define MAX_QUERY_LEN 4096

#define USIG_HMAC_SHA1 1
#define USIG_HMAC_MD5 2

#endif /* URL_SIG_H_ */
