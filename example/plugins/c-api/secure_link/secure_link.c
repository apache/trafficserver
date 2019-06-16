/** @file

  This plugin enables validation of link by performing checksum computations.

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

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/md5.h>

#include "ts/ts.h"
#include "ts/remap.h"

#define PLUGIN_NAME "secure_link"

typedef struct {
  char *secret;
  uint8_t strict;
} secure_link_info;

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  int i, len;
  time_t t, e;
  MD5_CTX ctx;
  struct sockaddr_in *in;
  const char *qh, *ph, *ip;
  unsigned char md[MD5_DIGEST_LENGTH];
  secure_link_info *sli = (secure_link_info *)ih;
  char *token = NULL, *tokenptr = NULL, *expire = NULL, *expireptr = NULL, *path = NULL;
  char *s, *ptr, *saveptr = NULL, *val, hash[32] = "";

  in = (struct sockaddr_in *)TSHttpTxnClientAddrGet(rh);
  ip = inet_ntoa(in->sin_addr);
  s  = TSUrlStringGet(rri->requestBufp, rri->requestUrl, &len);
  TSDebug(PLUGIN_NAME, "request [%.*s] from [%s]", len, s, ip);
  TSfree(s);

  qh = TSUrlHttpQueryGet(rri->requestBufp, rri->requestUrl, &len);
  if (qh && len > 0) {
    s = (char *)TSstrndup(qh, len);
    if ((ptr = strtok_r(s, "&", &saveptr)) != NULL) {
      do {
        if ((val = strchr(ptr, '=')) != NULL) {
          *val++ = '\0';
          if (strcmp(ptr, "st") == 0) {
            tokenptr = val;
          } else if (strcmp(ptr, "ex") == 0) {
            expireptr = val;
          }
        } else {
          TSError("[%s] Invalid parameter [%s]", PLUGIN_NAME, ptr);
          break;
        }
      } while ((ptr = strtok_r(NULL, "&", &saveptr)) != NULL);
      token  = (NULL == tokenptr ? NULL : TSstrdup(tokenptr));
      expire = (NULL == expireptr ? NULL : TSstrdup(expireptr));
    } else {
      TSError("[%s] strtok didn't find a & in the query string", PLUGIN_NAME);
      /* this is just example, so set fake params to prevent plugin crash */
      token  = TSstrdup("d41d8cd98f00b204e9800998ecf8427e");
      expire = TSstrdup("00000000");
    }
    TSfree(s);
  } else {
    TSError("[%s] TSUrlHttpQueryGet returns empty value", PLUGIN_NAME);
  }

  ph = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &len);
  if (ph && len > 0) {
    s = TSstrndup(ph, len);
    if ((ptr = strrchr(s, '/')) != NULL) {
      *++ptr = '\0';
    }
    path = TSstrdup(s);
    TSfree(s);
  } else {
    TSError("[%s] TSUrlPathGet returns empty value", PLUGIN_NAME);
    /* this is just example, so set fake params to prevent plugin crash */
    path = TSstrdup("example/");
  }
  MD5_Init(&ctx);
  MD5_Update(&ctx, sli->secret, strlen(sli->secret));
  MD5_Update(&ctx, ip, strlen(ip));
  if (path) {
    MD5_Update(&ctx, path, strlen(path));
  }
  if (expire) {
    MD5_Update(&ctx, expire, strlen(expire));
  }
  MD5_Final(md, &ctx);
  for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
    sprintf(&hash[i * 2], "%02x", md[i]);
  }
  time(&t);
  e = (NULL == expire ? 0 : strtol(expire, NULL, 16));
  i = TSREMAP_DID_REMAP;
  if (e < t || (NULL == token || 0 != strcmp(hash, token))) {
    if (e < t) {
      TSDebug(PLUGIN_NAME, "link expired: [%lu] vs [%lu]", t, e);
    } else {
      TSDebug(PLUGIN_NAME, "tokens mismatch: [%s] vs [%s]", hash, token);
    }
    if (sli->strict) {
      TSDebug(PLUGIN_NAME, "request is DENY");
      TSHttpTxnStatusSet(rh, TS_HTTP_STATUS_FORBIDDEN);
      i = TSREMAP_NO_REMAP;
    } else {
      TSDebug(PLUGIN_NAME, "request is PASS");
    }
  }
  if (i == TSREMAP_DID_REMAP) {
    if (TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, "", -1) == TS_SUCCESS) {
      s = TSUrlStringGet(rri->requestBufp, rri->requestUrl, &len);
      TSDebug(PLUGIN_NAME, "new request string is [%.*s]", len, s);
      TSfree(s);
    } else {
      i = TSREMAP_NO_REMAP;
    }
  }
  TSfree(expire);
  TSfree(token);
  TSfree(path);
  return i;
}

TSReturnCode
TSRemapNewInstance(int argc, char **argv, void **ih, char *errbuf, int errbuf_size)
{
  int i;
  secure_link_info *sli;

  // squash unused variable warnings ...
  (void)errbuf;
  (void)errbuf_size;

  sli         = (secure_link_info *)TSmalloc(sizeof(secure_link_info));
  sli->secret = NULL;
  sli->strict = 0;

  for (i = 2; i < argc; i++) {
    char *ptr;
    if ((ptr = strchr(argv[i], ':')) != NULL) {
      *ptr++ = '\0';
      if (strcmp(argv[i], "secret") == 0) {
        if (sli->secret != NULL) {
          TSfree(sli->secret);
        }
        sli->secret = TSstrdup(ptr);
      } else if (strcmp(argv[i], "policy") == 0) {
        sli->strict = !strcasecmp(ptr, "strict");
      } else {
        TSDebug(PLUGIN_NAME, "Unknown parameter [%s]", argv[i]);
      }
    } else {
      TSError("[%s] Invalid parameter [%s]", PLUGIN_NAME, argv[i]);
    }
  }

  if (sli->secret == NULL) {
    sli->secret = TSstrdup("");
  }

  *ih = (void *)sli;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  secure_link_info *sli = (secure_link_info *)ih;

  TSfree(sli->secret);
  TSfree(sli);
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  // squash unused variable warnings ...
  (void)api_info;
  (void)errbuf;
  (void)errbuf_size;

  return TS_SUCCESS;
}
