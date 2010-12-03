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

#ifndef _MACRO_H_
#define _MACRO_H_

#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME

#define LOG_AUTO_ERROR(API_NAME, COMMENT) \
{ \
    TSDebug(AUTO_TAG, "%s %s [%s: line %d] (%s)", PLUGIN_NAME, API_NAME, FUNCTION_NAME, \
            __LINE__, COMMENT); \
}
#define LOG_API_ERROR(API_NAME) { \
    TSDebug(DEBUG_TAG, "%s: %s %s [%s] File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}

#define LOG_API_ERROR_COMMENT(API_NAME, COMMENT) { \
    TSDebug(DEBUG_TAG, "%s: %s %s [%s] File %s, line number %d (%s)", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__, COMMENT); \
}

#define LOG_ERROR_AND_RETURN(API_NAME) \
{ \
    LOG_API_ERROR(API_NAME); \
    return -1; \
}
#define LOG_ERROR_AND_CLEANUP(API_NAME) \
{ \
    LOG_API_ERROR(API_NAME); \
    goto done; \
}
#define LOG_ERROR_AND_REENABLE(API_NAME) \
{ \
    LOG_API_ERROR(API_NAME); \
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE); \
}

#define LOG_ERROR_NEG(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}

/* added by nkale for internal plugins */
#define LOG_NEG_ERROR(API_NAME) { \
    TSDebug(NEG_ERROR_TAG, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}

/* Release macros */
#define VALID_PTR(X) ((X != NULL) && (X != TS_ERROR_PTR))

#define FREE(X) \
{ \
    if (VALID_PTR(X)) { \
        TSfree((void *)X); \
        X = NULL; \
    } \
} \

#define HANDLE_RELEASE(P_BUFFER, P_PARENT, P_MLOC) \
{ \
    if (VALID_PTR(P_MLOC)) { \
        if (TSHandleMLocRelease(P_BUFFER, P_PARENT, P_MLOC) == TS_ERROR) { \
            LOG_API_ERROR("TSHandleMLocRelease"); \
        } else { \
            P_MLOC = (TSMLoc)NULL; \
        } \
    } \
}\

#define URL_DESTROY(P_BUFFER, P_MLOC) \
{ \
    if (VALID_PTR(P_MLOC)) {\
        TSUrlDestroy (P_BUFFER, P_MLOC); \
    } else { \
        P_MLOC = (TSMLoc)NULL; \
    } \
}\

#define HDR_DESTROY(P_BUFFER, P_MLOC) \
{ \
    if (VALID_PTR(P_MLOC)) \
        if (TSHttpHdrDestroy (P_BUFFER, P_MLOC) == TS_ERROR) \
            LOG_API_ERROR("TSHttpHdrDestroy"); \
}\

#define BUFFER_DESTROY(P_BUFFER) \
{ \
    if (VALID_PTR(P_BUFFER)) \
        if (TSMBufferDestroy (P_BUFFER) == TS_ERROR) \
            LOG_API_ERROR("TSMBufferDestroy"); \
}\

#endif
