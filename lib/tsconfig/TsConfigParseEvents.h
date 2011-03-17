# if ! defined(TS_CONFIG_PARSE_EVENTS_HEADER)
# define TS_CONFIG_PARSE_EVENTS_HEADER

/** @file

    Definition of parsing events and handlers.

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

# include "TsConfigTypes.h"

typedef void (*TsConfigEventFunction)(void* data, YYSTYPE* token);
struct TsConfigEventHandler {
    TsConfigEventFunction _f; ///< Callback function.
    void* _data; ///< Callback context data.
};
typedef int (*TsConfigErrorFunction)(void* data, char const* text);
struct TsConfigErrorHandler {
    TsConfigErrorFunction _f; ///< Callback function.
    void* _data; ///< Callback context data.
};

enum TsConfigEventType {
    TsConfigEventGroupOpen,
    TsConfigEventGroupName,
    TsConfigEventGroupClose,
    TsConfigEventListOpen,
    TsConfigEventListClose,
    TsConfigEventPathOpen,
    TsConfigEventPathTag,
    TsConfigEventPathIndex,
    TsConfigEventPathClose,
    TsConfigEventLiteralValue,
    TsConfigEventInvalidToken
};

# if defined(__cplusplus)
static const size_t TS_CONFIG_N_EVENT_TYPES = TsConfigEventInvalidToken + 1;
# else
# define TS_CONFIG_N_EVENT_TYPES (TsConfigEventInvalidToken + 1)
# endif

struct TsConfigHandlers {
    struct TsConfigErrorHandler error; ///< Syntax error.
    /// Parsing event handlers.
    /// Indexed by @c TsConfigEventType.
    struct TsConfigEventHandler handler[TS_CONFIG_N_EVENT_TYPES];
};

# endif // TS_CONFIG_PARSE_EVENTS_HEADER
