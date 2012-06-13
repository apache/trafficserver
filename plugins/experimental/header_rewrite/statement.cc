//////////////////////////////////////////////////////////////////////////////////////////////
// statement.cc: Implementation of the statement base class.
//
//

#define UNUSED __attribute__ ((unused))
static char UNUSED rcsId__statement_cc[] = "@(#) $Id$ built on " __DATE__ " " __TIME__;

#include <ts/ts.h>

#include "statement.h"

void
Statement::append(Statement* stmt)
{
  Statement* tmp = this;

  TSReleaseAssert(stmt->_next == NULL);
  while (tmp->_next)
    tmp = tmp->_next;
  tmp->_next = stmt;
}


const ResourceIDs
Statement::get_resource_ids() const
{
  const Statement* stmt = this;
  ResourceIDs ids = RSRC_NONE;

  while (stmt) {
    ids = static_cast<ResourceIDs>(ids | stmt->_rsrc);
    stmt = stmt->_next;
  }

  return ids;
}


bool
Statement::set_hook(TSHttpHookID hook) {
  bool ret = std::find(_allowed_hooks.begin(), _allowed_hooks.end(), hook) != _allowed_hooks.end();

  if (ret)
    _hook = hook;

  return ret;
}


// This should be overridden for any Statement which only supports some hooks
void
Statement::initialize_hooks() {
  add_allowed_hook(TS_HTTP_READ_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_HTTP_READ_REQUEST_PRE_REMAP_HOOK);
  add_allowed_hook(TS_HTTP_READ_REQUEST_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_REQUEST_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_REMAP_PSEUDO_HOOK);
}


// Parse URL qualifiers
UrlQualifiers
Statement::parse_url_qualifier(const std::string& q) {
  UrlQualifiers qual = URL_QUAL_NONE;

  if (q == "HOST")
    qual = URL_QUAL_HOST;
  else if (q == "PORT")
    qual = URL_QUAL_PORT;
  else if (q == "PATH")
    qual = URL_QUAL_PATH;
  else if (q == "QUERY")
    qual = URL_QUAL_QUERY;
  else if (q == "MATRIX")
    qual = URL_QUAL_MATRIX;
  else if (q == "SCHEME")
    qual = URL_QUAL_SCHEME;
  else if (q == "URL")
    qual = URL_QUAL_URL;

  return qual;
}
