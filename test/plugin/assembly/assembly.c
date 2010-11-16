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
 * assembly.c: plugin to dynamically assemble pages.
 *
 *
 *
 *	Usage:
 *	  assembly.so <cache_port>
 *
 *
 *
 *
 */

/*

  PAGE TEMPLATE SYNTAX
  ---------------------

  Note: the TEMPLATE tag is not yet implemented ! Only the DYNAMIC one is....

  <TEMPLATE>
      TEMPLATENAME=String         // Unique identifier for the template


      CACHEABLE="true"|"false"    // Is this template cacheable ?
                                  // If not, template will be fetch from OS
				  // for every request.

      ASMCACHEABLE="true"|"false" // Should we cache page assembled from this template ?
                                  // Note: If CACHEABLE is set to false, ASMCACHEABLE
				  // can not be set to true. It doesn't make sense
				  // to cache assembled page from a template that changes
				  // for every request.

  </TEMPLATE>



  <DYNAMIC>
      BLOCKNAME=String            // Unique identifier for the block.
                                  // Used as part of the cache key

      CACHEABLE="true"|"false"    // Should we cache this block ? Default is false

      TTL=Integer                 // Time To Live for the block in cache (in sec)

      URL=String                  // Url to use to fetch the block
                                  // ${QUERYSTRING} is substituted client request
				  // query string

      CACHESIZE=Integer           // Max number of different version of this
                                  // block stored in the cache

      KEY                         // Parameters, headers and cookies the block depends on
      [Optional]

          QUERY=String            // List of query string parameters separated by commas
	  [Optional]              // NO = No dependency on query string parameters
				  // ALL= Dependency on all query string parameters

	  COOKIES=String          // List of cookie names separated by commas
	  [Optional]              // NO = No dependency on cookies
				  // ALL= Dependency on all cookies

	  HEADERS=String          // List of header names separated by commas
	  [Optional]

  </DYNAMIC>

Example:
  <HTML>
  <TEMPLATE>
      TEMPLATENAME=MyTemplate
      CACHEABLE=true
      ASMCACHEABLE=true

  </TEMPLATE>
  <HEAD>
  .
  .
  </HEAD>
  <BODY>
  .
  .
  HTML code
  .
  .
  <DYNAMIC>
      BLOCKNAME=News
      CACHEABLE=true
      TTL=3600
      URL=http://www.franckc.com/blocks/News?${QUERYSTRING}
      CACHESIZE=1000
      KEY
          QUERY=${ALL}
	  COOKIES=Location
	  HEADERS=MyHeader
  </DYNAMIC>
  .
  .
  HTML code
  .
  .
  </BODY>


   TO BE DONE:
   -----------

   - Cache the template page
       Right now, we cache the template without the query string in the URL and
       after having appended ".template"
       i.e. A template http://www.foo.com/cgi/main.htm is cached under
                       http://www.foo.com/cgi/main.htm.template
       As it is not possible to set the cached URL in the READ_RESPONSE hook,
       we have to do that in READ_REQUEST.
       Then in READ_RESPONSE, based on whether or not the X-Template header is
       present, we decide to cache the document.

       Drawback: we loose capability of caching URLs that look dynamic
       => need to change that in the final release of dynamic caching

   - Cache the assembled page
       To be done in the final release. The assembled page
       should be cached in the dynamic partition of the cache.

       Make sure it is really realistic to cache assembled page:
       Potentially, the # of assembled page can be huge.
       What is the chance to hit an assembled page ?
       The assembled page key in the cache should take into
       account all the query params, cookies and header used by blocks in the template.

       Another fact is that if the template is not cacheable, then
       there is no point in caching the assembled page.


   - Invalidate blocks when template changes ?
       - the block meta-data are embedded in the template page.
         A change in template can mean a change in its embedded blocks meta-data.

	 Would the solution be to have each block carrying its own meta-data ?
	 Is it doable for the OS to put that meta-data into blocks ?

   - Limit the # of blocks in the cache
       - have a partition dedicated to dynamic caching
       - if # of blocks > threshold, scan the partition and
         remove old/unused blocks.

   - Mutex use for calls to cache API ?

   - Is it possible to associate data to a transaction ? i.e. TSContDataSet(txnp)

   - Rather than the query ALL, specify which query param to send when requesting blocks ?
     i.e. QUERY=ParamName1,ParamName2

   - Send cookies to server when requesting blocks ?


  PROTOTYPE KNOWN BUGS / LIMITATIONS
  ----------------------------------

   - To be investigated: it seems that for pages that are dynamic, we request
     page.template to the OS ??

   - IMS/Browser Reload
     If the OS response doesn't contain Last-Modified date, when sending a
     GET page with header Pragma: no-cache, the CACHED URL is used to send
     the request. i.e. GET page.template
     This happens with browser reload.

   - In case of a browser reload, the request contains Pragma: no-cache
     If doc is in the cache and fresh, we setup the transform.
     But then request is sent to OS. If document sent back has changed
     and is no longer a template we try to tranform it.


 */



#include <stdio.h>
#include <limits.h>
#include <sys/ddi.h>
#include <time.h>


#if !defined (_WIN32)
#  include <netinet/in.h>
#else
#  include <windows.h>
#endif

#include <string.h>

#include "ts.h"

#include "common.h"
#include "list.h"
#include "headers.h"


#define LOCK_CONT_MUTEX(_c) TSMutexLock(TSContMutexGet(_c))

#define UNLOCK_CONT_MUTEX(_c) TSMutexUnlock(TSContMutexGet(_c))



#if !defined (_WIN32)
static in_addr_t server_ip;
#else
static unsigned int server_ip;
#endif

static int server_port;


/* Function prototypes */
static int asm_input_buffer(TSCont contp, AsmData * data);

static int asm_process_dynamic(TSCont contp, AsmData * data, const char *include_buffer);

static int asm_parse_input_buffer(TSCont contp, AsmData * data);

static int asm_parse_input_buffer_init(TSCont contp, AsmData * data);

static int asm_ts_connect(TSCont contp, AsmData * data);

static int asm_ts_write(TSCont contp, AsmData * data);

static int asm_ts_read(TSCont contp, AsmData * data);

static int asm_block_bypass(TSCont contp, AsmData * data);

static int asm_output_buffer(TSCont contp, AsmData * data);

static void asm_transform_destroy(TSCont contp);


/*-------------------------------------------------------------------------
  strstr_block

  Search for a null terminated string in a non null
  terminated block of known size
  -------------------------------------------------------------------------*/
static char *
strstr_block(const char *block, const char *str, int blocklen)
{
  int i = 0;
  int j = 0;
  char c = str[j];

  do {
    if (block[i] == str[j]) {
      j++;
    } else {
      j = 0;
    }
  } while ((++i < blocklen) && (str[j] != '\0'));

  return ((str[j] == '\0') ? (char *) (block + (i - j)) : (char *) NULL);
}


/*-------------------------------------------------------------------------
  print_iobuffer

  Prints out an iobuffer on the standard output
  -------------------------------------------------------------------------*/
static void
print_iobuffer(TSIOBuffer buf)
{
  TSIOBufferReader printreader = TSIOBufferReaderAlloc(buf);
  TSIOBufferBlock blkp = TSIOBufferReaderStart(printreader);

  printf("Buffer (%d chars)=\n", TSIOBufferReaderAvail(printreader));
  while (blkp != NULL) {
    int l;
    const char *b = TSIOBufferBlockReadStart(blkp, printreader, &l);
    int i;
    for (i = 0; i < l; i++) {
      printf("%c", b[i]);
    }
    blkp = TSIOBufferBlockNext(blkp);
  }
  printf("Buffer End\n");
  TSIOBufferReaderFree(printreader);
}


/*-------------------------------------------------------------------------
  write_iobuffer

  Appends len bytes from buf to the IOBuffer output
  -------------------------------------------------------------------------*/
static void
write_iobuffer(char *buf, int len, TSIOBuffer output)
{
  TSIOBufferBlock block;
  char *ptr_block;
  int ndone, ntodo, towrite, avail;

  ndone = 0;
  ntodo = len;
  while (ntodo > 0) {
    block = TSIOBufferStart(output);
    ptr_block = TSIOBufferBlockWriteStart(block, &avail);
    towrite = min(ntodo, avail);
    memcpy(ptr_block, buf + ndone, towrite);
    TSIOBufferProduce(output, towrite);
    ntodo -= towrite;
    ndone += towrite;
  }
}


/*-------------------------------------------------------------------------
  writec_iobuffer

  Appends on character to the IOBuffer output

  OPTIMIZATION: write a bufferized version of this routine
  -------------------------------------------------------------------------*/
static void
writec_iobuffer(char c, TSIOBuffer output)
{
  char buf[1];
  buf[0] = c;
  write_iobuffer(buf, 1, output);
}


/*-------------------------------------------------------------------------
  strfind_ioreader

  Looks for str in reader. Returns offset of str or -1 if not found.
  No data is consumed from the reader.
  -------------------------------------------------------------------------*/
