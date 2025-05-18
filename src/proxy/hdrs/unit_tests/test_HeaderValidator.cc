/** @file
 *
 *  Catch-based unit tests for the HeaderValidator class
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "catch.hpp"

#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/HeaderValidator.h"
#include <string_view>
#include <vector>

namespace
{
using Fields_type              = std::vector<std::pair<std::string, std::string>>;
constexpr bool IS_VALID_HEADER = true;

void
add_field_value_to_hdr(HTTPHdr &hdr, std::string_view field_name, std::string_view field_value)
{
  MIMEField *new_field = hdr.field_create(field_name);
  new_field->value_set(hdr.m_heap, hdr.m_mime, field_value);
  hdr.field_attach(new_field);
}

void
check_header(const Fields_type &fields, HTTPHdr &hdr, bool expectation, bool is_trailer = false)
{
  for (auto &field : fields) {
    add_field_value_to_hdr(hdr, field.first, field.second);
  }
  auto ret = HeaderValidator::is_h2_h3_header_valid(hdr, hdr.type_get() == HTTPType::RESPONSE, is_trailer);
  REQUIRE(ret == expectation);
}
} // end anonymous namespace

TEST_CASE("testIsHeaderValid", "[proxy][hdrtest]")
{
  HTTPHdr hdr;
  // extra to prevent proxy allocation.
  HdrHeap *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);

  SECTION("Test (valid) request with 4 required pseudo headers")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "GET"         },
      {":scheme",    "https"       },
      {":authority", "www.this.com"},
      {":path",      "/some/path"  },
    };
    check_header(fields, hdr, IS_VALID_HEADER);
  }
  SECTION("Test request with missing method field")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":scheme",    "https"       },
      {":authority", "www.this.com"},
      {":path",      "/some/path"  },
    };
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test request with missing authority field")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method", "GET"       },
      {":scheme", "https"     },
      {":path",   "/some/path"},
    };
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test request with missing scheme field")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "GET"         },
      {":authority", "www.this.com"},
      {":path",      "/some/path"  },
    };
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test request with missing path field")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "GET"         },
      {":scheme",    "https"       },
      {":authority", "www.this.com"},
    };
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test request with extra pseudo headers")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "GET"         },
      {":scheme",    "https"       },
      {":authority", "www.this.com"},
      {":path",      "/some/path"  },
      {":extra",     "abc"         },
    };
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test CONNECT request with all required fields")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "CONNECT"     },
      {":authority", "www.this.com"},
      {"extra",      "abc"         }
    };
    check_header(fields, hdr, IS_VALID_HEADER);
  }
  SECTION("Test CONNECT request with disallowed :scheme field")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "CONNECT"     },
      {":authority", "www.this.com"},
      {":scheme",    "https"       },
      {"extra",      "abc"         }
    };
    // :scheme and :path should be omitted in CONNECT requests.
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test CONNECT request with disallowed :path field")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "CONNECT"     },
      {":authority", "www.this.com"},
      {":path",      "/some/path"  },
      {"extra",      "abc"         }
    };
    // :scheme and :path should be omitted in CONNECT requests.
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test (valid) response with only the status field")
  {
    hdr.create(HTTPType::RESPONSE, HTTP_1_1, heap);
    Fields_type fields = {
      {":status", "200"},
    };
    check_header(fields, hdr, IS_VALID_HEADER);
  }

  SECTION("Test response with more than the status field")
  {
    hdr.create(HTTPType::RESPONSE, HTTP_1_1, heap);
    Fields_type fields = {
      {":status", "200"},
      {":method", "GET"},
    };
    // Response headers cannot have pseudo headers other than :status.
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test response with no status field")
  {
    hdr.create(HTTPType::RESPONSE, HTTP_1_1, heap);
    Fields_type fields = {
      {":method", "GET"},
    };
    // Response headers must contain :status.
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test (invalid) trailer header with pseudo-header field")
  {
    hdr.create(HTTPType::RESPONSE, HTTP_1_1, heap);
    Fields_type fields = {
      {":status", "500"},
    };
    static constexpr bool IS_TRAILER = true;
    // Trailer headers may not contain any pseudo-header field.
    check_header(fields, hdr, !IS_VALID_HEADER, IS_TRAILER);
  }
  SECTION("Test request with Connection headers")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "GET"         },
      {":scheme",    "https"       },
      {":authority", "www.this.com"},
      {":path",      "/some/path"  },
      {"Connection", "Keep-Alive"  },
    };
    // Connection-specific headers are not allowed.
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test request with Keep-Alive headers")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "GET"                },
      {":scheme",    "https"              },
      {":authority", "www.this.com"       },
      {":path",      "/some/path"         },
      {"Keep-Alive", "timeout=5, max=1000"},
    };
    // Connection-specific headers are not allowed.
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test request with Proxy-Connection headers")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",          "GET"         },
      {":scheme",          "https"       },
      {":authority",       "www.this.com"},
      {":path",            "/some/path"  },
      {"Proxy-Connection", "Keep-Alive"  },
    };
    // Connection-specific headers are not allowed.
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  SECTION("Test request with Upgrade headers")
  {
    hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
    Fields_type fields = {
      {":method",    "GET"         },
      {":scheme",    "https"       },
      {":authority", "www.this.com"},
      {":path",      "/some/path"  },
      {"Upgrade",    "HTTP/2.0"    },
    };
    // Connection-specific headers are not allowed.
    check_header(fields, hdr, !IS_VALID_HEADER);
  }
  // teardown
  hdr.destroy();
}
