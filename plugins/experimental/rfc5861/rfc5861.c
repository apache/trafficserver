/** @file

  Implements RFC 5861 (HTTP Cache-Control Extensions for Stale Content)

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

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <search.h>
#include <ts/ts.h>
#include <getopt.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
#include <arpa/inet.h>

#include <ts/experimental.h>

#define LOG_PREFIX "rfc5861"

//#define ENABLE_SAVE_ORIGINAL_REQUEST

static const char HTTP_VALUE_STALE_WHILE_REVALIDATE[] = "stale-while-revalidate";
static const char HTTP_VALUE_STALE_IF_ERROR[] = "stale-if-error";
static const char HTTP_VALUE_STALE_WARNING[] = "110 Response is stale";

void *troot = NULL;
TSMutex troot_mutex;
int txn_slot;

struct
{
    TSTextLogObject object;
    bool all, stale_if_error, stale_while_revalidate;
    char* filename;
} log_info = {NULL,false,false,false,"rfc5861"};

typedef struct
{
    time_t date, stale_while_revalidate, stale_on_error, max_age;
} CachedHeaderInfo;

typedef struct
{
    char *effective_url;
    TSMBuffer buf;
    TSMLoc http_hdr_loc;
    struct sockaddr *client_addr;
} RequestInfo;

typedef struct
{
    TSMBuffer buf;
    TSMLoc http_hdr_loc;
    TSHttpParser parser;
    bool parsed;
    TSHttpStatus status;
} ResponseInfo;

typedef struct
{
    TSHttpTxn txn;
    TSCont main_cont;
    bool async_req;
    TSIOBuffer req_io_buf, resp_io_buf;
    TSIOBufferReader req_io_buf_reader, resp_io_buf_reader;
    TSVIO r_vio, w_vio;
    TSVConn vconn;
    RequestInfo *req_info;
    ResponseInfo *resp_info;
    time_t txn_start;
} StateInfo;

static ResponseInfo*
create_response_info(void)
{
    ResponseInfo *resp_info;

    TSDebug(LOG_PREFIX, "Entering create_response_info");

    resp_info = (ResponseInfo *) TSmalloc(sizeof(ResponseInfo));

    resp_info->buf = TSMBufferCreate();
    resp_info->http_hdr_loc = TSHttpHdrCreate(resp_info->buf);
    resp_info->parser = TSHttpParserCreate();
    resp_info->parsed = false;

    TSDebug(LOG_PREFIX, "Leaving create_reseponse_info");

    return resp_info;
}

static void
free_response_info(ResponseInfo *resp_info)
{
    TSDebug(LOG_PREFIX, "Entering free_response_info");

    TSHandleMLocRelease(resp_info->buf, TS_NULL_MLOC, resp_info->http_hdr_loc);
    TSMBufferDestroy(resp_info->buf);
    TSHttpParserDestroy(resp_info->parser);
    TSfree(resp_info);

    TSDebug(LOG_PREFIX, "Leaving free_response_info");
}

static RequestInfo*
create_request_info(TSHttpTxn txn)
{
    RequestInfo *req_info;
    char *url;
    int url_len;
    TSMBuffer buf;
    TSMLoc loc;

    TSDebug(LOG_PREFIX, "Entering create_request_info");

    req_info = (RequestInfo *) TSmalloc(sizeof(RequestInfo));

    url = TSHttpTxnEffectiveUrlStringGet(txn, &url_len);
    req_info->effective_url = TSstrndup(url, url_len);
    TSfree(url);
    //TSDebug(LOG_PREFIX, "URL: %s", req_info->effective_url);

    TSHttpTxnClientReqGet(txn, &buf, &loc);
    req_info->buf = TSMBufferCreate();
    TSHttpHdrClone(req_info->buf, buf, loc, &(req_info->http_hdr_loc));
    TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);

    req_info->client_addr = TSmalloc(sizeof(struct sockaddr));
    memmove((void *) req_info->client_addr, (void *) TSHttpTxnClientAddrGet(txn), sizeof(struct sockaddr));

    TSDebug(LOG_PREFIX, "Leaving create_request_info");

    return req_info;
}

static void
free_request_info(RequestInfo *req_info)
{
    TSDebug(LOG_PREFIX, "Entering free_request_info");
    TSDebug(LOG_PREFIX, "Free effective URL");
    //TSDebug(LOG_PREFIX, "URL: %s", req_info->effective_url);
    TSfree(req_info->effective_url);
    TSDebug(LOG_PREFIX, "Release Http Header");
    TSHandleMLocRelease(req_info->buf, TS_NULL_MLOC, req_info->http_hdr_loc);
    TSDebug(LOG_PREFIX, "Destroy Buffer");
    TSMBufferDestroy(req_info->buf);
    TSDebug(LOG_PREFIX, "Free Client Addr");
    TSfree(req_info->client_addr);
    TSDebug(LOG_PREFIX, "Free Request Info");
    TSfree(req_info);

    TSDebug(LOG_PREFIX, "Leaving free_request_info");
}

static CachedHeaderInfo*
get_cached_header_info(TSHttpTxn txn)
{
    CachedHeaderInfo* chi;
    TSMBuffer cr_buf;
    TSMLoc cr_hdr_loc, cr_date_loc, cr_cache_control_loc, cr_cache_control_dup_loc;
    int cr_cache_control_count, val_len, i;
    char *value, *ptr;

    chi = (CachedHeaderInfo *) TSmalloc(sizeof(CachedHeaderInfo));
    chi->date = 0;
    chi->max_age = 0;
    chi->stale_while_revalidate = 0;
    chi->stale_on_error = 0;

    TSDebug(LOG_PREFIX, "Inside get_cached_header_info");

    if (TSHttpTxnCachedRespGet(txn, &cr_buf, &cr_hdr_loc) == TS_SUCCESS)
    {
        cr_date_loc = TSMimeHdrFieldFind(cr_buf, cr_hdr_loc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE);
        if (cr_date_loc != TS_NULL_MLOC)
        {
            TSDebug(LOG_PREFIX, "Found a date");
            chi->date = TSMimeHdrFieldValueDateGet(cr_buf, cr_hdr_loc, cr_date_loc);
            TSHandleMLocRelease(cr_buf, cr_hdr_loc, cr_date_loc);
        }

        cr_cache_control_loc = TSMimeHdrFieldFind(cr_buf, cr_hdr_loc, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL);

        while(cr_cache_control_loc != TS_NULL_MLOC)
        {
            TSDebug(LOG_PREFIX, "Found cache-control");
            cr_cache_control_count = TSMimeHdrFieldValuesCount(cr_buf, cr_hdr_loc, cr_cache_control_loc);

            for (i = 0; i < cr_cache_control_count; i++)
            {
                value = (char *) TSMimeHdrFieldValueStringGet(cr_buf, cr_hdr_loc, cr_cache_control_loc, i, &val_len);
                ptr = value;

                if (strncmp(value, TS_HTTP_VALUE_MAX_AGE, TS_HTTP_LEN_MAX_AGE) == 0)
                {
                    TSDebug(LOG_PREFIX, "Found max-age");
                    ptr += TS_HTTP_LEN_MAX_AGE;
                    if (*ptr == '=')
                    {
                        ptr++;
                        chi->max_age = atol(ptr);
                    }
                    else
                    {
                        ptr = TSstrndup(value, TS_HTTP_LEN_MAX_AGE + 2);
                        TSDebug(LOG_PREFIX, "This is what I found: %s", ptr);
                        TSfree(ptr);
                    }
                }
                else if (strncmp(value, HTTP_VALUE_STALE_WHILE_REVALIDATE, strlen(HTTP_VALUE_STALE_WHILE_REVALIDATE)) == 0)
                {
                    TSDebug(LOG_PREFIX, "Found stale-while-revalidate");
                    ptr += strlen(HTTP_VALUE_STALE_WHILE_REVALIDATE);
                    if (*ptr == '=')
                    {
                        ptr++;
                        chi->stale_while_revalidate = atol(ptr);
                    }
                }
                else if (strncmp(value, HTTP_VALUE_STALE_IF_ERROR, strlen(HTTP_VALUE_STALE_IF_ERROR)) == 0)
                {
                    TSDebug(LOG_PREFIX, "Found stale-on-error");
                    ptr += strlen(HTTP_VALUE_STALE_IF_ERROR);
                    if (*ptr == '=')
                    {
                        ptr++;
                        chi->stale_on_error = atol(ptr);
                    }
                }
                else
                {
                    TSDebug(LOG_PREFIX, "Unknown field value");
                }
            }

            cr_cache_control_dup_loc = TSMimeHdrFieldNextDup(cr_buf, cr_hdr_loc, cr_cache_control_loc);
            TSHandleMLocRelease(cr_buf, cr_hdr_loc, cr_cache_control_loc);
            cr_cache_control_loc = cr_cache_control_dup_loc;
        }
        TSHandleMLocRelease(cr_buf, TS_NULL_MLOC, cr_hdr_loc);
    }

    TSDebug(LOG_PREFIX, "Leaving get_cached_header_info");
    return chi;
}

static int
xstrcmp(const void *a, const void *b)
{
    return strcmp((const char *) a, (const char *) b);
}

static void
parse_response(StateInfo *state)
{
    TSIOBufferBlock block;
    TSParseResult pr = TS_PARSE_CONT;
    int64_t avail;
    char *start;

    TSDebug(LOG_PREFIX, "Entering parse_response");

    block = TSIOBufferReaderStart(state->resp_io_buf_reader);

    while ((pr == TS_PARSE_CONT) && (block != NULL))
    {
        start = (char *) TSIOBufferBlockReadStart(block, state->resp_io_buf_reader, &avail);
        if (avail > 0)
        {
            pr = TSHttpHdrParseResp(state->resp_info->parser, state->resp_info->buf, state->resp_info->http_hdr_loc, (const char **) &start, (const char *) (start + avail));
        }
        block = TSIOBufferBlockNext(block);
    }

    if (pr != TS_PARSE_CONT)
    {
        state->resp_info->status = TSHttpHdrStatusGet(state->resp_info->buf, state->resp_info->http_hdr_loc);
        state->resp_info->parsed = true;
        TSDebug(LOG_PREFIX, "HTTP Status: %d", state->resp_info->status);
    }

    TSDebug(LOG_PREFIX, "Leaving parse_response");
}

static int
consume_resource(TSCont cont, TSEvent event, void *edata)
{
    StateInfo *state;
    int64_t avail;
    TSVConn vconn;
    TSMLoc url_loc;
    int lookup_count;

    TSDebug(LOG_PREFIX, "Entering consume_resource");

    vconn = (TSVConn) edata;
    state = (StateInfo *) TSContDataGet(cont);

    switch (event)
    {
        case TS_EVENT_VCONN_WRITE_READY:
            // We shouldn't get here because we specify the exact size of the buffer.
            TSDebug(LOG_PREFIX, "Write Ready");
        case TS_EVENT_VCONN_WRITE_COMPLETE:
            TSDebug(LOG_PREFIX, "Write Complete");
            //TSDebug(LOG_PREFIX, "TSVConnShutdown()");
            //TSVConnShutdown(state->vconn, 0, 1);
            //TSVIOReenable(state->w_vio);
            break;
        case TS_EVENT_VCONN_READ_READY:
            TSDebug(LOG_PREFIX, "Read Ready");

            avail = TSIOBufferReaderAvail(state->resp_io_buf_reader);

            if ((state->resp_info) && !state->resp_info->parsed)
            {
                parse_response(state);
            }

            // Consume data
            avail = TSIOBufferReaderAvail(state->resp_io_buf_reader);
            TSIOBufferReaderConsume(state->resp_io_buf_reader, avail);
            TSVIONDoneSet(state->r_vio, TSVIONDoneGet(state->r_vio) + avail);
            TSVIOReenable(state->r_vio);
            break;
        case TS_EVENT_VCONN_READ_COMPLETE:
        case TS_EVENT_VCONN_EOS:
        case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
            if (event == TS_EVENT_VCONN_INACTIVITY_TIMEOUT)
            {
                TSDebug(LOG_PREFIX, "Inactivity Timeout");
                TSDebug(LOG_PREFIX, "TSVConnAbort()");
                TSVConnAbort(vconn, TS_VC_CLOSE_ABORT);
            }
            else
            {
                if (event == TS_EVENT_VCONN_READ_COMPLETE)
                {
                    TSDebug(LOG_PREFIX, "Read Complete");
                }
                else if (event == TS_EVENT_VCONN_EOS)
                {
                    TSDebug(LOG_PREFIX, "EOS");
                }
                TSDebug(LOG_PREFIX, "TSVConnClose()");
                TSVConnClose(state->vconn);
            }

            avail = TSIOBufferReaderAvail(state->resp_io_buf_reader);

            if ((state->resp_info) && !state->resp_info->parsed)
            {
                parse_response(state);
            }

            // Consume data
            avail = TSIOBufferReaderAvail(state->resp_io_buf_reader);
            TSIOBufferReaderConsume(state->resp_io_buf_reader, avail);
            TSVIONDoneSet(state->r_vio, TSVIONDoneGet(state->r_vio) + avail);
            if (state->async_req)
            {
                TSDebug(LOG_PREFIX, "Unlock URL");
                TSMutexLock(troot_mutex);
                tdelete(state->req_info->effective_url, &troot, xstrcmp);
                TSMutexUnlock(troot_mutex);
            }
            else
            {
                TSDebug(LOG_PREFIX, "In sync path. setting fresh and re-enabling");
                TSHttpTxnCacheLookupCountGet(state->txn, &lookup_count);
                if ((state->resp_info->status == 500) || ((state->resp_info->status >= 502) && (state->resp_info->status <= 504)) || lookup_count > 2)
                {
                    TSDebug(LOG_PREFIX, "Sending stale data as fresh");
                    if (log_info.object && (log_info.all || log_info.stale_if_error))
                    {
                        CachedHeaderInfo *chi = get_cached_header_info(state->txn);
                        TSTextLogObjectWrite(log_info.object, "stale-if-error: %d - %d < %d + %d %s", (int) state->txn_start, (int) chi->date, (int) chi->max_age, (int) chi->stale_on_error, state->req_info->effective_url);
                        TSfree(chi);
                    }
                    TSHttpTxnHookAdd(state->txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, state->main_cont);
                    TSHttpTxnCacheLookupStatusSet(state->txn, TS_CACHE_LOOKUP_HIT_FRESH);
                }
                else
                {
                    TSDebug(LOG_PREFIX, "Attempting new cache lookup");
                    TSHttpHdrUrlGet(state->req_info->buf, state->req_info->http_hdr_loc, &url_loc);
                    TSHttpTxnNewCacheLookupDo(state->txn, state->req_info->buf, url_loc);
                    TSHandleMLocRelease(state->req_info->buf, state->req_info->http_hdr_loc, url_loc);
                    // TODO add txn translation hook and pass result along, maybe inside continuation?
                    //TSHttpTxnHookAdd(state->txn, TS_HTTP_RESPONSE_TRANSFORM_HOOK, TSTransformCreate(replace_transform, state->txn));
                }
                TSHttpTxnReenable(state->txn, TS_EVENT_HTTP_CONTINUE);
            }
            free_request_info(state->req_info);
            if (state->resp_info)
            {
                free_response_info(state->resp_info);
            }
            TSIOBufferReaderFree(state->req_io_buf_reader);
            TSIOBufferDestroy(state->req_io_buf);
            TSIOBufferReaderFree(state->resp_io_buf_reader);
            TSIOBufferDestroy(state->resp_io_buf);
            TSfree(state);
            TSDebug(LOG_PREFIX, "Destroying Cont");
            TSContDestroy(cont);
            break;
        default:
            TSError("Unknown event %d.", event);
            break;
    }

    TSDebug(LOG_PREFIX, "Leaving consume_resource");
    return 0;
}

static int
fetch_resource(TSCont cont, TSEvent event, void *edata)
{
    StateInfo *state;
    TSCont consume_cont;
    //struct sockaddr_in client_addr;
    TSMLoc connection_hdr_loc, connection_hdr_dup_loc;

    TSDebug(LOG_PREFIX, "Entering fetch_resource");

    state = (StateInfo *) TSContDataGet(cont);

    TSDebug(LOG_PREFIX, "state: %p", state);

    //li = (RequestInfo *) edata;
    TSMutexLock(troot_mutex);
    // If already doing async lookup lets just close shop and go home
    if (state->async_req && (tfind(state->req_info->effective_url, &troot, xstrcmp) != NULL))
    {
        TSDebug(LOG_PREFIX, "Looks like an async is already in progress");
        TSMutexUnlock(troot_mutex);
        free_request_info(state->req_info);
        TSfree(state);
    }
    // Otherwise lets do the lookup!
    else
    {
        TSDebug(LOG_PREFIX, "Lets do the lookup");
        if (state->async_req)
        {
            // Lock in tree
            TSDebug(LOG_PREFIX, "Locking URL");
            tsearch(state->req_info->effective_url, &troot, xstrcmp);
        }
        TSMutexUnlock(troot_mutex);
        consume_cont = TSContCreate(consume_resource, NULL);
        TSContDataSet(consume_cont, (void *) state);

        if (state->async_req)
        {
            state->resp_info = NULL;
        }
        else
        {
            state->resp_info = create_response_info();
        }

        TSDebug(LOG_PREFIX, "Set Connection: close");
        connection_hdr_loc = TSMimeHdrFieldFind(state->req_info->buf, state->req_info->http_hdr_loc, TS_MIME_FIELD_CONNECTION, TS_MIME_LEN_CONNECTION);

        while(connection_hdr_loc != TS_NULL_MLOC)
        {
            TSDebug(LOG_PREFIX, "Found old Connection hdr");

            connection_hdr_dup_loc = TSMimeHdrFieldNextDup(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc);
            TSMimeHdrFieldRemove(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc);
            TSMimeHdrFieldDestroy(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc);
            TSHandleMLocRelease(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc);
            connection_hdr_loc = connection_hdr_dup_loc;
        }

        // This seems to have little effect
        TSDebug(LOG_PREFIX, "Creating Connection hdr");
        TSMimeHdrFieldCreateNamed(state->req_info->buf, state->req_info->http_hdr_loc, TS_MIME_FIELD_CONNECTION, TS_MIME_LEN_CONNECTION, &connection_hdr_loc);
        TSMimeHdrFieldValueStringInsert(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc, -1, TS_HTTP_VALUE_CLOSE, TS_HTTP_LEN_CLOSE);
        TSMimeHdrFieldAppend(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc);
        TSHandleMLocRelease(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc);

        /*
        TSDebug(LOG_PREFIX, "Creating @RFC5861 header");
        TSMimeHdrFieldCreateNamed(state->req_info->buf, state->req_info->http_hdr_loc, TS_MIME_FIELD_CONNECTION, TS_MIME_LEN_CONNECTION, &connection_hdr_loc);
        TSMimeHdrFieldValueStringInsert(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc, -1, TS_HTTP_VALUE_CLOSE, TS_HTTP_LEN_CLOSE);
        TSMimeHdrFieldAppend(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc);
        TSHandleMLocRelease(state->req_info->buf, state->req_info->http_hdr_loc, connection_hdr_loc);
        */

        TSDebug(LOG_PREFIX, "Create Buffers");
        state->req_io_buf = TSIOBufferCreate();
        state->req_io_buf_reader = TSIOBufferReaderAlloc(state->req_io_buf);
        state->resp_io_buf = TSIOBufferCreate();
        state->resp_io_buf_reader = TSIOBufferReaderAlloc(state->resp_io_buf);

        TSDebug(LOG_PREFIX, "HdrPrint()");
        TSHttpHdrPrint(state->req_info->buf, state->req_info->http_hdr_loc, state->req_io_buf);
        TSIOBufferWrite(state->req_io_buf, "\r\n", 2);

        TSDebug(LOG_PREFIX, "TSHttpConnect()");
        //memmove((void *) &client_addr, (void *) state->req_info->client_addr, sizeof(struct sockaddr));
        //TSDebug(LOG_PREFIX, "client_addr: %s:%d", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
        state->vconn = TSHttpConnect((struct sockaddr const *) state->req_info->client_addr);

        TSDebug(LOG_PREFIX, "TSVConnRead()");
        state->r_vio = TSVConnRead(state->vconn, consume_cont, state->resp_io_buf, INT64_MAX);
        TSDebug(LOG_PREFIX, "TSVConnWrite()");
        state->w_vio = TSVConnWrite(state->vconn, consume_cont, state->req_io_buf_reader, TSIOBufferReaderAvail(state->req_io_buf_reader));
    }

    TSContDestroy(cont);
    TSDebug(LOG_PREFIX, "Leaving fetch_resource");

    return 0;
}