static int
strfind_ioreader(TSIOBufferReader reader, const char *str)
{
  char *ptr = NULL;
  int pos = 0;                  /* position in the buffer where we'll start looking for str */

  /* Used to search for the string in between 2 blocks
     contains end of previous block and beginning of the current */
  char window[CHARS_WINDOW_SIZE];

  TSIOBufferBlock block = TSIOBufferReaderStart(reader);
  int slen = strlen(str);

  if (slen <= 0) {
    return -1;
  }

  window[0] = '\0';

  /* Loop thru each block */
  while (block != NULL) {
    int blocklen;
    const char *blockstr = TSIOBufferBlockReadStart(block, reader, &blocklen);

    if (window[0] != '\0') {
      /* copy the beginning of the block at the end of the window */
      int len = min(slen - 1, blocklen);
      memcpy((char *) (window + slen - 1), blockstr, len);
      window[len] = '\0';
      /* search for the string in between 2 blocks */
      ptr = strstr(window, str);
      if (ptr != NULL) {
        return (pos + (int) (ptr - window));
      }
    }

    /* search for the string in this block */
    ptr = strstr_block(blockstr, str, blocklen);
    if (ptr) {
      return (pos + (int) (ptr - blockstr));
    } else {
      /* copy the end of the block at the beginning of the window */
      int len = min(slen - 1, blocklen);
      memcpy(window, (char *) blockstr + blocklen - len, len);
    }

    /* Parse next block */
    pos += blocklen;
    block = TSIOBufferBlockNext(block);
  }

  return -1;
}


/*-------------------------------------------------------------------------
  strfind_iobuffer

  Looks for str in buffer. Returns offset of str or -1 if not found.
  start specifies the offset in buffer where the search should begin.
  -------------------------------------------------------------------------*/
static int
strfind_iobuffer(TSIOBuffer buffer, const char *str, int start)
{
  TSIOBufferReader reader = TSIOBufferReaderAlloc(buffer);
  int val;

  TSIOBufferReaderConsume(reader, start);
  val = strfind_ioreader(reader, str);
  TSIOBufferReaderFree(reader);
  return val;
}


/*-------------------------------------------------------------------------
  read_block_metadata

  read block meta data from an iobuffer read from cache
  Returns 0 if ok, -1 if error
  -------------------------------------------------------------------------*/
static int
read_block_metadata(TSIOBuffer buffer, BlockMetaData * meta)
{
  int ndone = 0;
  int ntodo = sizeof(BlockMetaData);
  TSIOBufferReader reader = TSIOBufferReaderAlloc(buffer);
  TSIOBufferBlock blkp = TSIOBufferReaderStart(reader);

  TSAssert(blkp);

  while ((ndone != ntodo) && (blkp != NULL)) {
    int len;
    const char *ptr = TSIOBufferBlockReadStart(blkp, reader, &len);
    int to_copy = min(ntodo, len);
    memcpy((char *) (meta + ndone), ptr, to_copy);
    ndone += to_copy;
    blkp = TSIOBufferBlockNext(blkp);
  }
  TSIOBufferReaderFree(reader);

  /* FIXME: THIS ASSERT FIRES !!!! */
  /* TSAssert(ndone != ntodo); */

  if ((ndone != ntodo) || (meta->template_id != TEMPLATE_ID)) {
    TSError("Error while reading meta data from cache");
  }

  if (ndone != ntodo) {
    /* Somehow the buffer didn't contain enough data... */
    return -2;
  }

  /* FIX ME: WE READ CORRUPTED DATA !! */
  /* TSAssert(meta->template_id == TEMPLATE_ID); */
  if (meta->template_id != TEMPLATE_ID) {
    /* This is a more serious error, block seems to be corrupted */
    return -1;
  }

  TSDebug(LOW, "Meta Data: write_time=%ld template_id=%d", meta->write_time, meta->template_id);
  return 0;
}


/*-------------------------------------------------------------------------
  block_is_fresh

  Use block metadata and ttl to determine if a block is fresh or not.
  Return 1 if block is fresh, 0 otherwise
  -------------------------------------------------------------------------*/
static int
block_is_fresh(const AsmData * data)
{
  time_t current_time, block_age;

  TSAssert(data->magic == MAGIC_ALIVE);

  time(&current_time);
  block_age = current_time - data->block_metadata.write_time;

  TSDebug(HIGH, "Block age = %ld, TTL = %d, fresh = %d", block_age, data->block_ttl,
           (block_age <= data->block_ttl) ? 1 : 0);

  return ((block_age <= data->block_ttl) ? 1 : 0);
}


/*-------------------------------------------------------------------------
  extract_attribute

  Returns value of attribute from a null terminated buffer of characters.
  Attribute syntax: name=value.
  Attribute value ends with ' ' or '\n' or '\t'
  The caller should deallocate string returned by calling TSfree
  -------------------------------------------------------------------------*/
static char *
extract_attribute(const char *include_buffer, const char *attribute)
{
  char *ptr_start;
  char *ptr_stop;
  char *value;
  int len;

  /* Search for attribute name in buffer */
  ptr_start = strstr(include_buffer, attribute);
  if (ptr_start == NULL) {
    TSDebug(LOW, "Could not extract attribute value");
    return NULL;
  }

  /* skip attribute name and char '=' */
  ptr_start += strlen(attribute) + 1;
  ptr_stop = ptr_start;

  while ((*ptr_stop != '\0') && (*ptr_stop != ' ')
         && (*ptr_stop != '\n') && (*ptr_stop != '\t')) {
    ptr_stop++;
  }

  len = ptr_stop - ptr_start;
  value = (char *) TSmalloc(len + 1);
  strncpy(value, ptr_start, len);
  value[len] = '\0';

  TSDebug(LOW, "Extracted value |%s| for attribute |%s|", value, attribute);

  return value;
}


/*-------------------------------------------------------------------------
  asm_destroy_data_block

  Destroy/free any data related to the current block.
  To be called once we're done processing a block and we're ready
  processing the next one.
  -------------------------------------------------------------------------*/
static void
asm_destroy_data_block(AsmData * data)
{
  TSAssert(data->magic == MAGIC_ALIVE);

  data->cache_read_retry_counter = 0;

  if (data->block_url) {
    TSfree(data->block_url);
    data->block_url = NULL;
  }

  if (data->block_key) {
    TSCacheKeyDestroy(data->block_key);
    data->block_key = NULL;
  }
}


/*-------------------------------------------------------------------------
  asm_input_buffer

  Bufferize response from the upstream vconnnection (either OS or cache).
  This response is the master document that may contains the include tags.
  Data is bufferized in input_buffer.
  Once all data is read, calls asm_parse_input_buffer_init.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_input_buffer(TSCont contp, AsmData * data)
{
  TSVIO write_vio;
  int towrite;
  int avail;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_input_buffer");

  /* If it's the first time the function is called, we need to create
     the input buffer. */
  if (!data->input_buffer) {
    /* Create the buffer into which the upstream connection will
     * write data on it.
     */
    data->input_buffer = TSIOBufferCreate();
    data->input_parse_reader = TSIOBufferReaderAlloc(data->input_buffer);
  }

  /* Get our parent write io = our input source
     Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = TSVConnWriteVIOGet(contp);

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this buffered
     transformation that means we're done buffering data. */
  if (!TSVIOBufferGet(write_vio)) {
    return asm_parse_input_buffer_init(contp, data);
  }

  /* Determine how much data we have left to read. For this server
     transform plugin this is also the amount of data we have left
     to write to the output connection. */
  towrite = TSVIONTodoGet(write_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the input buffer. */
      TSIOBufferCopy(data->input_buffer, TSVIOReaderGet(write_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), towrite);

      /* Modify the write VIO to reflect how much data we've
         completed. */
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
    }
  }

  /* Now we check the write VIO to see if there is data left to
     read. */
  if (TSVIONTodoGet(write_vio) > 0) {
    /* Call back the write VIO continuation to let it know that we
       are ready for more data. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
  } else {
    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);

    return asm_parse_input_buffer_init(contp, data);
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_parse_input_buffer_init

  Routine to initialize the processing of the master document.
  Called only once.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_parse_input_buffer_init(TSCont contp, AsmData * data)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_parse_input_buffer_init");

  /* Create the output buffer that will be sent to user agent */
  data->output_buffer = TSIOBufferCreate();
  data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);

  if (TSIsDebugTagSet(LOW)) {
    print_iobuffer(data->input_buffer);
  }

  /* Create a reader to scan the input buffer */
  data->input_parse_reader = TSIOBufferReaderAlloc(data->input_buffer);

  /* Start parsing the input buffer */
  return asm_parse_input_buffer(contp, data);
}


