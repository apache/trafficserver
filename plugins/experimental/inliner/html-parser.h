/** @file

  Inlines base64 images from the ATS cache

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
#ifndef HTML_PARSER_H
#define HTML_PARSER_H

#include <cstdint>
#include <memory>
#include <vector>

namespace ats
{
namespace inliner
{
  typedef std::pair<std::string, std::string> Pair;
  typedef std::vector<Pair> AttributeVector;

  struct Attributes : AttributeVector {
    operator std::string(void) const;
  };

  struct Tag {
    enum TAGS {
      kUndefined = 0,

      kTag,

      kTagI,
      kTagIM,
      kTagIMG,

      kTagS,
      kTagSC,
      kTagSCR,
      kTagSCRI,
      kTagSCRIP,
      kTagSCRIPT,

      kTagST,
      kTagSTY,
      kTagSTYL,
      kTagSTYLE,

      kTagL,
      kTagLI,
      kTagLIN,
      kTagLINK,

      kTagInvalid,

      kUpperBound,
    };
  };

  struct Attribute {
    enum ATTRIBUTES {
      kUndefined = 0,

      kPreName,
      kName,
      kPostName,
      kPreValue,
      kUnquotedValue,
      kSingleQuotedValue,
      kDoubleQuotedValue,

      kUpperBound,
    };
  };

  struct State {
    enum STATES {
      kUndefined = 0,

      kTag,
      kTagBypass,
      kClosingTag,
      kAttributes,

      kUpperBound,
    };
  };

  struct AttributeParser {
    Attribute::ATTRIBUTES state_;
    Attributes attributes;

    AttributeParser(void) : state_(Attribute::kPreName) {}
    void
    reset(void)
    {
      state_ = Attribute::kPreName;
      attributes.clear();
    }

    bool
    isValidName(char c) const
    {
      return std::isalnum(c) || c == '-' || c == '.' || c == '_';
    }

    // TODO(dmorilha): what is valid value? Check w3c.
    bool
    isValidValue(char c) const
    {
      return std::isalnum(c) || c == '-' || c == '.' || c == '_';
    }

    bool parse(const char);
  };

  struct HtmlParser {
    State::STATES state_;
    Tag::TAGS tag_;
    AttributeParser attributeParser_;

    HtmlParser(void) : state_(State::kUndefined), tag_(Tag::kUndefined) {}
    virtual ~HtmlParser() {}
    bool parseTag(const char);
    size_t parse(const char *, size_t, size_t o = 0);
    virtual void handleImage(const Attributes &) = 0;
    virtual size_t bypass(const size_t, const size_t) = 0;
  };

} // end of inliner namespace
} // end of ats namespace

#endif // HTML_PARSER_H
