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

#include <assert.h>
#include <locale>
#include <string>

#include "html-parser.h"

namespace ats
{
namespace inliner
{
  Attributes::operator std::string(void) const
  {
    std::string result;
    for (Attributes::const_iterator item = begin(); item != end(); ++item) {
      if (!item->first.empty()) {
        if (!item->second.empty()) {
          result += item->first + "=\"" + item->second += "\" ";
        } else {
          result += item->first;
        }
      }
    }
    return result;
  }

  bool
  HtmlParser::parseTag(const char c)
  {
    switch (c) {
    case ' ':
    case '/':
    case '>':
    case '\b':
    case '\n':
    case '\r':
    case '\t':
      return tag_ == Tag::kTagIMG || tag_ == Tag::kTagLINK || tag_ == Tag::kTagSCRIPT || tag_ == Tag::kTagSTYLE;
      break;

    case 'R':
    case 'r':
      if (tag_ == Tag::kTagSC) {
        tag_ = Tag::kTagSCR;
        return false;
      }
      break;

    case 'C':
    case 'c':
      if (tag_ == Tag::kTagS) {
        tag_ = Tag::kTagSC;
        return false;
      }
      break;

    case 'S':
    case 's':
      if (tag_ == Tag::kTag) {
        tag_ = Tag::kTagS;
        return false;
      }
      break;

    case 'T':
    case 't':
      if (tag_ == Tag::kTagS) {
        tag_ = Tag::kTagST;
        return false;
      } else if (tag_ == Tag::kTagSCRIP) {
        tag_ = Tag::kTagSCRIPT;
        return false;
      }
      break;

    case 'I':
    case 'i':
      if (tag_ == Tag::kTag) {
        tag_ = Tag::kTagI;
        return false;
      } else if (tag_ == Tag::kTagSCR) {
        tag_ = Tag::kTagSCRI;
        return false;
      } else if (tag_ == Tag::kTagL) {
        tag_ = Tag::kTagLI;
        return false;
      }
      break;

    case 'P':
    case 'p':
      if (tag_ == Tag::kTagSCRI) {
        tag_ = Tag::kTagSCRIP;
        return false;
      }
      break;

    case 'Y':
    case 'y':
      if (tag_ == Tag::kTagST) {
        tag_ = Tag::kTagSTY;
        return false;
      }
      break;

    case 'L':
    case 'l':
      if (tag_ == Tag::kTag) {
        tag_ = Tag::kTagL;
        return false;
      } else if (tag_ == Tag::kTagSTY) {
        tag_ = Tag::kTagSTYL;
        return false;
      }
      break;

    case 'E':
    case 'e':
      if (tag_ == Tag::kTagSTYL) {
        tag_ = Tag::kTagSTYLE;
        return false;
      }
      break;

    case 'N':
    case 'n':
      if (tag_ == Tag::kTagLI) {
        tag_ = Tag::kTagLIN;
        return false;
      }
      break;

    case 'K':
    case 'k':
      if (tag_ == Tag::kTagLIN) {
        tag_ = Tag::kTagLINK;
        return false;
      }
      break;

    case 'M':
    case 'm':
      if (tag_ == Tag::kTagI) {
        tag_ = Tag::kTagIM;
        return false;
      }
      break;

    case 'G':
    case 'g':
      if (tag_ == Tag::kTagIM) {
        tag_ = Tag::kTagIMG;
        return false;
      }
      break;
    }
    tag_ = Tag::kTagInvalid;
    return false;
  }

  bool
  AttributeParser::parse(const char c)
  {
    switch (state_) {
    case Attribute::kPreName:
      if (isValidName(c)) {
        state_ = Attribute::kName;
        attributes.push_back(Pair());
        attributes.back().first += c;
      } else if (c == '/' || c == '>') {
        return true;
      }
      break;
    case Attribute::kName:
      if (isValidName(c)) {
        attributes.back().first += c;
      } else if (c == '=') {
        state_ = Attribute::kPreValue;
      } else if (c == '/' || c == '>') {
        return true;
      } else {
        state_ = Attribute::kPostName;
      }
      break;
    case Attribute::kPostName:
      if (isValidName(c)) {
        state_ = Attribute::kName;
        attributes.push_back(Pair());
        attributes.back().first += c;
      } else if (c == '=') {
        state_ = Attribute::kPreValue;
      } else if (c == '/' || c == '>') {
        return true;
      }
      break;
    case Attribute::kPreValue:
      // TODO(dmorilha) add the unquoted value.
      if (c == '\'') {
        state_ = Attribute::kSingleQuotedValue;
      } else if (c == '"') {
        state_ = Attribute::kDoubleQuotedValue;
        // VERY BROKEN SYNTAX
      } else if (c == '/' || c == '>') {
        return true;
      } else if (isValidValue(c)) {
        state_ = Attribute::kUnquotedValue;
        attributes.back().second += c;
      }
      break;
    case Attribute::kUnquotedValue:
      if (isValidValue(c)) {
        attributes.back().second += c;
      } else if (c == '/' || c == '>' || c == '"' || c == '\'') {
        return true;
        // space?
      } else {
        state_ = Attribute::kPreName;
      }
      break;
    case Attribute::kSingleQuotedValue:
      if (c == '\'') {
        state_ = Attribute::kPreName;
      } else {
        attributes.back().second += c;
      }
      break;
    case Attribute::kDoubleQuotedValue:
      if (c == '"') {
        state_ = Attribute::kPreName;
      } else {
        attributes.back().second += c;
      }
      break;
    default:
      assert(false); // UNREACHABLE;
      break;
    }
    return false;
  }

  size_t
  HtmlParser::parse(const char *b, size_t l, size_t o)
  {
    const char *const end = b + l, *c = b;

    size_t done = 0;

    for (; c != end; ++c) {
      if (state_ == State::kAttributes) {
        if (attributeParser_.parse(*c)) {
          switch (tag_) {
          case Tag::kTagIMG:
            handleImage(attributeParser_.attributes);
            attributeParser_.reset();
            o += c - b;
            l -= c - b;
            b = c;
            break;
          default:
            break;
          }
          state_ = State::kTagBypass;
        }
        continue;
      }

      if (state_ == State::kTag) {
        if (parseTag(*c)) {
          state_ = State::kAttributes;
          attributeParser_.reset();
          const size_t p = c - b;
          if (p > 0 && tag_ == Tag::kTagIMG) {
            done += bypass(p, o);
            o += p;
            l -= p;
            b = c;
          }
        } else if (tag_ == Tag::kTagInvalid) {
          state_ = State::kTagBypass;
        }
        continue;
      }

      if (state_ == State::kTagBypass) {
        if (*c == '>') {
          state_ = State::kUndefined;
        }
        continue;
      }

      if (state_ == State::kUndefined) {
        if (*c == '<') {
          state_ = State::kTag;
          tag_   = Tag::kTag;
        }
        continue;
      }
    }

    if (l > 0 && (state_ != State::kAttributes || tag_ != Tag::kTagIMG)) {
      done += bypass(l, o);
    }

    return done;
  }

} // end of inliner namespace
} // end of ats namespace
