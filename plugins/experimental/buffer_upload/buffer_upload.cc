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

/*   buffer_upload.c - plugin for buffering POST data on proxy server
 *   before connecting to origin server. It supports two types of buffering:
 *   memory-only buffering and disk buffering
 *
 */

#include <cstdio>
#include <cstring>
#include <cctype>
#include <climits>
#include <ts/ts.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include <dirent.h>
#include <unistd.h>
#include <cstdlib>
#include <cinttypes>

/* #define DEBUG 1 */
#define DEBUG_TAG "buffer_upload-dbg"

/**************************************************
   Log macros for error code return verification
**************************************************/
#define PLUGIN_NAME "buffer_upload"
//#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME)                                                                                               \
  {                                                                                                                       \
    TSError("[%s] %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", __FUNCTION__, __FILE__, __LINE__); \
  }
#define LOG_ERROR_AND_RETURN(API_NAME) \
  {                                    \
    LOG_ERROR(API_NAME);               \
    return TS_ERROR;                   \
  }

#define VALID_PTR(X) (NULL != X)
#define NOT_VALID_PTR(X) (NULL == X)

struct upload_config_t {
  bool use_disk_buffer;
  bool convert_url;
  int64_t mem_buffer_size;
  int64_t chunk_size;
  char *url_list_file;
  int64_t max_url_length;
  int url_num;
  char **urls;
  char *base_dir;
  int subdir_num;
  int thread_num;
};

using upload_config = struct upload_config_t;

enum config_type {
  TYPE_INT,
  TYPE_UINT,
  TYPE_LONG,
  TYPE_ULONG,
  TYPE_STRING,
  TYPE_BOOL,
};

struct config_val_ul {
  const char *str;
  enum config_type type;
  void *val;
};

static int upload_vc_count;

static upload_config *uconfig = nullptr;

struct pvc_state_t {
  TSVConn p_vc;
  TSVIO p_read_vio;
  TSVIO p_write_vio;

  TSVConn net_vc;
  TSVIO n_read_vio;
  TSVIO n_write_vio;

  TSIOBuffer req_buffer;
  TSIOBufferReader req_reader;

  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  TSIOBufferReader req_hdr_reader;
  TSIOBuffer req_hdr_buffer;

  TSMutex disk_io_mutex;

  int fd;

  int64_t req_finished;
  int64_t resp_finished;
  int64_t nbytes_to_consume;
  int64_t req_size;
  int64_t size_written;
  int64_t size_read;

  int64_t write_offset;
  int64_t read_offset;

  char *chunk_buffer; // buffer to store the data read from disk
  int is_reading_from_disk;

  TSHttpTxn http_txnp;
};

using pvc_state = struct pvc_state_t;

// print IOBuffer for test purpose
/*
static void
print_buffer(TSIOBufferReader reader)
{
  TSIOBufferBlock block;
  int64_t size;
  const char *ptr;

  block = TSIOBufferReaderStart(reader);
  while (block != NULL) {
    ptr = TSIOBufferBlockReadStart(block, reader, &size);
    TSDebug(DEBUG_TAG, "buffer size: %d", size);
    TSDebug(DEBUG_TAG, "buffer: %.*s", size, ptr);
    block = TSIOBufferBlockNext(block);
  }
}
*/

static int
write_buffer_to_disk(TSIOBufferReader reader, pvc_state *my_state, TSCont contp)
{
  TSIOBufferBlock block;
  int64_t size;
  const char *ptr;
  char *pBuf;

  // LOG_SET_FUNCTION_NAME("write_buffer_to_disk");
  block = TSIOBufferReaderStart(reader);
  while (block != nullptr) {
    ptr  = TSIOBufferBlockReadStart(block, reader, &size);
    pBuf = (char *)TSmalloc(sizeof(char) * size);
    if (pBuf == nullptr) {
      LOG_ERROR_AND_RETURN("TSAIOWrite");
    }
    memcpy(pBuf, ptr, size);
    if (TSAIOWrite(my_state->fd, my_state->write_offset, pBuf, size, contp) == TS_ERROR) {
      LOG_ERROR_AND_RETURN("TSAIOWrite");
    }
    my_state->write_offset += size;
    block = TSIOBufferBlockNext(block);
  }
  return TS_SUCCESS;
}

static int
call_httpconnect(TSCont contp, pvc_state *my_state)
{
  // LOG_SET_FUNCTION_NAME("call_httpconnect");

  // unsigned int client_ip = TSHttpTxnClientIPGet(my_state->http_txnp);
  sockaddr const *client_ip = TSHttpTxnClientAddrGet(my_state->http_txnp);

  TSDebug(DEBUG_TAG, "call TSHttpConnect()");
  if ((my_state->net_vc = TSHttpConnect(client_ip)) == nullptr) {
    LOG_ERROR_AND_RETURN("TSHttpConnect");
  }
  my_state->p_write_vio = TSVConnWrite(my_state->p_vc, contp, my_state->resp_reader, INT_MAX);
  if (my_state->p_write_vio == nullptr) {
    LOG_ERROR_AND_RETURN("TSVConnWrite");
  }
  my_state->n_read_vio = TSVConnRead(my_state->net_vc, contp, my_state->resp_buffer, INT_MAX);
  if (my_state->n_read_vio == nullptr) {
    LOG_ERROR_AND_RETURN("TSVConnRead");
  }
  my_state->n_write_vio = TSVConnWrite(my_state->net_vc, contp, my_state->req_reader, INT_MAX);
  if (my_state->n_write_vio == nullptr) {
    LOG_ERROR_AND_RETURN("TSVConnWrite");
  }
  return TS_SUCCESS;
}

