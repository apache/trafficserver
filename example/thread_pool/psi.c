/** @file

  Proxy Side Include plugin (PSI)

  Synopsis:

  This plugin allows to insert the content of a file stored on the proxy disk
  into the body of an html response.

  The plugin illustrates how to use a pool of threads in order to do blocking
  calls (here, some disk i/o) in a Traffic Server plugin.

  Further details: Refer to README file.

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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/param.h>

#include "ts/ts.h"
#include "thread.h"
#include "ts/ink_defs.h"

/* This is the number of threads spawned by the plugin.
   Should be tuned based on performance requirements,
   blocking calls duration, etc... */
#define NB_THREADS 3

#define PSI_FILENAME_MAX_SIZE 512
#define PSI_PATH_MAX_SIZE 256
#define PSI_PATH "include"

#define PSI_START_TAG "<!--include="
#define PSI_START_TAG_LEN 12

#define PSI_END_TAG "-->"
#define PSI_END_TAG_LEN 3

#define MIME_FIELD_XPSI "X-Psi"

typedef enum {
  STATE_READ_DATA = 1,
  STATE_READ_PSI  = 2,
  STATE_DUMP_PSI  = 3,
} PluginState;

typedef enum {
  PARSE_SEARCH,
  PARSE_EXTRACT,
} ParseState;

typedef struct {
  unsigned int magic;
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;

  TSIOBuffer psi_buffer;
  TSIOBufferReader psi_reader;
  char psi_filename[PSI_FILENAME_MAX_SIZE + 128];
  int psi_filename_len;
  int psi_success;

  ParseState parse_state;

  PluginState state;
  int transform_bytes;
} ContData;

typedef struct {
  TSCont contp;
  TSEvent event;
} TryLockData;

typedef enum {
  STR_SUCCESS,
  STR_PARTIAL,
  STR_FAIL,
} StrOperationResult;

extern Queue job_queue;

static TSTextLogObject log;
static char psi_directory[PSI_PATH_MAX_SIZE];

static int trylock_handler(TSCont contp, TSEvent event, void *edata);

/*-------------------------------------------------------------------------
  cont_data_alloc
  Allocate and initialize a ContData structure associated to a transaction

  Input:
  Output:
  Return Value:
    Pointer on a new allocated ContData structure
  -------------------------------------------------------------------------*/
static ContData *
cont_data_alloc()
{
  ContData *data;

  data                = (ContData *)TSmalloc(sizeof(ContData));
  data->magic         = MAGIC_ALIVE;
  data->output_vio    = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;

  data->psi_buffer       = NULL;
  data->psi_reader       = NULL;
  data->psi_filename[0]  = '\0';
  data->psi_filename_len = 0;
  data->psi_success      = 0;

  data->parse_state = PARSE_SEARCH;

  data->state           = STATE_READ_DATA;
  data->transform_bytes = 0;

  return data;
}

/*-------------------------------------------------------------------------
  cont_data_destroy
  Deallocate ContData structure associated to a transaction

  Input:
    data   structure to deallocate
  Output:
  Return Value:
    none
  -------------------------------------------------------------------------*/
static void
cont_data_destroy(ContData *data)
{
  TSDebug(PLUGIN_NAME, "Destroying continuation data");
  if (data) {
    TSAssert(data->magic == MAGIC_ALIVE);
    if (data->output_reader) {
      TSIOBufferReaderFree(data->output_reader);
      data->output_reader = NULL;
    }
    if (data->output_buffer) {
      TSIOBufferDestroy(data->output_buffer);
      data->output_buffer = NULL;
    }
    if (data->psi_reader) {
      TSIOBufferReaderFree(data->psi_reader);
      data->psi_reader = NULL;
    }
    if (data->psi_buffer) {
      TSIOBufferDestroy(data->psi_buffer);
      data->psi_buffer = NULL;
    }
    data->magic = MAGIC_DEAD;
    TSfree(data);
  }
}