/*-------------------------------------------------------------------------
  asm_parse_input_buffer

  Parses the master document to extract DYNAMIC statements.
   - Data no part of DYNAMIC are appended to the output buffer.
   - DYNAMIC statements are extracted and passed to the
     asm_process_dynamic routine.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_parse_input_buffer(TSCont contp, AsmData * data)
{
  char include_buffer[DYN_TAG_MAX_SIZE];        /* To store the include statement */
  int include_len, nread, offset_start, offset_end, nbytes;
  TSIOBufferBlock block;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_parse_input_buffer");

  data->state = STATE_PARSE_BUFFER;

  /* Search for DYNAMIC tags */
  offset_start = strfind_ioreader(data->input_parse_reader, DYNAMIC_START);
  if (offset_start >= 0) {
    offset_end = strfind_ioreader(data->input_parse_reader, DYNAMIC_END);
  }

  if ((offset_start < 0) || (offset_end < 0)) {
    /* No DYNAMIC tags found */
    TSDebug(LOW, "No DYNAMIC tags");
    nbytes = TSIOBufferReaderAvail(data->input_parse_reader);
    TSIOBufferCopy(data->output_buffer, data->input_parse_reader, nbytes, 0);
  } else {
    TSDebug(LOW, "DYNAMIC tag offsets: start=%d, end=%d", offset_start, offset_end);

    /* Copy data that is before the DYNAMIC tag to the output buffer */
    TSIOBufferCopy(data->output_buffer, data->input_parse_reader, offset_start, 0);
    TSIOBufferReaderConsume(data->input_parse_reader, offset_start);

    /* Now extract the DYNAMIC statement */
    block = TSIOBufferReaderStart(data->input_parse_reader);
    include_len = offset_end + strlen(DYNAMIC_END) - offset_start;
    nread = 0;

    while ((block != NULL) && (nread < include_len)) {
      int blocklen;
      const char *blockstr = TSIOBufferBlockReadStart(block, data->input_parse_reader, &blocklen);
      int toread = min(include_len - nread, blocklen);

      memcpy((char *) (include_buffer + nread), blockstr, toread);
      nread += toread;

      block = TSIOBufferBlockNext(block);
    }

    /* Process the DYNAMIC statement */
    TSIOBufferReaderConsume(data->input_parse_reader, include_len);
    include_buffer[include_len] = '\0';
    TSDebug(LOW, "DYNAMIC statement |%s|", include_buffer);

    return asm_process_dynamic(contp, data, include_buffer);
  }

  TSIOBufferReaderFree(data->input_parse_reader);

  /* We're done assembling the page. Now send the data to the user agent */
  return asm_output_buffer(contp, data);
}


/*-------------------------------------------------------------------------
  asm_compute_block_key

  Computes a key to lookup a block in the cache

  Key is built using the following scheme:
    block_name/q1name=q1value/.../qNname=qNvalue/c1name=c1value/.../cNname=cNvalue

    qXname  = query parameter number X from the block's query parameter vary list
    qXvalue = value of query param X in the client's header request
    cXname  = cookie number X from the block's cookie vary list
    cXvalue = value of cookie X in the client's header request

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_compute_block_key(TSCont contp, AsmData * data, const char *block_name,
                      const char *query_list, const char *cookies_list)
{
#define KEY_SIZE_INIT      1024
#define KEY_SIZE_INCR      128

  char *key_value;
  char *name;
  const char *value;
  char *offset;
  int size = KEY_SIZE_INIT;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_compute_block_key");

  /* It's difficult to guess the key size.
     The cookie spec doesn't give a limit size for a cookie value.
     Hopefully, we shouldn't have huge cookies. */
  key_value = TSmalloc(KEY_SIZE_INIT);

  /* First prepend block name */
  sprintf(key_value, "%s", block_name);

  /* Then append query param pairs */
  offset = (char *) query_list;
  while ((name = getNextValue(query_list, &offset)) != NULL) {
    TSDebug(LOW, "searching value for query param |%s|", name);

    if ((value = pairListGetValue(&data->query, name)) != NULL) {
      while ((strlen(key_value) + 1 + strlen(name) + 1 + strlen(value) + 1) > size) {
        size += KEY_SIZE_INCR;
        key_value = TSrealloc(key_value, size);
      }
      sprintf(key_value, "%s/%s=%s", key_value, name, value);
    }
    TSfree(name);
  }

  /* Then append cookies pairs */
  offset = (char *) cookies_list;
  while ((name = getNextValue(cookies_list, &offset)) != NULL) {
    TSDebug(LOW, "searching value for cookie |%s|", name);

    if ((value = pairListGetValue(&data->cookies, name)) != NULL) {
      while ((strlen(key_value) + 1 + strlen(name) + 1 + strlen(value) + 1) > size) {
        size += KEY_SIZE_INCR;
        key_value = TSrealloc(key_value, size);
      }
      sprintf(key_value, "%s/%s=%s", key_value, name, value);
    }
    TSfree(name);
  }

  TSDebug(LOW, "Key value = |%s|", key_value);

  TSCacheKeyCreate(&(data->block_key));

  TSCacheKeyDigestSet(data->block_key, (const unsigned char *) key_value, strlen(key_value));

  /* Set the TTL */
  /* BUUUUUUUUUUUUUUUUUUUUUUUUGGGGGGGGGGGGGGGGGG !!!!!!!!!!!!!!!!! */
  /* DOOOOOOOO NOOOOOOOOOOOT UUUUUUUUUUUUUSSSSSSSSSSSEEEEEEEEEE */
  /* TSCacheKeySetPinned(data->block_key, data->block_ttl); */

  TSfree(key_value);

  return 0;
}


/*-------------------------------------------------------------------------
  asm_process_dynamic

  Processes a dynamic statement.
    - parses it
    - extracts the block name
    - extracts the URL of block to be included.
    - extract the param CACHEABLE
    - extracts TTL of block, if any
    - extracts vary query params/cookies
    - computes the block cache key

  Next state: asm_cache_prepare_read or asm_ts_connect (based on the cacheability)

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_process_dynamic(TSCont contp, AsmData * data, const char *statement_buffer)
{
  char *block_name;
  char *ttl;
  char *cacheable;
  char *query_list;
  char *cookies_list;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_process_dynamic");

  /* if we're called more than once, need to do some housekeeping
     before processing a new statement */
  asm_destroy_data_block(data);


  /* Extract block name */
  block_name = extract_attribute(statement_buffer, DYNAMIC_ATTR_BLOCKNAME);

  /* Extract URL */
  data->block_url = extract_attribute(statement_buffer, DYNAMIC_ATTR_URL);

  /* If error skip this dynamic statement */
  if (data->block_url == NULL) {
    TSError("Unable to extract attribute %s in statement %s. Skipping it.", DYNAMIC_ATTR_URL, statement_buffer);
    return asm_parse_input_buffer(contp, data);
  }
  TSDebug(MED, "URL of block to fetch = |%s|", data->block_url);

  /* Extract the cacheable boolean */
  cacheable = extract_attribute(statement_buffer, DYNAMIC_ATTR_CACHEABLE);
  if (cacheable == NULL) {
    TSDebug(LOW, "block %s has no CACHEABLE tag. Using default value %d",
             data->block_url, DYNAMIC_ATTR_CACHEABLE_DEFAULT_VALUE);
    data->block_is_cacheable = DYNAMIC_ATTR_CACHEABLE_DEFAULT_VALUE;
  } else {
    if (strcasecmp(cacheable, DYNAMIC_ATTR_CACHEABLE_VALUE_TRUE) == 0) {
      data->block_is_cacheable = 1;
    } else {
      data->block_is_cacheable = 0;
    }
    TSDebug(LOW, "CACHEABLE = %d for block %s", data->block_is_cacheable, data->block_url);
  }

  /* If block is NOT cacheable, no need to extract the rest of parameters.
     Let's jump to the state where we fetch the block from the OS */
  if (!data->block_is_cacheable) {
    if (block_name != NULL) {
      TSfree(block_name);
    }
    if (cacheable != NULL) {
      TSfree(cacheable);
    }
    return asm_ts_connect(contp, data);
  }

  /* Extract ttl parameter */
  ttl = extract_attribute(statement_buffer, DYNAMIC_ATTR_TTL);
  if (ttl == NULL) {
    TSDebug(LOW, "block %s has no TTL specified. Using default value %d",
             data->block_url, DYNAMIC_ATTR_TTL_DEFAULT_VALUE);
    data->block_ttl = DYNAMIC_ATTR_TTL_DEFAULT_VALUE;
  } else {
    data->block_ttl = atoi(ttl);
    TSDebug(LOW, "TTL is %d for block %s", data->block_ttl, data->block_url);
  }

  /* Extract vary query parameter names and cookie names */
  query_list = extract_attribute(statement_buffer, DYNAMIC_ATTR_QUERY);
  cookies_list = extract_attribute(statement_buffer, DYNAMIC_ATTR_COOKIES);
  TSDebug(LOW, "Vary on query: |%s|", query_list);
  TSDebug(LOW, "Vary on cookies: |%s|", cookies_list);

  /* Compute the key based on query and cookies values */
  asm_compute_block_key(contp, data, block_name, query_list, cookies_list);

  if (block_name) {
    TSfree(block_name);
  }
  if (cacheable) {
    TSfree(cacheable);
  }
  if (ttl) {
    TSfree(ttl);
  }
  if (query_list) {
    TSfree(query_list);
  }
  if (cookies_list) {
    TSfree(cookies_list);
  }

  /* Now do a cache lookup on the block */
  return asm_cache_prepare_read(contp, data);
}


