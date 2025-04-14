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

grammar hrw4u;

// -----------------------------
// Lexer Rules
// -----------------------------
VAR           : 'VAR';
IF            : 'if';
ELSE          : 'else';
IN            : 'in';
TRUE          : [tT][rR][uU][eE];
FALSE         : [fF][aA][lL][sS][eE];
WITH          : 'with';
BREAK         : 'break';

REGEX         : '/' ( '\\/' | ~[/\r\n] )* '/' ;
STRING        : '"' ( '\\' . | ~["\\\r\n] )* '"' ;

IPV4_LITERAL
              : (OCTET '.' OCTET '.' OCTET '.' OCTET ('/' IPV4_CIDR)?)
              | (OCTET '.' OCTET '.' OCTET '/' IPV4_CIDR)
              | (OCTET '.' OCTET '/' IPV4_CIDR)
              | (OCTET '/' IPV4_CIDR)
              ;

IPV6_LITERAL  : (HEXDIGIT+ ':')+ HEXDIGIT+ ('/' IPV6_CIDR)?
              | '::' (HEXDIGIT+ ':')* HEXDIGIT+ ('/' IPV6_CIDR)?
              | (HEXDIGIT+ ':')+ ':' ('/' IPV6_CIDR)?
              ;

fragment OCTET    : [0-9] [0-9]? [0-9]? ;
fragment HEXDIGIT : [0-9a-fA-F] ;

fragment IPV4_CIDR     : [1-9]
                       | [1-2][0-9]
                       | '3'[0-2]
                       ;

fragment IPV6_CIDR     : '3'[3-9]
                       | [4-9][0-9]
                       | '1'[0-1][0-9]
                       | '12'[0-8]
                       ;

IDENT         : [a-zA-Z_][a-zA-Z0-9_@.-]* ;
NUMBER        : [0-9]+ ;
LPAREN        : '(';
RPAREN        : ')';
LBRACE        : '{';
RBRACE        : '}';
LBRACKET      : '[';
RBRACKET      : ']';
EQUALS        : '==';
EQUAL         : '=';
NEQ           : '!=';
GT            : '>';
LT            : '<';
AND           : '&&';
OR            : '||';
TILDE         : '~';
NOT_TILDE     : '!~';
COLON         : ':';
COMMA         : ',';
SEMICOLON     : ';';

COMMENT       : '#' ~[\r\n]* -> skip ;
WS            : [ \t\r\n]+ -> skip ;

// -----------------------------
// Parser Rules
// -----------------------------
program
    : section+ EOF
    ;

section
    : VAR COLON variables
    | name=IDENT COLON (conditionalBlock | statementList)
    ;

statementList
    : statement+
    ;

variables
    : variableDecl*
    ;

variableDecl
    : name=IDENT COLON typeName=IDENT SEMICOLON
    ;

conditionalBlock
    : ifStatement elseClause?
    | block
    ;

ifStatement
    : IF condition block
    | IF LPAREN condition RPAREN block
    ;

elseClause
    : ELSE block
    ;

block
    : LBRACE statement* RBRACE
    ;

statement
    : lhs=IDENT EQUAL value SEMICOLON
    | op=IDENT SEMICOLON
    | functionCall SEMICOLON
    | BREAK SEMICOLON
    ;

condition
    : logicalExpression
    ;

logicalExpression
    : logicalExpression OR logicalTerm
    | logicalTerm
    ;

logicalTerm
    : logicalTerm AND logicalFactor
    | logicalFactor
    ;

logicalFactor
    : '!' logicalFactor
    | LPAREN logicalExpression RPAREN
    | functionCall
    | comparison
    | ident=IDENT
    | TRUE
    | FALSE
    ;

comparison
    : comparable (EQUALS | NEQ | GT | LT) value modifier?
    | comparable (TILDE | NOT_TILDE) regex modifier?
    | comparable IN set modifier?
    | comparable IN iprange
    ;

modifier
    : WITH modifierList
    ;

modifierList
    : mods+=IDENT (COMMA mods+=IDENT)*
    ;

comparable
    : ident=IDENT
    | functionCall
    ;

functionCall
    : funcName=IDENT LPAREN argumentList? RPAREN
    ;

argumentList
    : value (COMMA value)*
    ;

regex
    : REGEX
    ;

set
    : LBRACKET value (COMMA value)* RBRACKET
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

value
    : number=NUMBER
    | str=STRING
    | TRUE
    | FALSE
    | ident=IDENT
    | ip
    | iprange
    ;