static void
pvc_cleanup(TSCont contp, pvc_state *my_state)
{
  if (my_state->req_buffer) {
    TSIOBufferReaderFree(my_state->req_reader);
    my_state->req_reader = nullptr;
    TSIOBufferDestroy(my_state->req_buffer);
    my_state->req_buffer = nullptr;
  }

  if (my_state->resp_buffer) {
    TSIOBufferReaderFree(my_state->resp_reader);
    my_state->resp_reader = nullptr;
    TSIOBufferDestroy(my_state->resp_buffer);
    my_state->resp_buffer = nullptr;
  }

  if (my_state->req_hdr_buffer) {
    TSIOBufferReaderFree(my_state->req_hdr_reader);
    my_state->req_hdr_reader = nullptr;
    TSIOBufferDestroy(my_state->req_hdr_buffer);
    my_state->req_hdr_buffer = nullptr;
  }

  if (uconfig->use_disk_buffer && my_state->fd != -1) {
    close(my_state->fd);
    my_state->fd = -1;
  }

  if (my_state->chunk_buffer) {
    TSfree(my_state->chunk_buffer);
    my_state->chunk_buffer = nullptr;
  }

  TSfree(my_state);
  TSContDestroy(contp);

  /* Decrement upload_vc_count */
  TSStatIntDecrement(upload_vc_count, 1);
}

static void
pvc_check_done(TSCont contp, pvc_state *my_state)
{
  if (my_state->req_finished && my_state->resp_finished) {
    TSVConnClose(my_state->p_vc);
    TSVConnClose(my_state->net_vc);
    pvc_cleanup(contp, my_state);
  }
}

