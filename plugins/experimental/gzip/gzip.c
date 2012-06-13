/** @file

  Transforms content using gzip

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


// WHAT THIS PLUGIN DOES:
// in spite of its name, it compresses responses to the raw deflate format.
// it also normalizes the accept-encoding header from user agent requests 
// to either deflate or nothing. it compresses responses from origins only,
// though it can be modified easily to also compress contents from cache.
//
// MAJOR ISSUES:
// - there is an issue with *some* origins that send connection:close. 
//   the plugin will not execute in that situation as a temporary fix.
// - the workings of this plugin support our use case, but probably is not 
//   on size fits all. it shouldnt be too hard to adjust it to your own needs though.
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <ts/ts.h>
#include <inttypes.h>

#define DICT_PATH_MAX 512
#define DICT_ENTRY_MAX 2048

const char* PLUGIN_NAME = "gzip";

typedef struct
{
  TSHttpTxn txn;
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;
  int output_length;
  z_stream zstrm;
  uLong crc; //os: unused, we are using raw deflate
  int state;
} GzipData;


char preload_file[1024];
uLong dictId;
int preload = 0;
char dictionary[800000];

void
load_dictionary(char *dict, uLong *adler)
{
  FILE *fp;
  int i = 0;

  fp = fopen(preload_file, "r");
  if (!fp) {
    TSError("gzip-transform: ERROR: Unable to open dict file %s\n", preload_file);
    exit(0);
  }

  /* dict = (char *) calloc(8000, sizeof(char)); */

  i = 0;
  while (!feof(fp)) {
    if (fscanf(fp, "%s\n", dict + i) == 1) {
      i = strlen(dict);
      strcat(dict + i, " ");
      ++i;
    }
  }
  dict[i - 1] = '\0';

  /* TODO get the adler compute right */
  *adler = adler32(*adler, (const Byte *) dict, sizeof(dict));
}

static voidpf
gzip_alloc(voidpf opaque, uInt items, uInt size)
{
  TSDebug(PLUGIN_NAME, "gzip_alloc()");
  return (voidpf) TSmalloc(items * size);
}


static void
gzip_free(voidpf opaque, voidpf address)
{
  TSDebug(PLUGIN_NAME, "gzip_free() start");
  TSfree(address);
}


static GzipData *
gzip_data_alloc()
{
  GzipData *data;
  int err;

  TSDebug(PLUGIN_NAME, "gzip_data_alloc() start");

  data = (GzipData *)TSmalloc(sizeof(GzipData));
  data->output_vio = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;
  data->output_length = 0;
  data->state = 0;
  data->crc = crc32(0L, Z_NULL, 0);

  data->zstrm.next_in = Z_NULL;
  data->zstrm.avail_in = 0;
  data->zstrm.total_in = 0;
  data->zstrm.next_out = Z_NULL;
  data->zstrm.avail_out = 0;
  data->zstrm.total_out = 0;
  data->zstrm.zalloc = gzip_alloc;
  data->zstrm.zfree = gzip_free;
  data->zstrm.opaque = (voidpf) 0;
  data->zstrm.data_type = Z_ASCII;

#define MOD_GZIP_ZLIB_WINDOWSIZE -15
#define MOD_GZIP_ZLIB_CFACTOR    9
#define MOD_GZIP_ZLIB_BSIZE      8096

/* ZLIB's deflate() compression algorithm uses the same */
/* 0-9 based scale that GZIP does where '1' is 'Best speed' */
/* and '9' is 'Best compression'. Testing has proved level '6' */
/* to be about the best level to use in an HTTP Server. */


//os: FIXME: look into autoscaling the compression level based on connection speed
//a gprs device might benefit from a higher compression ratio, whereas a desktop w. high bandwith
//might be served better with little or no compression at all
#define MOD_GZIP_DEFLATE_DEFAULT_COMPRESSION_LEVEL 6
  err = deflateInit2(
        &data->zstrm, 
        MOD_GZIP_DEFLATE_DEFAULT_COMPRESSION_LEVEL,
        Z_DEFLATED,
        MOD_GZIP_ZLIB_WINDOWSIZE,
        MOD_GZIP_ZLIB_CFACTOR,
        Z_DEFAULT_STRATEGY);

  if (err != Z_OK) {
    TSError("gzip-transform: ERROR: deflateInit (%d)!", err);
    exit(1);
  }

  if (preload) {
    TSAssert(&data->zstrm);
    err = deflateSetDictionary(&data->zstrm, (const Bytef *) dictionary, strlen(dictionary));
    if (err != Z_OK) {
      TSError("gzip-transform: ERROR: deflateSetDictionary (%d)!", err);
    }
  }
  TSDebug(PLUGIN_NAME, "gzip_data_alloc() end");

  return data;
}