static int
rfc5861_plugin(TSCont cont, TSEvent event, void *edata)
{
    TSHttpTxn txn = (TSHttpTxn) edata;
    int status, lookup_count;
    CachedHeaderInfo *chi;
    TSCont fetch_cont;
    StateInfo *state;
    TSMBuffer buf;
    TSMLoc loc,warn_loc;
    TSHttpStatus http_status;

    TSDebug(LOG_PREFIX, "Entering rfc5861_plugin");
    switch (event)
    {
        // Is this the proper event?
        case TS_EVENT_HTTP_READ_REQUEST_HDR:
            TSDebug(LOG_PREFIX, "Event: TS_EVENT_HTTP_READ_REQUEST_HDR");

            if (TSHttpIsInternalRequest(txn) != TS_SUCCESS)
            {
                TSDebug(LOG_PREFIX, "External Request");
                state = TSmalloc(sizeof(StateInfo));
                time(&state->txn_start);
                state->req_info = create_request_info(txn);
                TSDebug(LOG_PREFIX, "state after TSmalloc: %p", state);
                TSHttpTxnArgSet(txn, txn_slot, (void *) state);
                TSHttpTxnHookAdd(txn, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
            }
            else
            {
                TSDebug(LOG_PREFIX, "Internal Request"); // This is insufficient if there are other plugins using TSHttpConnect
                //TSHttpTxnHookAdd(txn, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
                TSHttpTxnHookAdd(txn, TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
                // This might be needed in 3.2.0 to fix a timeout issue
                //TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_IN, 5);
                //TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_TRANSACTION_NO_ACTIVITY_TIMEOUT_OUT, 5);
            }

            TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
            TSDebug(LOG_PREFIX, "TS_EVENT_HTTP_READ_REQUEST_HDR Event Handler End");
            break;
        case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
            TSDebug(LOG_PREFIX, "Event: TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE");
            state = (StateInfo *) TSHttpTxnArgGet(txn, txn_slot);
            TSHttpTxnCacheLookupCountGet(txn, &lookup_count);
            TSDebug(LOG_PREFIX, "state after arg get: %p", state);
            if (TSHttpTxnCacheLookupStatusGet(txn, &status) == TS_SUCCESS)
            {
                // Are we stale?
                if (status == TS_CACHE_LOOKUP_HIT_STALE)
                {
                    TSDebug(LOG_PREFIX, "CacheLookupStatus is STALE");
                    // Get headers
                    chi = get_cached_header_info(txn);

                    if ((state->txn_start - chi->date) < (chi->max_age + chi->stale_while_revalidate))
                    {
                        TSDebug(LOG_PREFIX, "Looks like we can return fresh info and validate in the background");
                        if (log_info.object && (log_info.all || log_info.stale_while_revalidate))
                            TSTextLogObjectWrite(log_info.object, "stale-while-revalidate: %d - %d < %d + %d %s", (int) state->txn_start, (int) chi->date, (int) chi->max_age, (int) chi->stale_while_revalidate, state->req_info->effective_url);
                        // lookup async

#if (TS_VERSION_NUMBER >= 3003000)
                        TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_INSERT_AGE_IN_RESPONSE, 1);
#endif
                        // Set warning header
                        TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);

                        TSDebug(LOG_PREFIX, "set state as async");
                        state->async_req = true;
                        TSDebug(LOG_PREFIX, "TSHttpTxnCacheLookupStatusSet()");
                        TSHttpTxnCacheLookupStatusSet(txn, TS_CACHE_LOOKUP_HIT_FRESH);
                        //TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
                        TSDebug(LOG_PREFIX, "TSContCreate()");
                        fetch_cont = TSContCreate(fetch_resource, NULL);
                        TSDebug(LOG_PREFIX, "TSContDataSet()");
                        TSContDataSet(fetch_cont, (void *) state);
                        TSDebug(LOG_PREFIX, "state: %p", state);
                        TSContSchedule(fetch_cont, 0, TS_THREAD_POOL_TASK);
                        TSDebug(LOG_PREFIX, "TSHttpTxnReenable()");
                        TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
                    }
                    else if ((state->txn_start - chi->date) < (chi->max_age + chi->stale_on_error))
                    {
                        TSDebug(LOG_PREFIX, "Looks like we can return fresh data on 500 error");
#if (TS_VERSION_NUMBER >= 3003000)
                        TSHttpTxnConfigIntSet(txn, TS_CONFIG_HTTP_INSERT_AGE_IN_RESPONSE, 1);
#endif
                        //lookup sync
                        state->async_req = false;
                        state->txn = txn;
                        state->main_cont = cont; // we need this for the warning header callback. not sure i like it, but it works.
                        fetch_cont = TSContCreate(fetch_resource, NULL);
                        TSContDataSet(fetch_cont, (void *) state);
                        TSContSchedule(fetch_cont, 0, TS_THREAD_POOL_TASK);
                    }
                    else
                    {
                        TSDebug(LOG_PREFIX, "No love? now: %d date: %d max-age: %d swr: %d soe: %d", (int) state->txn_start, (int) chi->date, (int) chi->max_age, (int) chi->stale_while_revalidate, (int) chi->stale_on_error);
                        if (lookup_count == 1)
                        {
                            free_request_info(state->req_info);
                            TSfree(state);
                        }
                        TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
                    }

                    TSfree(chi);
                }
                else
                {
                    TSDebug(LOG_PREFIX, "Not Stale!");
                    if (lookup_count == 1)
                    {
                        free_request_info(state->req_info);
                        TSfree(state);
                    }
                    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
                }
            }
            else
            {
                TSDebug(LOG_PREFIX, "Could not get CacheLookupStatus");
                if (lookup_count == 1)
                {
                    free_request_info(state->req_info);
                    TSfree(state);
                }
                TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
            }
            TSDebug(LOG_PREFIX, "TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE Event Handler End");
            break;
        case TS_EVENT_HTTP_READ_RESPONSE_HDR:
            TSDebug(LOG_PREFIX, "Event: TS_EVENT_HTTP_READ_RESPONSE_HDR");
            TSHttpTxnServerRespGet(txn, &buf, &loc);
            http_status = TSHttpHdrStatusGet(buf, loc);
            if ((http_status == 500) || ((http_status >= 502) && (http_status <= 504))) // 500, 502, 503, or 504
            {
                TSDebug(LOG_PREFIX, "Set non-cachable");
#if (TS_VERSION_NUMBER >= 3003000)
                TSHttpTxnServerRespNoStoreSet(txn,1);
#else
                TSHttpTxnServerRespNoStore(txn);
#endif
            }
            TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);
            TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
            TSDebug(LOG_PREFIX, "TS_EVENT_HTTP_READ_RESPONSE_HDR Event Handler End");
            break;
        case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
            TSDebug(LOG_PREFIX, "Event: TS_EVENT_HTTP_SEND_RESPONSE_HDR");
            TSDebug(LOG_PREFIX, "set warning header");
            TSHttpTxnClientRespGet(txn, &buf, &loc);
            TSMimeHdrFieldCreateNamed(buf, loc, TS_MIME_FIELD_WARNING, TS_MIME_LEN_WARNING, &warn_loc);
            TSMimeHdrFieldValueStringInsert(buf, loc, warn_loc, -1, HTTP_VALUE_STALE_WARNING, strlen(HTTP_VALUE_STALE_WARNING));
            TSMimeHdrFieldAppend(buf, loc, warn_loc);
            TSHandleMLocRelease(buf, loc, warn_loc);
            TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);
            TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
            TSDebug(LOG_PREFIX, "TS_EVENT_HTTP_SEND_RESPONSE_HDR Event Handler End");
            break;
        default:
            TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
            break;
    }

    TSDebug(LOG_PREFIX, "Leaving rfc5861_plugin");

    return 0;
}

