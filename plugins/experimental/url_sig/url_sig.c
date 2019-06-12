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

#define min(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a < _b ? _a : _b;      \
  })

#include "url_sig.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#include <ts/ts.h>
#include <ts/remap.h>

static const char PLUGIN_NAME[] = "url_sig";

struct config {
  TSHttpStatus err_status;
  char *err_url;
  char keys[MAX_KEY_NUM][MAX_KEY_LEN];
  pcre *regex;
  pcre_extra *regex_extra;
  int pristine_url_flag;
  char *sig_anchor;
};

static void
free_cfg(struct config *cfg)
{
  TSError("[url_sig] Cleaning up");
  TSfree(cfg->err_url);
  TSfree(cfg->sig_anchor);

  if (cfg->regex_extra) {
#ifndef PCRE_STUDY_JIT_COMPILE
    pcre_free(cfg->regex_extra);
#else
    pcre_free_study(cfg->regex_extra);
#endif
  }

  if (cfg->regex) {
    pcre_free(cfg->regex);
  }

  TSfree(cfg);
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    snprintf(errbuf, errbuf_size, "[tsremap_init] - Invalid TSRemapInterface argument");
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "plugin is successfully initialized");
  return TS_SUCCESS;
}

// To force a config file reload touch remap.config and do a "traffic_ctl config reload"
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  char config_filepath_buf[PATH_MAX], *config_file;
  struct config *cfg;

  if ((argc < 3) || (argc > 4)) {
    snprintf(errbuf, errbuf_size,
             "[TSRemapNewInstance] - Argument count wrong (%d)... config file path is required first pparam, \"pristineurl\" is"
             "optional second pparam.",
             argc);
    return TS_ERROR;
  }
  TSDebug(PLUGIN_NAME, "Initializing remap function of %s -> %s with config from %s", argv[0], argv[1], argv[2]);

  if (argv[2][0] == '/') {
    config_file = argv[2];
  } else {
    snprintf(config_filepath_buf, sizeof(config_filepath_buf), "%s/%s", TSConfigDirGet(), argv[2]);
    config_file = config_filepath_buf;
  }
  TSDebug(PLUGIN_NAME, "config file name: %s", config_file);
  FILE *file = fopen(config_file, "r");
  if (file == NULL) {
    snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - Error opening file %s", config_file);
    return TS_ERROR;
  }

  char line[300];
  int line_no = 0;
  int keynum;

  cfg = TSmalloc(sizeof(struct config));
  memset(cfg, 0, sizeof(struct config));

  while (fgets(line, sizeof(line), file) != NULL) {
    TSDebug(PLUGIN_NAME, "LINE: %s (%d)", line, (int)strlen(line));
    line_no++;
    if (line[0] == '#' || strlen(line) <= 1) {
      continue;
    }
    char *pos = strchr(line, '=');
    if (pos == NULL) {
      TSError("[url_sig] Error parsing line %d of file %s (%s)", line_no, config_file, line);
      continue;
    }
    *pos        = '\0';
    char *value = pos + 1;
    while (isspace(*value)) { // remove whitespace
      value++;
    }
    pos = strchr(value, '\n'); // remove the new line, terminate the string
    if (pos != NULL) {
      *pos = '\0';
    }
    if (pos == NULL || strlen(value) >= MAX_KEY_LEN) {
      snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - Maximum key length (%d) exceeded on line %d", MAX_KEY_LEN - 1, line_no);
      fclose(file);
      free_cfg(cfg);
      return TS_ERROR;
    }
    if (strncmp(line, "key", 3) == 0) {
      if (strncmp((char *)(line + 3), "0", 1) == 0) {
        keynum = 0;
      } else {
        TSDebug(PLUGIN_NAME, ">>> %s <<<", line + 3);
        keynum = atoi((char *)(line + 3));
        if (keynum == 0) {
          keynum = -1; // Not a Number
        }
      }
      TSDebug(PLUGIN_NAME, "key number %d == %s", keynum, value);
      if (keynum >= MAX_KEY_NUM || keynum < 0) {
        snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - Key number (%d) >= MAX_KEY_NUM (%d) or NaN", keynum, MAX_KEY_NUM);
        fclose(file);
        free_cfg(cfg);
        return TS_ERROR;
      }
      snprintf(&cfg->keys[keynum][0], MAX_KEY_LEN, "%s", value);
    } else if (strncmp(line, "error_url", 9) == 0) {
      if (atoi(value)) {
        cfg->err_status = atoi(value);
      }
      value += 3;
      while (isspace(*value)) {
        value++;
      }
      if (cfg->err_status == TS_HTTP_STATUS_MOVED_TEMPORARILY) {
        cfg->err_url = TSstrndup(value, strlen(value));
      } else {
        cfg->err_url = NULL;
      }
    } else if (strncmp(line, "sig_anchor", 10) == 0) {
      cfg->sig_anchor = TSstrndup(value, strlen(value));
    } else if (strncmp(line, "excl_regex", 10) == 0) {
      // compile and study regex
      const char *errptr;
      int erroffset, options = 0;

      if (cfg->regex) {
        TSDebug(PLUGIN_NAME, "Skipping duplicate excl_regex");
        continue;
      }

      cfg->regex = pcre_compile(value, options, &errptr, &erroffset, NULL);
      if (cfg->regex == NULL) {
        TSDebug(PLUGIN_NAME, "Regex compilation failed with error (%s) at character %d", errptr, erroffset);
      } else {
#ifdef PCRE_STUDY_JIT_COMPILE
        options = PCRE_STUDY_JIT_COMPILE;
#endif
        cfg->regex_extra = pcre_study(
          cfg->regex, options, &errptr); // We do not need to check the error here because we can still run without the studying?
      }
    } else {
      TSError("[url_sig] Error parsing line %d of file %s (%s)", line_no, config_file, line);
    }
  }

  fclose(file);

  if (argc > 3) {
    if (strcasecmp(argv[3], "pristineurl") == 0) {
      cfg->pristine_url_flag = 1;

    } else {
      snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - second pparam (if present) must be pristineurl");
      free_cfg(cfg);
      return TS_ERROR;
    }
  }

  switch (cfg->err_status) {
  case TS_HTTP_STATUS_MOVED_TEMPORARILY:
    if (cfg->err_url == NULL) {
      snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - Invalid config, err_status == 302, but err_url == NULL");
      free_cfg(cfg);
      return TS_ERROR;
    }
    break;
  case TS_HTTP_STATUS_FORBIDDEN:
    if (cfg->err_url != NULL) {
      snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - Invalid config, err_status == 403, but err_url != NULL");
      free_cfg(cfg);
      return TS_ERROR;
    }
    break;
  default:
    snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - Return code %d not supported", cfg->err_status);
    free_cfg(cfg);
    return TS_ERROR;
  }

  *ih = (void *)cfg;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  free_cfg((struct config *)ih);
}