static void
gzip_data_destroy(GzipData *data)
{
  int err;

  TSDebug(PLUGIN_NAME,"gzip-transform: gzip_data_destroy() starts");  
  if (data) {
    err = deflateEnd(&data->zstrm);

    //os: this can happen when clients abort.
    //    that is not very uncommon, so don't log it.
    if (err != Z_OK) {
      //TSDebug(PLUGIN_NAME,"gzip-transform: ERROR: deflateEnd (%d)!", err);
      //TSError("gzip-transform: ERROR: deflateEnd (%d)!", err);
    }

    if (data->output_buffer)
      TSIOBufferDestroy(data->output_buffer);
    TSfree(data);
  }
  
  TSDebug(PLUGIN_NAME,"gzip-transform: gzip_data_destroy() ends");  
}


static void
gzip_transform_init(TSCont contp, GzipData *data)
{
  TSVConn output_conn;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc ce_loc;               /* for the content encoding mime field */

  TSDebug(PLUGIN_NAME, "gzip_transform_init");

  data->state = 1;

  /* Mark the output data as having deflate content encoding */
  
  TSHttpTxnTransformRespGet(data->txn, &bufp, &hdr_loc);


  //FIXME: these todo's 
  //os: this is a little rough around the edges. 
  //this should insert/append field values as needed instead.

  //Probably should check for errors 
  TSMimeHdrFieldCreate(bufp, hdr_loc, &ce_loc); 
  TSMimeHdrFieldNameSet(bufp, hdr_loc, ce_loc, "Content-Encoding", -1);
  TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, ce_loc, -1, "deflate", -1);
  TSMimeHdrFieldAppend(bufp, hdr_loc, ce_loc);
  TSHandleMLocRelease(bufp, hdr_loc, ce_loc);


  //os: error checking. formally -> this header should be send for any document, 
  //that will potentially alternate on compression?

  TSMimeHdrFieldCreate(bufp, hdr_loc, &ce_loc); 
  TSMimeHdrFieldNameSet(bufp, hdr_loc, ce_loc, "Vary", -1);
  TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, ce_loc, -1, "Accept-Encoding", -1);
  TSMimeHdrFieldAppend(bufp, hdr_loc, ce_loc);
  TSHandleMLocRelease(bufp, hdr_loc, ce_loc);


  //os: since we alter the entity body, update the etag to something different as well  
  ce_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_ETAG, TS_MIME_LEN_ETAG);
  if (ce_loc){
    TSMimeHdrFieldValueAppend(bufp, hdr_loc, ce_loc, 0, "-df", 3);
    TSHandleMLocRelease(bufp, hdr_loc, ce_loc);
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  
  /* Get the output connection where we'll write data to. */
  output_conn = TSTransformOutputVConnGet(contp);

  data->output_buffer = TSIOBufferCreate();
  data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
  data->output_vio = TSVConnWrite(output_conn, contp, data->output_reader, INT64_MAX);
}