/*-------------------------------------------------------------------------
  asm_cache_prepare_read

  Tries to read the current block from the cache

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_prepare_read(TSCont contp, AsmData * data)
{
  TSAction action;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_prepare_read");

  data->state = STATE_CACHE_PREPARE_READ;

  action = TSCacheRead(contp, data->block_key);
  if (!TSActionDone(action)) {
    TSDebug(LOW, "CacheRead action not completed...");
    data->pending_action = action;
  } else {
    TSDebug(LOW, "CacheRead action completed");
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_cache_retry_read

  Schedule another cache_prepare_read

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_retry_read(TSCont contp, AsmData * data)
{
  TSAction action;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_retry_read");

  data->state = STATE_CACHE_RETRY_READ;

  action = TSContSchedule(contp, CACHE_READ_RETRY_DELAY);
  if (!TSActionDone(action)) {
    TSDebug(LOW, "ContSchedule action not completed...");
    data->pending_action = action;
  } else {
    TSDebug(LOW, "ContSchedule action completed");
    TSAssert(!"Schedule should not call us right away");
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_cache_read

  Start reading the current block from the cache

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_read(TSCont contp, AsmData * data)
{
  int cache_obj_size = 0;
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_read");

  data->state = STATE_CACHE_READ;

  /* Create the IOBuffer and Reader to read block from the cache */
  data->cache_read_buffer = TSIOBufferCreate();
  data->cache_read_reader = TSIOBufferReaderAlloc(data->cache_read_buffer);

  /* Create IOBUffer and Reader to bufferize the block */
  data->block_buffer = TSIOBufferCreate();
  data->block_reader = TSIOBufferReaderAlloc(data->block_buffer);

  /* Get size of doc to read in cache */
  TSVConnCacheObjectSizeGet(data->cache_vc, &cache_obj_size);
  TSDebug(LOW, "Size of block in cache = %d", cache_obj_size);

  /* Start reading the block content */
  data->cache_read_vio = TSVConnRead(data->cache_vc, contp, data->cache_read_buffer, cache_obj_size);
  return 0;
}


/*-------------------------------------------------------------------------
  asm_cache_read_buffer

  Bufferizes the block read from the cache

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_read_buffer(TSCont contp, AsmData * data)
{
  int avail;
  int ndone;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_read_buffer");

  TSDebug(LOW, "Reader avail = %d, TodoGet = %d, NDoneGet = %d",
           TSIOBufferReaderAvail(data->cache_read_reader),
           TSVIONTodoGet(data->cache_read_vio), TSVIONDoneGet(data->cache_read_vio));

  avail = TSIOBufferReaderAvail(data->cache_read_reader);
  ndone = TSVIONDoneGet(data->cache_read_vio);

  /* Bufferize available data in the block_buffer */
  if (avail > 0) {
    TSIOBufferCopy(data->block_buffer, data->cache_read_reader, avail, 0);
    TSIOBufferReaderConsume(data->cache_read_reader, avail);
    TSVIONDoneSet(data->cache_read_vio, TSVIONDoneGet(data->cache_read_vio) + avail);
  }

  TSDebug(LOW, "Reader avail = %d, TodoGet = %d, NDoneGet = %d",
           TSIOBufferReaderAvail(data->cache_read_reader),
           TSVIONTodoGet(data->cache_read_vio), TSVIONDoneGet(data->cache_read_vio));

  return 0;
}

/*-------------------------------------------------------------------------
  asm_cache_write_prepare

  Prepare a write into the cache

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_write_prepare(TSCont contp, AsmData * data)
{
  TSAction action;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_write_prepare");

  data->state = STATE_CACHE_PREPARE_WRITE;

  TSAssert(data->block_key);
  action = TSCacheWrite(contp, data->block_key);
  if (!TSActionDone(action)) {
    TSDebug(LOW, "CacheWrite action not completed...");
    data->pending_action = action;
  } else {
    TSDebug(LOW, "CacheWrite action completed");
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_cache_write

  Write into the cache the block read from TS

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_write(TSCont contp, AsmData * data)
{
  TSIOBufferData meta_data;
  TSIOBufferBlock meta_block;
  int block_len;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_write");

  data->state = STATE_CACHE_WRITE;

  /* Create a new IOBuffer that contains the Meta Data info first
     and then data read from TS */
  data->cache_write_buffer = TSIOBufferCreate();
  data->cache_write_reader = TSIOBufferReaderAlloc(data->cache_write_buffer);

  /* Fill out meta data structure before writing it to cache */
  time(&(data->block_metadata.write_time));
  data->block_metadata.template_id = TEMPLATE_ID;

  /* Prepend block meta data */
  TSDebug(LOW, "Appending metadata = %d bytes", sizeof(BlockMetaData));
  meta_data = TSIOBufferDataCreate(&data->block_metadata, sizeof(BlockMetaData), TS_DATA_CONSTANT);
  meta_block = TSIOBufferBlockCreate(meta_data, sizeof(BlockMetaData), 0);
  TSIOBufferAppend(data->cache_write_buffer, meta_block);

  /* Then add block content */
  block_len = TSIOBufferReaderAvail(data->block_reader);
  TSDebug(LOW, "Appending block content = %d bytes", block_len);

  TSIOBufferCopy(data->cache_write_buffer, data->block_reader, block_len, 0);

  TSDebug(LOW, "Writing %d bytes to cache", sizeof(BlockMetaData) + block_len);

  /* Finally write buffer to cache */
  data->cache_write_vio = TSVConnWrite(data->cache_vc, contp,
                                        data->cache_write_reader, sizeof(BlockMetaData) + block_len);

  return 0;
}


/*-------------------------------------------------------------------------
  asm_cache_remove

  Remove a stale block from cache.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_remove(TSCont contp, AsmData * data)
{
  TSAction action;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_remove");

  data->state = STATE_CACHE_REMOVE;

  TSDebug(LOW, "Removing block %s from cache", data->block_url);
  action = TSCacheRemove(contp, data->block_key);

  if (!TSActionDone(action)) {
    TSDebug(LOW, "Connection action not completed...");
    data->pending_action = action;
  } else {
    TSDebug(LOW, "Connection action completed");
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_ts_connect

  Creates a socket back to TS to fetch the include doc.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_ts_connect(TSCont contp, AsmData * data)
{
  TSAction action;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_ts_connect");

  data->state = STATE_TS_CONNECT;

  TSDebug(LOW, "Connecting to localhost on port %d", server_port);
  action = TSNetConnect(contp, server_ip, server_port);

  if (!TSActionDone(action)) {
    TSDebug(LOW, "Connection action not completed...");
    data->pending_action = action;
  } else {
    TSDebug(LOW, "Connection action completed");
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_create_block_http_request

  Returns the http request to be sent to TS via socket back to fetch a block.
  Request format is:
      GET <block_url> HTTP/1.0\r\n
      Cache-Control: no-cache
      X-Block: true\r\n\r\n
  If the URL to fetch the block contains ${QUERYSTRING}, substitute that
  with the current client request query string

  Caller must deallocate the returned pointer.
  -------------------------------------------------------------------------*/
static char *
asm_create_block_http_request(const char *block_url, const char *query_string, int *len)
{
  char *http_request;
  char *ptr;
  char *url;

  TSDebug(MED, "In asm_create_block_http_request");

  /* test for ${QUERYSTRING} and substitute with its value */
  if ((ptr = strstr(block_url, DYNAMIC_ATTR_URL_VAR_QUERYSTRING)) != NULL) {
    int len = (int) (ptr - block_url);
    TSDebug(LOW, "Variable %s detected in block url, doing substitution", DYNAMIC_ATTR_URL_VAR_QUERYSTRING);

    if (query_string != NULL) {
      url = TSmalloc(len + strlen(query_string) + 1);
      sprintf(url, "%.*s%s", len, block_url, query_string);
    } else {
      /* If no query value available remove the ? from url */
      url = TSmalloc(len);
      sprintf(url, "%.*s", len - 1, block_url);
    }
  } else {
    url = (char *) block_url;
  }

  http_request = TSmalloc(sizeof(BLOCK_HTTP_REQUEST_FORMAT) +
                           strlen(url) + sizeof(HEADER_NO_CACHE) + sizeof(HEADER_X_BLOCK));

  sprintf(http_request, BLOCK_HTTP_REQUEST_FORMAT, url, HEADER_NO_CACHE, HEADER_X_BLOCK);

  *len = strlen(http_request);
  return http_request;
}


/*-------------------------------------------------------------------------
  asm_ts_write

  Writes the request on the socket back to fetch the include doc.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_ts_write(TSCont contp, AsmData * data)
{
  TSIOBufferData http_request_data;
  TSIOBufferBlock http_request_block;
  char *http_request;
  int http_request_length;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_ts_write");

  data->state = STATE_TS_WRITE;

  /* Create a IOBuffer that will contain the request to send to TS */
  data->ts_input_buffer = TSIOBufferCreate();
  data->ts_input_reader = TSIOBufferReaderAlloc(data->ts_input_buffer);

  /* Create the request */
  http_request = asm_create_block_http_request(data->block_url, data->query_string, &http_request_length);

  /* Create a Block that contains the request and add it to the IOBuffer */
  http_request_data = TSIOBufferDataCreate(http_request, http_request_length, TS_DATA_CONSTANT);
  http_request_block = TSIOBufferBlockCreate(http_request_data, http_request_length, 0);

  TSIOBufferAppend(data->ts_input_buffer, http_request_block);

  TSDebug(LOW, "Writing request %s to socket back", http_request);

  data->ts_vio = TSVConnWrite(data->ts_vc, contp, data->ts_input_reader, http_request_length);

  return 0;
}


/*-------------------------------------------------------------------------
  asm_ts_read_init

  Routine to initialize data for reading response on the socket back.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_ts_read_init(TSCont contp, AsmData * data)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_ts_read_init");

  data->state = STATE_TS_READ;

  /* Create the IOBuffer and Reader to read
     response from TS on the socket back */
  data->ts_output_buffer = TSIOBufferCreate();
  data->ts_output_reader = TSIOBufferReaderAlloc(data->ts_output_buffer);

  /* Create IOBUffer and Reader to bufferize the include doc */
  data->block_buffer = TSIOBufferCreate();
  data->block_reader = TSIOBufferReaderAlloc(data->block_buffer);

  /* Read data on the socket back. Try to read the maximum */
  data->ts_vio = TSVConnRead(data->ts_vc, contp, data->ts_output_buffer, INT_MAX);

  return 0;
}


