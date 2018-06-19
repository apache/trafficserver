#include "intercept.h"

#include "ContentRange.h"
#include "Data.h"
#include "HttpHeader.h"
#include "range.h"
#include "sim.h"

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
  std::pair<int64_t, int64_t> const blockbe
    (range::forBlock(data->m_blocksize, data->m_blocknum));

//std::cerr << __func__ << " trying to build header" << std::endl;

  char rangestr[1024];
  int rangelen = 1023;
  bool const rpstat = range::closedStringFor
    (blockbe, rangestr, &rangelen);
TSAssert(rpstat);

  // reuse the incoming client header, just change the range
  HttpHeader header
    ( data->m_dnstream.m_hdr_mgr.m_buffer
    , data->m_dnstream.m_hdr_mgr.m_lochdr );

  // add/set sub range key and add slicer tag
  bool const rangestat = header.setKeyVal
    ( TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE
    , rangestr, rangelen );
TSAssert(rangestat);

  // create virtual connection back into ATS
  TSVConn const upvc = TSHttpConnectWithPluginId
    ((sockaddr*)&data->m_client_ip, "slicer", 0);

  // set up connection with the HttpConnect server
  data->m_upstream.setupConnection(upvc);
  data->m_upstream.setupVioWrite(contp);

/*
std::cerr << std::endl;
std::cerr << __func__ << " sending header to server" << std::endl;
std::cerr << header.toString() << std::endl;
*/

  TSHttpHdrPrint
    ( header.m_buffer
    , header.m_lochdr
    , data->m_upstream.m_write.m_iobuf );

  TSVIOReenable(data->m_upstream.m_write.m_vio);

  // get ready for data back from the server
  data->m_upstream.setupVioRead(contp);

  // anticipate the next server response header
  TSHttpParserClear(data->httpParser());

  return 0;
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
    TSHttpParser parser = data->httpParser();
    TSParseResult const parseres
      = data->m_dnstream.m_hdr_mgr.populateFrom
        ( parser
        , data->m_dnstream.m_read.m_reader
        , TSHttpHdrParseReq );
    if (TS_PARSE_DONE == parseres)
    {
      // get the header
      HttpHeader header
        ( data->m_dnstream.m_hdr_mgr.m_buffer
        , data->m_dnstream.m_hdr_mgr.m_lochdr );

/*
std::cerr << std::endl;
std::cerr << __func__ << " received header from client" << std::endl;
std::cerr << header.toString() << std::endl;
*/

      std::pair<int64_t, int64_t> rangebe
        (0, std::numeric_limits<int64_t>::max());

      // first range only, returns string of closed interval
      char rangestr[1024];
      int rangelen = 1024;
      bool const rstat = header.valueForKey
        ( TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE
        , rangestr, &rangelen
        , 0 );

      if (rstat)
      {
        rangebe = range::parseHalfOpenFrom(rangestr);
      }
      else
      {
//std::cerr << "setting full range for unknow file length" << std::endl;
        rangebe.first = 0;
        rangebe.second
          = std::numeric_limits<int64_t>::max() - data->m_blocksize;
      }

      if (! range::isValid(rangebe))
      {
        // send 416 header and shutdown
std::cerr << "Please send a 416 header and shutdown" << std::endl;
        shutdown(contp, data);
        return 0;
      }

       // set the initial range begin/end, we'll correct it later
       data->m_range_begend = rangebe;

       // set up the first block
       data->m_blocknum = range::firstBlock
         (data->m_blocksize, data->m_range_begend);

      // whack some ATS keys
      header.removeKey
        ( TS_MIME_FIELD_VIA, TS_MIME_LEN_VIA );
      header.removeKey
        ( TS_MIME_FIELD_X_FORWARDED_FOR
        , TS_MIME_LEN_X_FORWARDED_FOR );

      // normalize the range and set the X-Slicer-Info private tag
      char bufrange[1024];
      int buflen = 1023;
      range::closedStringFor
        (data->m_range_begend, bufrange, &buflen);
      header.setKeyVal
        ( SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO)
        , bufrange, buflen );

      // send off the first block request
//std::cerr << __func__ << " calling request_block" << std::endl;
      request_block(contp, data);
    }
  }

  return 0;
}