/*-------------------------------------------------------------------------
  strsearch_ioreader
  Looks for string pattern in an iobuffer

  Input:
    reader   reader on a iobuffer
    pattern  string to look for (nul terminated)
  Output:
    nparse   number of chars scanned, excluding the matching pattern
  Return Value:
    STR_SUCCESS if pattern found
    STR_PARTIAL if pattern found partially
    STR_FAIL    if pattern not found
  -------------------------------------------------------------------------*/
static StrOperationResult
strsearch_ioreader(TSIOBufferReader reader, const char *pattern, int *nparse)
{
  int index             = 0;
  TSIOBufferBlock block = TSIOBufferReaderStart(reader);
  int slen              = strlen(pattern);

  if (slen <= 0) {
    return STR_FAIL;
  }

  *nparse = 0;

  /* Loop thru each block while we've not yet found the pattern */
  while ((block != NULL) && (index < slen)) {
    int64_t blocklen;
    const char *blockptr = TSIOBufferBlockReadStart(block, reader, &blocklen);
    const char *ptr;

    for (ptr = blockptr; ptr < blockptr + blocklen; ptr++) {
      (*nparse)++;
      if (*ptr == pattern[index]) {
        index++;
        if (index == slen) {
          break;
        }
      } else {
        index = 0;
      }
    }

    /* Parse next block */
    block = TSIOBufferBlockNext(block);
  }

  *nparse -= index; /* Adjust nparse so it doesn't include matching chars */
  if (index == slen) {
    TSDebug(PLUGIN_NAME, "strfind: match for %s at position %d", pattern, *nparse);
    return STR_SUCCESS;
  } else if (index > 0) {
    TSDebug(PLUGIN_NAME, "strfind: partial match for %s at position %d", pattern, *nparse);
    return STR_PARTIAL;
  } else {
    TSDebug(PLUGIN_NAME, "strfind no match for %s", pattern);
    return STR_FAIL;
  }
}

/*-------------------------------------------------------------------------
  strextract_ioreader
  Extract a string from an iobuffer.
  Start reading at position offset in iobuffer and extract until the
  string end_pattern is found.

  Input:
    reader      reader on a iobuffer
    offset      position to start reading
    end_pattern the termination string (nul terminated)
  Output:
    buffer      if success, contains the extracted string, nul terminated
    buflen      if success, contains the buffer length (excluding null char).
  Return Value:
    STR_SUCCESS if extraction successful
    STR_PARTIAL if extraction not yet completed
    STR_FAIL    if extraction failed
  -------------------------------------------------------------------------*/
static int
strextract_ioreader(TSIOBufferReader reader, int offset, const char *end_pattern, char *buffer, int *buflen)
{
  int buf_idx       = 0;
  int p_idx         = 0;
  int nbytes_so_far = 0;
  int plen          = strlen(end_pattern);
  const char *ptr;
  TSIOBufferBlock block = TSIOBufferReaderStart(reader);

  if (plen <= 0) {
    return STR_FAIL;
  }

  /* Now start extraction */
  while ((block != NULL) && (p_idx < plen) && (buf_idx < PSI_FILENAME_MAX_SIZE)) {
    int64_t blocklen;
    const char *blockptr = TSIOBufferBlockReadStart(block, reader, &blocklen);

    for (ptr = blockptr; ptr < blockptr + blocklen; ptr++, nbytes_so_far++) {
      if (nbytes_so_far >= offset) {
        /* Add a new character to the filename */
        buffer[buf_idx++] = *ptr;

        /* If we have reach the end of the filename, we're done */
        if (end_pattern[p_idx] == *ptr) {
          p_idx++;
          if (p_idx == plen) {
            break;
          }
        } else {
          p_idx = 0;
        }

        /* The filename is too long, something is fishy... let's abort extraction */
        if (buf_idx >= PSI_FILENAME_MAX_SIZE) {
          break;
        }
      }
    }

    block = TSIOBufferBlockNext(block);
  }

  /* Error, could not read end of filename */
  if (buf_idx >= PSI_FILENAME_MAX_SIZE) {
    TSDebug(PLUGIN_NAME, "strextract: filename too long");
    *buflen = 0;
    return STR_FAIL;
  }

  /* Full Match */
  if (p_idx == plen) {
    /* Nul terminate the filename, remove the end_pattern copied into the buffer */
    *buflen         = buf_idx - plen;
    buffer[*buflen] = '\0';
    TSDebug(PLUGIN_NAME, "strextract: filename = |%s|", buffer);
    return STR_SUCCESS;
  }
  /* End of filename not yet reached we need to read some more data */
  else {
    TSDebug(PLUGIN_NAME, "strextract: partially extracted filename");
    *buflen = buf_idx - p_idx;
    return STR_PARTIAL;
  }
}

