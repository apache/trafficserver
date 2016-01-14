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

#include "ts/ink_defs.h"
#include "url_sig.h"

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

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#include <ts/ts.h>
#include <ts/remap.h>

static const char *PLUGIN_NAME = "url_sig";

struct config {
  TSHttpStatus err_status;
  char *err_url;
  char keys[MAX_KEY_NUM][MAX_KEY_LEN];
  pcre *regex;
  pcre_extra *regex_extra;
};

static void
free_cfg(struct config *cfg)
{
  TSError("[url_sig] Cleaning up...");
  TSfree(cfg->err_url);

  if (cfg->regex_extra)
#ifndef PCRE_STUDY_JIT_COMPILE
    pcre_free(cfg->regex_extra);
#else
    pcre_free_study(cfg->regex_extra);
#endif

  if (cfg->regex)
    pcre_free(cfg->regex);

  TSfree(cfg);
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", (size_t)(errbuf_size - 1));
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "plugin is succesfully initialized");
  return TS_SUCCESS;
}

// To force a config file reload touch remap.config and do a "traffic_ctl config reload"
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  char config_file[PATH_MAX];
  struct config *cfg;

  if (argc != 3) {
    snprintf(errbuf, errbuf_size - 1,
             "[TSRemapNewKeyInstance] - Argument count wrong (%d)... Need exactly two pparam= (config file name).", argc);
    return TS_ERROR;
  }
  TSDebug(PLUGIN_NAME, "Initializing remap function of %s -> %s with config from %s", argv[0], argv[1], argv[2]);

  const char *install_dir = TSInstallDirGet();
  snprintf(config_file, sizeof(config_file), "%s/%s/%s", install_dir, "etc/trafficserver", argv[2]);
  TSDebug(PLUGIN_NAME, "config file name: %s", config_file);
  FILE *file = fopen(config_file, "r");
  if (file == NULL) {
    snprintf(errbuf, errbuf_size - 1, "[TSRemapNewInstance] - Error opening file %s.", config_file);
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
    if (line[0] == '#' || strlen(line) <= 1)
      continue;
    char *pos = strchr(line, '=');
    if (pos == NULL) {
      TSError("[url_sig] Error parsing line %d of file %s (%s).", line_no, config_file, line);
      continue;
    }
    *pos = '\0';
    char *value = pos + 1;
    while (isspace(*value)) // remove whitespace
      value++;
    pos = strchr(value, '\n'); // remove the new line, terminate the string
    if (pos != NULL) {
      *pos = '\0';
    }
    if (pos == NULL || strlen(value) >= MAX_KEY_LEN) {
      snprintf(errbuf, errbuf_size - 1, "[TSRemapNewInstance] - Maximum key length (%d) exceeded on line %d.", MAX_KEY_LEN - 1,
               line_no);
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
        snprintf(errbuf, errbuf_size - 1, "[TSRemapNewInstance] - Key number (%d) >= MAX_KEY_NUM (%d) or NaN.", keynum,
                 MAX_KEY_NUM);
        fclose(file);
        free_cfg(cfg);
        return TS_ERROR;
      }
      strncpy(&cfg->keys[keynum][0], value, MAX_KEY_LEN - 1);
    } else if (strncmp(line, "error_url", 9) == 0) {
      if (atoi(value)) {
        cfg->err_status = atoi(value);
      }
      value += 3;
      while (isspace(*value))
        value++;
      if (cfg->err_status == TS_HTTP_STATUS_MOVED_TEMPORARILY)
        cfg->err_url = TSstrndup(value, strlen(value));
      else
        cfg->err_url = NULL;
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
        TSDebug(PLUGIN_NAME, "Regex compilation failed with error (%s) at character %d.", errptr, erroffset);
      } else {
#ifdef PCRE_STUDY_JIT_COMPILE
        options = PCRE_STUDY_JIT_COMPILE;
#endif
        cfg->regex_extra = pcre_study(
          cfg->regex, options, &errptr); // We do not need to check the error here because we can still run without the studying?
      }
    } else {
      TSError("[url_sig] Error parsing line %d of file %s (%s).", line_no, config_file, line);
    }
  }

  switch (cfg->err_status) {
  case TS_HTTP_STATUS_MOVED_TEMPORARILY:
    if (cfg->err_url == NULL) {
      snprintf(errbuf, errbuf_size - 1, "[TSRemapNewInstance] - Invalid config, err_status == 302, but err_url == NULL");
      fclose(file);
      free_cfg(cfg);
      return TS_ERROR;
    }
    break;
  case TS_HTTP_STATUS_FORBIDDEN:
    if (cfg->err_url != NULL) {
      snprintf(errbuf, errbuf_size - 1, "[TSRemapNewInstance] - Invalid config, err_status == 403, but err_url != NULL");
      fclose(file);
      free_cfg(cfg);
      return TS_ERROR;
    }
    break;
  default:
    snprintf(errbuf, errbuf_size - 1, "[TSRemapNewInstance] - Return code %d not supported.", cfg->err_status);
    fclose(file);
    free_cfg(cfg);
    return TS_ERROR;
  }

  fclose(file);

  *ih = (void *)cfg;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  free_cfg((struct config *)ih);
}

