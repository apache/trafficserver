%{

/** @file

    Syntactic analyzer for TS Configuration.

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

# if defined(__GNUC__)
# pragma GCC diagnostic ignored "-Wsign-compare"
# endif


# if ! (defined(__clang_analyzer__) || defined(__COVERITY__))

# include "TsConfigParseEvents.h"
# include "TsConfigGrammar.h"

extern int tsconfigparse(yyscan_t lexer, struct TsConfigHandlers* handlers);

struct Location TsConfig_Lex_Location = { 0, 1 };

# define YY_USER_ACTION TsConfig_Lex_Location._col += yyleng;

# define FILL \
  yylval->_s = yytext; \
  yylval->_n = yyleng; \
  yylval->_loc = TsConfig_Lex_Location; \
  yylval->_loc._col -= yyleng;

# define ZRET(t) FILL; yylval->_type = t; return t;

# define HANDLE_EVENT(x) \
  if (yyextra) { \
    struct TsConfigEventHandler* h = &(yyextra->handler[TsConfigEvent##x]); \
    if (h->_f) h->_f(h->_data, yylval); \
  }

%}

%option outfile="lex.yy.c"
%option never-interactive reentrant bison-bridge noyywrap
%option prefix="tsconfig"
%option nounput noinput
%option extra-type="struct TsConfigHandlers*"

DSTRING		\"(?:[^\"\\]|\\.)*\"
SSTRING		'(?:[^'\\]|\\.)*'
QSTRING		{DSTRING}|{SSTRING}
IDENT		[[:alpha:]_](?:-*[[:alnum:]_])*

%x		bad

%%

\n              {
                  ++(TsConfig_Lex_Location._line);
                  TsConfig_Lex_Location._col = 0;
                }

{QSTRING}	ZRET(STRING); /* Quote string overrides comments */

(?:[[:space:]]{-}[\n])+       /* Ignore all white space. */
^[[:space:]]*#.*$             /* Leading '#' is a comment. */
\/\/.*$                       /* Trailing '//' is a comment. */

{IDENT}		ZRET(IDENT);
[[:digit:]]+	ZRET(INTEGER);
\{              ZRET(GROUP_OPEN);
\}              ZRET(GROUP_CLOSE);
\(              ZRET(LIST_OPEN);
\)              ZRET(LIST_CLOSE);
\<              ZRET(PATH_OPEN);
\>              ZRET(PATH_CLOSE);
\.              ZRET(PATH_SEPARATOR);
=               ZRET(ASSIGN);
[,;]+           ZRET(SEPARATOR);

.           BEGIN(bad); FILL;
<bad>\n     {
              BEGIN(0); // Terminate bad token mode.
              ++(TsConfig_Lex_Location._line); // Must bump line count.
              HANDLE_EVENT(InvalidToken);
            }
<bad>[[:space:]]  BEGIN(0); HANDLE_EVENT(InvalidToken);
<bad>.      ++(yylval->_n);
%%

int tsconfiglex_current_line(void) { return TsConfig_Lex_Location._line; }
int tsconfiglex_current_col(void) { return TsConfig_Lex_Location._col; }

// This is in here because it's easier than trying to convince automake
// to let me have a generated header from flex. The header is only needed
// to define the various macros used here outside of the .l file so if
// this is here, we don't need the header anymore. This also means we
// don't have to deal with C/C++ issues in that regard either.
int tsconfig_parse_buffer(
  struct TsConfigHandlers* handlers,
  char* buffer,
  size_t buffer_len
) {
  int zret;
  yyscan_t lexer;
  YY_BUFFER_STATE lexer_buffer_state;

  tsconfiglex_init(&lexer);
  tsconfigset_extra(handlers, lexer);
  lexer_buffer_state = tsconfig_scan_buffer(buffer, buffer_len, lexer);
  zret = tsconfigparse(lexer, handlers);
  tsconfig_delete_buffer(lexer_buffer_state, lexer);
  tsconfiglex_destroy(lexer);

  return zret;
}

# endif // __clang_analyzer__