/*-------------------------------------------------------------------------
  parse_data
  Search for psi filename in the data.

  Input:
    contp   continuation for the current transaction
    reader  reader on the iobuffer that contains data
    avail   amount of data available in the iobuffer
  Output:
    towrite    amount of data in the iobuffer that can be written
               to the downstream vconnection
    toconsume  amount of data in the iobuffer to consume
  Return Value:
    0   if no psi filename found
    1  if a psi filename was found
  -------------------------------------------------------------------------*/
static int
parse_data(TSCont contp, TSIOBufferReader input_reader, int avail, int *toconsume, int *towrite)
{
  ContData *data;
  int nparse = 0;
  int status;

  data = TSContDataGet(contp);
  TSAssert(data->magic == MAGIC_ALIVE);

  if (data->parse_state == PARSE_SEARCH) {
    /* Search for the start pattern */
    status = strsearch_ioreader(input_reader, PSI_START_TAG, &nparse);
    switch (status) {
    case STR_FAIL:
      /* We didn't found the pattern */
      *toconsume        = avail;
      *towrite          = avail;
      data->parse_state = PARSE_SEARCH;
      return 0;
    case STR_PARTIAL:
      /* We need to read some more data */
      *toconsume        = nparse;
      *towrite          = nparse;
      data->parse_state = PARSE_SEARCH;
      return 0;
    case STR_SUCCESS:
      /* We found the start_pattern, let's go ahead */
      data->psi_filename_len = 0;
      data->psi_filename[0]  = '\0';
      data->parse_state      = PARSE_EXTRACT;
      break;
    default:
      TSAssert(!"strsearch_ioreader returned unexpected status");
    }
  }

  /* And now let's extract the filename */
  status = strextract_ioreader(input_reader, nparse + PSI_START_TAG_LEN, PSI_END_TAG, data->psi_filename, &data->psi_filename_len);
  switch (status) {
  case STR_FAIL:
    /* We couldn't extract a valid filename */
    *toconsume        = nparse;
    *towrite          = nparse;
    data->parse_state = PARSE_SEARCH;
    return 0;
  case STR_PARTIAL:
    /* We need to read some more data */
    *toconsume        = nparse;
    *towrite          = nparse;
    data->parse_state = PARSE_EXTRACT;
    return 0;
  case STR_SUCCESS:
    /* We got a valid filename */
    *toconsume        = nparse + PSI_START_TAG_LEN + data->psi_filename_len + PSI_END_TAG_LEN;
    *towrite          = nparse;
    data->parse_state = PARSE_SEARCH;
    return 1;
  default:
    TSAssert(!"strextract_ioreader returned bad status");
  }

  return 0;
}

// TODO: Use libc basename function
//
/*-------------------------------------------------------------------------
  strip_path
  Utility func to strip path from a filename (= _basename cmd on unix)
  Input:
    filename
  Output :
    None
  Return Value:
    Filename with path stripped
  -------------------------------------------------------------------------*/
static const char *
_basename(const char *filename)
{
  char *cptr;
  const char *ptr = filename;

  while ((cptr = strchr(ptr, (int)'/')) != NULL) {
    ptr = cptr + 1;
  }
  return ptr;
}