/*-------------------------------------------------------------------------
  asm_ts_read

  Bufferizes the socket back response from TS

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_ts_read(TSCont contp, AsmData * data)
{
  int avail;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_ts_read");

  TSDebug(LOW, "Reader avail = %d, TodoGet = %d, NDoneGet = %d",
           TSIOBufferReaderAvail(data->ts_output_reader), TSVIONTodoGet(data->ts_vio), TSVIONDoneGet(data->ts_vio));

  avail = TSIOBufferReaderAvail(data->ts_output_reader);

  /* Bufferize available data in the block_buffer */
  if (avail > 0) {
    TSIOBufferCopy(data->block_buffer, data->ts_output_reader, avail, 0);
    TSIOBufferReaderConsume(data->ts_output_reader, avail);
    TSVIONDoneSet(data->ts_vio, TSVIONDoneGet(data->ts_vio) + avail);
  }

  TSDebug(LOW, "Reader avail = %d, TodoGet = %d, NDoneGet = %d",
           TSIOBufferReaderAvail(data->ts_output_reader), TSVIONTodoGet(data->ts_vio), TSVIONDoneGet(data->ts_vio));

  /* Now reenable the vio to let it know it can produce some more data */
  TSVIOReenable(data->ts_vio);

  return 0;
}


/*-------------------------------------------------------------------------
  asm_append_block

  Appends the include doc that is bufferized to the output buffer.
  Only the data between tags <block> ... </block> are appended.

  Once this is done, calls asm_parse_input_buffer to continue
  parsing the master doc.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_append_block(TSCont contp, AsmData * data)
{
  int offset_start, offset_end, len, nbytes, avail;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_append_block");

  avail = TSIOBufferReaderAvail(data->block_reader);
  TSDebug(LOW, "%d bytes in the include doc", avail);

  offset_start = strfind_ioreader(data->block_reader, BLOCK_START) + strlen(BLOCK_START);
  if (offset_start >= 0) {
    offset_end = strfind_ioreader(data->block_reader, BLOCK_END) - 1;
  }

  /* If no <block> tags found, append nothing. */
  if ((offset_start < 0) || (offset_end < 0)) {
    TSError("Could not find block marker %s and %s in %s", BLOCK_START, BLOCK_END, data->block_url);
  } else {
    len = offset_end - offset_start + 1;
    TSDebug(LOW, "Include doc parsing. offset_start = %d, end = %d, len = %d", offset_start, offset_end, len);

    nbytes = TSIOBufferCopy(data->output_buffer, data->block_reader, len, offset_start);
    TSDebug(LOW, "%d bytes append from include to buffer output", nbytes);
  }

  /* We can now free the iobuffer used to bufferize this block */
  TSIOBufferDestroy(data->block_buffer);
  data->block_buffer = NULL;

  /* once that is done, let's continue parsing the input buffer */
  return asm_parse_input_buffer(contp, data);
}


/*-------------------------------------------------------------------------
  asm_block_bypass

  Called in case an error occured while attempting to create/write/read
  on the socket back or when reading from the cache.
  Closes all the open VCs.
  Free iobuffers.
  Calls assemby_parse_input to continue parsing the master doc.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_block_bypass(TSCont contp, AsmData * data)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_block_bypass");

  data->state = STATE_ERROR;

  /* Close the socket back */
  if (data->ts_vc) {
    TSVConnAbort(data->ts_vc, 1);
    data->ts_vc = NULL;
    data->ts_vio = NULL;
  }

  /* Close the cache vc */
  if (data->cache_vc) {
    TSVConnAbort(data->cache_vc, 1);
    data->cache_vc = NULL;
    data->cache_read_vio = NULL;
    data->cache_write_vio = NULL;
  }

  /* FIX ME: should we deallocate readers also ? */
  /* Free buffers. readers will be automatically freed */
  if (data->ts_input_buffer) {
    TSIOBufferDestroy(data->ts_input_buffer);
    data->ts_input_buffer = NULL;
  }
  if (data->ts_output_buffer) {
    TSIOBufferDestroy(data->ts_output_buffer);
    data->ts_output_buffer = NULL;
  }
  if (data->cache_read_buffer) {
    TSIOBufferDestroy(data->cache_read_buffer);
    data->cache_read_buffer = NULL;
  }
  if (data->cache_write_buffer) {
    TSIOBufferDestroy(data->cache_write_buffer);
    data->cache_write_buffer = NULL;
  }

  if (data->block_buffer) {
    TSIOBufferDestroy(data->block_buffer);
    data->block_buffer = NULL;
  }

  /* Try to continue parsing some data */
  return asm_parse_input_buffer(contp, data);
}


/*-------------------------------------------------------------------------
  asm_output_buffer

  Dumps the assembled document to the downstream vconnection.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_output_buffer(TSCont contp, AsmData * data)
{
  int towrite;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_output_buffer");

  /* Change the state */
  data->state = STATE_OUTPUT_WRITE;

  /* Check to see if we need to initiate the output operation. */
  if (!data->output_vio) {
    TSVConn output_conn;

    /* Get the output connection where we'll write data to. */
    output_conn = TSTransformOutputVConnGet(contp);

    /* Write all the data we have in the output buffer */
    towrite = TSIOBufferReaderAvail(data->output_reader);
    TSDebug(LOW, "Writing %d bytes to the downstream connection", towrite);

    if (TSIsDebugTagSet(LOW)) {
      print_iobuffer(data->output_buffer);
    }

    data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, towrite);
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_input_buffer_events_handler

  Handles events coming from the upstream vconnection while reading
  the master document.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_input_buffer_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_input_buffer_events_handler");

  switch (event) {
  case TS_EVENT_IMMEDIATE:
    TSDebug(LOW, "Getting TS_EVENT_IMMEDIATE in input_buffer handler");
    asm_input_buffer(contp, data);
    break;

  default:
    TSError("Getting unexpected event %d in input_buffer handler", event);
    TSAssert(!"Unexpected event");
    break;
  }

  return 0;
}

/*-------------------------------------------------------------------------
  asm_cache_prepare_read_events_handler

  Handles events received while doing cache lookup for a particular block

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_prepare_read_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  int cache_error = 0;
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_prepare_read_events_handler");

  switch (event) {

  case TS_EVENT_CACHE_OPEN_READ:
    /* Cache HIT */
    TSDebug(LOW, "Cache HIT: block %s is in the cache", data->block_url);
    data->pending_action = NULL;
    data->cache_vc = (TSVConn) edata;
    /* Read the block content from the cache */
    return asm_cache_read(contp, data);
    break;

  case TS_EVENT_CACHE_OPEN_READ_FAILED:
    /* Cache MISS */
    cache_error = (int) edata;
    TSDebug(LOW, "Cache MISS: block %s is not the cache", data->block_url);
    data->pending_action = NULL;
    data->cache_vc = NULL;

    /* -20401 = ECACHE_DOC_BUSY */
    if ((cache_error == -20401) && (data->cache_read_retry_counter < CACHE_READ_MAX_RETRIES)) {
      /* If cache is busy, retry */
      data->cache_read_retry_counter++;
      TSDebug(LOW, "Cache busy. Read failed. Retrying %d", data->cache_read_retry_counter);
      return asm_cache_retry_read(contp, data);

    } else {
      /* Cache miss or cache read failed... */
      /* Fetch the block from the OS via TS socket back */
      TSDebug(LOW, "Cache MISS or Cache read failed. Fetching block from OS");
      return asm_ts_connect(contp, data);
    }
    break;

  default:
    TSError("Got an unexpected event %d in cache_prepare_read_events_handler", event);
    TSAssert(!"Unexpected event in cache_prepare_read_events_handler");
    break;
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_cache_read_events_handler

  Handles events received while doing cache read for a particular block

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_read_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  int read_meta = 0;

  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_read_events_handler");

  switch (event) {
  case TS_EVENT_ERROR:
    /* An error occurred while reading block from cache. */
    /* TEMP: bypass the block */
    /* TODO remove this cache entry + fetch block from OS */
    TSError("Error while reading from the cache");
    TSDebug(LOW, "Got an EVENT_ERROR event");

    /* FIX ME: Do we need to abort the cache VC ? */
    TSVConnAbort(data->cache_vc, 1);
    data->cache_vc = NULL;
    data->cache_read_vio = NULL;

    return asm_block_bypass(contp, data);
    break;

  case TS_EVENT_VCONN_READ_READY:
    /* Some more data available to read */
    TSDebug(LOW, "Got an EVENT_VCONN_READ_READY event");
    asm_cache_read_buffer(contp, data);
    /* Now reenable the vio to let it know it can produce some more data */
    TSVIOReenable(data->cache_read_vio);
    return 0;
    break;

  case TS_EVENT_VCONN_READ_COMPLETE:
    /* Means we're done reading block from cache */
    TSDebug(LOW, "Got an EVENT_VCONN_READ_COMPLETE event");

    /* finish reading any data available */
    asm_cache_read_buffer(contp, data);

    /* Close connection and go ahead in assembly */
    TSVConnClose(data->cache_vc);
    data->cache_vc = NULL;
    data->cache_read_vio = NULL;

    /* Now Read the block meta-data */
    read_meta = read_block_metadata(data->cache_read_buffer, &data->block_metadata);

    /* FIXME: For these 2 error case, we should try fetching block from OS. */
    if (read_meta == -2) {
      /* couldn't read expected number of bytes from cache iobuffer */
      TSDebug(HIGH, "Error: could not read enough data from cache");
      TSError("Error: could not read enough data from cache");
      return asm_block_bypass(contp, data);

    } else if (read_meta == -1) {
      /* data is corrupted... */
      TSDebug(HIGH, "Error: read corrupted block");
      TSError("Read a corrupted block from cache");
      return asm_block_bypass(contp, data);
    }

    TSAssert(read_meta == 0);

    /* Make sure the block is fresh.
       If not, remove it from cache and fetch a fresh version from OS via TS */
    if (block_is_fresh(data)) {
      TSDebug(HIGH, "Block %s is FRESH", data->block_url);
      return asm_append_block(contp, data);
    } else {
      TSDebug(HIGH, "Block %s is STALE", data->block_url);
      return asm_cache_remove(contp, data);
    }

    break;

  default:
    TSError("Got an unexpected event %d in cache_read_events_handler", event);
    TSAssert(!"Unexpected event in cache_read_events_handler");
    break;
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_cache_prepare_write_events_handler

  Handles events received while trying to opening the cache

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_prepare_write_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_prepare_write_events_handler");

  switch (event) {
  case TS_EVENT_CACHE_OPEN_WRITE:
    /* Succeeded in initializing write cache. Do the write */
    TSDebug(LOW, "Got CACHE_OPEN_WRITE event");

    /* cache call us back with the cche vc as argument */
    data->cache_vc = (TSVConn) edata;
    data->pending_action = NULL;

    return asm_cache_write(contp, data);
    break;

  case TS_EVENT_CACHE_OPEN_WRITE_FAILED:
    /* Cache write error. Let's continue w/o caching this block */
    TSDebug(LOW, "Got CACHE_OPEN_WRITE_FAILED event");
    TSError("Error while writing to the cache");

    data->pending_action = NULL;
    data->cache_vc = NULL;

    /* Even if the cache write failed, we can still use the block and append it to the output */
    return asm_append_block(contp, data);
    break;

  default:
    TSError("Got an unexpected event %d in cache_prepare_write_events_handler", event);
    TSAssert(!"Unexpected event");
    break;
  }

  return 0;
}

