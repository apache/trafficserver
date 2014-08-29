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
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo* rri)
{
  int i, len;
  time_t t, e;
  MD5_CTX ctx;
  struct sockaddr_in *in;
  const char *qh, *ph, *ip;
  char *s, *ptr, *val, hash[32];
  unsigned char md[MD5_DIGEST_LENGTH];
  secure_link_info *sli = (secure_link_info *)ih;
  char *token = NULL, *expire = NULL, *path = NULL;

  in = (struct sockaddr_in *)TSHttpTxnClientAddrGet(rh);
  ip = inet_ntoa(in->sin_addr);
  s = TSUrlStringGet(rri->requestBufp, rri->requestUrl, &len);
  TSDebug(PLUGIN_NAME, "request [%.*s] from [%s]", len, s, ip);
  TSfree(s);

  qh = TSUrlHttpQueryGet(rri->requestBufp, rri->requestUrl, &len);
  if(qh && len > 0) {
    s = (char *)TSstrndup(qh, len);
    if((ptr = strtok(s, "&")) != NULL) {
      do {
        if((val = strchr(ptr, '=')) != NULL) {
          *val++ = '\0';
          if(strcmp(ptr, "st") == 0) {
            token = TSstrdup(val);
          } else if(strcmp(ptr, "ex") == 0) {
            expire = TSstrdup(val);
          }
        } else {
          TSError("Invalid parameter [%s]", ptr);
          break;
        }
      } while((ptr = strtok(NULL, "&")) != NULL);
    } else {
      TSError("strtok didn't find a & in the query string");
      /* this is just example, so set fake params to prevent plugin crash */
      token = TSstrdup("d41d8cd98f00b204e9800998ecf8427e");
      expire = TSstrdup("00000000");
    }
    TSfree(s);
  } else {
    TSError("TSUrlHttpQueryGet returns empty value");
  }

  ph = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &len);
  if(ph && len > 0) {
    s = TSstrndup(ph, len);
    if((ptr = strrchr(s, '/')) != NULL) {
      *++ptr = '\0';
    }
    path = TSstrdup(s);
    TSfree(s);
  } else {
    TSError("TSUrlPathGet returns empty value");
    /* this is just example, so set fake params to prevent plugin crash */
    path = TSstrdup("example/");
  }
  MD5_Init(&ctx);
  MD5_Update(&ctx, sli->secret, strlen(sli->secret));
  MD5_Update(&ctx, ip, strlen(ip));
  if (path) 
    MD5_Update(&ctx, path, strlen(path));
  if (expire)
    MD5_Update(&ctx, expire, strlen(expire));
  MD5_Final(md, &ctx);
  for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
    sprintf(&hash[i * 2], "%02x", md[i]);
  }
  time(&t);
  e = strtol(expire, NULL, 16);
  i = TSREMAP_DID_REMAP;
  if(e < t || strcmp(hash, token) != 0) {
    if(e < t) {
      TSDebug(PLUGIN_NAME, "link expired: [%lu] vs [%lu]", t, e);
    } else {
      TSDebug(PLUGIN_NAME, "tokens mismatch: [%s] vs [%s]", hash, token);
    }
    if(sli->strict) {
      TSDebug(PLUGIN_NAME, "request is DENY");
      TSHttpTxnSetHttpRetStatus(rh, TS_HTTP_STATUS_FORBIDDEN);
      i = TSREMAP_NO_REMAP;
    } else {
      TSDebug(PLUGIN_NAME, "request is PASS");
    }
  }
  if(i == TSREMAP_DID_REMAP) {
    if(TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, "", -1) == TS_SUCCESS) {
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
  char *ptr;
  secure_link_info *sli;

  // squash unused variable warnings ...
  (void)errbuf;
  (void)errbuf_size;

  sli = (secure_link_info *)TSmalloc(sizeof(secure_link_info));
  sli->secret = NULL;
  sli->strict = 0;

  for(i = 2; i < argc; i++) {
    if((ptr = strchr(argv[i], ':')) != NULL) {
      *ptr++ = '\0';
      if(strcmp(argv[i], "secret") == 0) {
        if(sli->secret != NULL) {
          TSfree(sli->secret);
        }
        sli->secret = TSstrdup(ptr);
      } else if(strcmp(argv[i], "policy") == 0) {
        sli->strict = !strcasecmp(ptr, "strict");
      } else {
        TSDebug(PLUGIN_NAME, "Unknown parameter [%s]", argv[i]);
      }
    } else {
      TSError("Invalid parameter [%s]", argv[i]);
    }
  }

  if(sli->secret == NULL) {
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