/*-------------------------------------------------------------------------
  psi_include
  Read file to include. Copy its content into an iobuffer.

  This is the function doing blocking calls and called by the plugin's threads

  Input:
    data      continuation for the current transaction
  Output :
    data->psi_buffer  contains the file content
    data->psi_sucess  0 if include failed, 1 if success
  Return Value:
    0  if failure
    1  if success
  -------------------------------------------------------------------------*/
static int
psi_include(TSCont contp, void *edata ATS_UNUSED)
{
#define BUFFER_SIZE 1024
  ContData *data;
  TSFile filep;
  char buf[BUFFER_SIZE];
  char inc_file[PSI_PATH_MAX_SIZE + PSI_FILENAME_MAX_SIZE];

  /* We manipulate plugin continuation data from a separate thread.
     Grab mutex to avoid concurrent access */
  TSMutexLock(TSContMutexGet(contp));
  data = TSContDataGet(contp);
  TSAssert(data->magic == MAGIC_ALIVE);

  if (!data->psi_buffer) {
    data->psi_buffer = TSIOBufferCreate();
    data->psi_reader = TSIOBufferReaderAlloc(data->psi_buffer);
  }

  /* For security reason, we do not allow to include files that are
     not in the directory <plugin_path>/include.
     Also include file cannot contain any path. */
  sprintf(inc_file, "%s/%s", psi_directory, _basename(data->psi_filename));

  /* Read the include file and copy content into iobuffer */
  if ((filep = TSfopen(inc_file, "r")) != NULL) {
    TSDebug(PLUGIN_NAME, "Reading include file %s", inc_file);

    while (TSfgets(filep, buf, BUFFER_SIZE) != NULL) {
      TSIOBufferBlock block;
      int64_t len, avail, ndone, ntodo, towrite;
      char *ptr_block;

      len   = strlen(buf);
      ndone = 0;
      ntodo = len;
      while (ntodo > 0) {
        /* TSIOBufferStart allocates more blocks if required */
        block     = TSIOBufferStart(data->psi_buffer);
        ptr_block = TSIOBufferBlockWriteStart(block, &avail);
        towrite   = MIN(ntodo, avail);

        memcpy(ptr_block, buf + ndone, towrite);
        TSIOBufferProduce(data->psi_buffer, towrite);
        ntodo -= towrite;
        ndone += towrite;
      }
    }
    TSfclose(filep);
    data->psi_success = 1;
    if (log) {
      TSTextLogObjectWrite(log, "Successfully included file: %s", inc_file);
    }
  } else {
    data->psi_success = 0;
    if (log) {
      TSTextLogObjectWrite(log, "Failed to include file: %s", inc_file);
    }
  }

  /* Change state and schedule an event EVENT_IMMEDIATE on the plugin continuation
     to let it know we're done. */

  /* Note: if the blocking call was not in the transformation state (i.e. in
     TS_HTTP_READ_REQUEST_HDR, TS_HTTP_OS_DNS and so on...) we could
     use TSHttpTxnReenable to wake up the transaction instead of sending an event. */

  TSContSchedule(contp, 0, TS_THREAD_POOL_DEFAULT);
  data->psi_success = 0;
  data->state       = STATE_READ_DATA;
  TSMutexUnlock(TSContMutexGet(contp));

  return 0;
}

/*-------------------------------------------------------------------------
  wake_up_streams
  Send an event to the upstream vconnection to either
    - ask for more data
    - let it know we're done
  Reenable the downstream vconnection
  Input:
    contp      continuation for the current transaction
  Output :
  Return Value:
   0 if failure
   1 if success
  -------------------------------------------------------------------------*/
static int
wake_up_streams(TSCont contp)
{
  TSVIO input_vio;
  ContData *data;
  int ntodo;

  data = TSContDataGet(contp);
  TSAssert(data->magic == MAGIC_ALIVE);

  input_vio = TSVConnWriteVIOGet(contp);
  ntodo     = TSVIONTodoGet(input_vio);

  if (ntodo > 0) {
    TSVIOReenable(data->output_vio);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
  } else {
    TSDebug(PLUGIN_NAME, "Total bytes produced by transform = %d", data->transform_bytes);
    TSVIONBytesSet(data->output_vio, data->transform_bytes);
    TSVIOReenable(data->output_vio);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }

  return 1;
}

