/** @file

  Transforms content using gzip or deflate

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

#define COMPRESSION_TYPE_DEFLATE 1
#define COMPRESSION_TYPE_GZIP 2

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
#define CACHE_TRANSFORMED_RESPONSES 0
#define REMOVE_SERVER_REQUEST_ENCODING 1

const char* PLUGIN_NAME = "gzip";

int arg_idx_hooked;
int hook_set = 1;
char * hidden_header_name;

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
  int compression_type;
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
  return (voidpf) TSmalloc(items * size);
}


static void
gzip_free(voidpf opaque, voidpf address)
{
  TSfree(address);
}


static GzipData *
gzip_data_alloc(int compression_type)
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
  data->compression_type = compression_type;
  data->crc = crc32(0L, Z_NULL, 0); //os: not used?
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

  int window_size = -15;//deflate
  if (compression_type == COMPRESSION_TYPE_GZIP) 
    window_size = 15 + 16;//gzip

  TSDebug("gzip","initializing window size %d", window_size);

  err = deflateInit2(
        &data->zstrm, 
        MOD_GZIP_DEFLATE_DEFAULT_COMPRESSION_LEVEL,
        Z_DEFLATED,
        window_size,
        MOD_GZIP_ZLIB_CFACTOR,
        Z_DEFAULT_STRATEGY);

  if (err != Z_OK) {
    TSDebug("gzip", "deflate init error %d", err);
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

  return data;
}


static void
gzip_data_destroy(GzipData *data)
{
  int err;

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

  if (data->compression_type == COMPRESSION_TYPE_DEFLATE) {
    TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, ce_loc, -1, "deflate", -1);
  } else if (data->compression_type == COMPRESSION_TYPE_GZIP) {
    TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, ce_loc, -1, "gzip", -1);
  } 

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

  if (ce_loc) {
    int changetag = 1;
    int strl;
    const char * strv = TSMimeHdrFieldValueStringGet(bufp,hdr_loc,ce_loc,-1,&strl);
    //do not alter weak etags.
    //FIXME: consider just making the etag weak for compressed content
    if (strl>=2) {
      if ( ( strv[0] == 'w' || strv[0] == 'W') && strv[1] == '/'  ) {
        changetag=0;
      }
      if (changetag) {
	TSMimeHdrFieldValueAppend(bufp,hdr_loc,ce_loc,0,"-df",3);
      }

      TSHandleMLocRelease(bufp, hdr_loc, ce_loc);
    }
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

  while (amount > 0) {
    blkp = TSIOBufferReaderStart(input_reader);

    /* TSIOBufferReaderStart may return an error pointer */
    if (!blkp) {
       TSDebug(PLUGIN_NAME, "couldn't get from IOBufferBlock");
       TSError("couldn't get from IOBufferBlock");
       return;
    }

    ibuf = TSIOBufferBlockReadStart(blkp, input_reader, &ilength);
    
    /* TSIOBufferReaderStart may return an error pointer */
    if (!ibuf) {
       TSDebug(PLUGIN_NAME, "couldn't get from TSIOBufferBlockReadStart");
       TSError("couldn't get from TSIOBufferBlockReadStart");
       return;
    }
    
    if (ilength > amount) {
      ilength = amount;
    }

    data->zstrm.next_in = (unsigned char *) ibuf;
    data->zstrm.avail_in = ilength;

    while (data->zstrm.avail_in > 0) {
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
      
      if (data->zstrm.avail_out > 0) {
        if (data->zstrm.avail_in != 0) {
          TSError("gzip-transform: ERROR: avail_in is (%d): should be 0", data->zstrm.avail_in);
        }
      }
    }

    /* compute CRC for error checking at client */
    data->crc = crc32(data->crc, (unsigned char *) ibuf, ilength);

    TSIOBufferReaderConsume(input_reader, ilength);
    amount -= ilength;
  }
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
    if (data->compression_type == COMPRESSION_TYPE_GZIP) { 
      char*p = (char *)&(data->zstrm.adler);
      int length = 8;
      while (length > 0) {
	obuf = TSIOBufferBlockWriteStart (blkp, &olength);
	if (olength > length) 
	  olength = length;
	memcpy (obuf, p, olength);
	p += olength;
	length -= olength;
	TSIOBufferProduce (data->output_buffer, olength);
      }
      data->output_length += 8;
    }
  }
}