static void
gzip_transform_one(GzipData *data, TSIOBufferReader input_reader, int amount)
{
  TSIOBufferBlock blkp;
  const char *ibuf;
  char *obuf;
  int64_t ilength, olength;
  int err;

  TSDebug(PLUGIN_NAME, "gzip_transform_one");

  while (amount > 0) {
    TSDebug(PLUGIN_NAME, "gzip_transform_one->loop, amount: %d", amount);
    blkp = TSIOBufferReaderStart(input_reader);

    /* TSIOBufferReaderStart may return an error pointer */
    if (!blkp) {
       TSDebug(PLUGIN_NAME, "couldn't get from IOBufferBlock");
       TSError("couldn't get from IOBufferBlock");
       return;
    }

    TSDebug(PLUGIN_NAME, "gzip_transform_one->TSIOBufferReaderStart finished");
    ibuf = TSIOBufferBlockReadStart(blkp, input_reader, &ilength);
    
    /* TSIOBufferReaderStart may return an error pointer */
    if (!ibuf) {
       TSDebug(PLUGIN_NAME, "couldn't get from TSIOBufferBlockReadStart");
       TSError("couldn't get from TSIOBufferBlockReadStart");
       return;
    }
    
    TSDebug(PLUGIN_NAME, "gzip_transform_one->TSIOBufferBlockReadStart ilength: %" PRId64, ilength);
    
    if (ilength > amount) {
      ilength = amount;
    }

    data->zstrm.next_in = (unsigned char *) ibuf;
    data->zstrm.avail_in = ilength;

    while (data->zstrm.avail_in > 0) {
      TSDebug(PLUGIN_NAME, "gzip_transform_one->Tdata->zstrm.avail_in: %d", data->zstrm.avail_in);
      blkp = TSIOBufferStart(data->output_buffer);

      obuf = TSIOBufferBlockWriteStart(blkp, &olength);

      data->zstrm.next_out = (unsigned char *) obuf;
      data->zstrm.avail_out = olength;

      /* Encode */
      err = deflate(&data->zstrm, Z_NO_FLUSH);
      
      if (err != Z_OK)
         TSDebug(PLUGIN_NAME,"deflate() call failed: %d", err);
      
      if (olength > data->zstrm.avail_out) {
        TSIOBufferProduce(data->output_buffer, olength - data->zstrm.avail_out);
        data->output_length += (olength - data->zstrm.avail_out);
      }
      
      TSDebug(PLUGIN_NAME,"deflate() call values olength: %" PRId64 " , ilength: %" PRId64 ", data->output_length: %d",
              olength, ilength, data->output_length );
         
      if (data->zstrm.avail_out > 0) {
        if (data->zstrm.avail_in != 0) {
          TSError("gzip-transform: ERROR: avail_in is (%d): should be 0", data->zstrm.avail_in);
        }
      }
    }
    TSDebug(PLUGIN_NAME, "gzip_transform_one pre compute crc");
    /* compute CRC for error checking at client */
    data->crc = crc32(data->crc, (unsigned char *) ibuf, ilength);

    TSDebug(PLUGIN_NAME, "gzip_transform_one pre consume %" PRId64 " from reader", ilength);
    TSIOBufferReaderConsume(input_reader, ilength);
    amount -= ilength;
  }
  TSDebug(PLUGIN_NAME, "gzip_transform_one survived");
}


static void
gzip_transform_finish(GzipData *data)
{
  TSDebug(PLUGIN_NAME, "gzip_transform_finish");
  if (data->state == 1) {
    TSIOBufferBlock blkp;
    char *obuf;
    int64_t olength;
    int err;

    data->state = 2;

    for (;;) {
      blkp = TSIOBufferStart(data->output_buffer);

      obuf = TSIOBufferBlockWriteStart(blkp, &olength);
      data->zstrm.next_out = (unsigned char *) obuf;
      data->zstrm.avail_out = olength;

      /* Encode remaining data */
      err = deflate(&data->zstrm, Z_FINISH);

      if (olength > data->zstrm.avail_out) {
        TSIOBufferProduce(data->output_buffer, olength - data->zstrm.avail_out);
        data->output_length += (olength - data->zstrm.avail_out);
      }

      if (err == Z_OK) {        /* some more data to encode */
        continue;
      }
      /* done! */
      if (err != Z_STREAM_END) {
        TSDebug(PLUGIN_NAME, "deflate should report Z_STREAM_END");
      }
      break;
    }

    if (data->output_length != (data->zstrm.total_out)) {
      TSError("gzip-transform: ERROR: output lengths don't match (%d, %ld)", data->output_length,
               data->zstrm.total_out);
    }

    /* compute/append crc to end of stream */

    /*
       blkp = TSIOBufferStart (data->output_buffer);
       char crcbuf[8];
       char* buf=crcbuf;
       uLong tmp = data->crc;
       buf[0] = tmp & 0xff; tmp >>= 8;
       buf[1] = tmp & 0xff; tmp >>= 8;
       buf[2] = tmp & 0xff; tmp >>= 8;
       buf[3] = tmp & 0xff;

       tmp = data->zstrm.total_in;
       buf[4] = tmp & 0xff; tmp >>= 8;
       buf[5] = tmp & 0xff; tmp >>= 8;
       buf[6] = tmp & 0xff; tmp >>= 8;
       buf[7] = tmp & 0xff;

       char*p = buf;
       int length;
       length = 8;

       while (length > 0) {
       obuf = TSIOBufferBlockWriteStart (blkp, &olength);
       if (olength > length) {
       olength = length;
       }

       memcpy (obuf, p, olength);
       p += olength;
       length -= olength;

       TSIOBufferProduce (data->output_buffer, olength);
       }

       data->output_length += 8;*/
     
  }
}