/*-------------------------------------------------------------------------
  handle_transform
   Get data from upstream vconn.
   Parse it.
   Include file if include tags found.
   Copy data to downstream vconn.
   Wake up upstream to get more data.

  Input:
    contp      continuation for the current transaction
  Output :
  Return Value:
   0 if failure
   1 if success
  -------------------------------------------------------------------------*/
static int
handle_transform(TSCont contp)
{
  TSVConn output_conn;
  TSVIO input_vio;
  ContData *data;
  TSIOBufferReader input_reader;
  int toread, avail, psi, toconsume = 0, towrite = 0;

  /* Get the output (downstream) vconnection where we'll write data to. */
  output_conn = TSTransformOutputVConnGet(contp);

  /* Get upstream vio */
  input_vio = TSVConnWriteVIOGet(contp);
  data      = TSContDataGet(contp);
  TSAssert(data->magic == MAGIC_ALIVE);

  if (!data->output_buffer) {
    data->output_buffer = TSIOBufferCreate();
    data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);

    /* INT64_MAX because we don't know yet how much bytes we'll produce */
    data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, INT64_MAX);
  }

  /* If the input VIO's buffer is NULL, the transformation is over */
  if (!TSVIOBufferGet(input_vio)) {
    TSDebug(PLUGIN_NAME, "input_vio NULL, terminating transformation");
    TSVIONBytesSet(data->output_vio, data->transform_bytes);
    TSVIOReenable(data->output_vio);
    return 1;
  }

  /* Determine how much data we have left to read. */
  toread = TSVIONTodoGet(input_vio);

  if (toread > 0) {
    input_reader = TSVIOReaderGet(input_vio);
    avail        = TSIOBufferReaderAvail(input_reader);

    /* There are some data available for reading. Let's parse it */
    if (avail > 0) {
      /* No need to parse data if there are too few bytes left to contain
         an include command... */
      if (toread > (PSI_START_TAG_LEN + PSI_END_TAG_LEN)) {
        psi = parse_data(contp, input_reader, avail, &toconsume, &towrite);
      } else {
        towrite   = avail;
        toconsume = avail;
        psi       = 0;
      }

      if (towrite > 0) {
        /* Update the total size of the doc so far */
        data->transform_bytes += towrite;

        /* Copy the data from the read buffer to the output buffer. */
        /* TODO: Should we check the return value of TSIOBufferCopy() ? */
        TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(input_vio), towrite, 0);
        /* Reenable the output connection so it can read the data we've produced. */
        TSVIOReenable(data->output_vio);
      }

      if (toconsume > 0) {
        /* Consume data we've processed an we are no longer interested in */
        TSIOBufferReaderConsume(input_reader, toconsume);

        /* Modify the input VIO to reflect how much data we've completed. */
        TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + toconsume);
      }

      /* Did we find a psi filename to execute in the data ? */
      if (psi) {
        Job *new_job;
        /* Add a request to include a file into the jobs queue.. */
        /* We'll be called back once it's done with an EVENT_IMMEDIATE */
        TSDebug(PLUGIN_NAME, "Psi filename extracted, adding an include job to thread queue");
        data->state = STATE_READ_PSI;

        /* Create a new job request and add it to the queue */
        new_job = job_create(contp, &psi_include, NULL);
        add_to_queue(&job_queue, new_job);

        /* Signal to the threads there is a new job */
        thread_signal_job();

        return 1;
      }
    }
  }

  /* Wake up upstream and downstream vconnections */
  wake_up_streams(contp);

  return 1;
}

/*-------------------------------------------------------------------------
  dump_psi
  Dump the psi_output to the downstream vconnection.

  Input:
    contp      continuation for the current transaction
  Output :
  Return Value:
   0 if failure
   1 if success
  -------------------------------------------------------------------------*/
