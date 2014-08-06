/** @file

    A brief file description

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

#ifndef ATS_DEMO_FILTER_H_
#define ATS_DEMO_FILTER_H_

#include "base/basictypes.h"
#include "base/string_piece.h"
#include "net/instaweb/htmlparse/public/empty_html_filter.h"
#include "net/instaweb/util/public/atom.h"
#include "net/instaweb/htmlparse/public/html_parse.h"
#include "net/instaweb/htmlparse/public/html_element.h"
#include <string>

using base::StringPiece;

namespace net_instaweb {


  
  class AtsDemoFilter : public EmptyHtmlFilter {
 public:
    static const char* kPoweredByHtml;

    explicit AtsDemoFilter(HtmlParse* parser, bool banner);
    virtual void StartElement(HtmlElement* element);
    virtual const char* Name() const { return "AtsDemo"; }
    // TODO: move to constructor
    void set_domains(const StringPiece& to_domain, const StringPiece& from_domain) 
    { 
      to_domain.CopyToString(&to_domain_); 
      from_domain.CopyToString(&from_domain_); 
    }

  private:
    std::string to_domain_;
    std::string from_domain_;
    HtmlParse* parser_;
    bool banner_;
    DISALLOW_COPY_AND_ASSIGN(AtsDemoFilter);
  };

}  // namespace net_instaweb

#endif  // ATS_DEMO_FILTER_H_