static void
gzip_transform_do(TSCont contp)
{
  TSVIO write_vio;
  GzipData *data;
  int towrite;
  int avail;
  int length;
  
  TSDebug(PLUGIN_NAME, "gzip_transform_do");

   /* Get our data structure for this operation. The private data
     structure contains the output vio and output buffer. If the
     private data structure pointer is NULL, then we'll create it
     and initialize its internals. */
  data = TSContDataGet(contp);
  if (data->state == 0) {
    TSDebug(PLUGIN_NAME, "gzip_transform_do: data->state == 0, get_transform_init");
    gzip_transform_init(contp, data);
  } else {
    TSDebug(PLUGIN_NAME, "gzip_transform_do: data->state == %d, NO get_transform_init", data->state);
  }
  
  /* Get the write vio for the write operation that was performed on
     ourself. This vio contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = TSVConnWriteVIOGet(contp);

  length = data->output_length;
  TSDebug(PLUGIN_NAME, "gzip_transform_do: first length: %d", length);
  /* We also check to see if the write vio's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this simplistic
     transformation that means we're done. In a more complex
     transformation we might have to finish writing the transformed
     data to our output connection. */
  if (!TSVIOBufferGet(write_vio)) {
    TSDebug(PLUGIN_NAME, "gzip_transform_do->!TSVIOBufferGet(write_vio)...");
    gzip_transform_finish(data);

    TSVIONBytesSet(data->output_vio, data->output_length);
    TSDebug(PLUGIN_NAME, "Compressed size %d (bytes)", data->output_length);

    if (data->output_length > length) {
      TSDebug(PLUGIN_NAME, "gzip_transform_do->!reeanble data->output_vio");
      TSVIOReenable(data->output_vio);
    }
    return;
  }

  /* Determine how much data we have left to read. For this gzip
     transform plugin this is also the amount of data we have left
     to write to the output connection. */
  towrite = TSVIONTodoGet(write_vio);
  TSDebug(PLUGIN_NAME, "gzip_transform_do: towrite: %d", towrite);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
    TSDebug(PLUGIN_NAME, "gzip_transform_do: avail: %d", avail);    
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      TSDebug(PLUGIN_NAME, "gzip_transform_do: call gzip_transform_one");    
      gzip_transform_one(data, TSVIOReaderGet(write_vio), towrite);
      TSDebug(PLUGIN_NAME, "gzip_transform_do-> gzip_transform_one finished");
      /* Modify the write vio to reflect how much data we've
         completed. */
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
      TSDebug(PLUGIN_NAME, "gzip_transform_do-> TSVIONDoneSet finished");
    }
  }

  TSDebug(PLUGIN_NAME,"TSVIONTodoGet(write_vio) -> %" PRId64, TSVIONTodoGet(write_vio) );

  /* Now we check the write vio to see if there is data left to
     read. */
  if (TSVIONTodoGet(write_vio) > 0) {
    TSDebug(PLUGIN_NAME, "gzip_transform_do-> towrite: %d", towrite);
    if (towrite > 0) {
      /* If we output some data then we reenable the output
         connection by reenabling the output vio. This will wakeup
         the output connection and allow it to consume data from the
         output buffer. */
         
      TSDebug(PLUGIN_NAME, "gzip_transform_do: data->output_length > length? %d > %d",
              data->output_length, length);
      if (data->output_length > length) {
        TSDebug(PLUGIN_NAME, "gzip_transform_do-> vio renable");
        TSVIOReenable(data->output_vio);
        //return;
      }
      
      /* Call back the write vio continuation to let it know that we
         are ready for more data. */
      TSDebug(PLUGIN_NAME, "gzip_transform_do: TSContCall - TS_EVENT_VCONN_WRITE_READY");    
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
    }
  } else {
    TSDebug(PLUGIN_NAME, "gzip_transform_do-> towrite <= 0");
    /* If there is no data left to read, then we modify the output
       vio to reflect how much data the output connection should
       expect. This allows the output connection to know when it
       is done reading. We then reenable the output connection so
       that it can consume the data we just gave it. */
       
    TSDebug(PLUGIN_NAME, "gzip_transform_do-> pre gzip_transform_finish");
    gzip_transform_finish(data);
    TSDebug(PLUGIN_NAME, "gzip_transform_do-> post gzip_transform_finish");
    TSVIONBytesSet(data->output_vio, data->output_length);
    TSDebug(PLUGIN_NAME, "gzip_transform_do-> post TSVIONBytesSet");
    TSDebug(PLUGIN_NAME, "gzip_transform_do-> Compressed size %d (bytes)", data->output_length);

    if (data->output_length > length) {
      TSDebug(PLUGIN_NAME, "gzip_transform_do-> call TSVIOReenable");
      TSVIOReenable(data->output_vio);
    }

    /* Call back the write vio continuation to let it know that we
       have completed the write operation. */
    TSDebug(PLUGIN_NAME, "gzip_transform_do: TSContCall - TS_EVENT_VCONN_WRITE_COMPLETE");    
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }
  TSDebug(PLUGIN_NAME, "gzip_transform_do-> survived");
}


