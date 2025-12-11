/** @file

  Configuration format conversion command for traffic_ctl.

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

#include "ConvertConfigCommand.h"
#include "config/ssl_multicert.h"

#include <fstream>
#include <iostream>

ConvertConfigCommand::ConvertConfigCommand(ts::Arguments *args) : CtrlCommand(args)
{
  BasePrinter::Options print_opts{parse_print_opts(args)};
  _printer = std::make_unique<GenericPrinter>(print_opts);

  if (args->get("ssl_multicert")) {
    auto const &convert_args = args->get("ssl_multicert");
    if (convert_args.size() < 2) {
      throw std::invalid_argument("ssl_multicert requires <input_file> <output_file>");
    }
    _input_file   = convert_args[0];
    _output_file  = convert_args[1];
    _invoked_func = [this]() { convert_ssl_multicert(); };
  } else {
    throw std::invalid_argument("Unsupported config type for conversion");
  }
}

void
ConvertConfigCommand::convert_ssl_multicert()
{
  config::SSLMultiCertParser                       parser;
  config::ConfigResult<config::SSLMultiCertConfig> result = parser.parse(_input_file);

  if (!result.ok()) {
    std::string error_msg = "Failed to parse input file '" + _input_file + "'";
    if (!result.errata.empty()) {
      error_msg += ": ";
      error_msg += std::string(result.errata.front().text());
    }
    _printer->write_output(error_msg);
    return;
  }

  config::SSLMultiCertMarshaller marshaller;
  std::string const              yaml_output = marshaller.to_yaml(result.value);

  // Write to output file or stdout if output is "-".
  if (_output_file == "-") {
    std::cout << yaml_output;
  } else {
    std::ofstream out(_output_file);
    if (!out) {
      _printer->write_output("Failed to open output file '" + _output_file + "' for writing");
      return;
    }
    out << yaml_output;
    out.close();
    _printer->write_output("Converted " + _input_file + " -> " + _output_file);
  }
}
