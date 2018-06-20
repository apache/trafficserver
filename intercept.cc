#include "intercept.h"

#include "ContentRange.h"
#include "Data.h"
#include "HttpHeader.h"
#include "range.h"
#include "slice.h"

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>

static
void
shutdown
  ( TSCont contp
  , Data * const data
  )
{
  delete data;
  TSContDataSet(contp, nullptr);
  TSContDestroy(contp);
}

// create and issue a block request
static
int
request_block
  ( TSCont contp
  , Data * const data
  )
{
  std::pair<int64_t, int64_t> blockbe
    (range::forBlock(data->m_blocksize, data->m_blocknum));

  // fix up end of block for full content

//std::cerr << __func__ << " trying to build header" << std::endl;

  char rangestr[1024];
  int rangelen = 1023;
  bool const rpstat = range::closedStringFor
    (blockbe, rangestr, &rangelen);
TSAssert(rpstat);

DEBUG_LOG("request_block: %*s", rangelen, rangestr);

  // reuse the incoming client header, just change the range
  HttpHeader header
    ( data->m_dnstream.m_hdr_mgr.m_buffer
    , data->m_dnstream.m_hdr_mgr.m_lochdr );

  // add/set sub range key and add slicer tag
  bool const rangestat = header.setKeyVal
    ( TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE
    , rangestr, rangelen );
TSAssert(rangestat);

/*
  if ( nullptr == data->m_upstream.m_vc
    || TSVConnClosedGet(data->m_upstream.m_vc) )
*/
  {
//std::cerr << "having to create a virtual connection" << std::endl;
    // create virtual connection back into ATS
    TSVConn const upvc = TSHttpConnectWithPluginId
      ((sockaddr*)&data->m_client_ip, "slicer", 0);

    // set up connection with the HttpConnect server
    data->m_upstream.setupConnection(upvc);
    data->m_upstream.setupVioWrite(contp);
  }
/*
  else
  {
std::cerr << "trying to reuse http connect" << std::endl;
  }
*/

/*
std::cerr << std::endl;
std::cerr << __func__ << " sending header to server" << std::endl;
std::cerr << header.toString() << std::endl;
*/

  TSHttpHdrPrint
    ( header.m_buffer
    , header.m_lochdr
    , data->m_upstream.m_write.m_iobuf );

  TSVIOReenable(data->m_upstream.m_write.m_vio); // not necessary signal

  // get ready for data back from the server
  data->m_upstream.setupVioRead(contp);

  // anticipate the next server response header
  TSHttpParserClear(data->httpParser());
  data->m_upstream.m_hdr_mgr.resetHeader();


  return TS_EVENT_CONTINUE;
}

// this is called once per transaction when the client sends a req header
static
int
handle_client_req
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (TS_EVENT_VCONN_READ_READY == event)
  {
DEBUG_LOG("client has data ready to read");
    // the client request header didn't fit into the input buffer:
    if (TS_PARSE_DONE != data->m_dnstream.m_hdr_mgr.populateFrom
        ( data->httpParser()
        , data->m_dnstream.m_read.m_reader
        , TSHttpHdrParseReq ) )
    {
      return TS_EVENT_CONTINUE;
    }

    // get the header
    HttpHeader header
      ( data->m_dnstream.m_hdr_mgr.m_buffer
      , data->m_dnstream.m_hdr_mgr.m_lochdr );

/*
std::cerr << std::endl;
std::cerr << __func__ << " received header from client" << std::endl;
std::cerr << header.toString() << std::endl;
*/

    // default: whole file (unknown, wait for first server response)
    std::pair<int64_t, int64_t> rangebe
      (0, std::numeric_limits<int64_t>::max() - data->m_blocksize);

    char rangestr[1024];
    int rangelen = 1024;
    bool const rstat = header.valueForKey
      ( TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE
      , rangestr, &rangelen, 0 ); // <-- first range only
    if (rstat)
    {
      // write parsed header into slicer meta tag
      header.setKeyVal
        ( SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO)
        , rangestr, rangelen );
      rangebe = range::parseHalfOpenFrom(rangestr);
    }
    else
    {
static char const * const valstr = "200 request";
static size_t const vallen = strlen(valstr);
      header.setKeyVal
        ( SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO)
        , valstr, vallen );
    }

    if (! range::isValid(rangebe))
    {
        // send 416 header and shutdown
std::cerr << "Please send a 416 header and shutdown" << std::endl;
      shutdown(contp, data);
      return TS_EVENT_CONTINUE;
    }

     // set the initial range begin/end, we'll correct it later
     data->m_range_begend = rangebe;

     // set to the first block in range
     data->m_blocknum = range::firstBlock
       (data->m_blocksize, data->m_range_begend);

    // whack some ATS keys (avoid 404)
    header.removeKey
      (TS_MIME_FIELD_VIA, TS_MIME_LEN_VIA);
    header.removeKey
      (TS_MIME_FIELD_X_FORWARDED_FOR, TS_MIME_LEN_X_FORWARDED_FOR);

    // send the first block request to server
    request_block(contp, data);
  }

  return TS_EVENT_CONTINUE;
}