/*-------------------------------------------------------------------------
  asm_cache_write_events_handler

  Handles events received while doing cache write for a particular block

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_write_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_write_events_handler");

  switch (event) {

  case TS_EVENT_VCONN_WRITE_READY:
    /* Cache wants to read some more data */
    TSDebug(LOW, "Got VCONN_WRITE_READY event");
    TSVIOReenable(data->cache_write_vio);
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* Block stored in cache */
    TSDebug(LOW, "Got WRITE_COMPLETE event");
    TSVConnClose(data->cache_vc);
    data->cache_vc = NULL;
    data->cache_write_vio = NULL;
    TSIOBufferReaderFree(data->cache_write_reader);
    data->cache_write_reader = NULL;

    return asm_append_block(contp, data);
    break;

  default:
    TSError("Got an unexpected event %d in cache_write_events_handler", event);
    TSAssert(!"Unexpected event");
    break;
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_ts_connect_events_handler

  Handles events received while attempting to remove a block from cache.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_cache_remove_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_cache_remove_events_handler");

  switch (event) {
  case TS_EVENT_CACHE_REMOVE:
    /* If we successfully removed block from cache,
       we now have to fetch a more recent version. */
    TSDebug(LOW, "Got CACHE_REMOVE event");
    data->pending_action = NULL;
    return asm_ts_connect(contp, data);
    break;

  case TS_EVENT_CACHE_REMOVE_FAILED:
    /* We failed removing the block from cache. Something is wrong.
       Log an error and bypass this block */
    TSDebug(LOW, "Got REMOVE_FAILED event");
    data->pending_action = NULL;
    TSError("Error while trying to remove block %s from cache", data->block_url);
    return asm_block_bypass(contp, data);
    break;

  default:
    TSError("Got an unexpected event %d in cache_remove_events_handler", event);
    TSAssert(!"Unexpected event in cache_remove_events_handler");
    break;
  }

  return 0;
}

/*-------------------------------------------------------------------------
  asm_ts_connect_events_handler

  Handles events received while attempting to create the socket back.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_ts_connect_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_ts_connect_events_handler");

  switch (event) {
  case TS_EVENT_NET_CONNECT:
    /* If creation of socket back succeeded, let's go ahead and
       send a request on this socket */
    TSDebug(LOW, "Got NET_CONNECT event, Connection succeeded");
    data->pending_action = NULL;
    data->ts_vc = (TSVConn) edata;
    return asm_ts_write(contp, data);
    break;

  case TS_EVENT_NET_CONNECT_FAILED:
    /* We failed creating this socket back. We won't include that document. */
    TSDebug(LOW, "Got NET_CONNECT_FAILED, Connection failed");
    TSError("Error while attempting to connect to TS");
    data->pending_action = NULL;
    return asm_block_bypass(contp, data);
    break;

  default:
    TSError("Got an unexpected event %d in ts_connect_events_handler", event);
    TSAssert(!"Unexpected event");
    break;
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_ts_connect_events_handler

  Handles events received while writing to the socket back.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_ts_write_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_ts_write_events_handler");

  switch (event) {
    /* An error occurred while writing to the server. Close down
       the connection to the server and bypass the include. */
  case TS_EVENT_VCONN_EOS:
    /* FALLTHROUGH */

  case TS_EVENT_ERROR:
    TSDebug(LOW, "Got an ERROR or EOS event");
    TSVConnAbort(data->ts_vc, 1);
    data->ts_vc = NULL;
    TSIOBufferDestroy(data->ts_input_buffer);
    data->ts_input_buffer = NULL;

    return asm_block_bypass(contp, data);
    break;

  case TS_EVENT_VCONN_WRITE_READY:
    TSDebug(LOW, "Got a WRITE_READY event");
    TSVIOReenable(data->ts_vio);
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* TS is done reading our request */
    TSDebug(LOW, "Got a WRITE_COMPLETE event");
    TSIOBufferDestroy(data->ts_input_buffer);
    data->ts_input_buffer = NULL;

    return asm_ts_read_init(contp, data);
    break;

  default:
    TSError("Got an unexpected event %d.", event);
    TSAssert(!"Unexpected event in asm_ts_write_events_handler");
    break;
  }

  return 0;
}

/*-------------------------------------------------------------------------
  asm_ts_read_events_handler

  Handles events received while reading from the socket back.

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_ts_read_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  TSAssert(data->magic == MAGIC_ALIVE);
  TSDebug(MED, "In asm_ts_read_events_handler");

  switch (event) {
  case TS_EVENT_ERROR:
    /* An error occurred while writing to the server. Close down
       the connection to the server and bypass the include. */
    TSDebug(LOW, "Got an EVENT_ERROR event");
    return asm_block_bypass(contp, data);
    break;

  case TS_EVENT_VCONN_EOS:
    /* Means we're done reading the include doc */
    TSDebug(LOW, "Got an EVENT_VCONN_EOS event");

    TSVConnAbort(data->ts_vc, 1);
    data->ts_vc = NULL;
    data->ts_vio = NULL;

    if (data->block_is_cacheable) {
      return asm_cache_write_prepare(contp, data);
    } else {
      return asm_append_block(contp, data);
    }
    break;

  case TS_EVENT_VCONN_READ_COMPLETE:
    /* Means we're done reading on TS socket back */
    TSDebug(LOW, "Got an EVENT_VCONN_READ_COMPLETE event");

    TSVConnClose(data->ts_vc);
    data->ts_vc = NULL;
    data->ts_vio = NULL;

    if (data->block_is_cacheable) {
      return asm_cache_write_prepare(contp, data);
    } else {
      return asm_append_block(contp, data);
    }
    break;

  case TS_EVENT_VCONN_READ_READY:
    /* Some more data available to read */
    TSDebug(LOW, "Got an EVENT_VCONN_READ_READY event");
    return asm_ts_read(contp, data);
    break;


  default:
    TSError("Got a %d event in asm_ts_read_events_handler", event);
    TSAssert(!"Unexpected event in asm_ts_read_events_handler");
    break;
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_output_buffer_events_handler

  Handles events received while writing assembled doc to the
  downstream vconnection

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_output_buffer_events_handler(TSCont contp, AsmData * data, TSEvent event, void *edata)
{
  TSDebug(MED, "In asm_output_buffer_events_handler");

  TSAssert(data->magic == MAGIC_ALIVE);

  switch (event) {

    /* TODO checkout the TS_EVENT_ERROR statement */
  case TS_EVENT_ERROR:
    {
      TSVIO input_vio;
      TSDebug(LOW, "Getting a TS_EVENT_ERROR in output_buffer_handler");

      /* Get the write VIO for the write operation that was
         performed on ourself. This VIO contains the continuation of
         our parent transformation. This is the input VIO. */
      input_vio = TSVConnWriteVIOGet(contp);

      /* Call back our parent continuation to let it know that we
         have completed the write operation. */
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    }
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(LOW, "Getting a TS_EVENT_VCONN_WRITE_COMPLETE in output_buffer_handler");
    /* When our output connection says that it has finished
       reading all the data we've written to it then we should
       shutdown the write portion of its connection to
       indicate that we don't want to hear about it anymore. */
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;

  case TS_EVENT_VCONN_WRITE_READY:
    /* Our child continuation is ready to get more data.
       We've already written all what we had.
       So do nothing. */
    TSDebug(LOW, "Getting a TS_EVENT_VCONN_WRITE_READY in output_buffer_handler");
    break;

  case TS_EVENT_IMMEDIATE:
    /* Probably we were reenable. Do nothing */
    TSDebug(LOW, "Getting a TS_EVENT_IMMEDIATE in output_buffer_handler");
    break;

  default:
    TSError("Getting unexpected event %d in output_buffer handler", event);
    TSAssert(!"Unexpected event in asm_output_buffer_events_handler");
    break;
  }

  return 0;
}