static int
dump_psi(TSCont contp)
{
  ContData *data;
  int psi_output_len;

/* TODO: This is odd, do we need to get the input_vio, but never use it ?? */
#if 0
  TSVIO input_vio;
  input_vio = TSVConnWriteVIOGet(contp);
#endif

  data = TSContDataGet(contp);
  TSAssert(data->magic == MAGIC_ALIVE);

  /* If script exec succeeded, copy its output to the downstream vconn */
  if (data->psi_success == 1) {
    psi_output_len = TSIOBufferReaderAvail(data->psi_reader);

    if (psi_output_len > 0) {
      data->transform_bytes += psi_output_len;

      TSDebug(PLUGIN_NAME, "Inserting %d bytes from include file", psi_output_len);
      /* TODO: Should we check the return value of TSIOBufferCopy() ? */
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), data->psi_reader, psi_output_len, 0);
      /* Consume all the output data */
      TSIOBufferReaderConsume(data->psi_reader, psi_output_len);

      /* Reenable the output connection so it can read the data we've produced. */
      TSVIOReenable(data->output_vio);
    }
  }

  /* Change state to finish up reading upstream data */
  data->state = STATE_READ_DATA;
  return 0;
}

/*-------------------------------------------------------------------------
  transform_handler
  Handler for all events received during the transformation process

  Input:
    contp      continuation for the current transaction
    event      event received
    data       pointer on optional data
  Output :
  Return Value:
  -------------------------------------------------------------------------*/
static int
transform_handler(TSCont contp, TSEvent event, void *edata ATS_UNUSED)
{
  TSVIO input_vio;
  ContData *data;
  int state, retval;

  /* This section will be called by both TS internal
     and the thread. Protect it with a mutex to avoid
     concurrent calls. */

  /* Handle TryLock result */
  if (TSMutexLockTry(TSContMutexGet(contp)) != TS_SUCCESS) {
    TSCont c       = TSContCreate(trylock_handler, NULL);
    TryLockData *d = TSmalloc(sizeof(TryLockData));

    d->contp = contp;
    d->event = event;
    TSContDataSet(c, d);
    TSContSchedule(c, 10, TS_THREAD_POOL_DEFAULT);
    return 1;
  }

  data = TSContDataGet(contp);
  TSAssert(data->magic == MAGIC_ALIVE);

  state = data->state;

  /* Check to see if the transformation has been closed */
  retval = TSVConnClosedGet(contp);
  if (retval) {
    /* If the thread is still executing its job, we don't want to destroy
       the continuation right away as the thread will call us back
       on this continuation. */
    if (state == STATE_READ_PSI) {
      TSContSchedule(contp, 10, TS_THREAD_POOL_DEFAULT);
    } else {
      TSMutexUnlock(TSContMutexGet(contp));
      cont_data_destroy(TSContDataGet(contp));
      TSContDestroy(contp);
      return 1;
    }
  } else {
    switch (event) {
    case TS_EVENT_ERROR:
      input_vio = TSVConnWriteVIOGet(contp);
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
      break;

    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;

    case TS_EVENT_VCONN_WRITE_READY:
      /* downstream vconnection is done reading data we've write into it.
         let's read some more data from upstream if we're in read state. */
      if (state == STATE_READ_DATA) {
        handle_transform(contp);
      }
      break;

    case TS_EVENT_IMMEDIATE:
      if (state == STATE_READ_DATA) {
        /* upstream vconnection signals some more data ready to be read
           let's try to transform some more data */
        handle_transform(contp);
      } else if (state == STATE_DUMP_PSI) {
        /* The thread scheduled an event on our continuation to let us
           know it has completed its job
           Let's dump the include content to the output vconnection */
        dump_psi(contp);
        wake_up_streams(contp);
      }
      break;

    default:
      TSAssert(!"Unexpected event");
      break;
    }
  }

  TSMutexUnlock(TSContMutexGet(contp));
  return 1;
}

/*-------------------------------------------------------------------------
  trylock_handler
  Small handler to handle TSMutexLockTry failures

  Input:
    contp      continuation for the current transaction
    event      event received
    data       pointer on optional data
  Output :
  Return Value:
  -------------------------------------------------------------------------*/
