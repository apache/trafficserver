/** @file

  SSL Multi-Certificate configuration command for traffic_ctl.

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

#include "SSLMultiCertCommand.h"
#include "config/ssl_multicert.h"
#include "tscore/Layout.h"
#include "tscore/Filenames.h"

#include <iostream>

namespace
{

/// Get the default ssl_multicert.yaml file path.
std::string
get_default_ssl_multicert_path()
{
  std::string sysconfdir;
  if (char const *env = getenv("PROXY_CONFIG_CONFIG_DIR")) {
    sysconfdir = Layout::get()->relative(env);
  } else {
    sysconfdir = Layout::get()->sysconfdir;
  }
  return Layout::get()->relative_to(sysconfdir, ts::filename::SSL_MULTICERT);
}

} // namespace

SSLMultiCertCommand::SSLMultiCertCommand(ts::Arguments *args) : CtrlCommand(args)
{
  BasePrinter::Options print_opts{parse_print_opts(args)};
  _printer = std::make_unique<GenericPrinter>(print_opts);

  if (args->get("show")) {
    _invoked_func = [this]() { show_config(); };
  } else {
    throw std::invalid_argument("Unsupported ssl-multicert subcommand");
  }
}

void
SSLMultiCertCommand::show_config()
{
  std::string const filename = get_default_ssl_multicert_path();

  config::SSLMultiCertParser                       parser;
  config::ConfigResult<config::SSLMultiCertConfig> result = parser.parse(filename);

  if (!result.ok()) {
    std::string error_msg = "Failed to parse ssl_multicert config";
    if (!result.errata.empty()) {
      error_msg += ": ";
      error_msg += std::string(result.errata.front().text());
    }
    _printer->write_output(error_msg);
    return;
  }

  config::SSLMultiCertMarshaller marshaller;

  // Output in JSON format for easy consumption by tools.
  std::string const output = marshaller.to_json(result.value);
  _printer->write_output(output);
}