static int
gzip_transform(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "gzip_transform event %d", event);
  /* Check to see if the transformation has been closed by a call to
     TSVConnClose. */
  if (TSVConnClosedGet(contp)) {
    TSDebug(PLUGIN_NAME,"gzip_transform -> transformation has been closed");
    //.TSHttpTxnRespCacheableSet((TSHttpTxn)edata,0);
    gzip_data_destroy(TSContDataGet(contp));
    TSContDestroy(contp);
    return 0;
  } else {
      TSDebug(PLUGIN_NAME, "gzip_transform: switch on event");
    switch (event) {
    case TS_EVENT_ERROR:
      {
        TSDebug(PLUGIN_NAME, "gzip_transform: TS_EVENT_ERROR starts");
        TSVIO write_vio;

        /* Get the write vio for the write operation that was
           performed on ourself. This vio contains the continuation of
           our parent transformation. */
        write_vio = TSVConnWriteVIOGet(contp);

        /* Call back the write vio continuation to let it know that we
           have completed the write operation. */
        TSContCall(TSVIOContGet(write_vio), TS_EVENT_ERROR, write_vio);
      }
      break;

    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSDebug(PLUGIN_NAME, "gzip_transform: TS_EVENT_VCONN_WRITE_COMPLETE starts");
      /* When our output connection says that it has finished
         reading all the data we've written to it then we should
         shutdown the write portion of its connection to
         indicate that we don't want to hear about it anymore. */
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;

    case TS_EVENT_VCONN_WRITE_READY:
       TSDebug(PLUGIN_NAME, "gzip_transform: TS_EVENT_VCONN_WRITE_READY starts");
       /* If we get a WRITE_READY we'll attempt to transform more data. */
       gzip_transform_do(contp);    
       break;

    case TS_EVENT_IMMEDIATE:
        TSDebug(PLUGIN_NAME, "gzip_transform: TS_EVENT_IMMEDIATE received. how about spitting out an error?");
        gzip_transform_do(contp);    
        break;

    default:
      TSDebug(PLUGIN_NAME, "gzip_transform: default starts %d", event);
      /* If we get any other type of event (sent, perhaps, because we were reenabled) then
         we'll attempt to transform more data. */     
      gzip_transform_do(contp);    
      break;
    }
  }

  return 0;
}


