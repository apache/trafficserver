/** @file

  A brief file description

  @section license License

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

/*************************** -*- Mod: C++ -*- *********************

  Defines.h
******************************************************************/
#ifndef _Defines_h_
#define _Defines_h_
#define MAX_USERS 20001         /* Max simultaneous users */
#define MAX_ORIGIN_SERVERS 10   /* Max Origin Servers */
#define MAX_STATUS_LEN 1024     /* Max Status Line Len */
#define MAX_SIZES 1000          /* Max number of synthetic doc sizes */
#define MAX_THINKTIMES 1000     /* Max number of think times */
#define MAX_TARGET_BYTERATES 1000       /* Max number of byterates */
#define MAX_SIZESTR_SIZE 20     /* sizestr looks like "size120 */
#define MAX_SERIALNUMBERSTR_SIZE 20
  /* serial numbers can range from 0 to doc size which could very
     large say 10 billion */
#define MAX_ONEREQUESTSTR_SIZE 256      /* each GET request */
#define MAX_ORIGINSERVERSTR_SIZE 64     /* eg. origin.inktomi.com:80 */
#define MAX_REQUEST_SIZE 1024   /* multiple keepalive GETs */
#define MAX_READBUF_SIZE 100000 /* Max read size */
#define MAX_HOSTNAME_SIZE 64
#define MAX_PORTNAME_SIZE 64
#define MAX_LINE_SIZE 1000
#define MAX_FILENAME_SIZE 80
#define MAX_WARMUP_USERS 120    // See INKqa04115: Warmup causes x86 linux to hang

#endif // #define _Defines_h_
