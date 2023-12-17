/** @file

  Demonstrate a plugin using TSHttpSetCategoryIPSpaces.

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

/** A plugin that demnstrates implementing ACME_INTERNAL, ACME_EXTERNAL, and
 * ACME_ALL IP categories.
 *
 *
 *   Usage:
 *     # Place the following in plugin.config:
 *     categories_from_file.so --category_file=categories.csv
 */

#include <getopt.h>
#include <string>
#include <sys/socket.h>
#include <system_error>

#include "swoc/bwf_ip.h"
#include "swoc/BufferWriter.h"
#include "swoc/IPAddr.h"
#include "swoc/IPRange.h"
#include "swoc/TextView.h"
#include "swoc/swoc_file.h"

#include "tsutil/DbgCtl.h"
#include "ts/apidefs.h"
#include "ts/ts.h"

namespace
{

std::string const PLUGIN_NAME = "categories_from_file";
DbgCtl dbg_ctl{"categories_from_file"};

using Categories_t = std::unordered_map<std::string, swoc::IPSpace<bool>>;

enum Category {
  ALL = 1,       // Literaly all addresses.
  ACME_INTERNAL, // ACME's internal network (work stations, printers, etc.).
  ACME_EXTERNAL, // ACME's external network (web servers, VPN gateways, etc.).
  ACME_ALL,      // All ACME addresses.
};

std::unordered_map<std::string, int> const global_category_map = {
  {"ALL",           ALL          }, // Literaly all addresses.
  {"ACME_INTERNAL", ACME_INTERNAL}, // ACME's internal network (work stations, printers, etc.).
  {"ACME_EXTERNAL", ACME_EXTERNAL}, // ACME's external network (web servers, VPN gateways, etc.).
  {"ACME_ALL",      ACME_ALL     }, // All ACME addresses.
};

std::string_view xCategoryHeader{"X-Category"};
std::string global_category_file;
const std::string TS_CONFIG_DIR{TSConfigDirGet()};

/** Return the IP cagories associated with the given address.
 *
 * This expects the following format for each line in the file:
 *  <ip-range>:<category>[,<category>...]
 *
 * @param addr The address to check.
 * @return The categories associated with the address.
 */
Categories_t
get_ip_categories()
{
  Categories_t categories;
  swoc::file::path fp{global_category_file};
  if (!fp.is_absolute()) {
    fp = swoc::file::path{TS_CONFIG_DIR} / fp; // slap the config dir on it to make it absolute.
  }
  // bulk load the file.
  std::error_code ec;
  std::string content{swoc::file::load(fp, ec)};
  if (ec) {
    TSError("[%s] unable to read file '%s' : %s.", PLUGIN_NAME.c_str(), fp.c_str(), ec.message().c_str());
    return categories;
  }
  // walk the lines.
  int line_no = 0;
  swoc::TextView src{content};
  while (!src.empty()) {
    swoc::TextView line{src.take_prefix_at('\n').trim_if(&isspace)};
    ++line_no;
    if (line.empty() || '#' == *line) {
      continue; // empty or comment, ignore.
    }

    swoc::TextView token = line.take_prefix_at(':').trim_if(&isspace);
    if (token.empty()) {
      TSError("[%s] In '%s', missing address range on line %d.", PLUGIN_NAME.c_str(), fp.c_str(), line_no);
      continue;
    }
    swoc::IPRange range{token};

    while (!line.empty()) {
      token = line.take_prefix_at(',').trim_if(&isspace);
      if (token.empty()) {
        TSError("[%s] In '%s', missing category on line %d.", PLUGIN_NAME.c_str(), fp.c_str(), line_no);
        continue;
      }

      std::string category{token};
      categories[category].mark(range, true);
    }
  }
  return categories;
}

/** Parse the IP category config file and upload its data to the core. */
void
parse_and_set_new_categories()
{
  Categories_t categories = get_ip_categories();
  swoc::LocalBufferWriter<1024> w;
  w.print("Loading {} categories: ", categories.size());
  for (auto const &[name, ipspace] : categories) {
    w.print("{}:", name);
    for (auto const &range : ipspace) {
      w.print("{},", range.range_view());
    }
  }
  Dbg(dbg_ctl, "%.*s", static_cast<int>(w.size()), w.data());
  TSHttpSetCategoryIPSpaces(categories);
}

/** Inspect client requests and reset the IP Category map if X-Category: reload
 * is present.
 */
int
read_request_hdr_event_handler(TSCont contp, TSEvent event, void *edata)
{
  Dbg(dbg_ctl, "read_request_hdr_event_handler(): event: %d", event);
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  if (event != TS_EVENT_HTTP_READ_REQUEST_HDR) {
    TSError("[%s] Unexpected event %d", PLUGIN_NAME.c_str(), event);
    return TS_ERROR;
  }

  // Obtain the client request
  TSMBuffer buffer;
  TSMLoc hdr_loc;
  if (TS_SUCCESS != TSHttpTxnClientReqGet(txnp, &buffer, &hdr_loc)) {
    TSError("[%s] Failed to obtain client request header", PLUGIN_NAME.c_str());
    return TS_ERROR;
  }

  // Look for X-Category HTTP field name
  TSMLoc field = TSMimeHdrFieldFind(buffer, hdr_loc, xCategoryHeader.data(), xCategoryHeader.size());
  if (field == nullptr) {
    // This request doesn't have X-Category header, so we don't need to do anything.
    TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  // Verify that the value says "reload".
  int length            = 0;
  char const *value_raw = TSMimeHdrFieldValueStringGet(buffer, hdr_loc, field, -1, &length);

  if (length == 0) {
    // This request has X-Category header, but the value is empty.
    TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }
  std::string value{value_raw, static_cast<size_t>(length)};
  if (value != "reload") {
    // This request has X-Category header, but the value is not "reload".
    TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }
  Dbg(dbg_ctl, "\"X-Category: reload\" received, Reloading the configuration.");

  // The user has asked us to reload the config.
  parse_and_set_new_categories();

  TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

/** Parse the user's categories_from_file.so arguments.
 * @param[out] category_file The file containing the IP categories as specified
 * by the user.
 */
bool
parse_arguments(int argc, const char *argv[], std::string &category_file)
{
  // Construct longopts for a single option that takes a filename.
  const struct option longopts[] = {
    {"category_file", required_argument, nullptr, 1},
    {nullptr,         0,                 nullptr, 0}
  };

  int opt = 0;
  std::string local_category_file;
  while ((opt = getopt_long(argc, const_cast<char *const *>(argv), "", longopts, nullptr)) >= 0) {
    switch (opt) {
    case 1:
      local_category_file = optarg;
      break;
    case '?':
      TSError("[%s] Unknown option '%c'", PLUGIN_NAME.c_str(), optopt);
    case 0:
    case -1:
      break;
    default:
      TSError("[%s] Unexpected option parsing error", PLUGIN_NAME.c_str());
      return false;
    }
  }

  if (local_category_file.empty()) {
    TSError("[%s] Missing required option @param=--category_file", PLUGIN_NAME.c_str());
    return false;
  }
  category_file = local_category_file;

  Dbg(dbg_ctl, "parse_arguments(): category_file: %s", category_file.c_str());
  return true;
}

} // anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME.c_str();
  info.vendor_name   = "apache";
  info.support_email = "edge@yahooinc.com";
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s]: failure calling TSPluginRegister.", PLUGIN_NAME.c_str());
    return;
  }

  if (!parse_arguments(argc, argv, global_category_file)) {
    TSError("[%s] Unable to parse arguments, plugin not engaged.", PLUGIN_NAME.c_str());
    return;
  }

  parse_and_set_new_categories();

  // Populate the callback for dynamic category queries from the core.
  auto cont = TSContCreate(read_request_hdr_event_handler, nullptr);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
}
