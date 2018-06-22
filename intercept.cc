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

namespace
{

void
shutdown
  ( TSCont contp
  , Data * const data
  )
{
DEBUG_LOG("shutting down transaction");
  data->m_upstream.close();
  data->m_dnstream.close();
/*
  delete data;
  TSContDataSet(contp, nullptr);
  TSContDestroy(contp);
*/
}

// create and issue a block request
int
request_block
  ( TSCont contp
  , Data * const data
  )
{
  std::pair<int64_t, int64_t> const blockbe
    = range::forBlock(data->m_blocksize, data->m_blocknum);

//std::cerr << __func__ << " trying to build header" << std::endl;

  char rangestr[1024];
  int rangelen = 1023;
  bool const rpstat = range::closedStringFor
    (blockbe, rangestr, &rangelen);
TSAssert(rpstat);

DEBUG_LOG("request_block: %s", rangestr);

  // reuse the incoming client header, just change the range
  HttpHeader header
    ( data->m_req_hdrmgr.m_buffer
    , data->m_req_hdrmgr.m_lochdr );

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

  TSVIOReenable(data->m_upstream.m_write.m_vio); // not necessary signal

  // get ready for data back from the server
  data->m_upstream.setupVioRead(contp);

  // anticipate the next server response header
  TSHttpParserClear(data->m_http_parser);
  data->m_resp_hdrmgr.resetHeader();

  return TS_EVENT_CONTINUE;
}

// this is called once per transaction when the client sends a req header
int
handle_client_req
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if ( TS_EVENT_VCONN_READ_READY == event
    || TS_EVENT_VCONN_READ_COMPLETE == event )
  {
DEBUG_LOG("client has data ready to read");
    if (nullptr == data->m_http_parser)
    {
      data->m_http_parser = TSHttpParserCreate();
    }

    // the client request header didn't fit into the input buffer:
    if (TS_PARSE_DONE != data->m_req_hdrmgr.populateFrom
        ( data->m_http_parser
        , data->m_dnstream.m_read.m_reader
        , TSHttpHdrParseReq ) )
    {
      return TS_EVENT_CONTINUE;
    }

    // get the header
    HttpHeader header
      ( data->m_req_hdrmgr.m_buffer
      , data->m_req_hdrmgr.m_lochdr );

/*
std::cerr << std::endl;
std::cerr << __func__ << " received header from client" << std::endl;
std::cerr << header.toString() << std::endl;
*/

    // set the request url back to pristine in case of plugin stacking
//    header.setUrl(data->m_urlbuffer, data->m_urlloc);

    header.setKeyVal
      ( TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST
      , data->m_hostname, data->m_hostlen );

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

      if (range::isValid(rangebe))
      {
        data->m_statustype = TS_HTTP_STATUS_PARTIAL_CONTENT;
      }
      else // reset the range
      {
        rangebe = std::make_pair
          (0, std::numeric_limits<int64_t>::max() - data->m_blocksize);
        data->m_statustype = TS_HTTP_STATUS_OK;
      }
    }
    else
    {
static char const * const valstr = "200 request";
static size_t const vallen = strlen(valstr);
      header.setKeyVal
        ( SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO)
        , valstr, vallen );
      data->m_statustype = TS_HTTP_STATUS_OK;
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
  int64_t const toskip = std::min(data->m_skipbytes, read_avail);
  if (0 < toskip)
  {
    TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, toskip);
    data->m_skipbytes -= toskip;
    read_avail -= toskip;
  }

  if (0 < read_avail)
  {
    int64_t const bytesleft = (data->m_bytestosend - data->m_bytessent);
    int64_t const tocopy = std::min(read_avail, bytesleft);

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

// transfer all bytes from the server (error condition)
int64_t
transfer_all_bytes
  ( Data * const data
  )
{
DEBUG_LOG("transfer_all_bytes");
  int64_t consumed = 0;
  int64_t const read_avail = TSIOBufferReaderAvail
    (data->m_upstream.m_read.m_reader);

  if (0 < read_avail)
  {
    int64_t const copied
      ( TSIOBufferCopy
        ( data->m_dnstream.m_write.m_iobuf
        , data->m_upstream.m_read.m_reader
        , read_avail
        , 0 ) );

    TSIOBufferReaderConsume(data->m_upstream.m_read.m_reader, copied);
    TSVIOReenable(data->m_dnstream.m_write.m_vio);

    consumed = copied;
  }

  return consumed;
}

// canned body string for a 416, stolen from nginx
std::string
bodyString416
  ()
{
  std::string bodystr;
  bodystr.append("<html>\n");
  bodystr.append("<head><title>416 Requested Range Not Satisfiable</title></head>\n");
  bodystr.append("<body bgcolor=\"white\">\n");
  bodystr.append("<center><h1>416 Requested Range Not Satisfiable</h1></center>");
  bodystr.append("<hr><center>ATS/");
  bodystr.append(TS_VERSION_STRING);
  bodystr.append("</center>\n");
  bodystr.append("</body>\n");
  bodystr.append("</html>\n");
  
  return bodystr;
}

void
form416HeaderAndBody
  ( HttpHeader & header
  , int64_t const contentlen
  , std::string const & bodystr
  )
{
  header.setStatus(TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE);
  char const * const reason = TSHttpHdrReasonLookup
    (TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE);
  header.setReason(reason, strlen(reason));

  char bufstr[256];
  int buflen = snprintf
//    (bufstr, 255, "%" PRId64, bodystr.size());
    (bufstr, 255, "%lu", bodystr.size());
  header.setKeyVal
    ( TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH
    , bufstr, buflen );

static char const * const ctypestr = "text/html";
static int const ctypelen = strlen(ctypestr);
  header.setKeyVal
    ( TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE
    , ctypestr, ctypelen );
            
  buflen = snprintf
    (bufstr, 255, "*/%" PRId64, contentlen);
  header.setKeyVal
    ( TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE
    , bufstr, buflen );
}

// this is called every time the server has data for us
int
handle_server_resp
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (data->m_bail)
  {
    shutdown(contp, data);
    return TS_EVENT_CONTINUE;
  }

  if ( TS_EVENT_VCONN_READ_READY == event
    || TS_EVENT_VCONN_READ_COMPLETE == event )
  {
DEBUG_LOG("server has data ready to read");
    // has block reponse header been parsed??
    if (! data->m_server_block_header_parsed)
    {
      // the server response header didn't fit into the input buffer??
      if ( TS_PARSE_DONE != data->m_resp_hdrmgr.populateFrom
            ( data->m_http_parser
            , data->m_upstream.m_read.m_reader
            , TSHttpHdrParseResp ) )
      {
        return TS_EVENT_CONTINUE;
      }

      HttpHeader header
        ( data->m_resp_hdrmgr.m_buffer
        , data->m_resp_hdrmgr.m_lochdr );

/*
std::cerr << std::endl;
std::cerr << "got a response header from server" << std::endl;
std::cerr << header.toString() << std::endl;
*/

      // only process a 206, everything else gets a pass through
      if (TS_HTTP_STATUS_PARTIAL_CONTENT != header.status())
      {
        // this is the first server response
        if (! data->m_client_header_sent)
        {
          data->m_bail = true;
          data->m_blocknum = -1;

          // same as ! data->m_client_header_sent
          if (nullptr == data->m_dnstream.m_write.m_vio)
          {
            data->m_dnstream.setupVioWrite(contp);
            TSHttpHdrPrint
              ( header.m_buffer
              , header.m_lochdr
              , data->m_dnstream.m_write.m_iobuf );
            transfer_all_bytes(data);
          }
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
/*
        std::string const headerstr = header.toString();
        DEBUG_LOG("invalid response header\n%s", headerstr.c_str());
*/
        DEBUG_LOG("invalid response header, no Content-Range");
        shutdown(contp, data);
      }

      ContentRange crange;
      if (! crange.fromStringClosed(rangestr))
      {
        // abort transaction ????
        TSError("Unable to parse range: %s", rangestr);
        shutdown(contp, data);
        return TS_EVENT_CONTINUE;
      }

      // Is this the very first response header?
      if (! data->m_server_first_header_parsed)
      {
        // set the resource content length
        data->m_contentlen = crange.m_length;
TSAssert(data->m_range_begend.first < data->m_range_begend.second);

        // fix up request range end
        int64_t const rend = std::min
          (crange.m_length, data->m_range_begend.second);
        data->m_range_begend.second = rend;

        // convert block content range to client response content range
        crange.m_begin = data->m_range_begend.first;
        crange.m_end = rend;

        data->m_bytestosend = crange.rangeSize();

//std::cerr << "bytes to send:" << data->m_bytestosend << std::endl;

        if (data->m_bytestosend <= 0) // assume 416 needs to be sent
        {
          std::string const bodystr(bodyString416());
          form416HeaderAndBody(header, data->m_contentlen, bodystr);

          data->m_dnstream.setupVioWrite(contp);

          TSHttpHdrPrint
            ( header.m_buffer
            , header.m_lochdr
            , data->m_dnstream.m_write.m_iobuf );

          TSIOBufferWrite
            ( data->m_dnstream.m_write.m_iobuf
            , bodystr.data(), bodystr.size() );

          data->m_bail = true;
          
          return TS_EVENT_CONTINUE;
        }
        else
        if (TS_HTTP_STATUS_PARTIAL_CONTENT == data->m_statustype)
        {
          rangelen = sizeof(rangestr) - 1;
          bool const crstat = crange.toStringClosed(rangestr, &rangelen);
          if (! crstat)
          {
            DEBUG_LOG("Bad/invalid response content range");
          }

          header.setKeyVal
            ( TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE
            , rangestr, rangelen );
        }
        else // fix up for 200 response
        if (TS_HTTP_STATUS_OK == data->m_statustype)
        {
          header.setStatus(TS_HTTP_STATUS_OK);
          char const * const reason
            = TSHttpHdrReasonLookup(TS_HTTP_STATUS_OK);
          header.setReason(reason, strlen(reason));
          header.removeKey
            (TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE);
        }

        char bufstr[256];
        int buflen = snprintf
          (bufstr, 255, "%" PRId64, data->m_bytestosend);
        header.setKeyVal
          ( TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH
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

      // how much to fast forward into the (first) data block
      data->m_skipbytes = range::skipBytesForBlock
          (data->m_blocksize, data->m_blocknum, data->m_range_begend);

      data->m_server_block_header_parsed = true;
    }

// send data down to the reader

    // if necessary create downstream and send the manufactured header
    if (! data->m_client_header_sent)
    {
TSAssert(data->m_server_first_header_parsed);
TSAssert(nullptr == data->m_dnstream.m_write.m_vio);
      data->m_dnstream.setupVioWrite(contp);

      // write the (previously) manipulated server resp header to the client
      HttpHeader header
        ( data->m_resp_hdrmgr.m_buffer
        , data->m_resp_hdrmgr.m_lochdr );

/*
std::cerr << std::endl;
std::cerr << __func__ << " sending header to client" << std::endl;
std::cerr << header.toString() << std::endl;
*/
      TSHttpHdrPrint
        ( header.m_buffer
        , header.m_lochdr
        , data->m_dnstream.m_write.m_iobuf );

      data->m_client_header_sent = true;
    }

    // transfer any remaining content data
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
    // Btw this isn't implemented yet, to be handled
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
int
handle_client_resp
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (data->m_bail)
  {
    shutdown(contp, data);
    return TS_EVENT_CONTINUE;
  }

  if ( TS_EVENT_VCONN_WRITE_READY == event
    || TS_EVENT_VCONN_WRITE_COMPLETE == event )
  {
DEBUG_LOG("client wants more data");
    if (0 == transfer_content_bytes(data))
    {
      int64_t const bytessent
        (TSVIONDoneGet(data->m_dnstream.m_write.m_vio));
      if (data->m_bytestosend <= bytessent)
      {
        // everything has been sent, close down !!!
        shutdown(contp, data);
        return TS_EVENT_CONTINUE;
      }
    }
  }
  else
  if (TS_EVENT_ERROR == event) // client closed connection
  {
    DEBUG_LOG("got a TS_EVENT_ERROR from the client -- it probably bailed");
/*
std::cerr << __func__ << ": " << "TS_EVENT_ERROR" << std::endl;
std::cerr << "bytes: sent " << bytessent << " of " << data->m_bytestosend << std::endl;
*/
    shutdown(contp, data);
  }
  else // close it all out???
  {
    DEBUG_LOG("Unhandled event: %d", event);
std::cerr << __func__ << ": unhandled event: " << event << std::endl;
  }

  return TS_EVENT_CONTINUE;
}

} // private namespace

int
intercept_hook
  ( TSCont contp
  , TSEvent event
  , void * edata
  )
{
DEBUG_LOG("intercept_hook: %d", event);

  Data * const data = static_cast<Data*>(TSContDataGet(contp));

  // After the initial TS_EVENT_NET_ACCEPT
  // any "events" will be handled by the vio read or write channel handler
  if (TS_EVENT_NET_ACCEPT == event)
  {
    // set up reader from client
    TSVConn const downvc = (TSVConn)edata;
    data->m_dnstream.setupConnection(downvc);
    data->m_dnstream.setupVioRead(contp);
  }
  else
  if ( TS_EVENT_VCONN_INACTIVITY_TIMEOUT == event
    || TS_EVENT_VCONN_ACTIVE_TIMEOUT == event )
  {
    shutdown(contp, data);
  }
  else
  if (TS_EVENT_HTTP_TXN_CLOSE == event)
  {
DEBUG_LOG("TS_EVENT_HTTP_TXN_CLOSE");
    delete data;
    TSContDestroy(contp);
  }
  else if (nullptr != data)
  {
    // data from client -- only the initial header
    if ( data->m_dnstream.m_read.isValid()
      && edata == data->m_dnstream.m_read.m_vio )
    {
      handle_client_req(contp, event, data);
      DEBUG_LOG("shutting down read from client pipe");
      TSVConnShutdown(data->m_dnstream.m_vc, 1, 0);
    }
    else // server wants more data from us
    if ( data->m_upstream.m_write.isValid()
      && edata == data->m_upstream.m_write.m_vio )
    {
      DEBUG_LOG("shutting down send to server pipe");
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
      DEBUG_LOG("Unhandled event: %d", event);
std::cerr << __func__
  << ": events received after intercept state torn down"
  << std::endl;
    }
  }
  else // if (nullptr == data)
  {
    DEBUG_LOG("Events handled after data already torn down");
    TSContDestroy(contp);
    return TS_EVENT_ERROR;
  }

  return TS_EVENT_CONTINUE;
}
