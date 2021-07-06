/** @file

  stek.h - a containuer of connection objects

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
#pragma once

/* STEK - Session Ticket Encryption Key stuff */

#define STEK_ID_NAME "@stek@" // ACTUALLY it is redis channel minus cluster_name prefix, aka mdbm keyname
#define STEK_ID_RESEND "@resendstek@"
#define STEK_MAX_LIFETIME 86400 // 24 hours max - should rotate STEK

#define STEK_NOT_CHANGED_WARNING_INTERVAL (2 * STEK_MAX_LIFETIME) // warn on non-stek rotate every X secs.

#define SSL_KEY_LEN 16

struct ssl_ticket_key_t // an STEK
{
  unsigned char key_name[SSL_KEY_LEN]; // tickets use this name to identify who encrypted
  unsigned char hmac_secret[SSL_KEY_LEN];
  unsigned char aes_key[SSL_KEY_LEN];
};

void STEK_update(const std::string &encrypted_stek);
bool isSTEKMaster();
int STEK_Send_To_Network(struct ssl_ticket_key_t *stekToSend);