static int
trylock_handler(TSCont contp, TSEvent event ATS_UNUSED, void *edata ATS_UNUSED)
{
  TryLockData *data = TSContDataGet(contp);
  transform_handler(data->contp, data->event, NULL);
  TSfree(data);
  TSContDestroy(contp);
  return 0;
}

/*-------------------------------------------------------------------------
  transformable
  Determine if the current transaction should be transformed or not

  Input:
    txnp      current transaction
  Output :
  Return Value:
    1  if transformable
    0  if not
  -------------------------------------------------------------------------*/
static int
transformable(TSHttpTxn txnp)
{
  /*  We are only interested in transforming "200 OK" responses
     with a Content-Type: text/ header and with X-Psi header */
  TSMBuffer bufp;
  TSMLoc hdr_loc, field_loc;
  TSHttpStatus resp_status;
  const char *value;

  if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc)) {
    resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
    if (resp_status != TS_HTTP_STATUS_OK) {
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      return 0;
    }

    field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_TYPE, -1);
    if (field_loc == TS_NULL_MLOC) {
      TSError("[%s] Unable to search Content-Type field", PLUGIN_NAME);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      return 0;
    }

    value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, NULL);
    if ((value == NULL) || (strncasecmp(value, "text/", sizeof("text/") - 1) != 0)) {
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      return 0;
    }

    TSHandleMLocRelease(bufp, hdr_loc, field_loc);

    field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, MIME_FIELD_XPSI, -1);

    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }

  return 1;
}

/*-------------------------------------------------------------------------
  transform_add
  Create a transformation and alloc data structure

  Input:
    txnp      current transaction
  Output :
  Return Value:
    1  if transformation created
    0  if not
  -------------------------------------------------------------------------*/
static int
transform_add(TSHttpTxn txnp)
{
  TSCont contp;
  ContData *data;

  contp = TSTransformCreate(transform_handler, txnp);
  data  = cont_data_alloc();
  TSContDataSet(contp, data);

  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, contp);
  return 1;
}

/*-------------------------------------------------------------------------
  read_response_handler
  Handler for events related to hook READ_RESPONSE

  Input:
    contp      continuation for the current transaction
    event      event received
    data       pointer on eventual data
  Output :
  Return Value:
  -------------------------------------------------------------------------*/
static int
read_response_handler(TSCont contp ATS_UNUSED, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    if (transformable(txnp)) {
      TSDebug(PLUGIN_NAME, "Add a transformation");
      transform_add(txnp);
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  default:
    break;
  }

  return 0;
}

/*-------------------------------------------------------------------------
  TSPluginInit
  Function called at plugin init time

  Input:
    argc  number of args
    argv  list vof args
  Output :
  Return Value:
  -------------------------------------------------------------------------*/
void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  TSPluginRegistrationInfo info;
  int i;
  TSReturnCode retval;

  info.plugin_name   = "psi";
  info.vendor_name   = "Apache";
  info.support_email = "";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  /* Initialize the psi directory = <plugin_path>/include */
  sprintf(psi_directory, "%s/%s", TSPluginDirGet(), PSI_PATH);

  /* create an TSTextLogObject to log any psi include */
  retval = TSTextLogObjectCreate("psi", TS_LOG_MODE_ADD_TIMESTAMP, &log);
  if (retval == TS_ERROR) {
    TSError("[%s] Failed creating log for psi plugin", PLUGIN_NAME);
    log = NULL;
  }

  /* Create working threads */
  thread_init();
  init_queue(&job_queue);

  for (i = 0; i < NB_THREADS; i++) {
    char *thread_name = (char *)TSmalloc(64);
    sprintf(thread_name, "Thread[%d]", i);
    if (!TSThreadCreate(thread_loop, thread_name)) {
      TSError("[%s] Failed creating threads", PLUGIN_NAME);
      return;
    }
  }

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(read_response_handler, TSMutexCreate()));
  TSDebug(PLUGIN_NAME, "Plugin started");
}
