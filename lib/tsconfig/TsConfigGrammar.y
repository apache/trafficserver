%code requires {
/** @file

    TS Configuration grammar.

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
}

%code top {
# if ! (defined(__clang_analyzer__) || defined(__COVERITY__))
# include "TsConfigTypes.h"
# include <stdlib.h>

// Inhibit Bison definitions.
# define YYMALLOC malloc
# define YYFREE free

# include "TsConfigParseEvents.h"
# include "tscore/ink_defs.h"

// Types we need for the lexer.
typedef void* yyscan_t;
extern int tsconfiglex(YYSTYPE* yylval, yyscan_t lexer);

}

%code {

# define HANDLE_EVENT(x,y)                                                   \
  if (handlers) {                                                            \
    struct TsConfigEventHandler* h = &(handlers->handler[TsConfigEvent##x]); \
    if (h->_f) h->_f(h->_data, &(y));                                        \
  }

int tsconfigerror(
  yyscan_t lexer ATS_UNUSED,
  struct TsConfigHandlers* handlers,
  char const* text
) {
  return (handlers && handlers->error._f)
    ? handlers->error._f(handlers->error._data, text)
    : 0
    ;
}

}

%token STRING
%token IDENT
%token INTEGER
%token LIST_OPEN
%token LIST_CLOSE
%token GROUP_OPEN
%token GROUP_CLOSE
%token PATH_OPEN
%token PATH_CLOSE
%token PATH_SEPARATOR
%token SEPARATOR
%token ASSIGN

%error-verbose
%define api.pure
%parse-param { yyscan_t lexer }
%parse-param { struct TsConfigHandlers* handlers }
%lex-param { yyscan_t lexer }

%%

config: group_items;

group: group_open group_items group_close ;

group_open: GROUP_OPEN { HANDLE_EVENT(GroupOpen, $1); } ;

group_close: GROUP_CLOSE { HANDLE_EVENT(GroupClose, $1); } ;

group_items: /* empty */ | group_items assign separator | group_items error separator ;

assign: IDENT ASSIGN { HANDLE_EVENT(GroupName, $1); } value ;

list: list_open list_items list_close ;

list_open: LIST_OPEN { HANDLE_EVENT(ListOpen, $1); } ;

list_close: LIST_CLOSE { HANDLE_EVENT(ListClose, $1); } ;

list_items: /* empty */ | list_items value separator | list_items error separator;

value: literal { HANDLE_EVENT(LiteralValue, $1); } | list | group | path;

literal: STRING | IDENT | INTEGER ;

separator: /* empty */ | SEPARATOR ;

path: path_open path_item path_close;

path_open: PATH_OPEN { HANDLE_EVENT(PathOpen, $1); }

path_close: PATH_CLOSE { HANDLE_EVENT(PathClose, $1); }

path_item: path_tag | path_item PATH_SEPARATOR path_tag ;

path_tag: IDENT { HANDLE_EVENT(PathTag, $1); } | INTEGER { HANDLE_EVENT(PathIndex, $1); };

%%

# endif // __clang_analyzer__
