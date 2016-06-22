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
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/* cacheurl.cc - Plugin to modify the URL used as a cache key for certain
 * requests, without modifying the URL used for actually fetching data from
 * the origin server.
 */

#include <stdio.h>
#include <string.h>

#include "ts/ts.h"
#include "ts/remap.h"

#include "ts/ink_defs.h"
#include "ts/ink_memory.h"

#include <string>
#include <vector>

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#define TOKENCOUNT 10
#define OVECOUNT 30
#define PLUGIN_NAME "cacheurl"
#define DEFAULT_CONFIG "cacheurl.config"

struct regex_info {
  pcre *re;          /* Compiled regular expression */
  int tokcount;      /* Token count */
  char *pattern;     /* Pattern string */
  char *replacement; /* Replacement string */
  int *tokens;       /* Array of $x token values */
  int *tokenoffset;  /* Array of $x token offsets */
};

struct pr_list {
  std::vector<regex_info *> pr;

  pr_list() {}
  ~pr_list()
  {
    for (std::vector<regex_info *>::iterator info = this->pr.begin(); info != this->pr.end(); ++info) {
      TSfree((*info)->tokens);
      TSfree((*info)->tokenoffset);
      pcre_free((*info)->re);
      TSfree(*info);
    }
  }
};

static int
regex_substitute(char **buf, char *str, regex_info *info)
{
  int matchcount;
  int ovector[OVECOUNT]; /* Locations of matches in regex */

  int replacelen; /* length of replacement string */
  int i;
  int offset;
  int prev;

  /* Perform the regex matching */
  matchcount = pcre_exec(info->re, NULL, str, strlen(str), 0, 0, ovector, OVECOUNT);
  if (matchcount < 0) {
    switch (matchcount) {
    case PCRE_ERROR_NOMATCH:
      break;
    default:
      TSError("[%s] Matching error: %d", PLUGIN_NAME, matchcount);
      break;
    }
    return 0;
  }

  /* Verify the replacement has the right number of matching groups */
  for (i = 0; i < info->tokcount; i++) {
    if (info->tokens[i] >= matchcount) {
      TSError("[%s] Invalid reference int replacement: $%d", PLUGIN_NAME, info->tokens[i]);
      return 0;
    }
  }

  /* malloc the replacement string */
  replacelen = strlen(info->replacement);
  replacelen -= info->tokcount * 2; /* Subtract $1, $2 etc... */
  for (i = 0; i < info->tokcount; i++) {
    replacelen += (ovector[info->tokens[i] * 2 + 1] - ovector[info->tokens[i] * 2]);
  }
  replacelen++; /* Null terminator */
  *buf = (char *)TSmalloc(replacelen);

  /* perform string replacement */
  offset = 0; /* Where we are adding new data in the string */
  prev   = 0;
  for (i = 0; i < info->tokcount; i++) {
    memcpy(*buf + offset, info->replacement + prev, info->tokenoffset[i] - prev);
    offset += (info->tokenoffset[i] - prev);
    prev = info->tokenoffset[i] + 2;

    memcpy(*buf + offset, str + ovector[info->tokens[i] * 2], ovector[info->tokens[i] * 2 + 1] - ovector[info->tokens[i] * 2]);
    offset += (ovector[info->tokens[i] * 2 + 1] - ovector[info->tokens[i] * 2]);
  }
  memcpy(*buf + offset, info->replacement + prev, strlen(info->replacement) - prev);
  offset += strlen(info->replacement) - prev;
  (*buf)[offset] = 0; /* Null termination */
  return 1;
}

