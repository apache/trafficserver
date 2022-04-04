/** @file

  stek_utils.cc - Deal with STEK

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

#include <iostream>
#include <cstring>
#include <mutex>
#include <cassert>
#include <unistd.h>
#include <sys/time.h>

#include <openssl/rand.h>
#include <ts/ts.h>
#include <ts/apidefs.h>

#include "stek_utils.h"
#include "common.h"

int
get_good_random(char *buffer, int size, int need_good_entropy)
{
  FILE *fp;
  int numread = 0;
  char *rand_file_name;

  /* /dev/random blocks until good entropy and can take up to 2 seconds per byte on idle machines */
  /* /dev/urandom does not have entropy check, and is very quick.
   * Caller decides quality needed */
  rand_file_name = const_cast<char *>((need_good_entropy) ? /* Good & slow */ "/dev/random" : /*Fast*/ "/dev/urandom");

  if (nullptr == (fp = fopen(rand_file_name, "r"))) {
    return -1; /* failure */
  }
  numread = static_cast<int>(fread(buffer, 1, size, fp));
  fclose(fp);

  return ((numread == size) ? 0 /* success*/ : -1 /*failure*/);
}

int
generate_new_stek(ssl_ticket_key_t *return_stek, int entropy_ensured)
{
  /* Generate a new Session-Ticket-Encryption-Key and places it into buffer
   * provided return -1 on failure, 0 on success.
   * if boolean global_key is set (inidcating it's the global key space),
     will get global key lock before setting */

  ssl_ticket_key_t new_key; // tmp local buffer

  /* We create key in local buff to minimize lock time on global,
   * because entropy ensuring can take a very long time e.g. 2 seconds per byte of entropy*/
  if ((get_good_random(reinterpret_cast<char *>(&(new_key.aes_key)), SSL_KEY_LEN, (entropy_ensured) ? 1 : 0) != 0) ||
      (get_good_random(reinterpret_cast<char *>(&(new_key.hmac_secret)), SSL_KEY_LEN, (entropy_ensured) ? 1 : 0) != 0) ||
      (get_good_random(reinterpret_cast<char *>(&(new_key.key_name)), SSL_KEY_LEN, 0) != 0)) {
    return -1; /* couldn't generate new STEK */
  }

  std::memcpy(return_stek, &new_key, SSL_TICKET_KEY_SIZE);
  std::memset(&new_key, 0, SSL_TICKET_KEY_SIZE); // keep our stack clean

  return 0; /* success */
}
