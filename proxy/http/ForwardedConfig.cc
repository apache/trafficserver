/** @file

  Configuration of Forwarded HTTP header option.

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

#include <bitset>
#include <string>
#include <cctype>

#include <ts/string_view.h>
#include <ts/MemView.h>

#include <HttpConfig.h>

namespace
{
class BadOptionsErrMsg
{
public:
  // Construct with referece to string that will contain error message.
  //
  BadOptionsErrMsg(ts::FixedBufferWriter &err) : _err(err), _count(0) {}
  // Add a bad option.
  //
  void
  add(ts::StringView badOpt)
  {
    if (_count == 0) {
      _err << "\"Forwarded\" configuration: ";
      _addQuoted(badOpt);
      _count = 1;
    } else if (_count == 1) {
      _saveLast = badOpt;
      _count    = 2;
    } else {
      _err << ", ";
      _addQuoted(_saveLast);
      _saveLast = badOpt;
      ++_count;
    }
  }

  // Returns true it error seen.
  //
  bool
  done()
  {
    if (_count == 0) {
      return false;
    }

    if (_count == 1) {
      _err << " is a bad option.";

    } else if (_count != 0) {
      _err << " and ";
      _addQuoted(_saveLast);
      _err << " are bad options.";
    }
    return true;
  }

private:
  void
  _addQuoted(ts::StringView sv)
  {
    _err << '\"' << ts::string_view(sv.begin(), sv.size()) << '\"';
  }

  ts::FixedBufferWriter &_err;

  ts::StringView _saveLast;

  int _count;
};

// Compare a StringView to a nul-termimated string, converting the StringView to lower case and ignoring whitespace in it.
//
bool
eqIgnoreCaseWs(ts::StringView sv, const char *target)
{
  const char *s = sv.begin();

  std::size_t skip = 0;
  std::size_t i    = 0;

  while ((i + skip) < sv.size()) {
    if (std::isspace(s[i + skip])) {
      ++skip;
    } else if (std::tolower(s[i + skip]) != target[i]) {
      return false;
    } else {
      ++i;
    }
  }

  return target[i] == '\0';
}

} // end anonymous namespace

namespace HttpForwarded
{
OptionBitSet
optStrToBitset(ts::string_view optConfigStr, ts::FixedBufferWriter &error)
{
  const ts::StringView Delimiters(":|");

  OptionBitSet optBS;

  // Convert to TS StringView to be able to use parsing members.
  //
  ts::StringView oCS(optConfigStr.data(), optConfigStr.size());

  if (eqIgnoreCaseWs(oCS, "none")) {
    return OptionBitSet();
  }

  BadOptionsErrMsg em(error);

  do {
    ts::StringView optStr = oCS.extractPrefix(Delimiters);

    if (eqIgnoreCaseWs(optStr, "for")) {
      optBS.set(FOR);

    } else if (eqIgnoreCaseWs(optStr, "by=ip")) {
      optBS.set(BY_IP);

    } else if (eqIgnoreCaseWs(optStr, "by=unknown")) {
      optBS.set(BY_UNKNOWN);

    } else if (eqIgnoreCaseWs(optStr, "by=servername")) {
      optBS.set(BY_SERVER_NAME);

    } else if (eqIgnoreCaseWs(optStr, "by=uuid")) {
      optBS.set(BY_UUID);

    } else if (eqIgnoreCaseWs(optStr, "proto")) {
      optBS.set(PROTO);

    } else if (eqIgnoreCaseWs(optStr, "host")) {
      optBS.set(HOST);

    } else if (eqIgnoreCaseWs(optStr, "connection=compact")) {
      optBS.set(CONNECTION_COMPACT);

    } else if (eqIgnoreCaseWs(optStr, "connection=std")) {
      optBS.set(CONNECTION_STD);

    } else if (eqIgnoreCaseWs(optStr, "connection=standard")) {
      optBS.set(CONNECTION_STD);

    } else if (eqIgnoreCaseWs(optStr, "connection=full")) {
      optBS.set(CONNECTION_FULL);

    } else {
      em.add(optStr);
    }
  } while (oCS);

  if (em.done()) {
    return OptionBitSet();
  }

  return optBS;

} // end optStrToBitset()

} // end namespace HttpForwarded
