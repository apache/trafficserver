#include "intercept.h"

#include "Data.h"
#include "HttpHeader.h"
#include "range.h"
#include "sim.h"

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

static
int
request_block
  ( TSCont contp
  , Data * const data
  )
{
  std::pair<int64_t, int64_t> const blockbe
    (range::forBlock(data->m_blocksize, data->m_blocknum));

std::cerr << __func__ << " trying to build header" << std::endl;

  char rangestr[1024];
  int rangelen = 1023;
  bool const rpstat = range::closedStringFor
    (blockbe, rangestr, &rangelen);
TSAssert(rpstat);

  HttpHeader header = data->m_dnstream.m_hdr_mgr.header();

  // add sub range key and add slicer tag
  header.setKeyVal
    ( TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE
    , rangestr, rangelen );

  header.setKeyVal
    ( SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO)
    , ";", 1 );
 
  // virtual connection
  TSVConn const upvc = TSHttpConnectWithPluginId
    ((sockaddr*)&data->m_client_ip, "slicer", 0);

  // set up connection with the HttpConnect server
  data->m_upstream.setupConnection(upvc);
  data->m_upstream.setupVioWrite(contp);

  TSHttpHdrPrint
    ( header.m_buffer
    , header.m_lochdr
    , data->m_upstream.m_write.m_iobuf );

/*
  TSIOBufferWrite
    ( data->m_upstream.m_write.m_iobuf
    , get_request.data()
    , get_request.size() );
*/

  TSVIOReenable(data->m_upstream.m_write.m_vio);

  // get ready for data back from the server
  data->m_upstream.setupVioRead(contp);

  return 0;
}

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
    TSParseResult const parse_res
      = data->m_dnstream.m_hdr_mgr.populateFrom
        ( parser
        , data->m_dnstream.m_read.m_reader
        , TSHttpHdrParseReq );
    if (parse_res == TS_PARSE_DONE)
    {
      TSHttpParserClear(parser); // for server response headers

      // get the header
      HttpHeader header = data->m_dnstream.m_hdr_mgr.header();

      std::pair<int64_t, int64_t> rangebe
        (0, std::numeric_limits<int64_t>::max());

      // first range only, returns string of closed interval
      char rangestr[1024];
      int rangelen = 1024;
      bool const vstat = header.valueForKey
        ( TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE
        , rangestr, &rangelen
        , 0 );

      if (vstat)
      {
        rangebe = range::parseHalfOpenFrom(rangestr);
      }

      if (! range::isValid(rangebe))
      {
        // send 416 header and shutdown
std::cerr << "Please send a 416 header and shutdown" << std::endl;
        shutdown(contp, data);
      }
      else
      {
        // for now quantize the range so we can
        // construct a valid response header

//        data->m_range_begend = rangebe;
        data->m_range_begend = range::quantize
          (data->m_blocksize, rangebe);

        data->m_blocknum = range::firstBlock
          (data->m_blocksize, data->m_range_begend);
        request_block(contp, data);
      }

      // whack some ATS keys
      header.removeKey
        ( TS_MIME_FIELD_VIA, TS_MIME_LEN_VIA );
      header.removeKey
        ( TS_MIME_FIELD_X_FORWARDED_FOR
        , TS_MIME_LEN_X_FORWARDED_FOR );


      // normalize the range and set the info to it
      char bufrange[1024];
      int buflen = 1024;
      range::closedStringFor
        (data->m_range_begend, bufrange, &buflen);

      // add in a blank ats key
      header.setKeyVal
        ( SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO)
        , bufrange, buflen );

// dump this friggin header
std::cerr << "cleaned header: " << '\n' << header.toString() << std::endl;
//shutdown(contp, data);
    }
  }

  return 0;
}

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
    int64_t read_avail
      (TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader));