static void
gzip_transform_do(TSCont contp)
{
  TSVIO write_vio;
  GzipData *data;
  int64_t towrite;
  int64_t avail;
  int64_t length;
  
  data = TSContDataGet(contp);
  if (data->state == 0) {
    gzip_transform_init(contp, data);
  }

  write_vio = TSVConnWriteVIOGet(contp);

  length = data->output_length;

  if (!TSVIOBufferGet(write_vio)) {
    gzip_transform_finish(data);

    TSVIONBytesSet(data->output_vio, data->output_length);
    TSDebug(PLUGIN_NAME, "Compressed size %d (bytes)", data->output_length);

    if (data->output_length > length) {
      TSVIOReenable(data->output_vio);
    }
    return;
  }

  towrite = TSVIONTodoGet(write_vio);

  if (towrite > 0) {
    avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));

    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      gzip_transform_one(data, TSVIOReaderGet(write_vio), towrite);
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
    }
  }

  if (TSVIONTodoGet(write_vio) > 0) {

    if (towrite > 0) {
      if (data->output_length > length) {
        TSVIOReenable(data->output_vio);
      }      
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
    }
  } else {
    gzip_transform_finish(data);
    TSVIONBytesSet(data->output_vio, data->output_length);
    TSDebug(PLUGIN_NAME, "gzip_transform_do-> Compressed size %d (bytes)", data->output_length);

    if (data->output_length > length) {
      TSVIOReenable(data->output_vio);
    }

    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }
}


static int
gzip_transform(TSCont contp, TSEvent event, void *edata)
{
  if (TSVConnClosedGet(contp)) {
    gzip_data_destroy(TSContDataGet(contp));
    TSContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case TS_EVENT_ERROR:
      {
        TSDebug(PLUGIN_NAME, "gzip_transform: TS_EVENT_ERROR starts");
        TSVIO write_vio = TSVConnWriteVIOGet(contp);
        TSContCall(TSVIOContGet(write_vio), TS_EVENT_ERROR, write_vio);
      }
      break;

    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;

    case TS_EVENT_VCONN_WRITE_READY:
       gzip_transform_do(contp);    
       break;

    case TS_EVENT_IMMEDIATE:
        gzip_transform_do(contp);    
        break;

    default:
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
  cfield = TSMimeHdrFieldFind(cbuf, chdr, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);

  if (TS_NULL_MLOC != cfield) {
    nvalues = TSMimeHdrFieldValuesCount(cbuf, chdr, cfield);
        
    value = TSMimeHdrFieldValueStringGet(cbuf, chdr, cfield, 0, &len);
    deflate_flag = 0;
    i = 0;
    while (nvalues > 0) {
      if (value && (strncasecmp(value, "deflate", strlen("deflate")) == 0)) {
        deflate_flag = 1;
        break;
      } else if (value && (strncasecmp(value, "gzip", strlen("gzip")) == 0)){
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
  int * tmp  = (int *)TSHttpTxnArgGet(txnp, arg_idx_hooked);

  if ( CACHE_TRANSFORMED_RESPONSES ) { 
    TSHttpTxnUntransformedRespCache(txnp, 1);
    TSHttpTxnTransformedRespCache(txnp, 0);
  } else { 
    TSHttpTxnTransformedRespCache(txnp, 0);
    TSHttpTxnUntransformedRespCache(txnp, 1);
  }

  if ( tmp ) {
    TSDebug("gzip", "hook already set, bail");
    return;
  } else {
    TSHttpTxnArgSet(txnp, arg_idx_hooked, &hook_set);
    TSDebug("gzip", "adding compression transform");
  }

  TSVConn connp;
  GzipData *data;

  TSDebug(PLUGIN_NAME,"zip_transform_add -> tstransformcreate()");
  connp = TSTransformCreate(gzip_transform, txnp);
  
  TSDebug(PLUGIN_NAME,"zip_transform_add -> gzip_data_alloc()");


  /* Client request header */
  TSMBuffer cbuf;
  TSMLoc chdr;
  TSMLoc cfield;
  int len;
  int gzip=0;

  TSHttpTxnClientReqGet(txnp, &cbuf, &chdr);
  cfield = TSMimeHdrFieldFind(cbuf, chdr, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  TSMimeHdrFieldValueStringGet(cbuf, chdr, cfield, 0, &len);

  if (len == 4) gzip=1;
  TSHandleMLocRelease(cbuf, chdr, cfield);  
  TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);



  data = gzip_data_alloc(gzip ? COMPRESSION_TYPE_GZIP : COMPRESSION_TYPE_DEFLATE);
  data->txn = txnp;
  
  TSDebug(PLUGIN_NAME,"zip_transform_add -> TSContDataSet()");
  TSContDataSet(connp, data);

  TSDebug(PLUGIN_NAME,"zip_transform_add -> TSHttpTxnHookAdd()");
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}

static void 
normalize_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc)
{
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  int deflate = 0;
  int gzip = 0;
    
  //remove the accept encoding field(s), 
  //while finding out if deflate is supported.    
  while (field) {
    TSMLoc tmp;
      
    if (!deflate && !gzip) {
      int value_count = TSMimeHdrFieldValuesCount(reqp, hdr_loc, field);

      while (value_count > 0) {
        int val_len = 0;
        const char* val;

        --value_count;
        val = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, field, value_count, &val_len);

	if (val_len == strlen("gzip"))
	  gzip = !strncmp(val, "gzip", val_len);
        else if (val_len ==  strlen("deflate"))
          deflate = !strncmp(val, "deflate", val_len);
      }
    }

    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);
    TSMimeHdrFieldDestroy(reqp, hdr_loc, field); //catch retval?
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }
   
  //append a new accept-encoding field in the header
  if (deflate || gzip){
    TSMimeHdrFieldCreate(reqp, hdr_loc, &field);
    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);

    if (gzip) {
      TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, field, -1, "gzip", strlen("gzip"));
      TSDebug("gzip","normalized accept encoding to gzip");
    }
    else if (deflate) {
      TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, field, -1, "deflate", strlen("deflate"));
      TSDebug("gzip","normalized accept encoding to deflate");
    }

    TSMimeHdrFieldAppend(reqp, hdr_loc, field);
    TSHandleMLocRelease(reqp, hdr_loc, field);
  } 
}

