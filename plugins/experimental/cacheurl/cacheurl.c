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

/* cacheurl.c - Plugin to modify the URL used as a cache key for certain
 * requests, without modifying the URL used for actually fetching data from
 * the origin server.
 */

#include <ts/ts.h>
#include <stdio.h>
#include <pcre.h>
#include <string.h>

#define TOKENCOUNT 10
#define OVECOUNT 30
#define PATTERNCOUNT 30
#define PLUGIN_NAME "cacheurl"

typedef struct {
    pcre *re;       /* Compiled regular expression */
    int tokcount;   /* Token count */
    char *pattern;  /* Pattern string */
    char *replacement; /* Replacement string */
    int *tokens;    /* Array of $x token values */
    int *tokenoffset; /* Array of $x token offsets */
} regex_info;

static regex_info *pr_list[PATTERNCOUNT]; /* Pattern/replacement list */
static int patterncount;

static TSTextLogObject log;

static int regex_substitute(char **buf, char *str, regex_info *info) {
    int matchcount;
    int ovector[OVECOUNT]; /* Locations of matches in regex */

    int replacelen; /* length of replacement string */
    int i;
    int offset;
    int prev;

    /* Perform the regex matching */
    matchcount = pcre_exec(info->re, NULL, str, strlen(str), 0, 0, ovector,
            OVECOUNT);
    if (matchcount < 0) {
        switch (matchcount) {
            case PCRE_ERROR_NOMATCH:
                break;
            default:
                TSError("[%s] Matching error: %d\n", PLUGIN_NAME, matchcount);
                break;
        }
        return 0;
    }

    /* Verify the replacement has the right number of matching groups */
    for (i=0; i<info->tokcount; i++) {
        if (info->tokens[i] >= matchcount) {
            TSError("[%s] Invalid reference int replacement: $%d\n", PLUGIN_NAME, info->tokens[i]);
            return 0;
        }
    }

    /* malloc the replacement string */
    replacelen = strlen(info->replacement);
    replacelen -= info->tokcount * 2; /* Subtract $1, $2 etc... */
    for (i=0; i<info->tokcount; i++) {
        replacelen += (ovector[info->tokens[i]*2+1] -
                ovector[info->tokens[i]*2]);
    }
    replacelen++; /* Null terminator */
    *buf = TSmalloc(replacelen);

    /* perform string replacement */
    offset = 0; /* Where we are adding new data in the string */
    prev = 0;
    for (i=0; i<info->tokcount; i++) {
        memcpy(*buf + offset, info->replacement + prev,
                info->tokenoffset[i] - prev);
        offset += (info->tokenoffset[i] - prev);
        prev = info->tokenoffset[i] + 2;

        memcpy(*buf + offset,  str + ovector[info->tokens[i]*2],
                ovector[info->tokens[i]*2+1] - ovector[info->tokens[i]*2]);
        offset += (ovector[info->tokens[i]*2+1] - ovector[info->tokens[i]*2]);
    }
    memcpy(*buf + offset, info->replacement + prev,
            strlen(info->replacement) - prev);
    offset += strlen(info->replacement) - prev;
    (*buf)[offset] = 0; /* Null termination */
    return 1;
}