static int
gzip_transformable(TSHttpTxn txnp, int server)
{
  // ToDo: This is pretty ugly, do we need a new scope here? XXX
  {
    TSMBuffer bufp;
    TSMLoc hdr_loc;
    TSHttpStatus resp_status;
    TSMLoc con_field;
    int con_len;
    const char *con_val;

    if (server) {
      TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc);
    } else {
      TSHttpTxnCachedRespGet(txnp, &bufp, &hdr_loc);
    }
    resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);

    con_field = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONNECTION, -1);
    if (con_field) {
      con_val = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, con_field, 0, &con_len);
      TSHandleMLocRelease(bufp, hdr_loc, con_field);

      //OS: !!!!!! FIXME !!!!!!! 
      //this is a hotfix for some weird behavior from an origin
      //needs to be patched properly. this disables support for transactions that send the connection:close header
      if (con_val && con_len == 5)
        {
          TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
          return -999;
        }
    }

    TSDebug(PLUGIN_NAME,"Got status %d", resp_status);
    if (TS_HTTP_STATUS_OK == resp_status) {
      if (TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc) != TS_SUCCESS) {
        TSError("Unable to release handle to server request");
      }
      //return 1;
    } else {
      if (TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc) != TS_SUCCESS) {
        TSError("Unable to release handle to server request");
      }
      return -100;
    }
  }

//  TSDebug(PLUGIN_NAME, "gzip_transformable");
  /* Server response header */
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc field_loc;

  /* Client request header */
  TSMBuffer cbuf;
  TSMLoc chdr;
  TSMLoc cfield;

  const char *value;
  int nvalues;
  int i, deflate_flag, len;

  TSHttpTxnClientReqGet(txnp, &cbuf, &chdr);

  /* check if client accepts "deflate" */
  cfield = TSMimeHdrFieldFind(cbuf, chdr, TS_MIME_FIELD_ACCEPT_ENCODING, -1);

  if (TS_NULL_MLOC != cfield) {
    nvalues = TSMimeHdrFieldValuesCount(cbuf, chdr, cfield);
        
    value = TSMimeHdrFieldValueStringGet(cbuf, chdr, cfield, 0, &len);
    deflate_flag = 0;
    i = 0;
    while (nvalues > 0) {
      if (value && (strncasecmp(value, "deflate", sizeof("deflate") - 1) == 0)) {
        deflate_flag = 1;
        break;
      }
      ++i;

      value = TSMimeHdrFieldValueStringGet(cbuf, chdr, cfield, i, &len);

      --nvalues;
    }

    if (!deflate_flag) {
      TSHandleMLocRelease(cbuf, chdr, cfield);
      TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);
      return -7;
    }

    TSHandleMLocRelease(cbuf, chdr, cfield);
    TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);
  } else {

    TSHandleMLocRelease(cbuf, chdr, cfield);
    TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);
    return -6;
  }

  if (server) {
    TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc);
  } else {
    TSHttpTxnCachedRespGet(txnp, &bufp, &hdr_loc);
  }

  /* If there already exists a content encoding then we don't want
     to do anything. */
  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_ENCODING, -1);
  if (field_loc) {
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return -3;
  }
  
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);

  /* We only want to do gzip compression on documents that have a
     content type of "text/" or "application/x-javascript". */

  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_TYPE, -1);
  if (!field_loc) {
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return -4;
  }

  value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, 0, &len);
  /*os: FIXME -> vary:accept-encoding needs to be added if any of these contenttypes is encountered
        */
  if ( len >= 5 && value && (strncasecmp(value, "text/", sizeof("text/") - 1) == 0)) {
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSDebug(PLUGIN_NAME, "@ transformable, it is text/*");
    return 0;
  } else if ( len >= (sizeof("application/x-javascript") -1) && value && (strncasecmp(value, "application/x-javascript", (sizeof("application/x-javascript") - 1)) == 0)) {
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSDebug(PLUGIN_NAME, "@ transformable, it is application/x-javascript");
    return 0;
  } else {
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSDebug(PLUGIN_NAME, "@ not a transformable content type");
    return -5;
  }
}


