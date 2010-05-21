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

/*
 *
 *
 *
 *	Usage:
 * 	(NT): psi.dll
 *	(Solaris): psi.so
 *
 *  Proxy Side Include plugin (PSI)
 *
 *   Synopsis:
 *
 *  This plugin allows to insert the content of a file stored on the proxy disk
 *  into the body of an html response.
 *
 *  The plugin illustrates how to use a pool of threads in order to do blocking
 *  calls (here, some disk i/o) in a Traffic Server plugin.
 *
 *
 *   Details:
 *
 *  Refer to README file.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ts/ts.h>

#include "thread.h"

#define MIN(x,y) ((x < y) ? x :y)

#define DBG_TAG "xpsi"

/* This is the number of threads spawned by the plugin.
   Should be tuned based on performance requirements,
   blocking calls duration, etc... */
#define NB_THREADS 3


#define PSI_FILENAME_MAX_SIZE 512
#define PSI_PATH_MAX_SIZE     256
#define PSI_PATH "include"

#define PSI_START_TAG      "<!--include="
#define PSI_START_TAG_LEN  12

#define PSI_END_TAG        "-->"
#define PSI_END_TAG_LEN    3

#define MIME_FIELD_XPSI "X-Psi"

typedef enum
{
  STATE_READ_DATA = 1,
  STATE_READ_PSI = 2,
  STATE_DUMP_PSI = 3
} PluginState;

typedef enum
{
  PARSE_SEARCH,
  PARSE_EXTRACT,
} ParseState;


typedef struct
{
  unsigned int magic;
  INKVIO output_vio;
  INKIOBuffer output_buffer;
  INKIOBufferReader output_reader;

  INKIOBuffer psi_buffer;
  INKIOBufferReader psi_reader;
  char psi_filename[PSI_FILENAME_MAX_SIZE + 128];
  int psi_filename_len;
  int psi_success;

  ParseState parse_state;

  PluginState state;
  int transform_bytes;
} ContData;


typedef struct
{
  INKCont contp;
  INKEvent event;
} TryLockData;


typedef enum
{
  STR_SUCCESS,
  STR_PARTIAL,
  STR_FAIL
} StrOperationResult;


extern Queue job_queue;

static INKTextLogObject log;
static char psi_directory[PSI_PATH_MAX_SIZE];