static int
cache_transformable(TSHttpTxn txnp) {
  int obj_status;
  if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
    TSError("[%s] Couldn't get cache status of object", __FUNCTION__);
    TSDebug("gzip_lu","[%s] Couldn't get cache status of object", __FUNCTION__);
    return 0;
  }
  if ((obj_status == TS_CACHE_LOOKUP_HIT_FRESH) /*|| (obj_status == TS_CACHE_LOOKUP_HIT_STALE)*/) {
    TSDebug("gzip_lu", "[%s] doc found in cache, will add transformation", __FUNCTION__);
    return 1;
  }
  TSDebug("gzip_lu", "[%s] cache object's status is %d; not transformable",
          __FUNCTION__, obj_status);
  return 0;
}

static void
kill_request_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc)
{
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING
                                    , TS_MIME_LEN_ACCEPT_ENCODING);

  while (field) {
    TSMLoc tmp;
    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);


    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, hidden_header_name, -1);
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }
}


static void
restore_request_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc)
{
  TSMLoc field = TSMimeHdrFieldFind(reqp, hdr_loc, hidden_header_name, -1);

  while (field) {
    TSMLoc tmp;
    tmp = TSMimeHdrFieldNextDup(reqp, hdr_loc, field);

    TSMimeHdrFieldNameSet(reqp, hdr_loc, field, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
    TSHandleMLocRelease(reqp, hdr_loc, field);
    field = tmp;
  }
}

static int
transform_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn) edata;
  int reason;
  
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

  case TS_EVENT_HTTP_READ_RESPONSE_HDR: {
    //os: boy oh boy. the accept encoding header needs to be restored
    //otherwise alt selection will fail. hopefully a better solution exists
    //then this header shuffle
    TSMBuffer req_buf;
    TSMLoc req_loc;
    
    if ( TSHttpTxnServerReqGet(txnp, &req_buf, &req_loc) == TS_SUCCESS ){
      if (REMOVE_SERVER_REQUEST_ENCODING) { 
	restore_request_accept_encoding(txnp, req_buf, req_loc);
      }
      TSHandleMLocRelease(req_buf, TS_NULL_MLOC, req_loc);
    }

    reason = gzip_transformable(txnp, 1);
    if (reason >= 0) {
      TSDebug(PLUGIN_NAME, "server content transformable");
      gzip_transform_add(txnp, 1);
    } else {
      TSDebug(PLUGIN_NAME, "server content NOT transformable [%d]", reason);
    }

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }  break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR: {
    if (REMOVE_SERVER_REQUEST_ENCODING) { 
      TSMBuffer req_buf;
      TSMLoc req_loc;
      if ( TSHttpTxnServerReqGet(txnp, &req_buf, &req_loc) == TS_SUCCESS ){
	kill_request_accept_encoding(txnp, req_buf, req_loc);
	TSHandleMLocRelease(req_buf, TS_NULL_MLOC, req_loc);
      }
    }

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }  break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    reason = cache_transformable(txnp);
    if (reason) {
      reason = gzip_transformable(txnp, 0);
      if (reason >= 0) {
        TSDebug("gzip-transform", "cached content transformable");
        gzip_transform_add(txnp, 0);
      } else {
        TSDebug("gzip-transform", "cached data:  forwarding unchanged (%d)", reason);
      }
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


  TSDebug("gzip-transform", "gzip plugin loads");

  if (TSHttpArgIndexReserve("gzip", "for remembering if the hook was set", &arg_idx_hooked) != TS_SUCCESS) {
    TSError("failed to reserve an argument index");
    exit(-1);
  }

  const char* var_name = "proxy.config.proxy_name";
  TSMgmtString result;

  if ( TSMgmtStringGet( var_name, &result) != TS_SUCCESS ) {
    TSDebug("gzip", "failed to get server name");
    exit(-1);
  } else {
    TSDebug("gzip", "got server name: %s", result);

    int hidden_header_name_len = strlen("x-accept-encoding-") + strlen(result);
    hidden_header_name = (char *)TSmalloc( hidden_header_name_len + 1 );
    result[hidden_header_name_len] = 0;
    sprintf( hidden_header_name, "x-accept-encoding-%s", result);
    TSDebug("gzip", "hidden header name: %s / %ld", hidden_header_name, strlen(hidden_header_name));
  }



  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(transform_plugin, NULL));
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(transform_plugin, NULL));
  TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, TSContCreate(transform_plugin, NULL));
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, TSContCreate(transform_plugin, NULL));
}