static void
gzip_transform_add(TSHttpTxn txnp, int server)
{
  TSVConn connp;
  GzipData *data;

  TSDebug(PLUGIN_NAME,"zip_transform_add -> tstransformcreate()");
  connp = TSTransformCreate(gzip_transform, txnp);
  
  TSDebug(PLUGIN_NAME,"zip_transform_add -> gzip_data_alloc()");
  data = gzip_data_alloc();
  data->txn = txnp;
  
  TSDebug(PLUGIN_NAME,"zip_transform_add -> TSContDataSet()");
  TSContDataSet(connp, data);

  TSDebug(PLUGIN_NAME,"zip_transform_add -> TSHttpTxnHookAdd()");
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}

static void 
normalize_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc)
{
//  const char *header = "Accept-Encoding";
//  int header_len = strlen(header);
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  int deflate = 0;
    
  //remove the accept encoding field(s), 
  //while finding out if deflate is supported.    
  while (field) {
    TSMLoc tmp;
      
    if (!deflate) {
      int value_count = TSMimeHdrFieldValuesCount(reqp, hdr_loc, field);

      while (value_count > 0) {
        int val_len = 0;
        const char* val;

        --value_count;
        val = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, field, value_count, &val_len);

        //FIXME
        //OS: these aren't always NULL terminated.
        if (val_len >=  7) //valgrind complains on the strstrs
          deflate = strstr(val, "deflate") != NULL;

        if (deflate)
          break;
      }
    }
      
    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);
    TSMimeHdrFieldDestroy(reqp, hdr_loc, field); //catch retval?
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }
   
  //if deflate is supported,
  //append a new accept-encoding field in the header
  if (deflate){
    TSMimeHdrFieldCreate(reqp, hdr_loc, &field);
    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
    TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, field, -1, "deflate", strlen("deflate"));
    TSMimeHdrFieldAppend(reqp, hdr_loc, field);
    TSHandleMLocRelease(reqp, hdr_loc, field);
  }
}


static int
transform_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;
  int reason;
  
  TSDebug(PLUGIN_NAME, "@ transform_plugin()");

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    {
      TSMBuffer req_buf;
      TSMLoc req_loc;
      if ( TSHttpTxnClientReqGet(txnp, &req_buf, &req_loc) == TS_SUCCESS ){
        normalize_accept_encoding(txnp, req_buf, req_loc);
        TSHandleMLocRelease(req_buf, TS_NULL_MLOC, req_loc);
      }
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    }
    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    reason = gzip_transformable(txnp, 1);
    if (reason >= 0) {
      TSDebug(PLUGIN_NAME, "server content transformable");
      gzip_transform_add(txnp, 1);
    } else {
      TSDebug(PLUGIN_NAME, "server content NOT transformable [%d]", reason);
    }

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_READ_CACHE_HDR:
    reason = gzip_transformable(txnp, 0);
    if (reason >= 0) {
      TSDebug(PLUGIN_NAME, "cached content transformable");
      gzip_transform_add(txnp, 1);
    } else {
      TSDebug(PLUGIN_NAME, "cached data:  forwarding unchanged (%d)", reason);
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  default:
    TSError("gzip transform unknown event, exit!");
    exit(1);
  }

  return 0;
}


void
TSPluginInit(int argc, const char *argv[])
{
  dictId = adler32(0L, Z_NULL, 0);
  if (argc == 2) {
    strcpy(preload_file, argv[1]);
    preload = 1;
    load_dictionary(dictionary, &dictId);
  }

  TSDebug(PLUGIN_NAME, "gzip plugin loads");

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(transform_plugin, NULL));
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(transform_plugin, NULL));
  //TSHttpHookAdd(TS_HTTP_READ_CACHE_HDR_HOOK, TSContCreate(transform_plugin, NULL));
}