static bool
check_ts_version()
{
    const char *ts_version = TSTrafficServerVersionGet();

    if (ts_version)
    {
        int major_ts_version = 0;
        int minor_ts_version = 0;
        int micro_ts_version = 0;

        if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &micro_ts_version) != 3)
        {
            return false;
        }

        if ((TS_VERSION_MAJOR == major_ts_version) && (TS_VERSION_MINOR == minor_ts_version) && (TS_VERSION_MICRO == micro_ts_version))
        {
            return true;
        }
    }

    return false;
}

void
TSPluginInit (int argc, const char *argv[])
{
    TSPluginRegistrationInfo info;
    TSCont main_cont;

    info.plugin_name = "rfc5861";
    info.vendor_name = "OmniTI Computer Consulting on behalf of Oregon Health & Science University";
    info.support_email = "phil@omniti.com";

    if (TSPluginRegister(TS_SDK_VERSION_3_0 , &info) != TS_SUCCESS)
    {
        TSError("Plugin registration failed.\n");
        return;
    }
    else
    {
        TSDebug(LOG_PREFIX, "Plugin registration succeeded.\n");
    }

    if (!check_ts_version())
    {
        TSError("Plugin requires Traffic Server %d.%d.%d\n", TS_VERSION_MAJOR, TS_VERSION_MINOR, TS_VERSION_MICRO);
        return;
    }

    if (argc > 1)
    {
        int c;
        static const struct option longopts[] = {
                { "log-all", no_argument, NULL, 'a' },
                { "log-stale-while-revalidate", no_argument, NULL, 'r' },
                { "log-stale-if-error", no_argument, NULL, 'e' },
                { "log-filename", required_argument, NULL, 'f' },
                { NULL, 0, NULL, 0 }
            };

        while ((c = getopt_long(argc, (char * const*) argv, "aref:", longopts, NULL)) != -1)
        {
            switch (c)
            {
                case 'a':
                    log_info.all = true;
                    break;
                case 'r':
                    log_info.stale_while_revalidate = true;
                    break;
                case 'e':
                    log_info.stale_if_error = true;
                    break;
                case 'f':
                    log_info.filename = strdup(optarg);
                    break;
                default:
                    break;
            }
        }

        if (log_info.all || log_info.stale_while_revalidate || log_info.stale_if_error)
            TSTextLogObjectCreate(log_info.filename, TS_LOG_MODE_ADD_TIMESTAMP, &log_info.object);
    }

    // proxy.config.http.insert_age_in_response
    TSHttpArgIndexReserve("rfc5861_state", "txn state info for rfc5861", &txn_slot);
    troot_mutex = TSMutexCreate();
    main_cont = TSContCreate(rfc5861_plugin, NULL);
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, main_cont);

    TSDebug(LOG_PREFIX, "Plugin Init Complete.\n");
}