static int
regex_compile(regex_info **buf, char *pattern, char *replacement)
{
  const char *reerror; /* Error string from pcre */
  int reerroffset;     /* Offset where any pcre error occured */

  int tokcount;
  int *tokens      = NULL;
  int *tokenoffset = NULL;

  int status       = 1; /* Status (return value) of the function */
  regex_info *info = (regex_info *)TSmalloc(sizeof(regex_info));

  /* Precompile the regular expression */
  info->re = pcre_compile(pattern, 0, &reerror, &reerroffset, NULL);
  if (!info->re) {
    TSError("[%s] Compilation of regex '%s' failed at char %d: %s", PLUGIN_NAME, pattern, reerroffset, reerror);
    status = 0;
  }

  /* Precalculate the location of $X tokens in the replacement */
  tokcount = 0;
  if (status) {
    tokens      = (int *)TSmalloc(sizeof(int) * TOKENCOUNT);
    tokenoffset = (int *)TSmalloc(sizeof(int) * TOKENCOUNT);
    for (unsigned i = 0; i < strlen(replacement); i++) {
      if (replacement[i] == '$') {
        if (tokcount >= TOKENCOUNT) {
          TSError("[%s] Error: too many tokens in replacement "
                  "string: %s",
                  PLUGIN_NAME, replacement);
          status = 0;
          break;
        } else if (replacement[i + 1] < '0' || replacement[i + 1] > '9') {
          TSError("[%s] Error: Invalid replacement token $%c in %s: should be $0 - $9", PLUGIN_NAME, replacement[i + 1],
                  replacement);
          status = 0;
          break;
        } else {
          /* Store the location of the replacement */
          /* Convert '0' to 0 */
          tokens[tokcount]      = replacement[i + 1] - '0';
          tokenoffset[tokcount] = i;
          tokcount++;
          /* Skip the next char */
          i++;
        }
      }
    }
  }

  if (status) {
    /* Everything went OK */
    info->tokcount    = tokcount;
    info->tokens      = tokens;
    info->tokenoffset = tokenoffset;

    info->pattern     = TSstrdup(pattern);
    info->replacement = TSstrdup(replacement);

    *buf = info;
  } else {
    /* Something went wrong, clean up */
    TSfree(tokens);
    TSfree(tokenoffset);
    if (info->re)
      pcre_free(info->re);
    TSfree(info);
  }
  return status;
}

static pr_list *
load_config_file(const char *config_file)
{
  char buffer[1024];
  std::string path;
  TSFile fh;
  ats_scoped_obj<pr_list> prl(new pr_list());

  /* locations in a config file line, end of line, split start, split end */
  char *eol, *spstart, *spend;
  int lineno = 0;
  int retval;
  regex_info *info = 0;

  if (config_file == NULL) {
    config_file = DEFAULT_CONFIG;
  }

  if (*config_file != '/') {
    // Relative paths are relative to the config directory
    path = TSConfigDirGet();
    path += "/";
    path += config_file;
  } else {
    // Absolute path ...
    path = config_file;
  }

  TSDebug(PLUGIN_NAME, "Opening config file: %s", path.c_str());
  fh = TSfopen(path.c_str(), "r");

  if (!fh) {
    TSError("[%s] Unable to open %s. No patterns will be loaded", PLUGIN_NAME, path.c_str());
    return NULL;
  }

  while (TSfgets(fh, buffer, sizeof(buffer) - 1)) {
    lineno++;

    // make sure line was not bigger than buffer
    if ((eol = strchr(buffer, '\n')) == NULL && (eol = strstr(buffer, "\r\n")) == NULL) {
      // Malformed line - skip
      TSError("%s: config line too long, did not get a good line in cfg, skipping, line: %s", PLUGIN_NAME, buffer);
      memset(buffer, 0, sizeof(buffer));
      continue;
    } else {
      *eol = 0;
    }
    // make sure line has something useful on it
    // or allow # Comments, only at line beginning
    if (eol - buffer < 2 || buffer[0] == '#') {
      memset(buffer, 0, sizeof(buffer));
      continue;
    }

    /* Split line into two parts based on whitespace */
    /* Find first whitespace */
    spstart = strstr(buffer, " ");
    if (!spstart) {
      spstart = strstr(buffer, "\t");
    }
    if (!spstart) {
      TSError("[%s] ERROR: Invalid format on line %d. Skipping", PLUGIN_NAME, lineno);
      TSfclose(fh);
      return NULL;
    }
    /* Find part of the line after any whitespace */
    spend = spstart + 1;
    while (*spend == ' ' || *spend == '\t') {
      spend++;
    }
    if (*spend == 0) {
      /* We reached the end of the string without any non-whitepace */
      TSError("[%s] ERROR: Invalid format on line %d. Skipping", PLUGIN_NAME, lineno);
      TSfclose(fh);
      return NULL;
    }

    *spstart = 0;
    /* We have the pattern/replacement, now do precompilation.
     * buffer is the first part of the line. spend is the second part just
     * after the whitespace */
    TSDebug(PLUGIN_NAME, "Adding pattern/replacement pair: '%s' -> '%s'", buffer, spend);
    retval = regex_compile(&info, buffer, spend);
    if (!retval) {
      TSError("[%s] Error precompiling regex/replacement. Skipping.", PLUGIN_NAME);
      TSfclose(fh);
      return NULL;
    }

    prl->pr.push_back(info);
  }
  TSfclose(fh);

  if (prl->pr.empty()) {
    TSError("[%s] No regular expressions loaded.", PLUGIN_NAME);
  }

  TSDebug(PLUGIN_NAME, "loaded %u regexes", (unsigned)prl->pr.size());
  return prl.release();
}