static void
err_log(char *url, char *msg)
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
getAppQueryString(char *query_string, int query_length)
{
  int done = 0;
  char *p;
  char buf[MAX_QUERY_LEN];

  if (query_length > MAX_QUERY_LEN) {
    TSDebug(PLUGIN_NAME, "Cannot process the query string as the length exceeds %d bytes.", MAX_QUERY_LEN);
    return NULL;
  }
  memset(buf, 0, MAX_QUERY_LEN);
  strncpy(buf, query_string, query_length);
  p = buf;

  TSDebug(PLUGIN_NAME, "query_string: %s, query_length: %d", query_string, query_length);
  if (p == NULL) {
    return NULL;
  }

  do {
    switch (*p) {
    case 'A':
    case 'C':
    case 'E':
    case 'K':
    case 'P':
    case 'S':
      done = 1;
      if (*(p - 1) == '&') {
        *(p - 1) = '\0';
      } else
        (*p = '\0');
      break;
    default:
      p = strchr(p, '&');
      if (p == NULL)
        done = 1;
      else
        p++;
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

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  struct config *cfg;
  cfg = (struct config *)ih;

  int url_len = 0;
  time_t expiration = 0;
  int algorithm = -1;
  int keyindex = -1;
  int cmp_res;
  int rval;
  int i = 0;
  int j = 0;
  unsigned int sig_len = 0;

  /* all strings are locally allocated except url... about 25k per instance */
  char *url;
  char signed_part[8192] = {'\0'}; // this initializes the whole array and is needed
  char urltokstr[8192] = {'\0'};
  char client_ip[CIP_STRLEN] = {'\0'};
  char ipstr[CIP_STRLEN] = {'\0'};
  unsigned char sig[MAX_SIG_SIZE + 1];
  char sig_string[2 * MAX_SIG_SIZE + 1];

  /* these are just pointers into other allocations */
  char *signature = NULL;
  char *parts = NULL;
  char *part = NULL;
  char *p = NULL, *pp = NULL;
  char *query = NULL, *app_qry = NULL;

  int retval, sockfd;
  socklen_t peer_len;
  struct sockaddr_in peer;

  url = TSUrlStringGet(rri->requestBufp, rri->requestUrl, &url_len);

  if (url_len >= MAX_REQ_LEN - 1) {
    err_log(url, "URL string too long.");
    goto deny;
  }

  TSDebug(PLUGIN_NAME, "%s", url);

  query = strstr(url, "?");

  if (cfg->regex) {
    int offset = 0, options = 0;
    int ovector[30];
    int len = url_len;
    char *anchor = strstr(url, "#");
    if (query && !anchor) {
      len -= (query - url);
    } else if (anchor && !query) {
      len -= (anchor - url);
    } else if (anchor && query) {
      len -= ((query < anchor ? query : anchor) - url);
    }
    if (pcre_exec(cfg->regex, cfg->regex_extra, url, len, offset, options, ovector, 30) >= 0) {
      goto allow;
    }
  }

  if (query == NULL) {
    err_log(url, "Has no query string.");
    goto deny;
  }

  if (strncmp(url, "http://", strlen("http://")) != 0) {
    err_log(url, "Invalid URL scheme - only http supported.");
    goto deny;
  }

  /* first, parse the query string */
  query++; /* get rid of the ? */
  TSDebug(PLUGIN_NAME, "Query string is:%s", query);

  // Client IP - this one is optional
  p = strstr(query, CIP_QSTRING "=");
  if (p != NULL) {
    p += strlen(CIP_QSTRING + 1);
    pp = strstr(p, "&");
    if ((pp - p) > CIP_STRLEN - 1 || (pp - p) < 4) {
      err_log(url, "IP address string too long or short.");
      goto deny;
    }
    strncpy(client_ip, p + strlen(CIP_QSTRING) + 1, (pp - p - (strlen(CIP_QSTRING) + 1)));
    client_ip[pp - p - (strlen(CIP_QSTRING) + 1)] = '\0';
    TSDebug(PLUGIN_NAME, "CIP: -%s-", client_ip);
    retval = TSHttpTxnClientFdGet(txnp, &sockfd);
    if (retval != TS_SUCCESS) {
      err_log(url, "Error getting sockfd.");
      goto deny;
    }
    peer_len = sizeof(peer);
    if (getpeername(sockfd, (struct sockaddr *)&peer, &peer_len) != 0) {
      perror("Can't get peer address:");
    }
    struct sockaddr_in *s = (struct sockaddr_in *)&peer;
    inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
    TSDebug(PLUGIN_NAME, "Peer address: -%s-", ipstr);
    if (strcmp(ipstr, client_ip) != 0) {
      err_log(url, "Client IP doesn't match signature.");
      goto deny;
    }
  }
  // Expiration
  p = strstr(query, EXP_QSTRING "=");
  if (p != NULL) {
    p += strlen(EXP_QSTRING) + 1;
    expiration = atoi(p);
    if (expiration == 0 || expiration < time(NULL)) {
      err_log(url, "Invalid expiration, or expired.");
      goto deny;
    }
    TSDebug(PLUGIN_NAME, "Exp: %d", (int)expiration);
  } else {
    err_log(url, "Expiration query string not found.");
    goto deny;
  }
  // Algorithm
  p = strstr(query, ALG_QSTRING "=");
  if (p != NULL) {
    p += strlen(ALG_QSTRING) + 1;
    algorithm = atoi(p);
    // The check for a valid algorithm is later.
    TSDebug(PLUGIN_NAME, "Algorithm: %d", algorithm);
  } else {
    err_log(url, "Algorithm query string not found.");
    goto deny;
  }
  // Key index
  p = strstr(query, KIN_QSTRING "=");
  if (p != NULL) {
    p += strlen(KIN_QSTRING) + 1;
    keyindex = atoi(p);
    if (keyindex < 0 || keyindex >= MAX_KEY_NUM || 0 == cfg->keys[keyindex][0]) {
      err_log(url, "Invalid key index.");
      goto deny;
    }
    TSDebug(PLUGIN_NAME, "Key Index: %d", keyindex);
  } else {
    err_log(url, "KeyIndex query string not found.");
    goto deny;
  }
  // Parts
  p = strstr(query, PAR_QSTRING "=");
  if (p != NULL) {
    p += strlen(PAR_QSTRING) + 1;
    parts = p; // NOTE parts is not NULL terminated it is terminated by "&" of next param
    p = strstr(parts, "&");
    TSDebug(PLUGIN_NAME, "Parts: %.*s", (int)(p - parts), parts);
  } else {
    err_log(url, "PartsSigned query string not found.");
    goto deny;
  }
  // And finally, the sig (has to be last)
  p = strstr(query, SIG_QSTRING "=");
  if (p != NULL) {
    p += strlen(SIG_QSTRING) + 1;
    signature = p; // NOTE sig is not NULL terminated, it has to be 20 chars
    if ((algorithm == USIG_HMAC_SHA1 && strlen(signature) < SHA1_SIG_SIZE) ||
        (algorithm == USIG_HMAC_MD5 && strlen(signature) < MD5_SIG_SIZE)) {
      err_log(url, "Signature query string too short (< 20).");
      goto deny;
    }
  } else {
    err_log(url, "Signature query string not found.");
    goto deny;
  }

  /* have the query string, and parameters passed initial checks */
  TSDebug(PLUGIN_NAME, "Found all needed parameters: C=%s E=%d A=%d K=%d P=%s S=%s", client_ip, (int)expiration, algorithm,
          keyindex, parts, signature);

  /* find the string that was signed - cycle through the parts letters, adding the part of the fqdn/path if it is 1 */
  p = strstr(url, "?");
  memcpy(urltokstr, &url[strlen("http://")], p - url - strlen("http://"));
  part = strtok_r(urltokstr, "/", &p);
  while (part != NULL) {
    if (parts[j] == '1') {
      strcpy(signed_part + strlen(signed_part), part);
      strcpy(signed_part + strlen(signed_part), "/");
    }
    if (parts[j + 1] == '0' ||
        parts[j + 1] == '1') // This remembers the last part, meaning, if there are no more valid letters in parts
      j++;                   // will keep repeating the value of the last one
    part = strtok_r(NULL, "/", &p);
  }

  signed_part[strlen(signed_part) - 1] = '?'; // chop off the last /, replace with '?'
  p = strstr(query, SIG_QSTRING "=");
  strncat(signed_part, query, (p - query) + strlen(SIG_QSTRING) + 1);

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
    err_log(url, "Algorithm not supported.");
    goto deny;
  }

  for (i = 0; i < sig_len; i++) {
    sprintf(&(sig_string[i * 2]), "%02x", sig[i]);
  }

  TSDebug(PLUGIN_NAME, "Expected signature: %s", sig_string);

  /* and compare to signature that was sent */
  cmp_res = strncmp(sig_string, signature, sig_len * 2);
  if (cmp_res != 0) {
    err_log(url, "Signature check failed.");
    goto deny;
  } else {
    TSDebug(PLUGIN_NAME, "Signature check passed.");
    goto allow;
  }

/* ********* Deny ********* */
deny:
  TSfree(url);

  switch (cfg->err_status) {
  case TS_HTTP_STATUS_MOVED_TEMPORARILY:
    TSDebug(PLUGIN_NAME, "Redirecting to %s", cfg->err_url);
    char *start, *end;
    start = cfg->err_url;
    end = start + strlen(cfg->err_url);
    if (TSUrlParse(rri->requestBufp, rri->requestUrl, (const char **)&start, end) != TS_PARSE_DONE) {
      err_log("url", "Error inn TSUrlParse!");
    }
    rri->redirect = 1;
    break;
  default:
    TSHttpTxnErrorBodySet(txnp, TSstrdup("Authorization Denied"), strlen("Authorization Denied") - 1, TSstrdup("text/plain"));
    break;
  }
  /* Always set the return status */
  TSHttpTxnSetHttpRetStatus(txnp, cfg->err_status);

  return TSREMAP_DID_REMAP;

/* ********* Allow ********* */
allow:
  app_qry = getAppQueryString(query, strlen(query));

  TSfree(url);
  /* drop the query string so we can cache-hit */
  if (app_qry != NULL) {
    rval = TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, app_qry, strlen(app_qry));
    TSfree(app_qry);
  } else {
    rval = TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, NULL, 0);
  }
  if (rval != TS_SUCCESS) {
    TSError("[url_sig] Error setting the query string: %d.", rval);
  }
  return TSREMAP_NO_REMAP;
}
