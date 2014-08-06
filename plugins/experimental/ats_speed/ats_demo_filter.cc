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

#include "net/instaweb/rewriter/public/add_head_filter.h"
#include "net/instaweb/htmlparse/public/html_parse.h"
#include "net/instaweb/htmlparse/public/html_element.h"
#include <string>

#include <ts/ts.h>
#include "ats_demo_filter.h"

namespace net_instaweb {
  const char* AtsDemoFilter::kPoweredByHtml =
      "<div id=\"weamp_poweredby\" style=\"bottom:0; height:30px; left:0;  width:100%;\">"
      "<div style=\"line-height:30px; margin:0 auto; width:100%; text-align:center; \">"
      "<a target=\"_blank\" title=\"Google PageSpeed optimization demo brought to you by We-Amp\" href=\"http://www.we-amp.com/\">Google PageSpeed optimization demo by We-Amp</a>"
      "</div>"
      "</div>";



AtsDemoFilter::AtsDemoFilter(HtmlParse* parser, bool banner) :
    parser_(parser),
    banner_(banner)
{
}

void AtsDemoFilter::StartElement(HtmlElement* element) {
  if (banner_ && element->keyword() == HtmlName::kBody) {
    HtmlNode* el = parser_->NewCharactersNode(NULL, AtsDemoFilter::kPoweredByHtml);
    parser_->InsertNodeBeforeCurrent(el);
  }
  
  if (element->keyword() == HtmlName::kA || element->keyword() == HtmlName::kBase
      || element->keyword() == HtmlName::kForm|| element->keyword() == HtmlName::kImg
      || element->keyword() == HtmlName::kLink || element->keyword() == HtmlName::kScript) { 
    HtmlElement::AttributeList* attributes = element->mutable_attributes();
    for (HtmlElement::AttributeIterator i(attributes->begin());
         i != attributes->end(); ++i) {
      
      HtmlElement::Attribute& attribute = *i;
      if (attribute.keyword() == HtmlName::kAction || attribute.keyword() == HtmlName::kHref 
          || attribute.keyword() == HtmlName::kSrc) {
        const char * attribute_value = NULL;
        if ( attribute.DecodedValueOrNull() != NULL ) {
          attribute_value = attribute.DecodedValueOrNull();
        } else {
          attribute_value = attribute.escaped_value();
        }
        
        if ( attribute_value != NULL) {
          GoogleUrl url( attribute_value );
          if (url.IsWebValid()) {
            if (url.Host() == from_domain_) {
              StringPiece scheme = url.Scheme();
              StringPiece host = to_domain_.c_str();
              StringPiece pathAndQuery = url.PathAndLeaf();
              GoogleString rewritten = StrCat(scheme,"://", host, pathAndQuery);
              attribute.SetValue(rewritten.c_str());
              break;
            }
          }
        }
      }   
    }      
  }
}


}  // namespace net_instaweb