static int
rewrite_cacheurl(pr_list *prl, TSHttpTxn txnp)
{
  int ok       = 1;
  char *newurl = 0;
  int retval;
  char *url;
  int url_length;

  url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_length);
  if (!url) {
    TSError("[%s] couldn't retrieve request url", PLUGIN_NAME);
    ok = 0;
  }

  if (ok) {
    for (std::vector<regex_info *>::iterator info = prl->pr.begin(); info != prl->pr.end(); ++info) {
      retval = regex_substitute(&newurl, url, *info);
      if (retval) {
        /* Successful match/substitution */
        break;
      }
    }
    if (newurl) {
      TSDebug(PLUGIN_NAME, "Rewriting cache URL for %s to %s", url, newurl);
      if (TSCacheUrlSet(txnp, newurl, strlen(newurl)) != TS_SUCCESS) {
        TSError("[%s] Unable to modify cache url from "
                "%s to %s",
                PLUGIN_NAME, url, newurl);
        ok = 0;
      }
    }
  }

  /* Clean up */
  if (url)
    TSfree(url);
  if (newurl)
    TSfree(newurl);

  return ok;
}

static int
handle_hook(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;
  pr_list *prl;
  int ok = 1;

  prl = (pr_list *)TSContDataGet(contp);

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    ok = rewrite_cacheurl(prl, txnp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    TSAssert(!"Unexpected event");
    ok = 0;
    break;
  }

  return ok;
}

/* Generic error message function for errors in plugin initialization */
static void
initialization_error(const char *msg)
{
  TSError("[%s] %s", PLUGIN_NAME, msg);
  TSError("[%s] Unable to initialize plugin (disabled).", PLUGIN_NAME);
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[tsremap_init] Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[tsremap_init] Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "remap plugin is successfully initialized");

  TSError("[%s] is deprecated and will be removed as of v7.0.0", PLUGIN_NAME);

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf ATS_UNUSED, int errbuf_size ATS_UNUSED)
{
  *ih = load_config_file(argc > 2 ? argv[2] : NULL);
  return (NULL == *ih) ? TS_ERROR : TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  pr_list *prl = (pr_list *)ih;

  TSDebug(PLUGIN_NAME, "Deleting remap instance");

  delete prl;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri ATS_UNUSED)
{
  int ok;

  ok = rewrite_cacheurl((pr_list *)ih, rh);
  if (ok) {
    return TSREMAP_NO_REMAP;
  } else {
    return TSREMAP_ERROR;
  }
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont contp;
  pr_list *prl;

  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "ERROR, Plugin registration failed");
    initialization_error("Plugin registration failed.");
    return;
  }

  prl = load_config_file(argc > 1 ? argv[1] : NULL);
  if (prl) {
    contp = TSContCreate((TSEventFunc)handle_hook, NULL);
    /* Store the pattern replacement list in the continuation */
    TSContDataSet(contp, prl);
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
  } else {
    TSDebug(PLUGIN_NAME, "ERROR, Plugin config load failed.");
    initialization_error("Plugin config load failed.");
    return;
  }

  TSError("[%s] is deprecated and will be removed as of v7.0.0", PLUGIN_NAME);
}
