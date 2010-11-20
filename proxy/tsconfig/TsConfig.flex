%{

#include "TsConfigTypes.h"
#include "TsConfig.tab.h"
#include "TsConfigParseEvents.h"

struct Location Lex_Location = { 0, 1 };

# define YY_USER_ACTION Lex_Location._col += yyleng;

# define FILL yylval->_s = yytext; yylval->_n = yyleng; yylval->_loc = Lex_Location; yylval->_loc._col -= yyleng;
# define ZRET(t) FILL; yylval->_type = t; return t;
# define HANDLE_EVENT(x) \
	if (yyextra) { \
		struct TsConfigEventHandler* h = &(yyextra->handler[TsConfigEvent##x]); \
		if (h->_f) h->_f(h->_data, yylval); \
	}
%}

%option header-file="TsConfig.lex.h"
%option never-interactive yylineno reentrant bison-bridge noyywrap
%option prefix="tsconfig"
%option nounput noinput
%option extra-type="struct TsConfigHandlers*"

DSTRING		\"(?:[^\"\\]|\\.)*\"
SSTRING		'(?:[^'\\]|\\.)*'
QSTRING		{DSTRING}|{SSTRING}
IDENT		[[:alpha:]_](?:-*[[:alnum:]_])*

%x		bad
%%
\n		    ++(Lex_Location._line); Lex_Location._col = 0;
(?:[[:space:]]{-}[\n])+
^[[:space:]]*#.*$
\/\/.*$
{QSTRING}	ZRET(STRING);
{IDENT}		ZRET(IDENT);
[[:digit:]]+	ZRET(INTEGER);
\{          ZRET(GROUP_OPEN);
\}          ZRET(GROUP_CLOSE);
\(          ZRET(LIST_OPEN);
\)          ZRET(LIST_CLOSE);
\<          ZRET(PATH_OPEN);
\>          ZRET(PATH_CLOSE);
\.          ZRET(PATH_SEPARATOR);
=           ZRET(ASSIGN);
[,;]+		ZRET(SEPARATOR);

.           BEGIN(bad); FILL;
<bad>\n		{
		        BEGIN(0); // Terminate bad token mode.
		        ++(Lex_Location._line); // Must bump line count.
		        HANDLE_EVENT(InvalidToken);
            }
<bad>[[:space:]]  BEGIN(0); HANDLE_EVENT(InvalidToken);
<bad>.      ++(yylval->_n);
%%