// transfer bytes from the server to the client
static
int64_t
transfer_content_bytes
  ( Data * const data
  )
{
DEBUG_LOG("transfer_content_bytes");
  int64_t consumed = 0;
  int64_t read_avail = TSIOBufferReaderAvail
    (data->m_upstream.m_read.m_reader);

  // handle offset into (first) block
  int64_t const toskip(std::min(data->m_skipbytes, read_avail));
  if (0 < toskip)
  {
    TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, toskip);
    data->m_skipbytes -= toskip;
    read_avail -= toskip;
  }

  if (0 < read_avail)
  {
    int64_t const bytesleft(data->m_bytestosend - data->m_bytessent);
    int64_t const tocopy(std::min(read_avail, bytesleft));

std::cerr << "read_avail: " << read_avail << std::endl;
std::cerr << "bytesleft: " << bytesleft << std::endl;
std::cerr << "tocopy: " << tocopy << std::endl;
std::cerr << "contentlen: " << data->m_contentlen << std::endl;

    int64_t const copied
      ( TSIOBufferCopy
        ( data->m_dnstream.m_write.m_iobuf
        , data->m_upstream.m_read.m_reader
        , tocopy
        , 0 ) );

    data->m_bytessent += copied;

    TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, copied);
    TSVIOReenable(data->m_dnstream.m_write.m_vio);

    consumed = copied;
  }

  return consumed;
}

// this is called every time the server has data for us
static
int
handle_server_resp
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (TS_EVENT_VCONN_READ_READY == event)
  {
DEBUG_LOG("server has data ready to read");
    // has the first server reponse header been parsed??
    if (! data->m_server_block_header_parsed)
    {
      // the server response header didn't fit into the input buffer??
      if ( TS_PARSE_DONE != data->m_upstream.m_hdr_mgr.populateFrom
            ( data->httpParser()
            , data->m_upstream.m_read.m_reader
            , TSHttpHdrParseResp ) )
      {
        return TS_EVENT_CONTINUE;
      }

      HttpHeader header
        ( data->m_upstream.m_hdr_mgr.m_buffer
        , data->m_upstream.m_hdr_mgr.m_lochdr );

std::cerr << std::endl;
std::cerr << "got a response header from server" << std::endl;
std::cerr << header.toString() << std::endl;

      // only process a 206, everything else gets a pass through
      if (TS_HTTP_STATUS_PARTIAL_CONTENT != header.status())
      {
        data->m_passthru = true;
        data->m_blocknum = -1;

        if (nullptr == data->m_dnstream.m_write.m_vio)
        {
          data->m_dnstream.setupVioWrite(contp);
          TSHttpHdrPrint
            ( header.m_buffer
            , header.m_lochdr
            , data->m_dnstream.m_write.m_iobuf );
        }

        return TS_EVENT_CONTINUE;
      }

      /**
        Pull content length off the response header
        and manipulate it into a client response header
        */
      char rangestr[1024];
      int rangelen = sizeof(rangestr) - 1;

      if ( ! header.valueForKey
          ( TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE
          , rangestr, &rangelen ) )
      {
        std::string const headerstr = header.toString();
        DEBUG_LOG("invalid response header\n%s", headerstr.c_str());
        shutdown(contp, data);
      }

      ContentRange crange;
      if (! crange.fromStringClosed(rangestr))
      {
        // abort transaction ????
        TSError("Unable to parse range: %s", rangestr);
      }

      if (! data->m_server_first_header_parsed)
      {
        // set the resource content length
        data->m_contentlen = crange.m_length;
std::cerr << "received content length: " << crange.m_length << std::endl;
TSAssert(data->m_range_begend.first < data->m_range_begend.second);

        // fix up request range end
        int64_t const rend = std::min
          (crange.m_length, data->m_range_begend.second);
        data->m_range_begend.second = rend;

        // convert block content range to response content range
        crange.m_begin = data->m_range_begend.first;
        crange.m_end = rend;

        rangelen = sizeof(rangestr) - 1;
        bool const crstat = crange.toStringClosed(rangestr, &rangelen);
        if (! crstat)
        {
          DEBUG_LOG("Bad/invalid response content range");
        }

        header.setKeyVal
          ( TS_MIME_FIELD_CONTENT_RANGE
          , TS_MIME_LEN_CONTENT_RANGE
          , rangestr, rangelen );

        data->m_bytestosend = crange.rangeSize();

        char bufstr[256];
        int buflen = snprintf
          (bufstr, 255, "%" PRId64, data->m_bytestosend);
        header.setKeyVal
          ( TS_MIME_FIELD_CONTENT_LENGTH
          , TS_MIME_LEN_CONTENT_LENGTH
          , bufstr, buflen );

        // add the response header length to the total bytes to send
        data->m_bytestosend += TSHttpHdrLengthGet
          (header.m_buffer, header.m_lochdr);

        data->m_server_first_header_parsed = true;
      }
      else
      {
TSAssert(data->m_contentlen == crange.m_length);
      }

      // fast forward into the data
      data->m_skipbytes = range::skipBytesForBlock
          (data->m_blocksize, data->m_blocknum, data->m_range_begend);

      data->m_server_block_header_parsed = true;
    }

// send data down to the reader

    // if necessary create downstream and a manufactured header
    if (! data->m_client_header_sent)
    {
TSAssert(data->m_server_first_header_parsed);
TSAssert(nullptr == data->m_dnstream.m_write.m_vio);
      data->m_dnstream.setupVioWrite(contp);

      // write the copied and manipulated header to the client
      HttpHeader header
        ( data->m_upstream.m_hdr_mgr.m_buffer
        , data->m_upstream.m_hdr_mgr.m_lochdr );

std::cerr << std::endl;
std::cerr << __func__ << " sending header to client" << std::endl;
std::cerr << header.toString() << std::endl;

      // dump the manipulated upstream header to the client
      TSHttpHdrPrint
        ( header.m_buffer
        , header.m_lochdr
        , data->m_dnstream.m_write.m_iobuf );

      data->m_client_header_sent = true;
    }

    transfer_content_bytes(data);
  }
  else // server block done, onto the next server request
  if (TS_EVENT_VCONN_EOS == event)
  {
DEBUG_LOG("EOS from server for block %" PRId64, data->m_blocknum);
    ++data->m_blocknum;

    // when we get a "bytes=-<end>" last N bytes request the plugin
    // (like nginx) issues a speculative request for the first block
    // in that case fast forward to the real first in range block
    bool adjusted = false;
    int64_t const firstblock
      (range::firstBlock(data->m_blocksize, data->m_range_begend));
    if (data->m_blocknum < firstblock)
    {
//std::cerr << "setting first block" << std::endl;
      data->m_blocknum = firstblock;
      adjusted = true;
    }

    if (adjusted || range::blockIsInside
        (data->m_blocksize, data->m_blocknum, data->m_range_begend))
    {
//std::cerr << __func__ << " calling request_block" << std::endl;
      request_block(contp, data);

      // reset the per block server header parsed flag
      data->m_server_block_header_parsed = false;
    }
    else
    {
      data->m_blocknum = -1; // signal value no more blocks
    }
  }
  else
  {
std::cerr << __func__ << ": unhandled event: " << event << std::endl;
  }

  return TS_EVENT_CONTINUE;
}

