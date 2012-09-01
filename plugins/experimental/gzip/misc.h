#ifndef _GZIP_MISC_H_
#define _GZIP_MISC_H_

#include <zlib.h>
#include <ts/ts.h>
#include <stdlib.h>             //exit()
#include <stdio.h>


//zlib stuff, see [deflateInit2] at http://www.zlib.net/manual.html
static const int ZLIB_MEMLEVEL = 9;     //min=1 (optimize for memory),max=9 (optimized for speed)
static const int WINDOW_BITS_DEFLATE = -15;
static const int WINDOW_BITS_GZIP = 31;

//misc
static const int COMPRESSION_TYPE_DEFLATE = 1;
static const int COMPRESSION_TYPE_GZIP = 2;
static const int HOOK_SET = 1;
static const int DICT_PATH_MAX = 512;
static const int DICT_ENTRY_MAX = 2048;

//this one is used to rename the accept encoding header
//it will be restored later on
//to make it work, the name must be different then downstream proxies though
//otherwise the downstream will restore the accept encoding header
char *hidden_header_name;


enum transform_state
{
  transform_state_initialized,
  transform_state_output,
  transform_state_finished
};

typedef struct
{
  TSHttpTxn txn;
  TSVIO downstream_vio;
  TSIOBuffer downstream_buffer;
  TSIOBufferReader downstream_reader;
  int downstream_length;
  z_stream zstrm;
  enum transform_state state;
  int compression_type;
} GzipData;


voidpf gzip_alloc(voidpf opaque, uInt items, uInt size);
void gzip_free(voidpf opaque, voidpf address);
void normalize_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc);
void hide_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc);
void restore_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc);
void init_hidden_header_name();
int check_ts_version();
int register_plugin();
const char *load_dictionary(const char *preload_file);
void gzip_log_ratio(int64_t in, int64_t out);

#endif
