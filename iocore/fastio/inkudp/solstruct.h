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

/*
 * STREAMS message format for sending a UDP packet
 */

#include <sys/int_types.h>

struct udppkt
{
  char hdr[22];                 /* 0x08000000, 10000000, 14000000 */
  /* 0x00000000, 00000000, 0200  */

  uint16_t port;                /* set to destination UDP port # */
  int32_t ip;
  char ftr[8];                  /* 0x35410000, 0x00000000 */

};


struct inkio_blockdat
{
  struct free_rtn *freecb;      /* so it can get freed */
  uint16_t blockID;

};
