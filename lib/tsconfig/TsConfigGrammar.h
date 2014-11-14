/* A Bison parser, made by GNU Bison 2.7.  */

/* Bison interface for Yacc-like parsers in C

      Copyright (C) 1984, 1989-1990, 2000-2012 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_TSCONFIG_TSCONFIGGRAMMAR_H_INCLUDED
# define YY_TSCONFIG_TSCONFIGGRAMMAR_H_INCLUDED
/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int tsconfigdebug;
#endif
/* "%code requires" blocks.  */
/* Line 2058 of yacc.c  */
#line 1 "TsConfigGrammar.y"

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


/* Line 2058 of yacc.c  */
#line 72 "TsConfigGrammar.h"

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     STRING = 258,
     IDENT = 259,
     INTEGER = 260,
     LIST_OPEN = 261,
     LIST_CLOSE = 262,
     GROUP_OPEN = 263,
     GROUP_CLOSE = 264,
     PATH_OPEN = 265,
     PATH_CLOSE = 266,
     PATH_SEPARATOR = 267,
     SEPARATOR = 268,
     ASSIGN = 269
   };
#endif
/* Tokens.  */
#define STRING 258
#define IDENT 259
#define INTEGER 260
#define LIST_OPEN 261
#define LIST_CLOSE 262
#define GROUP_OPEN 263
#define GROUP_CLOSE 264
#define PATH_OPEN 265
#define PATH_CLOSE 266
#define PATH_SEPARATOR 267
#define SEPARATOR 268
#define ASSIGN 269



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef int YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif


#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int tsconfigparse (void *YYPARSE_PARAM);
#else
int tsconfigparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int tsconfigparse (yyscan_t lexer, struct TsConfigHandlers* handlers);
#else
int tsconfigparse ();
#endif
#endif /* ! YYPARSE_PARAM */

#endif /* !YY_TSCONFIG_TSCONFIGGRAMMAR_H_INCLUDED  */
