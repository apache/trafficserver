//
// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information
//  regarding copyright ownership.  The ASF licenses this file
//  to you under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance
//  with the License.  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

grammar u4wrh;

// -----------------------------
// Lexer Rules
// -----------------------------
COND          : 'cond';
IF_OP         : 'if';
ENDIF_OP      : 'endif';
ELIF          : 'elif';
ELSE          : 'else';
AND_MOD       : 'AND';
OR_MOD        : 'OR';
NOT_MOD       : 'NOT';
NOCASE_MOD    : 'NOCASE';
SUF_MOD       : 'SUF';
PRE_MOD       : 'PRE';
EXT_MOD       : 'EXT';
MID_MOD       : 'MID';

REGEX         : '/' ( '\\/' | ~[/\r\n] )* '/' ;
STRING        : '"' ( '\\' . | ~["\\\r\n] )* '"' ;

IPV4_LITERAL  : DIGIT+ '.' DIGIT+ '.' DIGIT+ '.' DIGIT+ ('/' DIGIT+)?;
IPV6_LITERAL  : (HEXDIGIT+ ':')+ HEXDIGIT+ ('/' DIGIT+)?
              | (HEXDIGIT+ ':')+ ':' ('/' DIGIT+)?
              | '::' (HEXDIGIT+ ':')* HEXDIGIT+ ('/' DIGIT+)?
              | '::' ('/' DIGIT+)?
              ;

fragment DIGIT    : [0-9];
fragment HEXDIGIT : [0-9a-fA-F];

// Percent blocks - treat entire %{...} as one token
PERCENT_BLOCK : '%{' ~[}\r\n]* '}' '}'?;

IDENT          : [@a-zA-Z_][a-zA-Z0-9_@.-]* ;
COMPLEX_STRING : (~[ \t\r\n[\]{}(),=!><~%#])+;
NUMBER         : [0-9]+ ;
LPAREN         : '(';
RPAREN         : ')';
LBRACE         : '{';
RBRACE         : '}';
LBRACKET       : '[';
RBRACKET       : ']';
EQUALS         : '=';
NEQ            : '!=';
GT             : '>';
LT             : '<';
COMMA          : ',';

EOL            : '\r'? '\n';
COMMENT        : '#'~[\r\n]*;
WS             : [ \t]+ -> skip ;

// -----------------------------
// Parser Rules
// -----------------------------

program
    : line* EOF
    ;

line
    : condLine EOL
    | opLine EOL
    | ifLine EOL
    | endifLine EOL
    | elifLine EOL
    | elseLine EOL
    | commentLine EOL
    | EOL                     // blank line
    ;

condLine
    : COND condBody modList?
    ;

ifLine
    : IF_OP
    ;

endifLine
    : ENDIF_OP
    ;

elifLine
    : ELIF
    ;

elseLine
    : ELSE
    ;

condBody
    : comparison
    | functionCond
    | bareRef
    ;

// %{...} as a bare reference (TRUE/FALSE/GROUP hooks also arrive here)
bareRef
    : percentRef
    ;

// Comparison forms including implicit regex "~" and implicit equality "="
comparison
    : lhs ( cmpOp rhs
          | regex
          | set_                 // implicit "in" when set follows directly
          | iprange              // implicit "in" when iprange follows directly
          | STRING               // implicit "=" when string follows directly
          | NUMBER               // implicit "=" when number follows directly
          | IDENT                // implicit "=" when identifier follows directly
          | COMPLEX_STRING       // implicit "=" when complex string follows directly
          )
    ;

lhs
    : percentRef
    ;

cmpOp
    : EQUALS
    | NEQ
    | GT
    | LT
    ;

rhs
    : value
    ;

// %{...} used as a function-like condition (same token shape)
functionCond
    : percentFunc
    ;

// Both percent forms are the same token; visitor disambiguates
percentRef
    : PERCENT_BLOCK
    ;

percentFunc
    : PERCENT_BLOCK
    ;

// -----------------------------
// Values and Literals
// -----------------------------

value
    : NUMBER
    | STRING
    | IDENT
    | COMPLEX_STRING
    | ipv4
    | ipv6
    | iprange
    ;

regex
    : REGEX
    ;

set_
    : LBRACKET value (COMMA value)* RBRACKET
    | LPAREN value (COMMA value)* RPAREN
    ;

ip
    : ipv4
    | ipv6
    ;

ipv4
    : IPV4_LITERAL
    ;

ipv6
    : IPV6_LITERAL
    ;

iprange
    : LBRACE ip (COMMA ip)* RBRACE
    ;

// -----------------------------
// Modifiers
// -----------------------------

modList
    : LBRACKET modItem (COMMA modItem)* RBRACKET
    ;

modItem
    : AND_MOD
    | OR_MOD
    | NOT_MOD
    | NOCASE_MOD
    | SUF_MOD
    | PRE_MOD
    | EXT_MOD
    | MID_MOD
    ;

// -----------------------------
// Operator Lines
// -----------------------------

opLine
    : opText
    ;

opText
    : IDENT opTail*
    ;

opTail
    : IDENT
    | NUMBER
    | STRING
    | PERCENT_BLOCK
    | COMPLEX_STRING
    | LBRACKET opFlag (COMMA opFlag)* RBRACKET
    ;

opFlag
    : IDENT
    | 'QSA'
    ;

commentLine
    : COMMENT
    ;
