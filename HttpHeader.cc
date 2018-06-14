#include "HttpHeader.h"

#include "slice.h"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <iostream>

namespace
{

char const * const skipmestr = "X-Skip-Me";
char const * const yesstr = "absolutely";

std::pair<int64_t, int64_t>
parseRange
  ( char const * const rangestr
  )
{
  std::pair<int64_t, int64_t> frontback(0, -1);

  static char const DELIM_DASH = '-';
//  static char const DELIM_MULTI = ',';
  static char const * const BYTESTR = "bytes=";
  static size_t const BYTESTRLEN = 6;

  // make sure this is in byte units
  if (0 != strncmp(BYTESTR, rangestr, BYTESTRLEN)) {
    return frontback;
  }

  // advance past any white space
  char const * pfront = rangestr + BYTESTRLEN;
  while ('\0' != *pfront && isblank(*pfront)) {
    ++pfront;
  }

  // check for last N request
  if ('-' == *pfront) {
    ERROR_LOG("Last N byte request not handled");
    return frontback;
  }

  if ('\0' == *pfront) {
    ERROR_LOG("First Range number not found in '%s'", rangestr);
    return frontback;
  }

  char const * const pdash = strchr(pfront, DELIM_DASH);
  if (nullptr == pdash) {
    ERROR_LOG("Range Delim '%c' not found", DELIM_DASH);
    return frontback;
  }

  // interpret front value
  char * pfe = nullptr;
  int64_t const front = strtoll(pfront, &pfe, 10);

  if (pfe == pfront) {
    ERROR_LOG("Range front invalid: '%s'", rangestr);
   return frontback;
  }

  char const * pback = pdash + 1;

  // interpret back value
  char * pfb = nullptr;
  int64_t back = strtoll(pback, &pfb, 10);

  if (pfb == pback) { // blank value
    back = std::numeric_limits<int64_t>::max();
  }

  if (front <= back)
  {
    frontback = std::make_pair(front, back);
  }

  return frontback;
} // parseRange

} // namespace

char const *
HttpHeader :: method
  () const
{
  TSAssert(isValid());
  int methodlen = 0;
  return TSHttpHdrMethodGet(m_buffer, m_lochdr, &methodlen);
}

std::pair<int64_t, int64_t>
HttpHeader :: firstRange
   () const
{
  std::pair<int64_t, int64_t> range
    (0, std::numeric_limits<int64_t>::max());

  TSMLoc const locfield
    ( TSMimeHdrFieldFind
      ( m_buffer
      , m_lochdr
      , TS_MIME_FIELD_RANGE
      , TS_MIME_LEN_RANGE ) );

  if (nullptr != locfield)
  {
    int value_len = 0;
    char const * const value = TSMimeHdrFieldValueStringGet
        (m_buffer, m_lochdr, locfield, 0, &value_len);

    if (0 < value_len && value_len < 255)
    {
      char rangearr[256];
      memcpy(rangearr, value, value_len);
      rangearr[value_len + 1] = '\0';
      range = parseRange(rangearr);
    }

    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
  }
  
  return range;
}

bool
HttpHeader :: skipMe
  () const
{
  if (! isValid())
  {
    return false;
  }

  TSMLoc const locfield(TSMimeHdrFieldFind
    (m_buffer, m_lochdr, skipmestr, strlen(skipmestr)));
  if (nullptr != locfield)
  {
    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
    return true;
  }

  return false;
}

bool
HttpHeader :: setSkipMe
  ()
{
  if (! isValid())
  {
    return false;
  }

  bool status(false);

  TSMLoc locfield;
  int rcode(TSMimeHdrFieldCreateNamed
    (m_buffer, m_lochdr, skipmestr, strlen(skipmestr), &locfield));
  if (TS_SUCCESS == rcode)
  {
    rcode = TSMimeHdrFieldValueStringSet
      (m_buffer, m_lochdr, locfield, 0, yesstr, strlen(yesstr));
    if (TS_SUCCESS == rcode)
    {
      rcode = TSMimeHdrFieldAppend(m_buffer, m_lochdr, locfield);
      status = (TS_SUCCESS == rcode);
    }

    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
  }

  return status;
}

int64_t
HttpHeader :: contentBytes
  () const
{
  int64_t bytes(0);
  if (! isValid())
  {
    return bytes;
  }

  TSMLoc const locfield
    ( TSMimeHdrFieldFind
      ( m_buffer
      , m_lochdr
      , TS_MIME_FIELD_CONTENT_LENGTH
      , TS_MIME_LEN_CONTENT_LENGTH ) );

  if (nullptr != locfield)
  {
    bytes = TSMimeHdrFieldValueInt64Get
      (m_buffer, m_lochdr, locfield, -1);
    TSHandleMLocRelease(m_buffer, m_lochdr, locfield);
  }

  return bytes;
}

/////// ParseHdrMgr

TSParseResult
ParseHdrMgr :: populateFrom
  ( TSHttpParser const http_parser
  , TSIOBufferReader const reader
  , HeaderParseFunc const parsefunc
  )
{
  TSParseResult parse_res = TS_PARSE_CONT;

  if (nullptr == m_buffer && nullptr == m_lochdr)
  {
    m_buffer = TSMBufferCreate();
    m_lochdr = TSHttpHdrCreate(m_buffer);
  }

  int64_t read_avail = TSIOBufferReaderAvail(reader);
  if (0 < read_avail)
  {
    TSIOBufferBlock block = TSIOBufferReaderStart(reader);
    int64_t consumed = 0;

    parse_res = TS_PARSE_CONT;

    while (nullptr != block && 0 < read_avail)
    {
      int64_t blockbytes = 0;
      char const * const bstart = TSIOBufferBlockReadStart
        (block, reader, &blockbytes);

      char const * ptr = bstart;
      char const * endptr = ptr + blockbytes;

      parse_res = parsefunc
          ( http_parser
          , m_buffer
          , m_lochdr
          , &ptr
          , endptr );

      int64_t const bytes_parsed(ptr - bstart);

      consumed += bytes_parsed;
      read_avail -= bytes_parsed;

      if (TS_PARSE_CONT == parse_res)
      {
        block = TSIOBufferBlockNext(block);
      }
      else
      {
        break;
      }
    }
    TSIOBufferReaderConsume(reader, consumed);
  }

  return parse_res;
}