static void
err_log(const char *url, const char *msg)
{
  if (msg && url) {
    TSDebug(PLUGIN_NAME, "[URL=%s]: %s", url, msg);
    TSError("[url_sig] [URL=%s]: %s", url, msg); // This goes to error.log
  } else {
    TSError("[url_sig] Invalid err_log request");
  }
}

// See the README.  All Signing parameters must be concatenated to the end
// of the url and any application query parameters.
static char *
getAppQueryString(const char *query_string, int query_length)
{
  int done = 0;
  char *p;
  char buf[MAX_QUERY_LEN + 1];

  if (query_length > MAX_QUERY_LEN) {
    TSDebug(PLUGIN_NAME, "Cannot process the query string as the length exceeds %d bytes", MAX_QUERY_LEN);
    return NULL;
  }
  memset(buf, 0, sizeof(buf));
  memcpy(buf, query_string, query_length);
  p = buf;

  TSDebug(PLUGIN_NAME, "query_string: %s, query_length: %d", query_string, query_length);

  do {
    switch (*p) {
    case 'A':
    case 'C':
    case 'E':
    case 'K':
    case 'P':
    case 'S':
      done = 1;
      if ((p > buf) && (*(p - 1) == '&')) {
        *(p - 1) = '\0';
      } else {
        (*p = '\0');
      }
      break;
    default:
      p = strchr(p, '&');
      if (p == NULL) {
        done = 1;
      } else {
        p++;
      }
      break;
    }
  } while (!done);

  if (strlen(buf) > 0) {
    p = TSstrdup(buf);
    return p;
  } else {
    return NULL;
  }
}

