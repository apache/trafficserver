# if ! defined(TS_CONFIG_BUILDER_HEADER)
# define TS_CONFIG_BUILDER_HEADER

/** @file

    Header for handler for parsing events.

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

# include "TsValue.h"
# include "TsConfigTypes.h"
# include "TsConfigParseEvents.h"

namespace ts { namespace config {

/** Class to build the configuration table from parser events.
 */
class Builder {
public:
  typedef Builder self;
  struct Handler {
    self* _ptr; ///< Pointer to Builder instance.
    /// Pointer to method to invoke for this event.
    void (self::*_method)(Token const& token);

    /// Default constructor.
    Handler();
  };

  /// Default constructor.
  Builder();
  /// Destructor.
  virtual ~Builder() {}
  /// Construct with existing configuration.
  Builder(Configuration const& config);
  /// Build the table.
  /// @return The configuration or error status.
  Rv<Configuration> build(
			  Buffer const& buffer ///< Input text.
			  );
 protected:
  /// Dispatch table for parse events.
  Handler _dispatch[TS_CONFIG_N_EVENT_TYPES];
  /// Event handler table for the parser.
  TsConfigHandlers _handlers;
  /// Dispatch methods
  virtual void groupOpen(Token const& token);
  virtual void groupClose(Token const& token);
  virtual void groupName(Token const& token);
  virtual void listOpen(Token const& token);
  virtual void listClose(Token const& token);
  virtual void pathOpen(Token const& token);
  virtual void pathTag(Token const& token);
  virtual void pathIndex(Token const& token);
  virtual void pathClose(Token const& token);
  virtual void literalValue(Token const& token);
  virtual void invalidToken(Token const& token);
  /// Syntax error handler
  virtual int syntaxError(char const* text);
  /// Static method to handle parser event callbacks.
  static void dispatch(void* data, Token* token);
  /// Static method for syntax errors.
  static int syntaxErrorDispatch(void* data, char const* text);

  // Building state.
  Configuration _config; ///< Configuration to update.
  Errata _errata; ///< Error accumulator.
  Value _v; ///< Current value.
  Buffer _name; ///< Pending group name, if any.
  Buffer _extent; ///< Accumulator for multi-token text.
  Location _loc; ///< Cache for multi-token text.
  Path _path; ///< Path accumulator

  /// Initialization, called from constructors.
  self& init();
};

inline Builder::Handler::Handler() : _ptr(nullptr), _method(nullptr) { }
inline Builder::Builder() { this->init(); }
inline Builder::Builder(Configuration const& config) : _config(config) { this->init(); }

}} // namespace ts::config

# endif // TS_CONFIG_BUILDER_HEADER
