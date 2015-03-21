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
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "zstream.h"

namespace spdy
{
const uint8_t dictionary[] = "optionsgetheadpostputdeletetraceacceptaccept-charsetaccept-encodingaccept-"
                             "languageauthorizationexpectfromhostif-modified-sinceif-matchif-none-matchi"
                             "f-rangeif-unmodifiedsincemax-forwardsproxy-authorizationrangerefererteuser"
                             "-agent10010120020120220320420520630030130230330430530630740040140240340440"
                             "5406407408409410411412413414415416417500501502503504505accept-rangesageeta"
                             "glocationproxy-authenticatepublicretry-afterservervarywarningwww-authentic"
                             "ateallowcontent-basecontent-encodingcache-controlconnectiondatetrailertran"
                             "sfer-encodingupgradeviawarningcontent-languagecontent-lengthcontent-locati"
                             "oncontent-md5content-rangecontent-typeetagexpireslast-modifiedset-cookieMo"
                             "ndayTuesdayWednesdayThursdayFridaySaturdaySundayJanFebMarAprMayJunJulAugSe"
                             "pOctNovDecchunkedtext/htmlimage/pngimage/jpgimage/gifapplication/xmlapplic"
                             "ation/xhtmltext/plainpublicmax-agecharset=iso-8859-1utf-8gzipdeflateHTTP/1"
                             ".1statusversionurl";

#if NOTYET
unsigned long
dictionary_id()
{
  unsigned long id;

  id = adler32(0L, Z_NULL, 0);
  id = adler32(id, dictionary, sizeof(dictionary));
  return id;
}
#endif

static zstream_error
map_zerror(int error)
{
  const zstream_error z_errors[] = {
    z_version_error, // Z_VERSION_ERROR  (-6)
    z_buffer_error,  // Z_BUF_ERROR      (-5)
    z_memory_error,  // Z_MEM_ERROR      (-4)
    z_data_error,    // Z_DATA_ERROR     (-3)
    z_stream_error,  // Z_STREAM_ERROR   (-2)
    z_errno,         // Z_ERRNO          (-1)
    z_ok,            // Z_OK             ( 0)
    z_stream_end,    // Z_STREAM_END     ( 1)
    z_need_dict      // Z_NEED_DICT      ( 2)
  };
  const zstream_error *z = &z_errors[6];
  return z[error];
}

zstream_error
decompress::init(z_stream *zstr)
{
  return map_zerror(inflateInit(zstr));
}

zstream_error
decompress::transact(z_stream *zstr, int flush)
{
  int ret = inflate(zstr, flush);
  if (ret == Z_NEED_DICT) {
    // The spec says that the trailing NULL is not included in the
    // dictionary, but in practice, Chrome does include it.
    ret = inflateSetDictionary(zstr, dictionary, sizeof(dictionary));
    if (ret == Z_OK) {
      ret = inflate(zstr, flush);
    }
  }

  return map_zerror(ret);
}

zstream_error
decompress::destroy(z_stream *zstr)
{
  return map_zerror(inflateEnd(zstr));
}

zstream_error
compress::init(z_stream *zstr)
{
  zstream_error status;

  status = map_zerror(deflateInit(zstr, Z_DEFAULT_COMPRESSION));
  if (status != z_ok) {
    return status;
  }

  return map_zerror(deflateSetDictionary(zstr, dictionary, sizeof(dictionary)));
}

zstream_error
compress::transact(z_stream *zstr, int flush)
{
  int ret = deflate(zstr, flush);
  if (ret == Z_NEED_DICT) {
    ret = deflateSetDictionary(zstr, dictionary, sizeof(dictionary));
    if (ret == Z_OK) {
      ret = deflate(zstr, flush);
    }
  }

  return map_zerror(ret);
}

zstream_error
compress::destroy(z_stream *zstr)
{
  return map_zerror(deflateEnd(zstr));
}

} // namespace spdy
/* vim: set sw=4 ts=4 tw=79 et : */
