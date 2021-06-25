/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <unistd.h>
#include <getopt.h>
#include <cstdlib>

#include <ts/ts.h>
#include <ts/remap.h>

#include <openssl/ssl.h>

#include "utilities.h"
#include "limiter.h"

RateLimiter *
createConfig(int argc, const char *argv[])
{
  static const struct option longopt[] = {
    {const_cast<char *>("limit"), required_argument, nullptr, 'l'},
    {const_cast<char *>("queue"), required_argument, nullptr, 'q'},
    {const_cast<char *>("error"), required_argument, nullptr, 'e'},
    {const_cast<char *>("retry"), required_argument, nullptr, 'r'},
    {const_cast<char *>("header"), required_argument, nullptr, 'h'},
    {const_cast<char *>("maxage"), required_argument, nullptr, 'm'},
    // EOF
    {nullptr, no_argument, nullptr, '\0'},
  };

  RateLimiter *limiter = new RateLimiter();
  TSReleaseAssert(limiter);

  while (true) {
    int opt = getopt_long(argc, (char *const *)argv, "", longopt, nullptr);

    switch (opt) {
    case 'l':
      limiter->limit = strtol(optarg, nullptr, 10);
      break;
    case 'q':
      limiter->max_queue = strtol(optarg, nullptr, 10);
      break;
    case 'e':
      limiter->error = strtol(optarg, nullptr, 10);
      break;
    case 'r':
      limiter->retry = strtol(optarg, nullptr, 10);
      break;
    case 'm':
      limiter->max_age = std::chrono::milliseconds(strtol(optarg, nullptr, 10));
      break;
    case 'h':
      limiter->header = optarg;
      break;
    }
    if (opt == -1) {
      break;
    }
  }

  limiter->setupQueueCont();

  return limiter;
}

std::string_view
getSNI(SSL *ssl)
{
  const char *servername = nullptr;
  const unsigned char *p;
  size_t remaining, len = 0;

  // Parse the server name if the get extension call succeeds and there are more than 2 bytes to parse
  if (SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_server_name, &p, &remaining) && remaining > 2) {
    // Parse to get to the name, originally from test/handshake_helper.c in openssl tree
    /* Extract the length of the supplied list of names. */
    len = *(p++) << 8;
    len += *(p++);
    if (len + 2 == remaining) {
      remaining = len;
      /*
       * The list in practice only has a single element, so we only consider
       * the first one.
       */
      if (*p++ == TLSEXT_NAMETYPE_host_name) {
        remaining--;
        /* Now we can finally pull out the byte array with the actual hostname. */
        if (remaining > 2) {
          len = *(p++) << 8;
          len += *(p++);
          if (len + 2 <= remaining) {
            servername = reinterpret_cast<const char *>(p);
          }
        }
      }
    }
  }

  return std::string_view(servername, len);
}