static int regex_compile(regex_info **buf, char *pattern, char *replacement) {
    const char *reerror; /* Error string from pcre */
    int reerroffset;     /* Offset where any pcre error occured */

    int tokcount;
    int *tokens;
    int *tokenoffset;

    int i;

    int status = 1;      /* Status (return value) of the function */

    regex_info *info = TSmalloc(sizeof(regex_info));


    /* Precompile the regular expression */
    info->re =  pcre_compile(pattern, 0, &reerror, &reerroffset, NULL);
    if (!info->re) {
        TSError("[%s] Compilation of regex '%s' failed at char %d: %s\n",
                PLUGIN_NAME, pattern, reerroffset, reerror);
        status = 0;
    }

    /* Precalculate the location of $X tokens in the replacement */
    tokcount = 0;
    if (status) {
        tokens = TSmalloc(sizeof(int) * TOKENCOUNT);
        tokenoffset = TSmalloc(sizeof(int) * TOKENCOUNT);
        for (i=0; i<strlen(replacement); i++) {
            if (replacement[i] == '$') {
                if (tokcount >= TOKENCOUNT) {
                    TSError("[%s] Error: too many tokens in replacement "
                            "string: %s\n", PLUGIN_NAME, replacement);
                    status = 0;
                    break;
                } else if (replacement[i+1] < '0' || replacement[i+1] > '9') {
                    TSError("[%s] Error: Invalid replacement token $%c in "
                            "%s: should be $0 - $9\n", PLUGIN_NAME,
                            replacement[i+1], replacement);
                    status = 0;
                    break;
                } else {
                    /* Store the location of the replacement */
                    /* Convert '0' to 0 */
                    tokens[tokcount] = replacement[i+1] - '0';
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
        info->tokcount = tokcount;
        info->tokens = tokens;
        info->tokenoffset = tokenoffset;

        info->pattern = TSstrdup(pattern);
        info->replacement = TSstrdup(replacement);

        *buf = info;
    } else {
        /* Something went wrong, clean up */
        if (info->tokens) TSfree(info->tokens);
        if (info->tokenoffset) TSfree(info->tokenoffset);
        if (info->re) pcre_free(info->re);
        if (info) TSfree(info);
    }
    return status;
}

static void load_config_file() {
    char buffer[1024];
    char config_file[1024];
    TSFile fh;

    /* locations in a config file line, end of line, split start, split end */
    char *eol, *spstart, *spend;
    int lineno = 0;
    int retval;
    regex_info *info = 0;

    /* TODO - should this file be located in etc/ rather than the plugin dir?
     * */
    sprintf(config_file, "%s/cacheurl.config", TSPluginDirGet());
    TSDebug(PLUGIN_NAME, "Opening config file: %s", config_file);
    fh = TSfopen(config_file, "r");

    if (!fh) {
        TSError("[%s] Unable to open %s. No patterns will be loaded\n",
                PLUGIN_NAME, config_file);
        return;
    }

    while (TSfgets(fh, buffer, sizeof(buffer) - 1)) {
        lineno++;
        if (*buffer == '#') {
            /* # Comments, only at line beginning */
            continue;
        }
        eol = strstr(buffer, "\n");
        if (eol) {
            *eol = 0; /* Terminate string at newline */
        } else {
            /* Malformed line - skip */
            continue;
        }
        /* Split line into two parts based on whitespace */
        /* Find first whitespace */
        spstart = strstr(buffer, " ");
        if (!spstart) {
            spstart = strstr(buffer, "\t");
        }
        if (!spstart) {
            TSError("[%s] ERROR: Invalid format on line %d. Skipping\n",
                    PLUGIN_NAME, lineno);
            continue;
        }
        /* Find part of the line after any whitespace */
        spend = spstart + 1;
        while(*spend == ' ' || *spend == '\t') {
            spend++;
        }
        if (*spend == 0) {
            /* We reached the end of the string without any non-whitepace */
            TSError("[%s] ERROR: Invalid format on line %d. Skipping\n",
                    PLUGIN_NAME, lineno);
            continue;
        }

        *spstart = 0;
        /* We have the pattern/replacement, now do precompilation.
         * buffer is the first part of the line. spend is the second part just
         * after the whitespace */
        if (log) {
            TSTextLogObjectWrite(log,
                    "Adding pattern/replacement pair: '%s' -> '%s'",
                    buffer, spend);
        }
        TSDebug(PLUGIN_NAME, "Adding pattern/replacement pair: '%s' -> '%s'\n",
                buffer, spend);
        retval = regex_compile(&info, buffer, spend);
        if (!retval) {
            TSError("[%s] Error precompiling regex/replacement. Skipping.\n",
                    PLUGIN_NAME);
        }
        if (patterncount > PATTERNCOUNT) {
            TSError("[%s] Warning, too many patterns - skipping the rest"
                    "(max: %d)\n", PLUGIN_NAME, PATTERNCOUNT);
            TSfree(info);
            break;
        }
        pr_list[patterncount] = info;
        patterncount++;
    }
    TSfclose(fh);
}

static int handle_hook(TSCont *contp, TSEvent event, void *edata) {
    TSHttpTxn txnp = (TSHttpTxn) edata;
    char *newurl = 0;
    int retval;

    char *url;
    int url_length;
    int i;

    int ok = 1;

    switch (event) {
        case TS_EVENT_HTTP_READ_REQUEST_HDR:
            if (ok) {
                url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_length);
                if (!url) {
                    TSError("[%s] couldn't retrieve request url\n",
                            PLUGIN_NAME);
                    ok = 0;
                }
            }

            if (ok) {
                for (i=0; i<patterncount; i++) {
                    retval = regex_substitute(&newurl, url, pr_list[i]);
                    if (retval) {
                        /* Successful match/substitution */
                        break;
                    }
                }
                if (newurl) {
                    if (log) {
                        TSTextLogObjectWrite(log,
                                "Rewriting cache URL for %s to %s", url,
                                newurl);
                    }
                    TSDebug(PLUGIN_NAME, "Rewriting cache URL for %s to %s\n",
                            url, newurl);
                    if (TSCacheUrlSet(txnp, newurl, strlen(newurl))
                            != TS_SUCCESS) {
                        TSError("[%s] Unable to modify cache url from "
                                "%s to %s\n", PLUGIN_NAME, url, newurl);
                        ok = 0;
                    }
                }
            }
            /* Clean up */
            if (url) TSfree(url);
            if (newurl) TSfree(newurl);
            TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
            break;
        default:
            TSAssert(!"Unexpected event");
            ok = 0;
            break;
    }

    return ok;
}

/* Function to ensure we're running a recent enough version of Traffic Server.
 * (Taken from the example plugin)
 */
static int check_ts_version() {
  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version,
                &patch_ts_version) != 3) {
      return 0;
    }

    /* we are now v3.x */
    if (major_ts_version >= 3) {
      result = 1;
    }

  }
  return result;
}

/* Generic error message function for errors in plugin initialization */
static void initialization_error(char *msg) {
    TSError("[%s] %s\n", PLUGIN_NAME, msg);
    TSError("[%s] Unable to initialize plugin (disabled).\n", PLUGIN_NAME);
}

void TSPluginInit(int argc, const char *argv[]) {
    TSPluginRegistrationInfo info;
    TSReturnCode error;

    info.plugin_name = PLUGIN_NAME;
    info.vendor_name = "OmniTI";
    info.support_email = "sa@omniti.com";

    if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
        initialization_error("Plugin registration failed.");
        return;
    }

    if (!check_ts_version()) {
        initialization_error("Plugin requires Traffic Server 3.0 or later");
        return;
    }

    error = TSTextLogObjectCreate("cacheurl", TS_LOG_MODE_ADD_TIMESTAMP,
            &log);
    if (!log || error == TS_ERROR) {
        TSError("[%s] Error creating log file\n", PLUGIN_NAME);
    }

    load_config_file();

    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK,
            TSContCreate((TSEventFunc)handle_hook, NULL));
}