/*
    if (! data->m_server_res_header_parsed)
    {
      TSHttpHeader header;
      TSHttpParser const http_parser = data->httpParse();

      TSIOBufferBlock block = TSIOBufferReaderStart
        (data->m_upstream.m_read.m_reader);
      while (nullptr != block && ! data->m_server_res_header_parsed)
      {
        int64_t nbytes = 0;
        char const * ptr = TSIOBufferBlockReadStart
            (block, data->m_upstream.m_read.m_reader, &nbytes);
TSAssert(nullptr != ptr);
        char const * endptr = ptr + &nbytes;

        TSParseResult const parseres = TSHttpHdrParseReq
            ( http_parser
            , data->m_upstream.m_read.m_iobuf
            , header.m_buffer
            , header.m_lochdr
            , &ptr, endptr );

        if (TS_PARSE_DONE == parseres)
        {
          data->m_server_res_header_parsed = true;
        }
      }

      // the server response header didn't fit into a single block ???
      if (! data->m_server_res_header_parsed)
      {
        return 0;
      }
    }
*/
    // create the downstream connection and write out the header
    if (nullptr == data->m_dnstream.m_write.m_vio)
    {
      data->m_dnstream.setupVioWrite(contp);

      if (! data->m_client_header_sent) // <-- may be redudant
      {
        std::string const headerstr(simClientResponseHeader());

        int64_t const byteswritten
          ( TSIOBufferWrite
            ( data->m_dnstream.m_write.m_iobuf
            , headerstr.data()
            , headerstr.size() ) );
        data->m_client_header_sent = true;
        
        TSVIOReenable(data->m_dnstream.m_write.m_vio);
      }
    }

    if (0 < read_avail)
    {
      int64_t const copied
        ( TSIOBufferCopy
          ( data->m_dnstream.m_write.m_iobuf
          , data->m_upstream.m_read.m_reader
          , read_avail
          , 0 ) );
std::cerr << __func__ << ": copied: " << copied << " of: " << read_avail << std::endl;

      TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, copied);

/* what does this do again?
      TSVIONDoneSet
        ( data->m_upstream.m_read.m_vio
        , TSVIONDoneGet(data->m_upstream.m_read.m_vio) + copied );
*/

      TSVIOReenable(data->m_dnstream.m_write.m_vio);

/*
      int64_t consumed(0);
      TSIOBufferBlock block = TSIOBufferReaderStart
          (data->upstream->read->reader);

      while (nullptr != block && 0 < read_avail)
      {
        int64_t read_block(0);
        char const * const block_start = TSIOBufferBlockReadStart
            (block, data->upstream->read->reader, &read_block);
        if (0 < read_block)
        {
          int64_t const tocopy(std::min(read_block, read_avail));
          std::cerr << std::string(block_start, block_start + tocopy);
          read_avail -= tocopy;
          consumed += tocopy;
          block = TSIOBufferBlockNext(block);
        }
      }

      TSIOBufferReaderConsume(data->upstream->read->reader, consumed);
*/
    }
  }
  else // server block done, onto the next
  if (TS_EVENT_VCONN_EOS == event)
  {
    ++data->m_blocknum;
    if (range::blockIsInside
        (data->m_blocksize, data->m_blocknum, data->m_range_begend))
    {
      request_block(contp, data);

      // reset the server header parsed flag
      data->m_server_res_header_parsed = false;
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
    int64_t read_avail
      (TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader));

    if (0 < read_avail)
    {
      int64_t const copied
        ( TSIOBufferCopy
          ( data->m_dnstream.m_write.m_iobuf
          , data->m_upstream.m_read.m_reader
          , read_avail
          , 0 ) );

std::cerr << __func__ << " copied: " << copied << std::endl;

      TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, copied);
      TSVIOReenable(data->m_dnstream.m_write.m_vio);

/*
      int64_t consumed(0);
      TSIOBufferBlock block = TSIOBufferReaderStart
          (data->upstream->read->reader);

      while (nullptr != block && 0 < read_avail)
      {
        int64_t read_block(0);
        char const * const block_start = TSIOBufferBlockReadStart
            (block, data->upstream->read->reader, &read_block);
        if (0 < read_block)
        {
          int64_t const tocopy(std::min(read_block, read_avail));
          std::cerr << std::string(block_start, block_start + tocopy);
          read_avail -= tocopy;
          consumed += tocopy;
          block = TSIOBufferBlockNext(block);
        }
      }

      TSIOBufferReaderConsume(data->upstream->read->reader, consumed);
*/
    }
    else
    {
      if (-1 == data->m_blocknum) // signal value
      {
std::cerr << __func__ << ": this is a good place to clean up" << std::endl;
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

//std::cerr << __func__ << ": event: " << event << std::endl;

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