/*-------------------------------------------------------------------------
  asm_main_events_handler

  Hanles ALL the events.
  Based on the state, it dispatches it to the appropriate handler

  Returns 0 if ok, -1 in case an error occured.
  -------------------------------------------------------------------------*/
static int
asm_main_events_handler(TSCont contp, TSEvent event, void *edata)
{
  AsmData *data;
  int val;

  data = (AsmData *) TSContDataGet(contp);
  TSAssert(data->magic == MAGIC_ALIVE);
  TSAssert(data->state != STATE_DEAD);

  TSDebug(MED, "Got event %d in asm_main_events_handler", event);

  /* VERY IMPORTANT: First thing to do is to check to see if the transformation */
  /*   has been closed by a call to TSVConnClose. */
  if (TSVConnClosedGet(contp)) {
    asm_transform_destroy(contp);
    return 0;
  }


  if ((event == TS_EVENT_IMMEDIATE) && (data->state != STATE_INPUT_BUFFER) && (data->state != STATE_OUTPUT_WRITE)) {
    /* Probably our vconnection was reenabled. */
    /* But we've nothing to do as we're not in STATE_INPUT/WRITE_BUFFER */

    /* TSError("Got EVENT_IMMEDIATE in main_handler. Ignore it"); */
    /* fprintf(stderr, "Got EVENT_IMMEDIATE in main_handler. Ignore it\n"); */
    return 0;
  }

  LOCK_CONT_MUTEX(contp);

  switch (data->state) {
  case STATE_INPUT_BUFFER:
    TSDebug(LOW, "Redirecting event to input_buffer handler");
    val = asm_input_buffer_events_handler(contp, data, event, edata);
    break;
  case STATE_PARSE_BUFFER:
    /* We do not expect any event in this state ... */
    TSError("Got an unexpected event %d while in PARSE BUFFER STATE", event);
    TSAssert(!"Unexpected event");
    break;

  case STATE_CACHE_PREPARE_READ:
    TSDebug(LOW, "Redirecting event to cache_prepare_read handler");
    val = asm_cache_prepare_read_events_handler(contp, data, event, edata);
    break;

  case STATE_CACHE_RETRY_READ:
    /* No handler for this state. too basic */
    TSAssert(event == TS_EVENT_TIMEOUT);
    asm_cache_prepare_read(contp, data);
    break;

  case STATE_CACHE_READ:
    TSDebug(LOW, "Redirecting event to cache_read handler");
    val = asm_cache_read_events_handler(contp, data, event, edata);
    break;

  case STATE_CACHE_PREPARE_WRITE:
    TSDebug(LOW, "Redirecting event to cache_prepare_write handler");
    val = asm_cache_prepare_write_events_handler(contp, data, event, edata);
    break;

  case STATE_CACHE_WRITE:
    TSDebug(LOW, "Redirecting event to cache_write handler");
    val = asm_cache_write_events_handler(contp, data, event, edata);
    break;

  case STATE_CACHE_REMOVE:
    TSDebug(LOW, "Redirecting event to cache_remove handler");
    val = asm_cache_remove_events_handler(contp, data, event, edata);
    break;

  case STATE_TS_CONNECT:
    TSDebug(LOW, "Redirecting event to ts_connect handler");
    val = asm_ts_connect_events_handler(contp, data, event, edata);
    break;

  case STATE_TS_WRITE:
    TSDebug(LOW, "Redirecting event to ts_write_handler");
    val = asm_ts_write_events_handler(contp, data, event, edata);
    break;

  case STATE_TS_READ:
    TSDebug(LOW, "Redirecting event to ts_read_handler");
    val = asm_ts_read_events_handler(contp, data, event, edata);
    break;

  case STATE_OUTPUT_WRITE:
    TSDebug(LOW, "Redirecting event to output_write handler");
    val = asm_output_buffer_events_handler(contp, data, event, edata);
    break;

  case STATE_ERROR:
    /* No event expected in this state */
    TSError("Got an unexpected event %d while in STATE_ERROR state", event);
    TSAssert(!"Unexpected event");
    break;

  default:
    TSError("Unexpexted state %d", data->state);
    TSAssert(!"Unexpected state");
  }

  TSAssert(data->state != STATE_DEAD);
  TSAssert(data->magic == MAGIC_ALIVE);
  UNLOCK_CONT_MUTEX(contp);

  return 0;
}


/*-------------------------------------------------------------------------
  asm_transform_destroy

  Takes care of cleaning up all allocated buffer, data, etc...
  -------------------------------------------------------------------------*/
static void
asm_transform_destroy(TSCont contp)
{
  AsmData *data;

  LOCK_CONT_MUTEX(contp);

  TSDebug(MED, "In asm_transform_destroy");

  data = (AsmData *) TSContDataGet(contp);
  TSAssert(data->magic == MAGIC_ALIVE);

  data->magic = MAGIC_DEAD;

  data->state = STATE_DEAD;

  if ((data->pending_action) && (!TSActionDone(data->pending_action))) {
    TSActionCancel(data->pending_action);
    data->pending_action = NULL;
  }

  if (data->input_buffer) {
    TSIOBufferDestroy(data->input_buffer);
    data->input_buffer = NULL;
  }

  if (data->output_buffer) {
    TSIOBufferDestroy(data->output_buffer);
    data->output_buffer = NULL;
  }

  if (data->ts_vc) {
    TSVConnAbort(data->ts_vc, 1);
    data->ts_vc = NULL;
  }

  if (data->ts_input_buffer) {
    TSIOBufferDestroy(data->ts_input_buffer);
    data->ts_input_buffer = NULL;
  }

  if (data->ts_output_buffer) {
    TSIOBufferDestroy(data->ts_output_buffer);
    data->ts_output_buffer = NULL;
  }

  if (data->block_buffer) {
    TSIOBufferDestroy(data->block_buffer);
    data->block_buffer = NULL;
  }

  if (data->cache_vc) {
    TSVConnAbort(data->cache_vc, 1);
    data->cache_vc = NULL;
  }

  if (data->cache_read_buffer) {
    TSIOBufferDestroy(data->cache_read_buffer);
    data->cache_read_buffer = NULL;
  }

  if (data->cache_write_buffer) {
    TSIOBufferDestroy(data->cache_write_buffer);
    data->cache_write_buffer = NULL;
  }

  if (data->block_key) {
    TSCacheKeyDestroy(data->block_key);
    data->block_key = NULL;
  }

  if (data->block_url) {
    TSfree(data->block_url);
  }

  data->block_metadata.template_id = MAGIC_DEAD;

  pairListFree(&data->query);
  pairListFree(&data->cookies);

  if (data->query_string) {
    TSfree(data->query_string);
    data->query_string = NULL;
  }

  TSfree(data);
  data = NULL;

  UNLOCK_CONT_MUTEX(contp);

  TSContDestroy(contp);
}


/*-------------------------------------------------------------------------
  asm_transform_create

  Registers a callback to be called when we receive the body of
  the template page. Initializes structures required for the assembly process
  -------------------------------------------------------------------------*/
static void
asm_transform_create(TSHttpTxn txnp, TxnData * txn_data)
{
  TSVConn connp;
  AsmData *data;

  TSDebug(MED, "In asm_transform_create");

  connp = TSTransformCreate(asm_main_events_handler, txnp);

  /* By caching only the untransformed version, we reassemble each time
     there is a cache hit. Reassembling means fetching the blocks
     so by this way we make sure the blocks are also fresh. */
  TSHttpTxnUntransformedRespCache(txnp, 1);
  TSHttpTxnTransformedRespCache(txnp, 0);

  /* Initialize all data structure we'll need to do the assembly job */
  data = (AsmData *) TSmalloc(sizeof(AsmData));
  data->state = STATE_INPUT_BUFFER;
  data->txn = txnp;
  data->input_buffer = NULL;
  data->input_parse_reader = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;
  data->output_vio = NULL;
  data->output_vc = NULL;
  data->pending_action = NULL;
  data->ts_vc = NULL;
  data->ts_vio = NULL;
  data->ts_input_buffer = NULL;
  data->ts_input_reader = NULL;
  data->ts_output_buffer = NULL;
  data->ts_output_reader = NULL;
  data->block_buffer = NULL;
  data->block_reader = NULL;
  data->cache_vc = NULL;
  data->cache_read_vio = NULL;
  data->cache_write_vio = NULL;
  data->cache_read_buffer = NULL;
  data->cache_write_buffer = NULL;
  data->cache_read_reader = NULL;
  data->cache_write_reader = NULL;

  data->block_key = NULL;
  data->block_url = NULL;
  data->block_is_cacheable = 0;
  data->block_ttl = DYNAMIC_ATTR_TTL_DEFAULT_VALUE;

  data->block_metadata.template_id = MAGIC_ALIVE;

  data->cache_read_retry_counter = 0;

  data->magic = MAGIC_ALIVE;

  /* Extract query string and cookies info that will
     be used in the transformation */
  pairListInit(&data->query);
  pairListInit(&data->cookies);
  query_and_cookies_extract(txnp, txn_data, &data->query, &data->cookies);

  /* store the query string */
  query_string_extract(txn_data, &data->query_string);

  /* Associate data with the transformation we've created */
  TSContDataSet((TSCont) connp, data);

  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}