// this is when the client starts asking us for more data
static
int
handle_client_resp
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (TS_EVENT_VCONN_WRITE_READY == event)
  {
DEBUG_LOG("client wants more data");
    if (0 == transfer_content_bytes(data))
    {
      int64_t const bytessent
        (TSVIONDoneGet(data->m_dnstream.m_write.m_vio));
      if (data->m_bytestosend <= bytessent)
      {
//std::cerr << __func__ << ": this is a good place to clean up" << std::endl;
        shutdown(contp, data);
        return TS_EVENT_CONTINUE;
      }
    }
  }
  else
  if (TS_EVENT_ERROR == event)
  {
std::cerr << __func__ << ": " << "TS_EVENT_ERROR" << std::endl;
    shutdown(contp, data);
  }
  else // close it all out???
  {
std::cerr << __func__ << ": unhandled event: " << event << std::endl;
  }

  return TS_EVENT_CONTINUE;
}

int
intercept_hook
  ( TSCont contp
  , TSEvent event
  , void * edata
  )
{
DEBUG_LOG("intercept_hook: %d", event);

  Data * const data = (Data*)TSContDataGet(contp);

  // After the initial TS_EVENT_NET_ACCEPT
  // any "events" will be handled by the vio read or write channel handler
  if (TS_EVENT_NET_ACCEPT == event)
  {
    // set up reader from client
    TSVConn const downvc = (TSVConn)edata;
    data->m_dnstream.setupConnection(downvc);
    data->m_dnstream.setupVioRead(contp);
  }
  else if (nullptr != data)
  {
    // data from client -- only the initial header
    if ( data->m_dnstream.m_read.isValid()
      && edata == data->m_dnstream.m_read.m_vio )
    {
      handle_client_req(contp, event, data);
      TSVConnShutdown(data->m_dnstream.m_vc, 1, 0);
    }
    else // server wants more data from us
    if ( data->m_upstream.m_write.isValid()
      && edata == data->m_upstream.m_write.m_vio )
    {
DEBUG_LOG("server asking for more data after header already sent");
      TSVConnShutdown(data->m_upstream.m_vc, 0, 1);
    }
    else // server has data for us
    if ( data->m_upstream.m_read.isValid()
      && edata == data->m_upstream.m_read.m_vio )
    {
      handle_server_resp(contp, event, data);
    }
    else // client wants more data from us
    if ( data->m_dnstream.m_write.isValid()
      && edata == data->m_dnstream.m_write.m_vio )
    {
      handle_client_resp(contp, event, data);
    }
    else
    {
std::cerr << __func__
  << ": events received after intercept state torn down"
  << std::endl;
    }
  }
  else if (nullptr == data)
  {
    TSContDestroy(contp);
    return TS_EVENT_ERROR;
  }
  else
  {
std::cerr << __func__ << ": unhandled event: " << event << std::endl;
  }

  return TS_EVENT_CONTINUE;
}