/** fixedBufferWrite safely writes no more than *dest_len bytes to *dest_end
 * from src. If copying src_len bytes to *dest_len would overflow, it returns
 * zero. *dest_end is advanced and *dest_len is decremented to account for the
 * written data. No null-terminators are written automatically (though they
 * could be copied with data).
 */
static int
fixedBufferWrite(char **dest_end, int *dest_len, const char *src, int src_len)
{
  if (src_len > *dest_len) {
    return 0;
  }
  memcpy(*dest_end, src, src_len);
  *dest_end += src_len;
  *dest_len -= src_len;
  return 1;
}

static char *
urlParse(char *url, char *anchor, char *new_path_seg, int new_path_seg_len, char *signed_seg, unsigned int signed_seg_len)
{
  char *segment[MAX_SEGMENTS];
  unsigned char decoded_string[2048] = {'\0'};
  char new_url[8192]; /* new_url is not null_terminated */
  char *p = NULL, *sig_anchor = NULL, *saveptr = NULL;
  int i = 0, numtoks = 0, decoded_len = 0, sig_anchor_seg = 0;

  char *new_url_end    = new_url;
  int new_url_len_left = sizeof(new_url);

  char *new_path_seg_end    = new_path_seg;
  int new_path_seg_len_left = new_path_seg_len;

  char *skip = strchr(url, ':');
  if (!skip || skip[1] != '/' || skip[2] != '/') {
    return NULL;
  }
  skip += 3;
  // preserve the scheme in the new_url.
  if (!fixedBufferWrite(&new_url_end, &new_url_len_left, url, skip - url)) {
    TSError("insufficient space to copy schema into new_path_seg buffer.");
    return NULL;
  }
  TSDebug(PLUGIN_NAME, "%s:%d - new_url: %*s\n", __FILE__, __LINE__, (int)(new_url_end - new_url), new_url);

  // parse the url.
  if ((p = strtok_r(skip, "/", &saveptr)) != NULL) {
    segment[numtoks++] = p;
    do {
      p = strtok_r(NULL, "/", &saveptr);
      if (p != NULL) {
        segment[numtoks] = p;
        if (anchor != NULL && sig_anchor_seg == 0) {
          // look for the signed anchor string.
          if ((sig_anchor = strcasestr(segment[numtoks], anchor)) != NULL) {
            // null terminate this segment just before he signing anchor, this should be a ';'.
            *(sig_anchor - 1) = '\0';
            if ((sig_anchor = strstr(sig_anchor, "=")) != NULL) {
              *sig_anchor = '\0';
              sig_anchor++;
              sig_anchor_seg = numtoks;
            }
          }
        }
        numtoks++;
      }
    } while (p != NULL && numtoks < MAX_SEGMENTS);
  } else {
    return NULL;
  }
  if ((numtoks >= MAX_SEGMENTS) || (numtoks < 3)) {
    return NULL;
  }

  // create a new path string for later use when dealing with query parameters.
  // this string will not contain the signing parameters.  skips the fqdn by
  // starting with segment 1.
  for (i = 1; i < numtoks; i++) {
    // if no signing anchor is found, skip the signed parameters segment.
    if (sig_anchor == NULL && i == numtoks - 2) {
      // the signing parameters when no signature anchor is found, should be in the
      // last path segment so skip them.
      continue;
    }
    if (!fixedBufferWrite(&new_path_seg_end, &new_path_seg_len_left, segment[i], strlen(segment[i]))) {
      TSError("insufficient space to copy into new_path_seg buffer.");
      return NULL;
    }
    if (i != numtoks - 1) {
      if (!fixedBufferWrite(&new_path_seg_end, &new_path_seg_len_left, "/", 1)) {
        TSError("insufficient space to copy into new_path_seg buffer.");
        return NULL;
      }
    }
  }
  *new_path_seg_end = '\0';
  TSDebug(PLUGIN_NAME, "new_path_seg: %s", new_path_seg);

  // save the encoded signing parameter data
  if (sig_anchor != NULL) { // a signature anchor string was found.
    if (strlen(sig_anchor) < signed_seg_len) {
      memcpy(signed_seg, sig_anchor, strlen(sig_anchor));
    } else {
      TSError("insufficient space to copy into new_path_seg buffer.");
    }
  } else { // no signature anchor string was found, assum it is in the last path segment.
    if (strlen(segment[numtoks - 2]) < signed_seg_len) {
      memcpy(signed_seg, segment[numtoks - 2], strlen(segment[numtoks - 2]));
    } else {
      TSError("insufficient space to copy into new_path_seg buffer.");
      return NULL;
    }
  }
  TSDebug(PLUGIN_NAME, "signed_seg: %s", signed_seg);

  // no signature anchor was found so decode and save the signing parameters assumed
  // to be in the last path segment.
  if (sig_anchor == NULL) {
    if (TSBase64Decode(segment[numtoks - 2], strlen(segment[numtoks - 2]), decoded_string, sizeof(decoded_string),
                       (size_t *)&decoded_len) != TS_SUCCESS) {
      TSDebug(PLUGIN_NAME, "Unable to decode the  path parameter string.");
    }
  } else {
    if (TSBase64Decode(sig_anchor, strlen(sig_anchor), decoded_string, sizeof(decoded_string), (size_t *)&decoded_len) !=
        TS_SUCCESS) {
      TSDebug(PLUGIN_NAME, "Unable to decode the  path parameter string.");
    }
  }
  TSDebug(PLUGIN_NAME, "decoded_string: %s", decoded_string);

  {
    int oob = 0; /* Out Of Buffer */

    for (i = 0; i < numtoks; i++) {
      // cp the base64 decoded string.
      if (i == sig_anchor_seg && sig_anchor != NULL) {
        if (!fixedBufferWrite(&new_url_end, &new_url_len_left, segment[i], strlen(segment[i]))) {
          oob = 1;
          break;
        }
        if (!fixedBufferWrite(&new_url_end, &new_url_len_left, (char *)decoded_string, strlen((char *)decoded_string))) {
          oob = 1;
          break;
        }
        if (!fixedBufferWrite(&new_url_end, &new_url_len_left, "/", 1)) {
          oob = 1;
          break;
        }

        continue;
      } else if (i == numtoks - 2 && sig_anchor == NULL) {
        if (!fixedBufferWrite(&new_url_end, &new_url_len_left, (char *)decoded_string, strlen((char *)decoded_string))) {
          oob = 1;
          break;
        }
        if (!fixedBufferWrite(&new_url_end, &new_url_len_left, "/", 1)) {
          oob = 1;
          break;
        }
        continue;
      }
      if (!fixedBufferWrite(&new_url_end, &new_url_len_left, segment[i], strlen(segment[i]))) {
        oob = 1;
        break;
      }
      if (i < numtoks - 1) {
        if (!fixedBufferWrite(&new_url_end, &new_url_len_left, "/", 1)) {
          oob = 1;
          break;
        }
      }
    }
    if (oob) {
      TSError("insufficient space to copy into new_url.");
    }
  }
  return TSstrndup(new_url, new_url_end - new_url);
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  const struct config *cfg = (const struct config *)ih;

  int url_len         = 0;
  int current_url_len = 0;
  uint64_t expiration = 0;
  int algorithm       = -1;
  int keyindex        = -1;
  int cmp_res;
  int rval;
  unsigned int i       = 0;
  int j                = 0;
  unsigned int sig_len = 0;
  bool has_path_params = false;

  /* all strings are locally allocated except url... about 25k per instance */
  char *const current_url = TSUrlStringGet(rri->requestBufp, rri->requestUrl, &current_url_len);
  char *url               = current_url;
  char path_params[8192] = {'\0'}, new_path[8192] = {'\0'};
  char signed_part[8192]           = {'\0'}; // this initializes the whole array and is needed
  char urltokstr[8192]             = {'\0'};
  char client_ip[INET6_ADDRSTRLEN] = {'\0'}; // chose the larger ipv6 size
  char ipstr[INET6_ADDRSTRLEN]     = {'\0'}; // chose the larger ipv6 size
  unsigned char sig[MAX_SIG_SIZE + 1];
  char sig_string[2 * MAX_SIG_SIZE + 1];

  if (current_url_len >= MAX_REQ_LEN - 1) {
    err_log(current_url, "Request Url string too long");
    goto deny;
  }

  if (cfg->pristine_url_flag) {
    TSMBuffer mbuf;
    TSMLoc ul;
    TSReturnCode rc = TSHttpTxnPristineUrlGet(txnp, &mbuf, &ul);
    if (rc != TS_SUCCESS) {
      TSError("[url_sig] Failed call to TSHttpTxnPristineUrlGet()");
      goto deny;
    }
    url = TSUrlStringGet(mbuf, ul, &url_len);
    if (url_len >= MAX_REQ_LEN - 1) {
      err_log(url, "Pristine URL string too long.");
      goto deny;
    }
  } else {
    url_len = current_url_len;
  }

  TSDebug(PLUGIN_NAME, "%s", url);

  if (cfg->regex) {
    const int offset = 0, options = 0;
    int ovector[30];

    /* Only search up to the first ? or # */
    const char *base_url_end = url;
    while (*base_url_end && !(*base_url_end == '?' || *base_url_end == '#')) {
      ++base_url_end;
    }
    const int len = base_url_end - url;

    if (pcre_exec(cfg->regex, cfg->regex_extra, url, len, offset, options, ovector, 30) >= 0) {
      goto allow;
    }
  }

  const char *query = strchr(url, '?');

  // check for path params.
  if (query == NULL || strstr(query, "E=") == NULL) {
    char *const parsed = urlParse(url, cfg->sig_anchor, new_path, 8192, path_params, 8192);
    if (NULL == parsed) {
      err_log(url, "Has no signing query string or signing path parameters.");
      goto deny;
    }

    url             = parsed;
    has_path_params = true;
    query           = strstr(url, ";");

    if (query == NULL) {
      err_log(url, "Has no signing query string or signing path parameters.");
      goto deny;
    }
  }

  /* first, parse the query string */
  if (!has_path_params) {
    query++; /* get rid of the ? */
  }
  TSDebug(PLUGIN_NAME, "Query string is:%s", query);

  // Client IP - this one is optional
  const char *cp = strstr(query, CIP_QSTRING "=");
  const char *pp = NULL;
  if (cp != NULL) {
    cp += (strlen(CIP_QSTRING) + 1);
    struct sockaddr const *ip = TSHttpTxnClientAddrGet(txnp);
    if (ip == NULL) {
      TSError("Can't get client ip address.");
      goto deny;
    } else {
      switch (ip->sa_family) {
      case AF_INET:
        TSDebug(PLUGIN_NAME, "ip->sa_family: AF_INET");
        has_path_params == false ? (pp = strstr(cp, "&")) : (pp = strstr(cp, ";"));
        if ((pp - cp) > INET_ADDRSTRLEN - 1 || (pp - cp) < 4) {
          err_log(url, "IP address string too long or short.");
          goto deny;
        }
        strncpy(client_ip, cp, (pp - cp));
        client_ip[pp - cp] = '\0';
        TSDebug(PLUGIN_NAME, "CIP: -%s-", client_ip);
        inet_ntop(AF_INET, &(((struct sockaddr_in *)ip)->sin_addr), ipstr, sizeof ipstr);
        TSDebug(PLUGIN_NAME, "Peer address: -%s-", ipstr);
        if (strcmp(ipstr, client_ip) != 0) {
          err_log(url, "Client IP doesn't match signature.");
          goto deny;
        }
        break;
      case AF_INET6:
        TSDebug(PLUGIN_NAME, "ip->sa_family: AF_INET6");
        has_path_params == false ? (pp = strstr(cp, "&")) : (pp = strstr(cp, ";"));
        if ((pp - cp) > INET6_ADDRSTRLEN - 1 || (pp - cp) < 4) {
          err_log(url, "IP address string too long or short.");
          goto deny;
        }
        strncpy(client_ip, cp, (pp - cp));
        client_ip[pp - cp] = '\0';
        TSDebug(PLUGIN_NAME, "CIP: -%s-", client_ip);
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)ip)->sin6_addr), ipstr, sizeof ipstr);
        TSDebug(PLUGIN_NAME, "Peer address: -%s-", ipstr);
        if (strcmp(ipstr, client_ip) != 0) {
          err_log(url, "Client IP doesn't match signature.");
          goto deny;
        }
        break;
      default:
        TSError("%s: Unknown address family %d", PLUGIN_NAME, ip->sa_family);
        goto deny;
        break;
      }
    }
  }

  // Expiration
  cp = strstr(query, EXP_QSTRING "=");
  if (cp != NULL) {
    cp += strlen(EXP_QSTRING) + 1;
    if (sscanf(cp, "%" SCNu64, &expiration) != 1 || (time_t)expiration < time(NULL)) {
      err_log(url, "Invalid expiration, or expired");
      goto deny;
    }
    TSDebug(PLUGIN_NAME, "Exp: %" PRIu64, expiration);
  } else {
    err_log(url, "Expiration query string not found");
    goto deny;
  }
  // Algorithm
  cp = strstr(query, ALG_QSTRING "=");
  if (cp != NULL) {
    cp += strlen(ALG_QSTRING) + 1;
    algorithm = atoi(cp);
    // The check for a valid algorithm is later.
    TSDebug(PLUGIN_NAME, "Algorithm: %d", algorithm);
  } else {
    err_log(url, "Algorithm query string not found");
    goto deny;
  }
  // Key index
  cp = strstr(query, KIN_QSTRING "=");
  if (cp != NULL) {
    cp += strlen(KIN_QSTRING) + 1;
    keyindex = atoi(cp);
    if (keyindex < 0 || keyindex >= MAX_KEY_NUM || 0 == cfg->keys[keyindex][0]) {
      err_log(url, "Invalid key index");
      goto deny;
    }
    TSDebug(PLUGIN_NAME, "Key Index: %d", keyindex);
  } else {
    err_log(url, "KeyIndex query string not found");
    goto deny;
  }
  // Parts
  const char *parts = NULL;
  cp                = strstr(query, PAR_QSTRING "=");
  if (cp != NULL) {
    cp += strlen(PAR_QSTRING) + 1;
    parts = cp; // NOTE parts is not NULL terminated it is terminated by "&" of next param
    has_path_params == false ? (cp = strstr(parts, "&")) : (cp = strstr(parts, ";"));
    if (cp) {
      TSDebug(PLUGIN_NAME, "Parts: %.*s", (int)(cp - parts), parts);
    } else {
      TSDebug(PLUGIN_NAME, "Parts: %s", parts);
    }
  } else {
    err_log(url, "PartsSigned query string not found");
    goto deny;
  }
  // And finally, the sig (has to be last)
  const char *signature = NULL;
  cp                    = strstr(query, SIG_QSTRING "=");
  if (cp != NULL) {
    cp += strlen(SIG_QSTRING) + 1;
    signature = cp;
    if ((algorithm == USIG_HMAC_SHA1 && strlen(signature) < SHA1_SIG_SIZE) ||
        (algorithm == USIG_HMAC_MD5 && strlen(signature) < MD5_SIG_SIZE)) {
      err_log(url, "Signature query string too short (< 20)");
      goto deny;
    }
  } else {
    err_log(url, "Signature query string not found");
    goto deny;
  }

  /* have the query string, and parameters passed initial checks */
  TSDebug(PLUGIN_NAME, "Found all needed parameters: C=%s E=%" PRIu64 " A=%d K=%d P=%s S=%s", client_ip, expiration, algorithm,
          keyindex, parts, signature);

  /* find the string that was signed - cycle through the parts letters, adding the part of the fqdn/path if it is 1 */
  has_path_params == false ? (cp = strchr(url, '?')) : (cp = strchr(url, ';'));
  // Skip scheme and initial forward slashes.
  const char *skip = strchr(url, ':');
  if (!skip || skip[1] != '/' || skip[2] != '/') {
    goto deny;
  }
  skip += 3;
  memcpy(urltokstr, skip, cp - skip);
  char *strtok_r_p;
  const char *part = strtok_r(urltokstr, "/", &strtok_r_p);
  while (part != NULL) {
    if (parts[j] == '1') {
      strncat(signed_part, part, sizeof(signed_part) - strlen(signed_part) - 1);
      strncat(signed_part, "/", sizeof(signed_part) - strlen(signed_part) - 1);
    }
    if (parts[j + 1] == '0' ||
        parts[j + 1] == '1') { // This remembers the last part, meaning, if there are no more valid letters in parts
      j++;                     // will keep repeating the value of the last one
    }
    part = strtok_r(NULL, "/", &strtok_r_p);
  }

  // chop off the last /, replace with '?' or ';' as appropriate.
  has_path_params == false ? (signed_part[strlen(signed_part) - 1] = '?') : (signed_part[strlen(signed_part) - 1] = '\0');
  cp = strstr(query, SIG_QSTRING "=");
  TSDebug(PLUGIN_NAME, "cp: %s, query: %s, signed_part: %s", cp, query, signed_part);
  strncat(signed_part, query, (cp - query) + strlen(SIG_QSTRING) + 1);

  TSDebug(PLUGIN_NAME, "Signed string=\"%s\"", signed_part);

  /* calculate the expected the signature with the right algorithm */
  switch (algorithm) {
  case USIG_HMAC_SHA1:
    HMAC(EVP_sha1(), (const unsigned char *)cfg->keys[keyindex], strlen(cfg->keys[keyindex]), (const unsigned char *)signed_part,
         strlen(signed_part), sig, &sig_len);
    if (sig_len != SHA1_SIG_SIZE) {
      TSDebug(PLUGIN_NAME, "sig_len: %d", sig_len);
      err_log(url, "Calculated sig len !=  SHA1_SIG_SIZE !");
      goto deny;
    }

    break;
  case USIG_HMAC_MD5:
    HMAC(EVP_md5(), (const unsigned char *)cfg->keys[keyindex], strlen(cfg->keys[keyindex]), (const unsigned char *)signed_part,
         strlen(signed_part), sig, &sig_len);
    if (sig_len != MD5_SIG_SIZE) {
      TSDebug(PLUGIN_NAME, "sig_len: %d", sig_len);
      err_log(url, "Calculated sig len !=  MD5_SIG_SIZE !");
      goto deny;
    }
    break;
  default:
    err_log(url, "Algorithm not supported");
    goto deny;
  }

  for (i = 0; i < sig_len; i++) {
    sprintf(&(sig_string[i * 2]), "%02x", sig[i]);
  }

  TSDebug(PLUGIN_NAME, "Expected signature: %s", sig_string);

  /* and compare to signature that was sent */
  cmp_res = strncmp(sig_string, signature, sig_len * 2);
  if (cmp_res != 0) {
    err_log(url, "Signature check failed");
    goto deny;
  } else {
    TSDebug(PLUGIN_NAME, "Signature check passed");
    goto allow;
  }