/*-------------------------------------------------------------------------
  asm_txn_data_create
  -------------------------------------------------------------------------*/
static TxnData *
asm_txn_data_create(TSCont contp)
{
  TxnData *txn_data;
  TSDebug(MED, "In asm_cont_data_init");

  txn_data = (TxnData *) TSmalloc(sizeof(TxnData));
  txn_data->request_url_buf = NULL;
  txn_data->request_url_loc = NULL;
  txn_data->template_url_buf = NULL;
  txn_data->template_url_loc = NULL;
  txn_data->transform_created = 0;

  txn_data->magic = MAGIC_ALIVE;

  TSContDataSet(contp, (void *) txn_data);
  return txn_data;
}


/*-------------------------------------------------------------------------
  asm_txn_data_destroy
  -------------------------------------------------------------------------*/
static void
asm_txn_data_destroy(TSCont contp)
{
  TxnData *txn_data;
  TSDebug(MED, "In asm_txn_data_destroy");

  txn_data = TSContDataGet(contp);
  TSAssert(txn_data->magic == MAGIC_ALIVE);

  txn_data->magic = MAGIC_DEAD;

  if (txn_data->request_url_loc != NULL) {
    TSHandleMLocRelease(txn_data->request_url_buf, TS_NULL_MLOC, txn_data->request_url_loc);
  }

  if (txn_data->request_url_buf != NULL) {
    TSMBufferDestroy(txn_data->request_url_buf);
  }

  if (txn_data->template_url_loc != NULL) {
    TSHandleMLocRelease(txn_data->template_url_buf, TS_NULL_MLOC, txn_data->template_url_loc);
  }

  if (txn_data->template_url_buf != NULL) {
    TSMBufferDestroy(txn_data->template_url_buf);
  }

  if (txn_data != NULL) {
    TSfree(txn_data);
  }
}




/*-------------------------------------------------------------------------
  asm_main

  Callback function for the Http hooks
  -------------------------------------------------------------------------*/
static int
asm_main(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;
  int lookup_status;
  TxnData *txn_data;
  TSMBuffer bufp;
  TSMLoc url_loc, hdr_loc, x_field_loc;

  TSDebug(MED, "In asm_main");

  switch (event) {

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    /* We're done with cachelookup on the template */
    TSDebug(LOW, "Get an TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE event");

    txn_data = TSContDataGet(contp);
    TSAssert(txn_data->magic == MAGIC_ALIVE);

    if (!TSHttpTxnCacheLookupStatusGet(txnp, &lookup_status)) {
      TSError("Could not get cache lookup status");
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }

    switch (lookup_status) {
      /* TODO make sure the client request Url is used when sending IMS
         If it's the cached client request then we would have to change it.
         So the following case is covered: a cache hit on a template but
         actually on the OS the client request doc is no longer
         generated using a template and the OS response is a regular
         file with no X-Template header */
    case TS_CACHE_LOOKUP_MISS:
    case TS_CACHE_LOOKUP_HIT_STALE:

      TSDebug(LOW, "Cache %s", ((lookup_status == TS_CACHE_LOOKUP_MISS) ? "MISS" : "HIT STALE"));
      break;

    case TS_CACHE_LOOKUP_HIT_FRESH:
      /* Cache hit fresh on the template.
         Let's setup the transform for the assembly that will take place */
      TSDebug(LOW, "Cache HIT FRESH");
      asm_transform_create(txnp, txn_data);
      txn_data->transform_created = 1;
      break;

    default:
      TSAssert(!"Unexpected event");
      break;
    }

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
    break;


  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    /* Add header X-Template to each outgoing request to let the server
       know we're capable of doing assembly */
    TSHttpTxnServerReqGet(txnp, &bufp, &hdr_loc);

    /* Create new X-Template field */
    x_field_loc = TSMimeHdrFieldCreate(bufp, hdr_loc);
    TSMimeHdrFieldNameSet(bufp, hdr_loc, x_field_loc, HEADER_X_TEMPLATE, -1);
    TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, x_field_loc, "true", -1, -1);
    TSMimeHdrFieldAppend(bufp, hdr_loc, x_field_loc);
    TSHandleMLocRelease(bufp, hdr_loc, x_field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
    break;


  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    /* OK, we got a response from the OS.
       Determine if it's a template so we can assemble it */
    TSDebug(LOW, "Get an TS_EVENT_HTTP_READ_RESPONSE_HDR event");

    txn_data = TSContDataGet(contp);
    TSAssert(txn_data->magic == MAGIC_ALIVE);

    if (!TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc)) {
      TSError("Couldnt get server response Http header");
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }

    if (is_template_header(bufp, hdr_loc)) {
      TSDebug(HIGH, "Detected a template page. Read from OS");

      /* Check the X-NoCache header */
      if (has_nocache_header(bufp, hdr_loc)) {
        TSDebug(LOW, "NoCache header detected. The template will not be cached");
        TSHttpTxnServerRespNoStore(txnp);
      }

      /* Create the assembly transformation */
      /* CAUTION: If the client request contains
         Cache-Control: no-cache or Progma: no-cache
         even if it's a cache HIT FRESH, TS will send a request to the OS.
         So we have to prevent creating the transformation twice !!! */
      if (!txn_data->transform_created) {
        asm_transform_create(txnp, txn_data);
      }
    } else {
      /* If it's not a template, we do not want the document to be cached. */
      TSDebug(HIGH, "Not a template page. Do not transform nor cache this request");
      TSHttpTxnServerRespNoStore(txnp);
    }

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    TSDebug(LOW, "Get an TS_EVENT_HTTP_TXN_CLOSE event");

    txn_data = TSContDataGet(contp);
    TSAssert(txn_data->magic == MAGIC_ALIVE);

    /* destroy this transaction and its data */
    asm_txn_data_destroy(contp);
    TSContDestroy(contp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
    break;

  default:
    TSAssert(!"Unexpected event");
    break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}


/*-------------------------------------------------------------------------
  asm_read_request

  Get called for global hook READ_REQUEST
  If request looks dynamic, setup a cache lookup on template and register to
  other hooks..
  Else do not process further
  -------------------------------------------------------------------------*/
static int
asm_read_request(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;
  TSCont txn_contp;
  TxnData *txn_data;
  TSMBuffer bufp;
  TSMLoc hdr_loc, url_loc;
  int len;

  TSDebug(MED, "In asm_read_request");

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    TSDebug(LOW, "Get an TS_EVENT_HTTP_READ_REQUEST_HDR event");

    if (!TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
      TSError("Couldnt get client request Http header");
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }

    /* If request comes from socket back and is for a block, exit ! */
    if (is_block_request(bufp, hdr_loc)) {
      TSDebug(HIGH, "Block request. Do not assemble !");
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }


    /* OK, we can continue. Maybe this request is for an assembled page  */


    /* Create a continuation and data for this specific transaction */
    txn_contp = TSContCreate(asm_main, TSMutexCreate());
    txn_data = asm_txn_data_create(txn_contp);
    TSAssert(txn_data);


    /* Store original request url into txn_data structure */
    url_loc = TSHttpHdrUrlGet(bufp, hdr_loc);
    if (url_loc == NULL) {
      TSError("Could not get Url");
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      return -1;
    }

    txn_data->request_url_buf = TSMBufferCreate();
    txn_data->request_url_loc = TSUrlCreate(txn_data->request_url_buf);
    TSUrlCopy(txn_data->request_url_buf, txn_data->request_url_loc, bufp, url_loc);

    TSDebug(LOW, "Request url = |%s|", TSUrlStringGet(txn_data->request_url_buf, txn_data->request_url_loc, &len));

    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);


    /* Register for hooks later in the transaction */
    TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, txn_contp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, txn_contp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, txn_contp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);

    break;

  default:
    TSAssert(!"Unexpected event");
    break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}


int
check_ts_version()
{
  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 2.0 to run */
    if (major_ts_version >= 2) {
      result = 1;
    }
  }

  return result;
}


/*-------------------------------------------------------------------------
  TSPluginInit


  -------------------------------------------------------------------------*/
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont contp;

  TSError("Assembly engine ...taking off !");

  info.plugin_name = "assembly";
  info.vendor_name = "Apache";
  info.support_email = "";

  if (!TSPluginRegister(TS_SDK_VERSION_2_0, &info)) {
    TSError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 2.0 or later\n");
    return;
  }

  /* connect to TS port on localhost */
  server_ip = (127 << 24) | (0 << 16) | (0 << 8) | (1);
  server_ip = htonl(server_ip);

  if (argc == 2) {
    server_port = atoi(argv[1]);
  } else {
    server_port = TS_DEFAULT_PORT;
  }
  TSDebug(HIGH, "Using TS port %d for internal requests", server_port);

  contp = TSContCreate(asm_read_request, NULL);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);

  TSDebug(HIGH, "Assembly plugin processor started");
}