static int trylock_handler(INKCont contp, INKEvent event, void *edata);

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

  data = (ContData *) INKmalloc(sizeof(ContData));
  data->magic = MAGIC_ALIVE;
  data->output_vio = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;

  data->psi_buffer = NULL;
  data->psi_reader = NULL;
  data->psi_filename[0] = '\0';
  data->psi_filename_len = 0;
  data->psi_success = 0;

  data->parse_state = PARSE_SEARCH;

  data->state = STATE_READ_DATA;
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
cont_data_destroy(ContData * data)
{
  INKDebug(DBG_TAG, "Destroying continuation data");
  if (data) {
    INKAssert(data->magic == MAGIC_ALIVE);
    if (data->output_reader) {
      INKIOBufferReaderFree(data->output_reader);
      data->output_reader = NULL;
    }
    if (data->output_buffer) {
      INKIOBufferDestroy(data->output_buffer);
      data->output_buffer = NULL;
    }
    if (data->psi_reader) {
      INKIOBufferReaderFree(data->psi_reader);
      data->psi_reader = NULL;
    }
    if (data->psi_buffer) {
      INKIOBufferDestroy(data->psi_buffer);
      data->psi_buffer = NULL;
    }
    data->magic = MAGIC_DEAD;
    INKfree(data);
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
strsearch_ioreader(INKIOBufferReader reader, const char *pattern, int *nparse)
{
  int index = 0;
  INKIOBufferBlock block = INKIOBufferReaderStart(reader);
  int slen = strlen(pattern);

  if (slen <= 0) {
    return STR_FAIL;
  }
  if (block == INK_ERROR_PTR) {
    INKError("[strsearch_ioreader] Error while getting block from ioreader");
    return STR_FAIL;
  }

  *nparse = 0;

  /* Loop thru each block while we've not yet found the pattern */
  while ((block != NULL) && (index < slen)) {
    int blocklen;
    const char *blockptr = INKIOBufferBlockReadStart(block, reader, &blocklen);
    const char *ptr;

    if (blockptr == INK_ERROR_PTR) {
      INKError("[strsearch_ioreader] Error while getting block pointer");
      break;
    }

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
    block = INKIOBufferBlockNext(block);
    if (block == INK_ERROR_PTR) {
      INKError("[strsearch_ioreader] Error while getting block from ioreader");
      return STR_FAIL;
    }
  }

  *nparse -= index;             /* Adjust nparse so it doesn't include matching chars */
  if (index == slen) {
    INKDebug(DBG_TAG, "strfind: match for %s at position %d", pattern, *nparse);
    return STR_SUCCESS;
  } else if (index > 0) {
    INKDebug(DBG_TAG, "strfind: partial match for %s at position %d", pattern, *nparse);
    return STR_PARTIAL;
  } else {
    INKDebug(DBG_TAG, "strfind no match for %s", pattern);
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
strextract_ioreader(INKIOBufferReader reader, int offset, const char *end_pattern, char *buffer, int *buflen)
{
  int buf_idx = 0;
  int p_idx = 0;
  int nbytes_so_far = 0;
  int plen = strlen(end_pattern);
  const char *ptr;
  INKIOBufferBlock block = INKIOBufferReaderStart(reader);

  if (plen <= 0) {
    return STR_FAIL;
  }
  if (block == INK_ERROR_PTR) {
    INKError("[strextract_ioreader] Error while getting block from ioreader");
    return STR_FAIL;
  }

  /* Now start extraction */
  while ((block != NULL) && (p_idx < plen) && (buf_idx < PSI_FILENAME_MAX_SIZE)) {
    int blocklen;
    const char *blockptr = INKIOBufferBlockReadStart(block, reader, &blocklen);

    if (blockptr == INK_ERROR_PTR) {
      INKError("[strsearch_ioreader] Error while getting block pointer");
      break;
    }

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

    block = INKIOBufferBlockNext(block);
    if (block == INK_ERROR_PTR) {
      INKError("[strextract_ioreader] Error while getting block from ioreader");
      return STR_FAIL;
    }
  }

  /* Error, could not read end of filename */
  if (buf_idx >= PSI_FILENAME_MAX_SIZE) {
    INKDebug(DBG_TAG, "strextract: filename too long");
    *buflen = 0;
    return STR_FAIL;
  }

  /* Full Match */
  if (p_idx == plen) {
    /* Nul terminate the filename, remove the end_pattern copied into the buffer */
    *buflen = buf_idx - plen;
    buffer[*buflen] = '\0';
    INKDebug(DBG_TAG, "strextract: filename = |%s|", buffer);
    return STR_SUCCESS;
  }
  /* End of filename not yet reached we need to read some more data */
  else {
    INKDebug(DBG_TAG, "strextract: partially extracted filename");
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
parse_data(INKCont contp, INKIOBufferReader input_reader, int avail, int *toconsume, int *towrite)
{
  ContData *data;
  int nparse = 0;
  int status;

  data = INKContDataGet(contp);
  INKAssert(data->magic == MAGIC_ALIVE);

  if (data->parse_state == PARSE_SEARCH) {

    /* Search for the start pattern */
    status = strsearch_ioreader(input_reader, PSI_START_TAG, &nparse);
    switch (status) {
    case STR_FAIL:
      /* We didn't found the pattern */
      *toconsume = avail;
      *towrite = avail;
      data->parse_state = PARSE_SEARCH;
      return 0;
    case STR_PARTIAL:
      /* We need to read some more data */
      *toconsume = nparse;
      *towrite = nparse;
      data->parse_state = PARSE_SEARCH;
      return 0;
    case STR_SUCCESS:
      /* We found the start_pattern, let's go ahead */
      data->psi_filename_len = 0;
      data->psi_filename[0] = '\0';
      data->parse_state = PARSE_EXTRACT;
      break;
    default:
      INKAssert(!"strsearch_ioreader returned unexpected status");
    }
  }


  /* And now let's extract the filename */
  status = strextract_ioreader(input_reader, nparse + PSI_START_TAG_LEN,
                               PSI_END_TAG, data->psi_filename, &data->psi_filename_len);
  switch (status) {
  case STR_FAIL:
    /* We couldn't extract a valid filename */
    *toconsume = nparse;
    *towrite = nparse;
    data->parse_state = PARSE_SEARCH;
    return 0;
  case STR_PARTIAL:
    /* We need to read some more data */
    *toconsume = nparse;
    *towrite = nparse;
    data->parse_state = PARSE_EXTRACT;
    return 0;
  case STR_SUCCESS:
    /* We got a valid filename */
    *toconsume = nparse + PSI_START_TAG_LEN + data->psi_filename_len + PSI_END_TAG_LEN;
    *towrite = nparse;
    data->parse_state = PARSE_SEARCH;
    return 1;
  default:
    INKAssert(!"strextract_ioreader returned bad status");
  }

  return 0;
}

//TODO: Use libc basename function
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

  while ((cptr = strchr(ptr, (int) '/')) != NULL) {
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
psi_include(INKCont contp, void *edata)
{
#define BUFFER_SIZE 1024
  ContData *data;
  INKFile filep;
  char buf[BUFFER_SIZE];
  char inc_file[PSI_PATH_MAX_SIZE + PSI_FILENAME_MAX_SIZE];
  INKReturnCode retval;

  /* We manipulate plugin continuation data from a separate thread.
     Grab mutex to avoid concurrent access */
  retval = INKMutexLock(INKContMutexGet(contp));
  if (retval != INK_SUCCESS) {
    INKError("[psi_include] Could not lock mutex");
    return 0;
  }

  data = INKContDataGet(contp);
  INKAssert(data->magic == MAGIC_ALIVE);

  if (!data->psi_buffer) {
    data->psi_buffer = INKIOBufferCreate();
    data->psi_reader = INKIOBufferReaderAlloc(data->psi_buffer);

    if ((data->psi_buffer == INK_ERROR_PTR) || (data->psi_reader == INK_ERROR_PTR)) {
      INKError("[psi_include] Could not create iobuffer to store include content");
      goto error;
    }
  }

  /* For security reason, we do not allow to include files that are
     not in the directory <plugin_path>/include.
     Also include file cannot contain any path. */
  sprintf(inc_file, "%s/%s", psi_directory, _basename(data->psi_filename));

  /* Read the include file and copy content into iobuffer */
  if ((filep = INKfopen(inc_file, "r")) != NULL) {
    INKDebug(DBG_TAG, "Reading include file %s", inc_file);

    while (INKfgets(filep, buf, BUFFER_SIZE) != NULL) {
      INKIOBufferBlock block;
      int len, avail, ndone, ntodo, towrite;
      char *ptr_block;

      len = strlen(buf);
      ndone = 0;
      ntodo = len;
      while (ntodo > 0) {
        /* INKIOBufferStart allocates more blocks if required */
        block = INKIOBufferStart(data->psi_buffer);
        if (block == INK_ERROR_PTR) {
          INKError("[psi_include] Could not get buffer block");
          goto error;
        }
        ptr_block = INKIOBufferBlockWriteStart(block, &avail);
        if (ptr_block == INK_ERROR_PTR) {
          INKError("[psi_include] Could not get buffer block");
          goto error;
        }
        towrite = MIN(ntodo, avail);

        memcpy(ptr_block, buf + ndone, towrite);
        retval = INKIOBufferProduce(data->psi_buffer, towrite);
        if (retval == INK_ERROR) {
          INKError("[psi_include] Could not produce data");
          goto error;
        }
        ntodo -= towrite;
        ndone += towrite;
      }
    }
    INKfclose(filep);
    data->psi_success = 1;
    if (log) {
      INKTextLogObjectWrite(log, "Successfully included file: %s", inc_file);
    }
  } else {
    data->psi_success = 0;
    if (log) {
      INKTextLogObjectWrite(log, "Failed to include file: %s", inc_file);
    }
  }

  /* Change state and schedule an event EVENT_IMMEDIATE on the plugin continuation
     to let it know we're done. */

  /* Note: if the blocking call was not in the transformation state (i.e. in
     INK_HTTP_READ_REQUEST_HDR, INK_HTTP_OS_DNS and so on...) we could
     use INKHttpTxnReenable to wake up the transaction instead of sending an event. */

error:
  INKContSchedule(contp, 0);
  data->psi_success = 0;
  data->state = STATE_READ_DATA;
  INKMutexUnlock(INKContMutexGet(contp));
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
wake_up_streams(INKCont contp)
{
  INKVIO input_vio;
  ContData *data;
  int ntodo;
  INKReturnCode retval;

  data = INKContDataGet(contp);
  INKAssert(data->magic == MAGIC_ALIVE);

  input_vio = INKVConnWriteVIOGet(contp);
  if (input_vio == INK_ERROR_PTR) {
    INKError("[wake_up_streams] Error while getting input_vio");
    return 0;
  }

  ntodo = INKVIONTodoGet(input_vio);
  if (ntodo == INK_ERROR) {
    INKError("[wake_up_streams] Error while getting bytes left to read");
    return 0;
  }

  if (ntodo > 0) {
    retval = INKVIOReenable(data->output_vio);
    if (retval == INK_ERROR) {
      INKError("[wake_up_streams] Error while reenabling downstream vio");
      return 0;
    }
    INKContCall(INKVIOContGet(input_vio), INK_EVENT_VCONN_WRITE_READY, input_vio);
  } else {
    INKDebug(DBG_TAG, "Total bytes produced by transform = %d", data->transform_bytes);
    retval = INKVIONBytesSet(data->output_vio, data->transform_bytes);
    if (retval == INK_ERROR) {
      INKError("[wake_up_streams] Error while setting nbytes to downstream vio");
      return 0;
    }
    retval = INKVIOReenable(data->output_vio);
    if (retval == INK_ERROR) {
      INKError("[wake_up_streams] Error while reenabling downstream vio");
      return 0;
    }
    INKContCall(INKVIOContGet(input_vio), INK_EVENT_VCONN_WRITE_COMPLETE, input_vio);
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
handle_transform(INKCont contp)
{
  INKVConn output_conn;
  INKVIO input_vio;
  ContData *data;
  INKIOBufferReader input_reader;
  int toread, avail, psi, toconsume, towrite;
  INKReturnCode retval;

  /* Get the output (downstream) vconnection where we'll write data to. */
  output_conn = INKTransformOutputVConnGet(contp);
  if (output_conn == INK_ERROR_PTR) {
    INKError("[handle_transform] Error while getting transform VC");
    return 1;
  }

  /* Get upstream vio */
  input_vio = INKVConnWriteVIOGet(contp);
  if (input_vio == INK_ERROR_PTR) {
    INKError("[handle_transform] Error while getting input vio");
    return 1;
  }

  data = INKContDataGet(contp);
  INKAssert(data->magic == MAGIC_ALIVE);

  if (!data->output_buffer) {
    data->output_buffer = INKIOBufferCreate();
    data->output_reader = INKIOBufferReaderAlloc(data->output_buffer);

    /* INT_MAX because we don't know yet how much bytes we'll produce */
    data->output_vio = INKVConnWrite(output_conn, contp, data->output_reader, INT_MAX);

    if (data->output_vio == INK_ERROR_PTR) {
      INKError("[handle_transform] Error while writing to downstream VC");
      return 0;
    }
  }

  /* If the input VIO's buffer is NULL, the transformation is over */
  if (!INKVIOBufferGet(input_vio)) {
    INKDebug(DBG_TAG, "input_vio NULL, terminating transformation");
    INKVIONBytesSet(data->output_vio, data->transform_bytes);
    INKVIOReenable(data->output_vio);
    return 1;
  }

  /* Determine how much data we have left to read. */
  toread = INKVIONTodoGet(input_vio);

  if (toread > 0) {
    input_reader = INKVIOReaderGet(input_vio);
    avail = INKIOBufferReaderAvail(input_reader);
    if (avail == INK_ERROR) {
      INKError("[handle_transform] Error while getting number of bytes available");
      return 0;
    }

    /* There are some data available for reading. Let's parse it */
    if (avail > 0) {

      /* No need to parse data if there are too few bytes left to contain
         an include command... */
      if (toread > (PSI_START_TAG_LEN + PSI_END_TAG_LEN)) {
        psi = parse_data(contp, input_reader, avail, &toconsume, &towrite);
      } else {
        towrite = avail;
        toconsume = avail;
        psi = 0;
      }

      if (towrite > 0) {
        /* Update the total size of the doc so far */
        data->transform_bytes += towrite;

        /* Copy the data from the read buffer to the output buffer. */
        retval = INKIOBufferCopy(INKVIOBufferGet(data->output_vio), INKVIOReaderGet(input_vio), towrite, 0);
        if (retval == INK_ERROR) {
          INKError("[handle_transform] Error while copying bytes to output VC");
          return 0;
        }

        /* Reenable the output connection so it can read the data we've produced. */
        retval = INKVIOReenable(data->output_vio);
        if (retval == INK_ERROR) {
          INKError("[handle_transform] Error while reenabling output VC");
          return 0;
        }
      }

      if (toconsume > 0) {
        /* Consume data we've processed an we are no longer interested in */
        retval = INKIOBufferReaderConsume(input_reader, toconsume);
        if (retval == INK_ERROR) {
          INKError("[handle_transform] Error while consuming data from upstream VC");
          return 0;
        }

        /* Modify the input VIO to reflect how much data we've completed. */
        retval = INKVIONDoneSet(input_vio, INKVIONDoneGet(input_vio) + toconsume);
        if (retval == INK_ERROR) {
          INKError("[handle_transform] Error while setting ndone on upstream VC");
          return 0;
        }
      }

      /* Did we find a psi filename to execute in the data ? */
      if (psi) {
        Job *new_job;
        /* Add a request to include a file into the jobs queue.. */
        /* We'll be called back once it's done with an EVENT_IMMEDIATE */
        INKDebug(DBG_TAG, "Psi filename extracted. Adding an include job to thread queue.");
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
dump_psi(INKCont contp)
{
  ContData *data;
  int psi_output_len;
  INKVIO input_vio;
  INKReturnCode retval;

  input_vio = INKVConnWriteVIOGet(contp);
  if (input_vio == INK_ERROR_PTR) {
    INKError("[dump_psi] Error while getting input vio");
    return 1;
  }

  data = INKContDataGet(contp);
  INKAssert(data->magic == MAGIC_ALIVE);

  /* If script exec succeded, copy its output to the downstream vconn */
  if (data->psi_success == 1) {
    psi_output_len = INKIOBufferReaderAvail(data->psi_reader);
    if (psi_output_len == INK_ERROR) {
      INKError("[dump_psi] Error while getting available bytes from reader");
      return 1;
    }

    if (psi_output_len > 0) {
      data->transform_bytes += psi_output_len;

      INKDebug(DBG_TAG, "Inserting %d bytes from include file", psi_output_len);
      retval = INKIOBufferCopy(INKVIOBufferGet(data->output_vio), data->psi_reader, psi_output_len, 0);
      if (retval == INK_ERROR) {
        INKError("[dump_psi] Error while copying include bytes to downstream VC");
        return 1;
      }

      /* Consume all the output data */
      retval = INKIOBufferReaderConsume(data->psi_reader, psi_output_len);
      if (retval == INK_ERROR) {
        INKError("[dump_psi] Error while consuming data from buffer");
        return 1;
      }

      /* Reenable the output connection so it can read the data we've produced. */
      retval = INKVIOReenable(data->output_vio);
      if (retval == INK_ERROR) {
        INKError("[dump_psi] Error while reenabling output VIO");
        return 1;
      }
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
transform_handler(INKCont contp, INKEvent event, void *edata)
{
  INKVIO input_vio;
  ContData *data;
  int state, lock, retval;

  /* This section will be called by both TS internal
     and the thread. Protect it with a mutex to avoid
     concurrent calls. */
  lock = INKMutexTryLock(INKContMutexGet(contp));

  /* Handle TryLock result */
  if (!lock) {
    INKCont c = INKContCreate(trylock_handler, NULL);
    TryLockData *d = INKmalloc(sizeof(TryLockData));
    d->contp = contp;
    d->event = event;
    INKContDataSet(c, d);
    INKContSchedule(c, 10);
    return 1;
  }

  data = INKContDataGet(contp);
  INKAssert(data->magic == MAGIC_ALIVE);

  state = data->state;

  /* Check to see if the transformation has been closed */
  retval = INKVConnClosedGet(contp);
  if (retval == INK_ERROR) {
    INKError("[transform_handler] Error while getting close status of transformation");
  }
  if (retval) {
    /* If the thread is still executing its job, we don't want to destroy
       the continuation right away as the thread will call us back
       on this continuation. */
    if (state == STATE_READ_PSI) {
      INKContSchedule(contp, 10);
    } else {
      INKMutexUnlock(INKContMutexGet(contp));
      cont_data_destroy(INKContDataGet(contp));
      INKContDestroy(contp);
      return 1;
    }
  } else {
    switch (event) {
    case INK_EVENT_ERROR:
      input_vio = INKVConnWriteVIOGet(contp);
      if (input_vio == INK_ERROR_PTR) {
        INKError("[transform_handler] Error while getting upstream vio");
      } else {
        INKContCall(INKVIOContGet(input_vio), INK_EVENT_ERROR, input_vio);
      }
      break;

    case INK_EVENT_VCONN_WRITE_COMPLETE:
      INKVConnShutdown(INKTransformOutputVConnGet(contp), 0, 1);
      break;

    case INK_EVENT_VCONN_WRITE_READY:
      /* downstream vconnection is done reading data we've write into it.
         let's read some more data from upstream if we're in read state. */
      if (state == STATE_READ_DATA) {
        handle_transform(contp);
      }
      break;

    case INK_EVENT_IMMEDIATE:
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
      INKAssert(!"Unexpected event");
      break;
    }
  }

  INKMutexUnlock(INKContMutexGet(contp));
  return 1;
}

/*-------------------------------------------------------------------------
  trylock_handler
  Small handler to handle INKMutexTryLock failures

  Input:
    contp      continuation for the current transaction
    event      event received
    data       pointer on optional data
  Output :
  Return Value:
  -------------------------------------------------------------------------*/
static int
trylock_handler(INKCont contp, INKEvent event, void *edata)
{
  TryLockData *data = INKContDataGet(contp);
  transform_handler(data->contp, data->event, NULL);
  INKfree(data);
  INKContDestroy(contp);
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
transformable(INKHttpTxn txnp)
{
  /*  We are only interested in transforming "200 OK" responses
     with a Content-Type: text/ header and with X-Psi header */
  INKMBuffer bufp;
  INKMLoc hdr_loc, field_loc;
  INKHttpStatus resp_status;
  const char *value;

  INKHttpTxnServerRespGet(txnp, &bufp, &hdr_loc);

  resp_status = INKHttpHdrStatusGet(bufp, hdr_loc);
  if (resp_status == (INKHttpStatus)INK_ERROR) {
    INKError("[transformable] Error while getting http status");
  }
  if ((resp_status == (INKHttpStatus)INK_ERROR) || (resp_status != INK_HTTP_STATUS_OK)) {
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    return 0;
  }

  field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, INK_MIME_FIELD_CONTENT_TYPE, -1);
  if (field_loc == INK_ERROR_PTR) {
    INKError("[transformable] Error while searching Content-Type field");
  }
  if ((field_loc == INK_ERROR_PTR) || (field_loc == NULL)) {
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    return 0;
  }

  value = INKMimeHdrFieldValueGet(bufp, hdr_loc, field_loc, 0, NULL);
  if (value == INK_ERROR_PTR) {
    INKError("[transformable] Error while getting Content-Type field value");
  }
  if ((value == INK_ERROR_PTR) || (value == NULL) || (strncasecmp(value, "text/", sizeof("text/") - 1) != 0)) {
    INKHandleMLocRelease(bufp, hdr_loc, field_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    return 0;
  }

  INKHandleMLocRelease(bufp, hdr_loc, field_loc);

  field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, MIME_FIELD_XPSI, -1);
  if (value == INK_ERROR_PTR) {
    INKError("[transformable] Error while searching XPSI field");
  }
  if ((value == INK_ERROR_PTR) || (field_loc == NULL)) {
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    return 0;
  }

  INKHandleMLocRelease(bufp, hdr_loc, field_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

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
transform_add(INKHttpTxn txnp)
{
  INKCont contp;
  ContData *data;
  INKReturnCode retval;

  contp = INKTransformCreate(transform_handler, txnp);
  if (contp == INK_ERROR_PTR) {
    INKError("[transform_add] Error while creating a new transformation");
    return 0;
  }

  data = cont_data_alloc();
  INKContDataSet(contp, data);

  retval = INKHttpTxnHookAdd(txnp, INK_HTTP_RESPONSE_TRANSFORM_HOOK, contp);
  if (retval == INK_ERROR) {
    INKError("[transform_add] Error registering to transform hook");
    return 0;
  }
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
read_response_handler(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    if (transformable(txnp)) {
      INKDebug(DBG_TAG, "Add a transformation");
      transform_add(txnp);
    }
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    return 0;
  default:
    break;
  }

  return 0;
}


/*-------------------------------------------------------------------------
  check_ts_version
  Make sure TS version is at least 2.0

  Input:
  Output :
  Return Value:
    0  if error
    1  if success
  -------------------------------------------------------------------------*/
int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 2.0 */
    if (major_ts_version >= 2) {
      result = 1;
    }
  }

  return result;
}


/*-------------------------------------------------------------------------
  INKPluginInit
  Function called at plugin init time

  Input:
    argc  number of args
    argv  list vof args
  Output :
  Return Value:
  -------------------------------------------------------------------------*/
void
INKPluginInit(int argc, const char *argv[])
{
  INKPluginRegistrationInfo info;
  int i;
  INKReturnCode retval;

  info.plugin_name = "psi";
  info.vendor_name = "Apache";
  info.support_email = "";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 2.0 or later\n");
    return;
  }

  /* Initialize the psi directory = <plugin_path>/include */
  sprintf(psi_directory, "%s/%s", INKPluginDirGet(), PSI_PATH);

  /* create an INKTextLogObject to log any psi include */
  retval = INKTextLogObjectCreate("psi", INK_LOG_MODE_ADD_TIMESTAMP, &log);
  if (retval == INK_ERROR) {
    INKError("Failed creating log for psi plugin");
    log = NULL;
  }

  /* Create working threads */
  thread_init();
  init_queue(&job_queue);

  for (i = 0; i < NB_THREADS; i++) {
    char *thread_name = (char *) INKmalloc(64);
    sprintf(thread_name, "Thread[%d]", i);
    if (!INKThreadCreate((INKThreadFunc) thread_loop, thread_name)) {
      INKError("[INKPluginInit] Error while creating threads");
      return;
    }
  }

  retval = INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, INKContCreate(read_response_handler, INKMutexCreate()));
  if (retval == INK_ERROR) {
    INKError("[INKPluginInit] Error while registering to read response hook");
    return;
  }

  INKDebug(DBG_TAG, "Plugin started");
}