static void
pvc_process_accept(TSCont contp, int event, void *edata, pvc_state *my_state)
{
  TSDebug(DEBUG_TAG, "plugin called: pvc_process_accept with event %d", event);

  if (event == TS_EVENT_NET_ACCEPT) {
    my_state->p_vc = (TSVConn)edata;

    my_state->req_buffer = TSIOBufferCreate();
    my_state->req_reader = TSIOBufferReaderAlloc(my_state->req_buffer);
    // set the maximum memory buffer size for request (both request header and post data), default is 32K
    // only apply to memory buffer mode
    if (uconfig->use_disk_buffer == 0) {
      TSIOBufferWaterMarkSet(my_state->req_buffer, uconfig->mem_buffer_size);
    }
    my_state->resp_buffer = TSIOBufferCreate();
    my_state->resp_reader = TSIOBufferReaderAlloc(my_state->resp_buffer);

    if ((my_state->req_reader == nullptr) || (my_state->resp_reader == nullptr)) {
      LOG_ERROR("TSIOBufferReaderAlloc");
      TSVConnClose(my_state->p_vc);
      pvc_cleanup(contp, my_state);
    } else {
      my_state->p_read_vio = TSVConnRead(my_state->p_vc, contp, my_state->req_buffer, INT_MAX);
      if (my_state->p_read_vio == nullptr) {
        LOG_ERROR("TSVConnRead");
      }
    }
  } else if (event == TS_EVENT_NET_ACCEPT_FAILED) {
    pvc_cleanup(contp, my_state);
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

static void
pvc_process_p_read(TSCont contp, TSEvent event, pvc_state *my_state)
{
  int size, consume_size;

  // TSDebug(DEBUG_TAG, "plugin called: pvc_process_p_read with event %d", event);

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
    // Here we need to replace the server request header with client request header
    // print_buffer(my_state->req_reader);
    if (my_state->nbytes_to_consume == -1) { // -1 is the initial value
      my_state->nbytes_to_consume = TSHttpTxnServerReqHdrBytesGet(my_state->http_txnp);
    }
    size = TSIOBufferReaderAvail(my_state->req_reader);
    if (my_state->nbytes_to_consume > 0) {
      consume_size = (my_state->nbytes_to_consume < size) ? my_state->nbytes_to_consume : size;
      TSIOBufferReaderConsume(my_state->req_reader, consume_size);
      my_state->nbytes_to_consume -= consume_size;
      size -= consume_size;
    }
    if (my_state->nbytes_to_consume == 0) { // the entire server request header has been consumed
      if (uconfig->use_disk_buffer) {
        TSMutexLock(my_state->disk_io_mutex);
        if (write_buffer_to_disk(my_state->req_hdr_reader, my_state, contp) == TS_ERROR) {
          LOG_ERROR("write_buffer_to_disk");
          uconfig->use_disk_buffer = false;
          close(my_state->fd);
          my_state->fd = -1;
        }
        TSMutexUnlock(my_state->disk_io_mutex);
      }
      if (size > 0) {
        if (uconfig->use_disk_buffer) {
          TSMutexLock(my_state->disk_io_mutex);
          if (write_buffer_to_disk(my_state->req_reader, my_state, contp) == TS_ERROR) {
            TSDebug(DEBUG_TAG, "Error in writing to disk");
          }
          TSMutexUnlock(my_state->disk_io_mutex);
        } else {
          // never get chance to test this line, didn't get a test case to fall into this situation
          TSIOBufferCopy(my_state->req_hdr_buffer, my_state->req_reader, size, 0);
        }
        TSIOBufferReaderConsume(my_state->req_reader, size);
      }
      if (!uconfig->use_disk_buffer) {
        size = TSIOBufferReaderAvail(my_state->req_hdr_reader);
        TSIOBufferCopy(my_state->req_buffer, my_state->req_hdr_reader, size, 0);
      }
      my_state->nbytes_to_consume = -2; // -2 indicates the header replacement is done
    }
    if (my_state->nbytes_to_consume == -2) {
      size = TSIOBufferReaderAvail(my_state->req_reader);
      if (uconfig->use_disk_buffer) {
        if (size > 0) {
          TSMutexLock(my_state->disk_io_mutex);
          if (write_buffer_to_disk(my_state->req_reader, my_state, contp) == TS_ERROR) {
            TSDebug(DEBUG_TAG, "Error in writing to disk");
          }
          TSIOBufferReaderConsume(my_state->req_reader, size);
          TSMutexUnlock(my_state->disk_io_mutex);
        }
      } else {
        // if the entire post data had been read in memory, then connect to origin server.
        if (size >= my_state->req_size) {
          if (call_httpconnect(contp, my_state) == TS_ERROR) {
            LOG_ERROR("call_httpconnect");
          }
        }
      }
    }

    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_ERROR: {
    /* We're finished reading from the plugin vc */
    int ndone;

    ndone = TSVIONDoneGet(my_state->p_read_vio);
    if (ndone == TS_ERROR) {
      LOG_ERROR("TSVIODoneGet");
    }

    my_state->p_read_vio = nullptr;

    TSVConnShutdown(my_state->p_vc, 1, 0);
    // if client aborted the uploading in middle, need to cleanup the file from disk
    if (event == TS_EVENT_VCONN_EOS && uconfig->use_disk_buffer && my_state->fd != -1) {
      close(my_state->fd);
      my_state->fd = -1;
    }

    break;
  }
  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_n_write(TSCont contp, TSEvent event, pvc_state *my_state)
{
  int size;

  // TSDebug(DEBUG_TAG, "plugin called: pvc_process_n_write with event %d", event);

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    // print_buffer(my_state->req_reader);
    if (uconfig->use_disk_buffer) {
      TSMutexLock(my_state->disk_io_mutex);
      size = (my_state->req_size - my_state->read_offset) > uconfig->chunk_size ? uconfig->chunk_size :
                                                                                  (my_state->req_size - my_state->read_offset);
      if (size > 0 && !my_state->is_reading_from_disk) {
        my_state->is_reading_from_disk = 1;
        TSAIORead(my_state->fd, my_state->read_offset, my_state->chunk_buffer, size, contp);
        my_state->read_offset += size;
      }
      TSMutexUnlock(my_state->disk_io_mutex);
    }
    break;
  case TS_EVENT_ERROR:
    if (my_state->p_read_vio) {
      TSVConnShutdown(my_state->p_vc, 1, 0);
      my_state->p_read_vio = nullptr;
    }
  /* FALL THROUGH */
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read pvc side */
    TSAssert(my_state->p_read_vio == nullptr);
    TSVConnShutdown(my_state->net_vc, 0, 1);
    my_state->req_finished = 1;

    if (uconfig->use_disk_buffer && my_state->fd != -1) {
      close(my_state->fd);
      my_state->fd = -1;
    }
    pvc_check_done(contp, my_state);
    break;

  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_n_read(TSCont contp, TSEvent event, pvc_state *my_state)
{
  // TSDebug(DEBUG_TAG, "plugin called: pvc_process_n_read with event %d", event);

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
    // print_buffer(my_state->resp_reader);
    TSVIOReenable(my_state->p_write_vio);
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_ERROR: {
    /* We're finished reading from the plugin vc */
    int ndone;
    int todo;

    ndone = TSVIONDoneGet(my_state->n_read_vio);
    if (ndone == TS_ERROR) {
      LOG_ERROR("TSVIODoneGet");
    }

    my_state->n_read_vio = nullptr;
    TSVIONBytesSet(my_state->p_write_vio, ndone);
    TSVConnShutdown(my_state->net_vc, 1, 0);

    todo = TSVIONTodoGet(my_state->p_write_vio);
    if (todo == TS_ERROR) {
      LOG_ERROR("TSVIOTodoGet");
      /* Error so set it to 0 to cleanup */
      todo = 0;
    }

    if (todo == 0) {
      my_state->resp_finished = 1;
      TSVConnShutdown(my_state->p_vc, 0, 1);
      pvc_check_done(contp, my_state);
    } else {
      TSVIOReenable(my_state->p_write_vio);
    }

    break;
  }
  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_p_write(TSCont contp, TSEvent event, pvc_state *my_state)
{
  // TSDebug(DEBUG_TAG, "plugin called: pvc_process_p_write with event %d", event);

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    if (my_state->n_read_vio) {
      TSVIOReenable(my_state->n_read_vio);
    }
    break;
  case TS_EVENT_ERROR:
    if (my_state->n_read_vio) {
      TSVConnShutdown(my_state->net_vc, 1, 0);
      my_state->n_read_vio = nullptr;
    }
  /* FALL THROUGH */
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read net side */
    TSAssert(my_state->n_read_vio == nullptr);
    TSVConnShutdown(my_state->p_vc, 0, 1);
    my_state->resp_finished = 1;
    pvc_check_done(contp, my_state);
    break;
  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }
}

static int
pvc_plugin(TSCont contp, TSEvent event, void *edata)
{
  pvc_state *my_state    = static_cast<pvc_state *>(TSContDataGet(contp));
  TSAIOCallback callback = static_cast<TSAIOCallback>(edata);

  if (my_state == nullptr) {
    TSReleaseAssert(!"Unexpected: my_state is NULL");
    return 0;
  }

  if (event == TS_EVENT_NET_ACCEPT || event == TS_EVENT_NET_ACCEPT_FAILED) {
    pvc_process_accept(contp, event, edata, my_state);
  } else if (edata == my_state->p_read_vio) {
    pvc_process_p_read(contp, event, my_state);
  } else if (edata == my_state->p_write_vio) {
    pvc_process_p_write(contp, event, my_state);
  } else if (edata == my_state->n_read_vio) {
    pvc_process_n_read(contp, event, my_state);
  } else if (edata == my_state->n_write_vio) {
    pvc_process_n_write(contp, event, my_state);
  } else if (event == TS_AIO_EVENT_DONE && uconfig->use_disk_buffer) {
    TSMutexLock(my_state->disk_io_mutex);
    int size  = TSAIONBytesGet(callback);
    char *buf = TSAIOBufGet(callback);
    if (buf != my_state->chunk_buffer) {
      // this TS_AIO_EVENT_DONE event is from TSAIOWrite()
      TSDebug(DEBUG_TAG, "aio write size: %d", size);
      my_state->size_written += size;
      if (buf != nullptr) {
        TSfree(buf);
      }
      if (my_state->size_written >= my_state->req_size) {
        // the entire post data had been written to disk  already, make the connection now
        if (call_httpconnect(contp, my_state) == TS_ERROR) {
          TSDebug(DEBUG_TAG, "call_httpconnect");
        }
      }
    } else {
      // this TS_AIO_EVENT_DONE event is from TSAIORead()
      TSDebug(DEBUG_TAG, "aio read size: %d", size);
      TSIOBufferWrite(my_state->req_buffer, my_state->chunk_buffer, size);
      my_state->size_read += size;
      if (my_state->size_read >= my_state->req_size && my_state->fd != -1) {
        close(my_state->fd);
        my_state->fd = -1;
      }
      my_state->is_reading_from_disk = 0;
      TSVIOReenable(my_state->n_write_vio);
    }
    TSMutexUnlock(my_state->disk_io_mutex);

  } else {
    TSDebug(DEBUG_TAG, "event: %d", event);
    TSReleaseAssert(!"Unexpected Event");
  }

  return 0;
}

/*
 *  Convert specific URL format
 */
static void
convert_url_func(TSMBuffer req_bufp, TSMLoc req_loc)
{
  TSMLoc url_loc;
  TSMLoc field_loc;
  const char *str;
  int len, port;

  if (TSHttpHdrUrlGet(req_bufp, req_loc, &url_loc) == TS_ERROR) {
    return;
  }

  const char *hostname = getenv("HOSTNAME");
  if (hostname == nullptr) {
    return;
  }

  // in reverse proxy mode, TSUrlHostGet returns NULL here
  str = TSUrlHostGet(req_bufp, url_loc, &len);

  port = TSUrlPortGet(req_bufp, url_loc);

  // for now we assume the <upload proxy service domain> in the format is the hostname
  // but this needs to be verified later
  if ((NOT_VALID_PTR(str) || !strncmp(str, hostname, len)) && strlen(hostname) == (size_t)len) {
    const char *slash;
    const char *colon;
    // if (VALID_PTR(str))
    //  TSHandleStringRelease(req_bufp, url_loc, str);
    str   = TSUrlPathGet(req_bufp, url_loc, &len);
    slash = strstr(str, "/");
    if (slash == nullptr) {
      // if (VALID_PTR(str))
      //  TSHandleStringRelease(req_bufp, url_loc, str);
      TSHandleMLocRelease(req_bufp, req_loc, url_loc);
      return;
    }
    char pathTmp[len + 1];
    memcpy(pathTmp, str, len);
    pathTmp[len] = '\0';
    TSDebug(DEBUG_TAG, "convert_url_func working on path: %s", pathTmp);
    colon = strstr(str, ":");
    if (colon != nullptr && colon < slash) {
      char *port_str = (char *)TSmalloc(sizeof(char) * (slash - colon));
      strncpy(port_str, colon + 1, slash - colon - 1);
      port_str[slash - colon - 1] = '\0';
      TSUrlPortSet(req_bufp, url_loc, atoi(port_str));
      TSfree(port_str);
    } else {
      int length         = 0;
      const char *scheme = TSUrlSchemeGet(req_bufp, url_loc, &length);

      if ((length == TS_URL_LEN_HTTP && strncmp(TS_URL_SCHEME_HTTP, scheme, length) == 0 && port != 80) ||
          (length == TS_URL_LEN_HTTPS && strncmp(TS_URL_SCHEME_HTTPS, scheme, length) == 0 && port != 443)) {
        TSUrlPortSet(req_bufp, url_loc, port);
      }
      colon = slash;
    }

    TSUrlHostSet(req_bufp, url_loc, str, colon - str);
    TSUrlPathSet(req_bufp, url_loc, slash + 1, len - (slash - str) - 1);
    if ((field_loc = TSMimeHdrFieldFind(req_bufp, req_loc, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST)) != TS_NULL_MLOC &&
        field_loc != nullptr) {
      TSMimeHdrFieldValueStringSet(req_bufp, req_loc, field_loc, 0, str, slash - str);
      TSHandleMLocRelease(req_bufp, req_loc, field_loc);
    }
  } else {
    // if (VALID_PTR(str))
    //  TSHandleStringRelease(req_bufp, url_loc, str);
  }

  TSHandleMLocRelease(req_bufp, req_loc, url_loc);
}

static int
attach_pvc_plugin(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;
  TSMutex mutex;
  TSCont new_cont;
  pvc_state *my_state;
  TSMBuffer req_bufp;
  TSMLoc req_loc;
  TSMLoc field_loc;
  TSMLoc url_loc;
  char *url;
  int url_len;
  int content_length = 0;
  const char *method;
  int method_len;
  const char *host_str;
  int host_str_len;
  const char *host_hdr_str_val;
  int host_hdr_str_val_len;

  TSDebug(DEBUG_TAG, "inside attach_pvc_plugin");
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_PRE_REMAP:

    // if the request is issued by the TSHttpConnect() in this plugin, don't get in the endless cycle.
    if (TSHttpTxnIsInternal(txnp)) {
      TSDebug(DEBUG_TAG, "internal request");
      break;
    }

    if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_loc) == TS_ERROR) {
      LOG_ERROR("Error while retrieving client request header");
      break;
    }

    method = TSHttpHdrMethodGet(req_bufp, req_loc, &method_len);
    TSDebug(DEBUG_TAG, "inside handler");

    if (NOT_VALID_PTR(method) || method_len == 0) {
      TSDebug(DEBUG_TAG, "invalid method");

      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
      break;
    }
    // only deal with POST method
    TSDebug(DEBUG_TAG, "method: %s", method);

    if (static_cast<size_t>(method_len) != strlen(TS_HTTP_METHOD_POST) ||
        strncasecmp(method, TS_HTTP_METHOD_POST, method_len) != 0) {
      TSDebug(DEBUG_TAG, "Not POST method");

      // TSHandleStringRelease(req_bufp, req_loc, method);
      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
      break;
    }

    // TSHandleStringRelease(req_bufp, req_loc, method);

    TSDebug(DEBUG_TAG, "Got POST req");
    if (uconfig->url_list_file != nullptr) {
      TSDebug(DEBUG_TAG, "url_list_file != NULL");
      // check against URL list
      if (TSHttpHdrUrlGet(req_bufp, req_loc, &url_loc) == TS_ERROR) {
        LOG_ERROR("Couldn't get the url");
        TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
        break;
      }
      host_str = TSUrlHostGet(req_bufp, url_loc, &host_str_len);
      if (NOT_VALID_PTR(host_str) || host_str_len <= 0) {
        // reverse proxy mode
        field_loc = TSMimeHdrFieldFind(req_bufp, req_loc, TS_MIME_FIELD_HOST, -1);
        if (NOT_VALID_PTR(field_loc)) {
          // if (VALID_PTR(str))
          //  TSHandleStringRelease(req_bufp, url_loc, str);
          LOG_ERROR("Host field not found");
          TSHandleMLocRelease(req_bufp, req_loc, url_loc);
          TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
          break;
        }
        host_hdr_str_val = TSMimeHdrFieldValueStringGet(req_bufp, req_loc, field_loc, -1, &host_hdr_str_val_len);
        if (NOT_VALID_PTR(host_hdr_str_val) || host_hdr_str_val_len <= 0) {
          // if (VALID_PTR(str))
          //  TSHandleStringRelease(req_bufp, field_loc, str);
          TSHandleMLocRelease(req_bufp, req_loc, field_loc);
          TSHandleMLocRelease(req_bufp, req_loc, url_loc);
          TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
          break;
        }

        char replacement_host_str[host_hdr_str_val_len + 1];
        memcpy(replacement_host_str, host_hdr_str_val, host_hdr_str_val_len);
        replacement_host_str[host_hdr_str_val_len] = '\0';
        TSDebug(DEBUG_TAG, "Adding host to request url: %s", replacement_host_str);

        const char *colon = strchr(replacement_host_str, ':');
        if (colon != nullptr && colon[1] != '\0') {
          int length         = 0;
          const char *scheme = TSUrlSchemeGet(req_bufp, url_loc, &length);
          int port_str_val   = atoi(colon + 1);

          if ((length == TS_URL_LEN_HTTP && strncmp(TS_URL_SCHEME_HTTP, scheme, length) == 0 && port_str_val != 80) ||
              (length == TS_URL_LEN_HTTPS && strncmp(TS_URL_SCHEME_HTTPS, scheme, length) == 0 && port_str_val != 443)) {
            TSUrlPortSet(req_bufp, url_loc, port_str_val);
          }
          host_hdr_str_val_len = colon - replacement_host_str;
        }
        TSUrlHostSet(req_bufp, url_loc, host_hdr_str_val, host_hdr_str_val_len);

        // TSHandleStringRelease(req_bufp, field_loc, str);
        TSHandleMLocRelease(req_bufp, req_loc, field_loc);
      } else {
        // TSHandleStringRelease(req_bufp, url_loc, str);
      }

      int i = uconfig->url_num;
      url   = TSUrlStringGet(req_bufp, url_loc, &url_len);
      if (VALID_PTR(url)) {
        char urlStr[url_len + 1];
        memcpy(urlStr, url, url_len);
        urlStr[url_len] = '\0';
        TSDebug(DEBUG_TAG, "Request url: %s", urlStr);

        for (i = 0; i < uconfig->url_num; i++) {
          TSDebug(DEBUG_TAG, "uconfig url: %s", uconfig->urls[i]);
          if (strncmp(url, uconfig->urls[i], url_len) == 0) {
            break;
          }
        }

        TSfree(url);
      }
      TSHandleMLocRelease(req_bufp, req_loc, url_loc);

      if (uconfig->url_num > 0 && i == uconfig->url_num) {
        TSDebug(DEBUG_TAG, "breaking: url_num > 0 and i== url_num, URL match not found");
        TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
        break;
      }
    }

    if (uconfig->convert_url) {
      TSDebug(DEBUG_TAG, "doing convert url");
      convert_url_func(req_bufp, req_loc);
    }

    field_loc = TSMimeHdrFieldFind(req_bufp, req_loc, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
    if (field_loc == nullptr) {
      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
      LOG_ERROR("TSMimeHdrFieldRetrieve");
      break;
    }

    content_length = TSMimeHdrFieldValueIntGet(req_bufp, req_loc, field_loc, 0);
    /*{
  TSHandleMLocRelease(req_bufp, req_loc, field_loc);
  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
  LOG_ERROR("TSMimeFieldValueGet");
} else
    */
    //  content_length = value;

    mutex = TSMutexCreate();
    if (NOT_VALID_PTR(mutex)) {
      TSHandleMLocRelease(req_bufp, req_loc, field_loc);
      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
      LOG_ERROR("TSMutexCreate");
      break;
    }

    new_cont = TSContCreate(pvc_plugin, mutex);
    if (NOT_VALID_PTR(new_cont)) {
      TSHandleMLocRelease(req_bufp, req_loc, field_loc);
      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
      LOG_ERROR("TSContCreate");
      break;
    }

    my_state              = (pvc_state *)TSmalloc(sizeof(pvc_state));
    my_state->req_size    = content_length;
    my_state->p_vc        = nullptr;
    my_state->p_read_vio  = nullptr;
    my_state->p_write_vio = nullptr;

    my_state->net_vc      = nullptr;
    my_state->n_read_vio  = nullptr;
    my_state->n_write_vio = nullptr;

    my_state->req_buffer    = nullptr;
    my_state->req_reader    = nullptr;
    my_state->resp_buffer   = nullptr;
    my_state->resp_reader   = nullptr;
    my_state->fd            = -1;
    my_state->disk_io_mutex = nullptr;

    my_state->http_txnp = txnp; // not in use now, may need in the future

    my_state->req_finished  = 0;
    my_state->resp_finished = 0;
    my_state->nbytes_to_consume =
      -1; // the length of server request header to remove from incoming stream (will replace with client request header)

    my_state->size_written         = 0;
    my_state->size_read            = 0;
    my_state->write_offset         = 0;
    my_state->read_offset          = 0;
    my_state->is_reading_from_disk = 0;

    my_state->chunk_buffer = (char *)TSmalloc(sizeof(char) * uconfig->chunk_size);

    my_state->disk_io_mutex = TSMutexCreate();
    if (NOT_VALID_PTR(my_state->disk_io_mutex)) {
      LOG_ERROR("TSMutexCreate");
    }

    my_state->req_hdr_buffer = TSIOBufferCreate();
    my_state->req_hdr_reader = TSIOBufferReaderAlloc(my_state->req_hdr_buffer);
    TSHttpHdrPrint(req_bufp, req_loc, my_state->req_hdr_buffer);
    // print_buffer(my_state->req_hdr_reader);

    my_state->req_size += TSIOBufferReaderAvail(my_state->req_hdr_reader);

    /* Increment upload_vc_count */
    TSStatIntIncrement(upload_vc_count, 1);

    if (!uconfig->use_disk_buffer && my_state->req_size > uconfig->mem_buffer_size) {
      TSDebug(DEBUG_TAG,
              "The request size %" PRId64 " is larger than memory buffer size %" PRId64
              ", bypass upload proxy feature for this request",
              my_state->req_size, uconfig->mem_buffer_size);

      pvc_cleanup(new_cont, my_state);
      TSHandleMLocRelease(req_bufp, req_loc, field_loc);
      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
      break;
    }

    TSContDataSet(new_cont, my_state);

    if (uconfig->use_disk_buffer) {
      char path[500];
      // coverity[dont_call]
      int index = (int)(random() % uconfig->subdir_num);

      sprintf(path, "%s/%02X/tmp-XXXXXX", uconfig->base_dir, index);

      my_state->fd = mkstemp(path);
      unlink(path);
      if (my_state->fd < 0) {
        LOG_ERROR("open");
        uconfig->use_disk_buffer = false;
        my_state->fd             = -1;
      } else {
        TSDebug(DEBUG_TAG, "temp filename: %s", path);
      }
    }

    TSDebug(DEBUG_TAG, "calling TSHttpTxnIntercept()");
    TSHttpTxnIntercept(new_cont, txnp);

    break;
  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

static int
create_directory()
{
  char str[10];
  char cwd[4096];
  int i;
  DIR *dir;
  struct dirent *d;

  if (getcwd(cwd, 4096) == nullptr) {
    TSError("[%s] getcwd fails", PLUGIN_NAME);
    return 0;
  }

  if (chdir(uconfig->base_dir) < 0) {
    if (mkdir(uconfig->base_dir, S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
      TSError("[%s] Unable to enter or create %s", PLUGIN_NAME, uconfig->base_dir);
      goto error_out;
    }
    if (chdir(uconfig->base_dir) < 0) {
      TSError("[%s] Unable enter %s", PLUGIN_NAME, uconfig->base_dir);
      goto error_out;
    }
  }
  for (i = 0; i < uconfig->subdir_num; i++) {
    snprintf(str, 10, "%02X", i);
    if (chdir(str) < 0) {
      if (mkdir(str, S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
        TSError("[%s] Unable to enter or create %s/%s", PLUGIN_NAME, uconfig->base_dir, str);
        goto error_out;
      }
      if (chdir(str) < 0) {
        TSError("[%s] Unable to enter %s/%s", PLUGIN_NAME, uconfig->base_dir, str);
        goto error_out;
      }
    }
    dir = opendir(".");
    if (dir == nullptr) {
      goto error_out;
    }
    while ((d = readdir(dir))) {
      if (remove(d->d_name) < 0) {
        TSError("[%s] Unable to remove '%s': %s", PLUGIN_NAME, d->d_name, strerror(errno));
        closedir(dir);
        goto error_out;
      }
    }
    closedir(dir);
    if (chdir("..") == -1) {
      return 0;
    }
  }

  if (chdir(cwd) == -1) {
    return 0;
  }

  return 1;

error_out:
  /*  Debian's compiler chain complains about not using the return
      value of chdir() and cannot be silenced
      The reason is the combination of -D_FORTIFY_SOURCE=2 -O
   */
  if (chdir(cwd) == -1) {
    return 0;
  }

  return 0;
}

static void
load_urls(char *filename)
{
  TSFile file;
  char *url_buf;
  char *eol;
  int i;

  url_buf                          = (char *)TSmalloc(sizeof(char) * (uconfig->max_url_length + 1));
  url_buf[uconfig->max_url_length] = '\0';

  for (i = 0; i < 2; i++) {
    if ((file = TSfopen(filename, "r")) == nullptr) {
      TSfree(url_buf);
      TSError("[%s] Fail to open %s", PLUGIN_NAME, filename);
      return;
    }
    if (i == 0) { // first round
      uconfig->url_num = 0;
      while (TSfgets(file, url_buf, uconfig->max_url_length) != nullptr) {
        uconfig->url_num++;
      }
      uconfig->urls = (char **)TSmalloc(sizeof(char *) * uconfig->url_num);
    } else { // second round
      int idx = 0;
      while (TSfgets(file, url_buf, uconfig->max_url_length) != nullptr && idx < uconfig->url_num) {
        if ((eol = strstr(url_buf, "\r\n")) != nullptr) {
          /* To handle newlines on Windows */
          *eol = '\0';
        } else if ((eol = strchr(url_buf, '\n')) != nullptr) {
          *eol = '\0';
        } else {
          /* Not a valid line, skip it */
          continue;
        }
        uconfig->urls[idx] = TSstrdup(url_buf);
        idx++;
      }
      uconfig->url_num = idx;
    }
    TSfclose(file);
  }
  TSfree(url_buf);
}

void
parse_config_line(char *line, const struct config_val_ul *cv)
{
  const char *delim = "\t\r\n ";
  char *save        = nullptr;
  char *tok         = strtok_r(line, delim, &save);

  while (tok && cv->str) {
    if (!strcmp(tok, cv->str)) {
      tok = strtok_r(nullptr, delim, &save);
      if (tok) {
        switch (cv->type) {
        case TYPE_INT: {
          char *end = tok;
          int iv    = strtol(tok, &end, 10);
          if (end && *end == '\0') {
            *((int *)cv->val) = iv;
            TSError("[%s] Parsed int config value %s : %d", PLUGIN_NAME, cv->str, iv);
            TSDebug(DEBUG_TAG, "Parsed int config value %s : %d", cv->str, iv);
          }
          break;
        }
        case TYPE_UINT: {
          char *end        = tok;
          unsigned int uiv = strtoul(tok, &end, 10);
          if (end && *end == '\0') {
            *((unsigned int *)cv->val) = uiv;
            TSError("[%s] Parsed uint config value %s : %u", PLUGIN_NAME, cv->str, uiv);
            TSDebug(DEBUG_TAG, "Parsed uint config value %s : %u", cv->str, uiv);
          }
          break;
        }
        case TYPE_LONG: {
          char *end = tok;
          long lv   = strtol(tok, &end, 10);
          if (end && *end == '\0') {
            *((long *)cv->val) = lv;
            TSError("[%s] Parsed long config value %s : %ld", PLUGIN_NAME, cv->str, lv);
            TSDebug(DEBUG_TAG, "Parsed long config value %s : %ld", cv->str, lv);
          }
          break;
        }
        case TYPE_ULONG: {
          char *end         = tok;
          unsigned long ulv = strtoul(tok, &end, 10);
          if (end && *end == '\0') {
            *((unsigned long *)cv->val) = ulv;
            TSError("[%s] Parsed ulong config value %s : %lu", PLUGIN_NAME, cv->str, ulv);
            TSDebug(DEBUG_TAG, "Parsed ulong config value %s : %lu", cv->str, ulv);
          }
          break;
        }
        case TYPE_STRING: {
          size_t len = strlen(tok);
          if (len > 0) {
            *((char **)cv->val) = (char *)TSmalloc(len + 1);
            strcpy(*((char **)cv->val), tok);
            TSError("[%s] Parsed string config value %s : %s", PLUGIN_NAME, cv->str, tok);
            TSDebug(DEBUG_TAG, "Parsed string config value %s : %s", cv->str, tok);
          }
          break;
        }
        case TYPE_BOOL: {
          size_t len = strlen(tok);
          if (len > 0) {
            if (*tok == '1' || *tok == 't') {
              *((bool *)cv->val) = true;
            } else {
              *((bool *)cv->val) = false;
            }
            TSError("[%s] Parsed bool config value %s : %d", PLUGIN_NAME, cv->str, *((bool *)cv->val));
            TSDebug(DEBUG_TAG, "Parsed bool config value %s : %d", cv->str, *((bool *)cv->val));
          }
          break;
        }
        default:
          break;
        }
      }
    }
    cv++;
  }
}

bool
read_upload_config(const char *file_name)
{
  TSDebug(DEBUG_TAG, "read_upload_config: %s", file_name);
  uconfig                  = (upload_config *)TSmalloc(sizeof(upload_config));
  uconfig->use_disk_buffer = true;
  uconfig->convert_url     = false;
  uconfig->chunk_size      = 16 * 1024;
  uconfig->mem_buffer_size = 32 * 1024;
  uconfig->url_list_file   = nullptr;
  uconfig->max_url_length  = 4096;
  uconfig->url_num         = 0;
  uconfig->urls            = nullptr;
  uconfig->base_dir        = nullptr;
  uconfig->subdir_num      = 64;
  uconfig->thread_num      = 4;

  struct config_val_ul config_vals[] = {{"use_disk_buffer", TYPE_BOOL, &(uconfig->use_disk_buffer)},
                                        {"convert_url", TYPE_BOOL, &(uconfig->convert_url)},
                                        {"chunk_size", TYPE_ULONG, &(uconfig->chunk_size)},
                                        {"mem_buffer_size", TYPE_ULONG, &(uconfig->mem_buffer_size)},
                                        {"url_list_file", TYPE_STRING, &(uconfig->url_list_file)},
                                        {"max_url_length", TYPE_ULONG, &(uconfig->max_url_length)},
                                        {"base_dir", TYPE_STRING, &(uconfig->base_dir)},
                                        {"subdir_num", TYPE_UINT, &(uconfig->subdir_num)},
                                        {"thread_num", TYPE_UINT, &(uconfig->thread_num)},
                                        {nullptr, TYPE_LONG, nullptr}};
  TSFile conf_file;
  conf_file = TSfopen(file_name, "r");

  if (conf_file != nullptr) {
    TSDebug(DEBUG_TAG, "opened config: %s", file_name);
    char buf[1024];
    while (TSfgets(conf_file, buf, sizeof(buf) - 1) != nullptr) {
      if (buf[0] != '#') {
        parse_config_line(buf, config_vals);
      }
    }
    TSfclose(conf_file);
  } else {
    TSError("[%s] Failed to open upload config file %s", PLUGIN_NAME, file_name);
    // if fail to open config file, use the default config
  }

  if (uconfig->base_dir == nullptr) {
    uconfig->base_dir = TSstrdup("/FOOBAR/var/buffer_upload_tmp");
  } else {
    // remove the "/" at the end.
    if (uconfig->base_dir[strlen(uconfig->base_dir) - 1] == '/') {
      uconfig->base_dir[strlen(uconfig->base_dir) - 1] = '\0';
    }
  }

  if (uconfig->subdir_num <= 0) {
    // default value
    uconfig->subdir_num = 64;
  }

  if (uconfig->thread_num <= 0) {
    // default value
    uconfig->thread_num = 4;
  }

  return true;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont contp;
  char default_filename[1024];
  const char *conf_filename;

  if (argc > 1) {
    conf_filename = argv[1];
  } else {
    sprintf(default_filename, "%s/upload.conf", TSPluginDirGet());
    conf_filename = default_filename;
  }

  if (!read_upload_config(conf_filename) || !uconfig) {
    if (argc > 1) {
      TSError("[%s] Failed to read upload config %s", PLUGIN_NAME, argv[1]);
    } else {
      TSError("[%s] No config file specified. Specify conf file in plugin.conf: "
              "'buffer_upload.so /path/to/upload.conf'",
              PLUGIN_NAME);
    }
  }
  // set the num of threads for disk AIO
  if (TSAIOThreadNumSet(uconfig->thread_num) == TS_ERROR) {
    TSError("[%s] Failed to set thread number", PLUGIN_NAME);
  }

  TSDebug(DEBUG_TAG, "uconfig->url_list_file: %s", uconfig->url_list_file);
  if (uconfig->url_list_file) {
    load_urls(uconfig->url_list_file);
    TSDebug(DEBUG_TAG, "loaded uconfig->url_list_file, num urls: %d", uconfig->url_num);
  }

  info.plugin_name   = const_cast<char *>("buffer_upload");
  info.vendor_name   = const_cast<char *>("Apache Software Foundation");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  if (uconfig->use_disk_buffer && !create_directory()) {
    TSError("[%s] Directory creation failed", PLUGIN_NAME);
    uconfig->use_disk_buffer = false;
  }

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  /* create the statistic variables */
  upload_vc_count = TSStatCreate("upload_vc.count", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);

  contp = TSContCreate(attach_pvc_plugin, nullptr);
  TSHttpHookAdd(TS_HTTP_PRE_REMAP_HOOK, contp);
}