// transfer bytes from the server to the client
static
int64_t
transfer_bytes
  ( Data * const data
  )
{
  int64_t consumed = 0;

  int64_t read_avail
    (TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader));

  int64_t const toskip
    (std::min(data->m_skipbytes, read_avail));
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
    // has the first server reponse header been parsed??
    if (! data->m_server_block_header_parsed)
    {
      TSHttpParser parser = data->httpParser(); // reset by request_block
      TSParseResult const parseres
        = data->m_upstream.m_hdr_mgr.populateFrom
          ( parser
          , data->m_upstream.m_read.m_reader
          , TSHttpHdrParseResp );

      // the server response header didn't fit into a single block ???
      if (TS_PARSE_DONE != parseres)
      {
        return 0;
      }

      HttpHeader header
        ( data->m_upstream.m_hdr_mgr.m_buffer
        , data->m_upstream.m_hdr_mgr.m_lochdr );

/*
std::cerr << std::endl;
std::cerr << "got a response header from server" << std::endl;
std::cerr << header.toString() << std::endl;
*/

      // only process a 206, everything else gets a pass through
      TSHttpStatus const status = header.status();
      if (TS_HTTP_STATUS_PARTIAL_CONTENT != status)
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

        return 0;
      }


      /**
        Pull content length off the response header
        and manipulate it into a client response header
        */
      char rangestr[1024];
      int rangelen = 1023;

      if ( ! header.valueForKey
          ( TS_MIME_FIELD_CONTENT_RANGE
          , TS_MIME_LEN_CONTENT_RANGE
          , rangestr
          , &rangelen ) )
      {
std::cerr << "some header came back without a Content-Range header"
  << std::endl;
std::cerr << "response header" << std::endl;
std::cerr << header.toString() << std::endl;
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

        // trim the end of the requested range if necessary
        data->m_range_begend.second = std::min
          ( data->m_range_begend.second
          , crange.m_length );

TSAssert(data->m_range_begend.first < data->m_range_begend.second);

        // convert block content range to response content range
        crange.m_begin = data->m_range_begend.first;
        crange.m_end = data->m_range_begend.second;

        rangelen = sizeof(rangestr) - 1;
        bool const crstat =
            crange.toStringClosed(rangestr, &rangelen);
TSAssert(crstat);
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

        // fixup request header slicer tag
        buflen = 255;
        range::closedStringFor
          (data->m_range_begend, bufstr, &buflen);
        HttpHeader headerout
          ( data->m_dnstream.m_hdr_mgr.m_buffer
          , data->m_dnstream.m_hdr_mgr.m_lochdr );
        headerout.setKeyVal
          ( SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO)
          , bufstr, buflen );

        // add the header length to the total bytes to send
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
    if (nullptr == data->m_dnstream.m_write.m_vio)
    {
TSAssert(data->m_server_first_header_parsed);
TSAssert(! data->m_client_header_sent);

      data->m_dnstream.setupVioWrite(contp);

      // write the copied and manipulated header to the client
      HttpHeader header
        ( data->m_upstream.m_hdr_mgr.m_buffer
        , data->m_upstream.m_hdr_mgr.m_lochdr );

/*
std::cerr << std::endl;
std::cerr << __func__ << " sending header to client" << std::endl;
std::cerr << header.toString() << std::endl;
*/

      // dump the manipulated upstream header to the client
      TSHttpHdrPrint
        ( header.m_buffer
        , header.m_lochdr
        , data->m_dnstream.m_write.m_iobuf );

      data->m_client_header_sent = true;
    }

    transfer_bytes(data);
  }
  else // server block done, onto the next server request
  if (TS_EVENT_VCONN_EOS == event)
  {
    ++data->m_blocknum;

    // when we get a "bytes=-<end>" last N bytes request the plugin
    // (like nginx) issues a request for the first block
    // in that case fast forward to the first in range block
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
      data->m_blocknum = -1; // signal value that we are done
    }
  }
  else
  {
std::cerr << __func__ << ": unhandled event: " << event << std::endl;
  }

  return 0;
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
    if (0 == transfer_bytes(data))
    {
      int64_t const bytessent
        (TSVIONDoneGet(data->m_dnstream.m_write.m_vio));
      if (data->m_bytestosend <= bytessent)
      {
//std::cerr << __func__ << ": this is a good place to clean up" << std::endl;
        shutdown(contp, data);
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

  return 0;
}

int
intercept_hook
  ( TSCont contp
  , TSEvent event
  , void * edata
  )
{
  Data * const data = (Data*)TSContDataGet(contp);

  // After the initial TS_EVENT_NET_ACCEPT
  // any "events" will be handled by the vio read or write channel handler
  if (TS_EVENT_NET_ACCEPT == event)
  {
    // set up reader from client
    TSVConn const downvc = (TSVConn)edata;
    data->m_dnstream.setupConnection(downvc);
    data->m_dnstream.setupVioRead(contp);

    // this should be set up only when we actually send
    // a header back to the client.  We still need
    // to handle times when we have no data yet
    // (in between blocks, etc) when the
    // client still wants more.
//    data->m_dnstream.setupVioWrite(contp);
  }
  else if (nullptr == data)
  {
    TSContDestroy(contp);
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
      // header was already sent to server, not interested
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
  << ": events received after intercept state torn down" << std::endl;
    }
  }
  else
  {
std::cerr << __func__ << ": unhandled event: " << event << std::endl;
  }

  return 0;
}