/* ********* Deny ********* */
deny:
  if (url != current_url) {
    TSfree((void *)url);
  }
  TSfree((void *)current_url);

  switch (cfg->err_status) {
  case TS_HTTP_STATUS_MOVED_TEMPORARILY:
    TSDebug(PLUGIN_NAME, "Redirecting to %s", cfg->err_url);
    char *start, *end;
    start = cfg->err_url;
    end   = start + strlen(cfg->err_url);
    if (TSUrlParse(rri->requestBufp, rri->requestUrl, (const char **)&start, end) != TS_PARSE_DONE) {
      err_log("url", "Error inn TSUrlParse!");
    }
    rri->redirect = 1;
    break;
  default:
    TSHttpTxnErrorBodySet(txnp, TSstrdup("Authorization Denied"), sizeof("Authorization Denied") - 1, TSstrdup("text/plain"));
    break;
  }
  /* Always set the return status */
  TSHttpTxnStatusSet(txnp, cfg->err_status);

  return TSREMAP_DID_REMAP;

/* ********* Allow ********* */
allow:
  if (url != current_url) {
    TSfree((void *)url);
  }

  const char *current_query = strchr(current_url, '?');
  const char *app_qry       = NULL;
  if (current_query != NULL) {
    current_query++;
    app_qry = getAppQueryString(current_query, strlen(current_query));
  }
  TSDebug(PLUGIN_NAME, "has_path_params: %d", has_path_params);
  if (has_path_params) {
    if (*new_path) {
      TSUrlPathSet(rri->requestBufp, rri->requestUrl, new_path, strlen(new_path));
    }
    TSUrlHttpParamsSet(rri->requestBufp, rri->requestUrl, NULL, 0);
  }

  TSfree((void *)current_url);

  /* drop the query string so we can cache-hit */
  if (app_qry != NULL) {
    rval = TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, app_qry, strlen(app_qry));
    TSfree((void *)app_qry);
  } else {
    rval = TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, NULL, 0);
  }
  if (rval != TS_SUCCESS) {
    TSError("[url_sig] Error setting the query string: %d", rval);
  }

  return TSREMAP_NO_REMAP;
}
