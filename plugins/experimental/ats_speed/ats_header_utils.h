// Copyright 2013 We-Amp B.V.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: oschaaf@we-amp.com (Otto van der Schaaf)
#ifndef ATS_HEADER_UTILS_H
#define ATS_HEADER_UTILS_H

#include <string>

#include <ts/ts.h>

#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_util.h"


GoogleString get_header(TSMBuffer bufp, TSMLoc hdr_loc, const char * header_name);
void unset_header(TSMBuffer bufp, TSMLoc hdr_loc, const char * header_name);
void hide_accept_encoding(TSMBuffer reqp, TSMLoc hdr_loc, const char * hidden_header_name);
void restore_accept_encoding(TSMBuffer reqp, TSMLoc hdr_loc, const char * hidden_header_name);
void set_header(TSMBuffer bufp, TSMLoc hdr_loc, const char * header_name, const char * header_value);

#endif //  ATS_HEADER_UTILS_H
