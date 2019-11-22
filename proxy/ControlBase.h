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

/*****************************************************************************
 *
 *  ControlBase.h - Base class to process generic modifiers to
 *                         ControlMatcher Directives
 *
 *
 ****************************************************************************/

#pragma once

#include "tscore/ink_platform.h"
#include "vector"

class HttpRequestData;
class Tokenizer;
struct matcher_line;

class ControlBase
{
public:
  struct Modifier {
    enum Type {
      MOD_INVALID,
      MOD_PORT,
      MOD_SCHEME,
      MOD_PREFIX,
      MOD_SUFFIX,
      MOD_METHOD,
      MOD_TIME,
      MOD_SRC_IP,
      MOD_IPORT,
      MOD_TAG,
      MOD_INTERNAL,
    };
    /// Destructor - force virtual.
    virtual ~Modifier();
    /// Return the modifier type.
    virtual Type type() const;
    /// Return the name for the modifier type.
    virtual const char *name() const = 0;
    /** Test if the modifier matches the request.
        @return @c true if the request is matched, @c false if not.
    */
    virtual bool check(HttpRequestData *req ///< Request to check.
    ) const = 0;
    /// Print the mod information.
    virtual void print(FILE *f ///< Output stream.
    ) const = 0;
  };

  ControlBase();
  ~ControlBase();
  const char *ProcessModifiers(matcher_line *line_info);
  bool CheckModifiers(HttpRequestData *request_data);
  bool CheckForMatch(HttpRequestData *request_data, int last_number);
  void Print();
  int line_num = 0;
  Modifier *findModOfType(Modifier::Type t) const;

protected:
  /// Get the text for the Scheme modifier, if any.
  /// @return The text if present, 0 otherwise.
  /// @internal Ugly but it's the only place external access is needed.
  const char *getSchemeModText() const;

private:
  typedef std::vector<Modifier *> Array;
  Array _mods;
  const char *ProcessSrcIp(char *val, void **opaque_ptr);
  const char *ProcessTimeOfDay(char *val, void **opaque_ptr);
  const char *ProcessPort(char *val, void **opaque_ptr);

  // Reset to default constructed state, free all allocations.
  void clear();
};

inline ControlBase::ControlBase() {}

inline bool
ControlBase::CheckForMatch(HttpRequestData *request_data, int last_number)
{
  return (last_number < 0 || last_number > this->line_num) && this->CheckModifiers(request_data);
}
