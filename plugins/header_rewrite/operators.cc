/*
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
//////////////////////////////////////////////////////////////////////////////////////////////
// operators.cc: implementation of the operator classes
//
//
#include <arpa/inet.h>
#include <string.h>

#include "ts/ts.h"

#include "operators.h"
#include "expander.h"

// OperatorConfig
void
OperatorSetConfig::initialize(Parser &p)
{
  Operator::initialize(p);
  _config = p.get_arg();

  if (TS_SUCCESS == TSHttpTxnConfigFind(_config.c_str(), _config.size(), &_key, &_type)) {
    _value.set_value(p.get_value());
  } else {
    _key = TS_CONFIG_NULL;
    TSError("[%s] no such records config: %s", PLUGIN_NAME, _config.c_str());
  }
}

void
OperatorSetConfig::exec(const Resources &res) const
{
  if (TS_CONFIG_NULL != _key) {
    switch (_type) {
    case TS_RECORDDATATYPE_INT:
      if (TS_SUCCESS == TSHttpTxnConfigIntSet(res.txnp, _key, _value.get_int_value())) {
        TSDebug(PLUGIN_NAME, "OperatorSetConfig::exec() invoked on %s=%d", _config.c_str(), _value.get_int_value());
      }
      break;
    case TS_RECORDDATATYPE_FLOAT:
      if (TS_SUCCESS == TSHttpTxnConfigFloatSet(res.txnp, _key, _value.get_float_value())) {
        TSDebug(PLUGIN_NAME, "OperatorSetConfig::exec() invoked on %s=%f", _config.c_str(), _value.get_float_value());
      }
      break;
    case TS_RECORDDATATYPE_STRING:
      if (TS_SUCCESS == TSHttpTxnConfigStringSet(res.txnp, _key, _value.get_value().c_str(), _value.size())) {
        TSDebug(PLUGIN_NAME, "OperatorSetConfig::exec() invoked on %s=%s", _config.c_str(), _value.get_value().c_str());
      }
      break;
    default:
      TSError("[%s] unknown data type, whut?", PLUGIN_NAME);
      break;
    }
  }
}

// OperatorSetStatus
void
OperatorSetStatus::initialize(Parser &p)
{
  Operator::initialize(p);

  _status.set_value(p.get_arg());

  if (NULL == (_reason = TSHttpHdrReasonLookup((TSHttpStatus)_status.get_int_value()))) {
    TSError("[%s] unknown status %d", PLUGIN_NAME, _status.get_int_value());
    _reason_len = 0;
  } else {
    _reason_len = strlen(_reason);
  }

  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
  require_resources(RSRC_RESPONSE_STATUS);
}

void
OperatorSetStatus::initialize_hooks()
{
  add_allowed_hook(TS_HTTP_READ_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_HTTP_READ_REQUEST_HDR_HOOK);
  add_allowed_hook(TS_HTTP_READ_REQUEST_PRE_REMAP_HOOK);
  add_allowed_hook(TS_REMAP_PSEUDO_HOOK);
}

void
OperatorSetStatus::exec(const Resources &res) const
{
  switch (get_hook()) {
  case TS_HTTP_READ_RESPONSE_HDR_HOOK:
  case TS_HTTP_SEND_RESPONSE_HDR_HOOK:
    if (res.bufp && res.hdr_loc) {
      TSHttpHdrStatusSet(res.bufp, res.hdr_loc, (TSHttpStatus)_status.get_int_value());
      if (_reason && _reason_len > 0) {
        TSHttpHdrReasonSet(res.bufp, res.hdr_loc, _reason, _reason_len);
      }
    }
    break;
  default:
    TSHttpTxnSetHttpRetStatus(res.txnp, (TSHttpStatus)_status.get_int_value());
    break;
  }

  TSDebug(PLUGIN_NAME, "OperatorSetStatus::exec() invoked with status=%d", _status.get_int_value());
}

// OperatorSetStatusReason
void
OperatorSetStatusReason::initialize(Parser &p)
{
  Operator::initialize(p);

  _reason.set_value(p.get_arg());
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
}

void
OperatorSetStatusReason::initialize_hooks()
{
  add_allowed_hook(TS_HTTP_READ_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
}

void
OperatorSetStatusReason::exec(const Resources &res) const
{
  if (res.bufp && res.hdr_loc) {
    std::string reason;

    _reason.append_value(reason, res);
    if (reason.size() > 0) {
      TSDebug(PLUGIN_NAME, "Setting Status Reason to %s", reason.c_str());
      TSHttpHdrReasonSet(res.bufp, res.hdr_loc, reason.c_str(), reason.size());
    }
  }
}

// OperatorSetDestination
void
OperatorSetDestination::initialize(Parser &p)
{
  Operator::initialize(p);

  _url_qual = parse_url_qualifier(p.get_arg());
  _value.set_value(p.get_value());
  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
  require_resources(RSRC_SERVER_REQUEST_HEADERS);
}

void
OperatorSetDestination::exec(const Resources &res) const
{
  if (res._rri || (res.bufp && res.hdr_loc)) {
    std::string value;

    // Determine which TSMBuffer and TSMLoc to use
    TSMBuffer bufp;
    TSMLoc url_m_loc;
    if (res._rri) {
      bufp      = res._rri->requestBufp;
      url_m_loc = res._rri->requestUrl;
    } else {
      bufp = res.bufp;
      if (TSHttpHdrUrlGet(res.bufp, res.hdr_loc, &url_m_loc) != TS_SUCCESS) {
        TSDebug(PLUGIN_NAME, "TSHttpHdrUrlGet was unable to return the url m_loc");
        return;
      }
    }

    // Never set an empty destination value (I don't think that ever makes sense?)
    switch (_url_qual) {
    case URL_QUAL_HOST:
      _value.append_value(value, res);
      if (value.empty()) {
        TSDebug(PLUGIN_NAME, "Would set destination HOST to an empty value, skipping");
      } else {
        const_cast<Resources &>(res).changed_url = true;
        TSUrlHostSet(bufp, url_m_loc, value.c_str(), value.size());
        TSDebug(PLUGIN_NAME, "OperatorSetDestination::exec() invoked with HOST: %s", value.c_str());
      }
      break;

    case URL_QUAL_PATH:
      _value.append_value(value, res);
      if (value.empty()) {
        TSDebug(PLUGIN_NAME, "Would set destination PATH to an empty value, skipping");
      } else {
        const_cast<Resources &>(res).changed_url = true;
        TSUrlPathSet(bufp, url_m_loc, value.c_str(), value.size());
        TSDebug(PLUGIN_NAME, "OperatorSetDestination::exec() invoked with PATH: %s", value.c_str());
      }
      break;

    case URL_QUAL_QUERY:
      _value.append_value(value, res);
      if (value.empty()) {
        TSDebug(PLUGIN_NAME, "Would set destination QUERY to an empty value, skipping");
      } else {
        // 1.6.4--Support for preserving QSA in case of set-destination
        if (get_oper_modifiers() & OPER_QSA) {
          int query_len     = 0;
          const char *query = TSUrlHttpQueryGet(bufp, url_m_loc, &query_len);
          TSDebug(PLUGIN_NAME, "QSA mode, append original query string: %.*s", query_len, query);
          // std::string connector = (value.find("?") == std::string::npos)? "?" : "&";
          value.append("&");
          value.append(query, query_len);
        }

        const_cast<Resources &>(res).changed_url = true;
        TSUrlHttpQuerySet(bufp, url_m_loc, value.c_str(), value.size());
        TSDebug(PLUGIN_NAME, "OperatorSetDestination::exec() invoked with QUERY: %s", value.c_str());
      }
      break;

    case URL_QUAL_PORT:
      if (_value.get_int_value() <= 0 || _value.get_int_value() > 0xFFFF) {
        TSDebug(PLUGIN_NAME, "Would set destination PORT to an invalid range, skipping");
      } else {
        const_cast<Resources &>(res).changed_url = true;
        TSUrlPortSet(bufp, url_m_loc, _value.get_int_value());
        TSDebug(PLUGIN_NAME, "OperatorSetDestination::exec() invoked with PORT: %d", _value.get_int_value());
      }
      break;
    case URL_QUAL_URL:
      if (_value.empty()) {
        TSDebug(PLUGIN_NAME, "Would set destination URL to an empty value, skipping");
      } else {
        const char *start = _value.get_value().c_str();
        const char *end   = _value.get_value().size() + start;
        TSMLoc new_url_loc;
        if (TSUrlCreate(bufp, &new_url_loc) == TS_SUCCESS && TSUrlParse(bufp, new_url_loc, &start, end) == TS_PARSE_DONE &&
            TSHttpHdrUrlSet(bufp, res.hdr_loc, new_url_loc) == TS_SUCCESS) {
          TSDebug(PLUGIN_NAME, "Set destination URL to %s", _value.get_value().c_str());
        } else {
          TSDebug(PLUGIN_NAME, "Failed to set URL %s", _value.get_value().c_str());
        }
      }
      break;
    case URL_QUAL_SCHEME:
      if (_value.empty()) {
        TSDebug(PLUGIN_NAME, "Would set destination SCHEME to an empty value, skipping");
      } else {
        TSUrlSchemeSet(bufp, url_m_loc, _value.get_value().c_str(), _value.get_value().length());
        TSDebug(PLUGIN_NAME, "OperatorSetDestination::exec() invoked with SCHEME: %s", _value.get_value().c_str());
      }
      break;
    default:
      TSDebug(PLUGIN_NAME, "Set destination %i has no handler", _url_qual);
      break;
    }
  } else {
    TSDebug(PLUGIN_NAME, "OperatorSetDestination::exec() unable to continue due to missing bufp=%p or hdr_loc=%p, rri=%p!",
            res.bufp, res.hdr_loc, res._rri);
  }
}

// OperatorSetRedirect
void
OperatorSetRedirect::initialize(Parser &p)
{
  Operator::initialize(p);

  _status.set_value(p.get_arg());
  _location.set_value(p.get_value());

  if ((_status.get_int_value() != (int)TS_HTTP_STATUS_MOVED_PERMANENTLY) &&
      (_status.get_int_value() != (int)TS_HTTP_STATUS_MOVED_TEMPORARILY)) {
    TSError("[%s] unsupported redirect status %d", PLUGIN_NAME, _status.get_int_value());
  }

  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
  require_resources(RSRC_RESPONSE_STATUS);
}

void
OperatorSetRedirect::exec(const Resources &res) const
{
  if (res.bufp && res.hdr_loc && res.client_bufp && res.client_hdr_loc) {
    std::string value;

    _location.append_value(value, res);

    if (_location.need_expansion()) {
      VariableExpander ve(value);
      value = ve.expand(res);
    }

    bool remap = false;
    if (NULL != res._rri) {
      remap = true;
      TSDebug(PLUGIN_NAME, "OperatorSetRedirect:exec() invoked from remap plugin");
    } else {
      TSDebug(PLUGIN_NAME, "OperatorSetRedirect:exec() not invoked from remap plugin");
    }

    TSMBuffer bufp;
    TSMLoc url_loc;
    if (remap) {
      // Handle when called from remap plugin.
      bufp    = res._rri->requestBufp;
      url_loc = res._rri->requestUrl;
    } else {
      // Handle when not called from remap plugin.
      bufp = res.client_bufp;
      if (TS_SUCCESS != TSHttpHdrUrlGet(res.client_bufp, res.client_hdr_loc, &url_loc)) {
        TSDebug(PLUGIN_NAME, "Could not get client URL");
      }
    }

    // Replace %{PATH} to original path
    size_t pos_path = 0;
    if ((pos_path = value.find("%{PATH}")) != std::string::npos) {
      value.erase(pos_path, 7); // erase %{PATH} from the rewritten to url
      int path_len     = 0;
      const char *path = NULL;
      path             = TSUrlPathGet(bufp, url_loc, &path_len);
      if (path_len > 0) {
        TSDebug(PLUGIN_NAME, "Find %%{PATH} in redirect url, replace it with: %.*s", path_len, path);
        value.insert(pos_path, path, path_len);
      }
    }

    // Append the original query string
    int query_len     = 0;
    const char *query = NULL;
    query             = TSUrlHttpQueryGet(bufp, url_loc, &query_len);
    if ((get_oper_modifiers() & OPER_QSA) && (query_len > 0)) {
      TSDebug(PLUGIN_NAME, "QSA mode, append original query string: %.*s", query_len, query);
      std::string connector = (value.find("?") == std::string::npos) ? "?" : "&";
      value.append(connector);
      value.append(query, query_len);
    }

    // Prepare the destination URL for the redirect.
    const char *start = value.c_str();
    const char *end   = value.size() + start;
    if (remap) {
      // Set new location.
      TSUrlParse(bufp, url_loc, &start, end);
      // Set the new status.
      TSHttpTxnSetHttpRetStatus(res.txnp, (TSHttpStatus)_status.get_int_value());
      const_cast<Resources &>(res).changed_url = true;
      res._rri->redirect                       = 1;
    } else {
      // Set new location.
      TSMLoc field_loc;
      std::string header("Location");
      if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(res.bufp, res.hdr_loc, header.c_str(), header.size(), &field_loc)) {
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(res.bufp, res.hdr_loc, field_loc, -1, value.c_str(), value.size())) {
          TSDebug(PLUGIN_NAME, "   Adding header %s", header.c_str());
          TSMimeHdrFieldAppend(res.bufp, res.hdr_loc, field_loc);
        }
        TSHandleMLocRelease(res.bufp, res.hdr_loc, field_loc);
      }

      // Set the new status code and reason.
      TSHttpStatus status = (TSHttpStatus)_status.get_int_value();
      const char *reason  = TSHttpHdrReasonLookup(status);
      size_t len          = strlen(reason);
      TSHttpHdrStatusSet(res.bufp, res.hdr_loc, status);
      TSHttpHdrReasonSet(res.bufp, res.hdr_loc, reason, len);

      // Set the body.
      std::string msg = "<HTML>\n<HEAD>\n<TITLE>Document Has Moved</TITLE>\n</HEAD>\n"
                        "<BODY BGCOLOR=\"white\" FGCOLOR=\"black\">\n"
                        "<H1>Document Has Moved</H1>\n<HR>\n<FONT FACE=\"Helvetica,Arial\"><B>\n"
                        "Description: The document you requested has moved to a new location."
                        " The new location is \"" +
                        value + "\".\n</B></FONT>\n<HR>\n</BODY>\n";
      TSHttpTxnErrorBodySet(res.txnp, TSstrdup(msg.c_str()), msg.length(), TSstrdup("text/html"));
    }

    TSDebug(PLUGIN_NAME, "OperatorSetRedirect::exec() invoked with destination=%s and status code=%d", value.c_str(),
            _status.get_int_value());
  }
}

// OperatorSetTimeoutOut
void
OperatorSetTimeoutOut::initialize(Parser &p)
{
  Operator::initialize(p);

  if (p.get_arg() == "active") {
    _type = TO_OUT_ACTIVE;
  } else if (p.get_arg() == "inactive") {
    _type = TO_OUT_INACTIVE;
  } else if (p.get_arg() == "connect") {
    _type = TO_OUT_CONNECT;
  } else if (p.get_arg() == "dns") {
    _type = TO_OUT_DNS;
  } else {
    _type = TO_OUT_UNDEFINED;
    TSError("[%s] unsupported timeout qualifier: %s", PLUGIN_NAME, p.get_arg().c_str());
  }

  _timeout.set_value(p.get_value());
}

void
OperatorSetTimeoutOut::exec(const Resources &res) const
{
  switch (_type) {
  case TO_OUT_ACTIVE:
    TSDebug(PLUGIN_NAME, "OperatorSetTimeoutOut::exec(active, %d)", _timeout.get_int_value());
    TSHttpTxnActiveTimeoutSet(res.txnp, _timeout.get_int_value());
    break;

  case TO_OUT_INACTIVE:
    TSDebug(PLUGIN_NAME, "OperatorSetTimeoutOut::exec(inactive, %d)", _timeout.get_int_value());
    TSHttpTxnNoActivityTimeoutSet(res.txnp, _timeout.get_int_value());
    break;

  case TO_OUT_CONNECT:
    TSDebug(PLUGIN_NAME, "OperatorSetTimeoutOut::exec(connect, %d)", _timeout.get_int_value());
    TSHttpTxnConnectTimeoutSet(res.txnp, _timeout.get_int_value());
    break;

  case TO_OUT_DNS:
    TSDebug(PLUGIN_NAME, "OperatorSetTimeoutOut::exec(dns, %d)", _timeout.get_int_value());
    TSHttpTxnDNSTimeoutSet(res.txnp, _timeout.get_int_value());
    break;
  default:
    TSError("[%s] unsupported timeout", PLUGIN_NAME);
    break;
  }
}

// OperatorSkipRemap
void
OperatorSkipRemap::initialize(Parser &p)
{
  Operator::initialize(p);

  if (p.get_arg() == "1" || p.get_arg() == "true" || p.get_arg() == "TRUE") {
    _skip_remap = true;
  }
}

void
OperatorSkipRemap::exec(const Resources &res) const
{
  TSDebug(PLUGIN_NAME, "OperatorSkipRemap::exec() skipping remap: %s", _skip_remap ? "True" : "False");
  TSSkipRemappingSet(res.txnp, _skip_remap ? 1 : 0);
}

// OperatorRMHeader
void
OperatorRMHeader::exec(const Resources &res) const
{
  TSMLoc field_loc, tmp;

  if (res.bufp && res.hdr_loc) {
    TSDebug(PLUGIN_NAME, "OperatorRMHeader::exec() invoked on header %s", _header.c_str());
    field_loc = TSMimeHdrFieldFind(res.bufp, res.hdr_loc, _header.c_str(), _header.size());
    while (field_loc) {
      TSDebug(PLUGIN_NAME, "   Deleting header %s", _header.c_str());
      tmp = TSMimeHdrFieldNextDup(res.bufp, res.hdr_loc, field_loc);
      TSMimeHdrFieldDestroy(res.bufp, res.hdr_loc, field_loc);
      TSHandleMLocRelease(res.bufp, res.hdr_loc, field_loc);
      field_loc = tmp;
    }
  }
}

// OperatorAddHeader
void
OperatorAddHeader::initialize(Parser &p)
{
  OperatorHeaders::initialize(p);

  _value.set_value(p.get_value());
}

void
OperatorAddHeader::exec(const Resources &res) const
{
  std::string value;

  _value.append_value(value, res);

  if (_value.need_expansion()) {
    VariableExpander ve(value);

    value = ve.expand(res);
  }

  // Never set an empty header (I don't think that ever makes sense?)
  if (value.empty()) {
    TSDebug(PLUGIN_NAME, "Would set header %s to an empty value, skipping", _header.c_str());
    return;
  }

  if (res.bufp && res.hdr_loc) {
    TSDebug(PLUGIN_NAME, "OperatorAddHeader::exec() invoked on header %s: %s", _header.c_str(), value.c_str());
    TSMLoc field_loc;

    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(res.bufp, res.hdr_loc, _header.c_str(), _header.size(), &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(res.bufp, res.hdr_loc, field_loc, -1, value.c_str(), value.size())) {
        TSDebug(PLUGIN_NAME, "   Adding header %s", _header.c_str());
        TSMimeHdrFieldAppend(res.bufp, res.hdr_loc, field_loc);
      }
      TSHandleMLocRelease(res.bufp, res.hdr_loc, field_loc);
    }
  }
}

// OperatorSetHeader
void
OperatorSetHeader::initialize(Parser &p)
{
  OperatorHeaders::initialize(p);

  _value.set_value(p.get_value());
}

void
OperatorSetHeader::exec(const Resources &res) const
{
  std::string value;

  _value.append_value(value, res);

  // Never set an empty header (I don't think that ever makes sense?)
  if (value.empty()) {
    TSDebug(PLUGIN_NAME, "Would set header %s to an empty value, skipping", _header.c_str());
    return;
  }

  if (res.bufp && res.hdr_loc) {
    TSMLoc field_loc = TSMimeHdrFieldFind(res.bufp, res.hdr_loc, _header.c_str(), _header.size());

    TSDebug(PLUGIN_NAME, "OperatorSetHeader::exec() invoked on header %s: %s", _header.c_str(), value.c_str());

    if (!field_loc) {
      // No existing header, so create one
      if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(res.bufp, res.hdr_loc, _header.c_str(), _header.size(), &field_loc)) {
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(res.bufp, res.hdr_loc, field_loc, -1, value.c_str(), value.size())) {
          TSDebug(PLUGIN_NAME, "   Adding header %s", _header.c_str());
          TSMimeHdrFieldAppend(res.bufp, res.hdr_loc, field_loc);
        }
        TSHandleMLocRelease(res.bufp, res.hdr_loc, field_loc);
      }
    } else {
      TSMLoc tmp = NULL;
      bool first = true;

      while (field_loc) {
        if (first) {
          first = false;
          if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(res.bufp, res.hdr_loc, field_loc, -1, value.c_str(), value.size())) {
            TSDebug(PLUGIN_NAME, "   Overwriting header %s", _header.c_str());
          }
        } else {
          TSMimeHdrFieldDestroy(res.bufp, res.hdr_loc, field_loc);
        }
        tmp = TSMimeHdrFieldNextDup(res.bufp, res.hdr_loc, field_loc);
        TSHandleMLocRelease(res.bufp, res.hdr_loc, field_loc);
        field_loc = tmp;
      }
    }
  }
}

// OperatorCounter
void
OperatorCounter::initialize(Parser &p)
{
  Operator::initialize(p);

  _counter_name = p.get_arg();

  // Sanity
  if (_counter_name.length() == 0) {
    TSError("[%s] counter name is empty", PLUGIN_NAME);
    return;
  }

  // Check if counter already created by another rule
  if (TSStatFindName(_counter_name.c_str(), &_counter) == TS_ERROR) {
    _counter = TSStatCreate(_counter_name.c_str(), TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
    if (_counter == TS_ERROR) {
      TSError("[%s] TSStatCreate() failed. Can't create counter: %s", PLUGIN_NAME, _counter_name.c_str());
      return;
    }
    TSDebug(PLUGIN_NAME, "OperatorCounter::initialize(%s) created counter with id: %d", _counter_name.c_str(), _counter);
  } else {
    TSDebug(PLUGIN_NAME, "OperatorCounter::initialize(%s) reusing id: %d", _counter_name.c_str(), _counter);
  }
}

void
OperatorCounter::exec(const Resources & /* ATS_UNUSED res */) const
{
  // Sanity
  if (_counter == TS_ERROR)
    return;

  TSDebug(PLUGIN_NAME, "OperatorCounter::exec() invoked on counter %s", _counter_name.c_str());
  TSStatIntIncrement(_counter, 1);
}

// OperatorSetConnDSCP
void
OperatorSetConnDSCP::initialize(Parser &p)
{
  Operator::initialize(p);

  _ds_value.set_value(p.get_arg());
}

void
OperatorSetConnDSCP::initialize_hooks()
{
  add_allowed_hook(TS_HTTP_READ_REQUEST_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_REMAP_PSEUDO_HOOK);
}

void
OperatorSetConnDSCP::exec(const Resources &res) const
{
  if (res.txnp) {
    TSHttpTxnClientPacketDscpSet(res.txnp, _ds_value.get_int_value());
    TSDebug(PLUGIN_NAME, "   Setting DSCP to %d", _ds_value.get_int_value());
  }
}

// OperatorSetDebug
void
OperatorSetDebug::initialize(Parser &p)
{
  Operator::initialize(p);
}

void
OperatorSetDebug::initialize_hooks()
{
  add_allowed_hook(TS_HTTP_READ_REQUEST_HDR_HOOK);
}

void
OperatorSetDebug::exec(const Resources &res) const
{
  TSHttpTxnDebugSet(res.txnp, 1);
}
