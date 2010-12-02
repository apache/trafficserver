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

/*
 *
 * ink_isolatin_table.h
 *   The eight bit table used by the isolatin macros.
 *
 * $Date: 2003-06-01 18:36:44 $
 *
 *
 */

#include "ink_isolatin_table.h"
#include "ink_unused.h"       /* MAGIC_EDITING_TAG */
#include "ink_apidefs.h"

#define UNDEF  0
#define DIGIT  1
#define ALPHL  2
#define ALPHU  3
#define PUNCT  4
#define WHSPC  5

/*
 * The eight bit table.
 */
ink_undoc_liapi int eight_bit_table[] = {
                                /* 000 */ UNDEF,
                                /* ^@  NUL */
                                /* 001 */ UNDEF,
                                /* ^A  SOH */
                                /* 002 */ UNDEF,
                                /* ^B  STX */
                                /* 003 */ UNDEF,
                                /* ^C  ETX */
                                /* 004 */ UNDEF,
                                /* ^D  EOT */
                                /* 005 */ UNDEF,
                                /* ^E  ENQ */
                                /* 006 */ UNDEF,
                                /* ^F  ACK */
                                /* 007 */ UNDEF,
                                /* ^G  BEL */
                                /* 008 */ UNDEF,
                                /* ^H  BS  */

                                /* 009 */ WHSPC,
                                /* " " HT  */
                                /* 010 */ WHSPC,
                                /* " " LF  */
                                /* 011 */ WHSPC,
                                /*   VT  */
                                /* 012 */ WHSPC,
                                /* ^L  NP  */
                                /* 013 */ WHSPC,
                                /* ^M  CR  */

                                /* 014 */ UNDEF,
                                /* ^N  SO  */
                                /* 015 */ UNDEF,
                                /* ^O  SI  */
                                /* 016 */ UNDEF,
                                /* ^P  DLE */
                                /* 017 */ UNDEF,
                                /* ^Q  DC1 */
                                /* 018 */ UNDEF,
                                /* ^R  DC2 */
                                /* 019 */ UNDEF,
                                /* ^S  DC3 */
                                /* 020 */ UNDEF,
                                /* ^T  DC4 */
                                /* 021 */ UNDEF,
                                /* ^U  NAK */
                                /* 022 */ UNDEF,
                                /* ^V  SYN */
                                /* 023 */ UNDEF,
                                /* ^W  ETB */
                                /* 024 */ UNDEF,
                                /* ^X  CAN */
                                /* 025 */ UNDEF,
                                /* ^Y  EM  */
                                /* 026 */ UNDEF,
                                /* ^Z  SUB */
                                /* 027 */ UNDEF,
                                /* ^[  ESC */
                                /* 028 */ UNDEF,
                                /* ^\  FS  */
                                /* 029 */ UNDEF,
                                /* ^]  GS  */
                                /* 030 */ UNDEF,
                                /* ^^  RS  */
                                /* 031 */ UNDEF,
                                /* ^_  US  */

                                /* 032 */ WHSPC,
                                /* " " SP  */

                                /* 033 */ PUNCT,
                                /* ! */
                                /* 034 */ PUNCT,
                                /* " */
                                /* 035 */ PUNCT,
                                /* # */
                                /* 036 */ PUNCT,
                                /* $ */
                                /* 037 */ PUNCT,
                                /* % */
                                /* 038 */ PUNCT,
                                /* & */
                                /* 039 */ PUNCT,
                                /* ' */
                                /* 040 */ PUNCT,
                                /* ( */
                                /* 041 */ PUNCT,
                                /* ) */
                                /* 042 */ PUNCT,
                                /* * */
                                /* 043 */ PUNCT,
                                /* + */
                                /* 044 */ PUNCT,
                                /* , */
                                /* 045 */ PUNCT,
                                /* - */
                                /* 046 */ PUNCT,
                                /* . */
                                /* 047 */ PUNCT,
                                /* / */

                                /* 048 */ DIGIT,
                                /* 0 */
                                /* 049 */ DIGIT,
                                /* 1 */
                                /* 050 */ DIGIT,
                                /* 2 */
                                /* 051 */ DIGIT,
                                /* 3 */
                                /* 052 */ DIGIT,
                                /* 4 */
                                /* 053 */ DIGIT,
                                /* 5 */
                                /* 054 */ DIGIT,
                                /* 6 */
                                /* 055 */ DIGIT,
                                /* 7 */
                                /* 056 */ DIGIT,
                                /* 8 */
                                /* 057 */ DIGIT,
                                /* 9 */

                                /* 058 */ PUNCT,
                                /* : */
                                /* 059 */ PUNCT,
                                /* ; */
                                /* 060 */ PUNCT,
                                /* < */
                                /* 061 */ PUNCT,
                                /* = */
                                /* 062 */ PUNCT,
                                /* > */
                                /* 063 */ PUNCT,
                                /* ? */
                                /* 064 */ PUNCT,
                                /* @ */

                                /* 065 */ ALPHU,
                                /* A */
                                /* 066 */ ALPHU,
                                /* B */
                                /* 067 */ ALPHU,
                                /* C */
                                /* 068 */ ALPHU,
                                /* D */
                                /* 069 */ ALPHU,
                                /* E */
                                /* 070 */ ALPHU,
                                /* F */
                                /* 071 */ ALPHU,
                                /* G */
                                /* 072 */ ALPHU,
                                /* H */
                                /* 073 */ ALPHU,
                                /* I */
                                /* 074 */ ALPHU,
                                /* J */
                                /* 075 */ ALPHU,
                                /* K */
                                /* 076 */ ALPHU,
                                /* L */
                                /* 077 */ ALPHU,
                                /* M */
                                /* 078 */ ALPHU,
                                /* N */
                                /* 079 */ ALPHU,
                                /* O */
                                /* 080 */ ALPHU,
                                /* P */
                                /* 081 */ ALPHU,
                                /* Q */
                                /* 082 */ ALPHU,
                                /* R */
                                /* 083 */ ALPHU,
                                /* S */
                                /* 084 */ ALPHU,
                                /* T */
                                /* 085 */ ALPHU,
                                /* U */
                                /* 086 */ ALPHU,
                                /* V */
                                /* 087 */ ALPHU,
                                /* W */
                                /* 088 */ ALPHU,
                                /* X */
                                /* 089 */ ALPHU,
                                /* Y */
                                /* 090 */ ALPHU,
                                /* Z */

                                /* 091 */ PUNCT,
                                /* [ */
                                /* 092 */ PUNCT,
                                /* \ */
                                /* 093 */ PUNCT,
                                /* ] */
                                /* 094 */ PUNCT,
                                /* ^ */
                                /* 095 */ PUNCT,
                                /* _ */
                                /* 096 */ PUNCT,
                                /* ` */

                                /* 097 */ ALPHL,
                                /* a */
                                /* 098 */ ALPHL,
                                /* b */
                                /* 099 */ ALPHL,
                                /* c */
                                /* 100 */ ALPHL,
                                /* d */
                                /* 101 */ ALPHL,
                                /* e */
                                /* 102 */ ALPHL,
                                /* f */
                                /* 103 */ ALPHL,
                                /* g */
                                /* 104 */ ALPHL,
                                /* h */
                                /* 105 */ ALPHL,
                                /* i */
                                /* 106 */ ALPHL,
                                /* j */
                                /* 107 */ ALPHL,
                                /* k */
                                /* 108 */ ALPHL,
                                /* l */
                                /* 109 */ ALPHL,
                                /* m */
                                /* 110 */ ALPHL,
                                /* n */
                                /* 111 */ ALPHL,
                                /* o */
                                /* 112 */ ALPHL,
                                /* p */
                                /* 113 */ ALPHL,
                                /* q */
                                /* 114 */ ALPHL,
                                /* r */
                                /* 115 */ ALPHL,
                                /* s */
                                /* 116 */ ALPHL,
                                /* t */
                                /* 117 */ ALPHL,
                                /* u */
                                /* 118 */ ALPHL,
                                /* v */
                                /* 119 */ ALPHL,
                                /* w */
                                /* 120 */ ALPHL,
                                /* x */
                                /* 121 */ ALPHL,
                                /* y */
                                /* 122 */ ALPHL,
                                /* z */

                                /* 123 */ PUNCT,
                                /* { */
                                /* 124 */ PUNCT,
                                /* | */
                                /* 125 */ PUNCT,
                                /* } */
                                /* 126 */ PUNCT,
                                /* ~ */

                                /* 127 */ UNDEF,
                                /* ^?  DEL */
                                /* 128 */ UNDEF,
                                /* Ä */
                                /* 129 */ UNDEF,
                                /* Å */
                                /* 130 */ UNDEF,
                                /* Ç */
                                /* 131 */ UNDEF,
                                /* É */
                                /* 132 */ UNDEF,
                                /* Ñ */
                                /* 133 */ UNDEF,
                                /* Ö */
                                /* 134 */ UNDEF,
                                /* Ü */
                                /* 135 */ UNDEF,
                                /* á */
                                /* 136 */ UNDEF,
                                /* à */
                                /* 137 */ UNDEF,
                                /* â */
                                /* 138 */ UNDEF,
                                /* ä */
                                /* 139 */ UNDEF,
                                /* ã */
                                /* 140 */ UNDEF,
                                /* å */
                                /* 141 */ UNDEF,
                                /* ç */
                                /* 142 */ UNDEF,
                                /* é */
                                /* 143 */ UNDEF,
                                /* è */
                                /* 144 */ UNDEF,
                                /* ê */
                                /* 145 */ UNDEF,
                                /* ë */
                                /* 146 */ UNDEF,
                                /* í */
                                /* 147 */ UNDEF,
                                /* ì */
                                /* 148 */ UNDEF,
                                /* î */
                                /* 149 */ UNDEF,
                                /* ï */
                                /* 150 */ UNDEF,
                                /* ñ */
                                /* 151 */ UNDEF,
                                /* ó */
                                /* 152 */ UNDEF,
                                /* ò */
                                /* 153 */ UNDEF,
                                /* ô */
                                /* 154 */ UNDEF,
                                /* ö */
                                /* 155 */ UNDEF,
                                /* õ */
                                /* 156 */ UNDEF,
                                /* ú */
                                /* 157 */ UNDEF,
                                /* ù */
                                /* 158 */ UNDEF,
                                /* û */
                                /* 159 */ UNDEF,
                                /* ü */
                                /* 160 */ UNDEF,
                                /*      */

                                /* 161 */ PUNCT,
                                /* ° */
                                /* 162 */ PUNCT,
                                /* ¢ */
                                /* 163 */ PUNCT,
                                /* £ */
                                /* 164 */ PUNCT,
                                /* § */
                                /* 165 */ PUNCT,
                                /* • */
                                /* 166 */ PUNCT,
                                /* ¶ */
                                /* 167 */ PUNCT,
                                /* ß */
                                /* 168 */ PUNCT,
                                /* ® */
                                /* 169 */ PUNCT,
                                /* © */
                                /* 170 */ PUNCT,
                                /* ™ */
                                /* 171 */ PUNCT,
                                /* ´ */
                                /* 172 */ PUNCT,
                                /* ¨ */
                                /* 173 */ PUNCT,
                                /* ≠ */
                                /* 174 */ PUNCT,
                                /* Æ */
                                /* 175 */ PUNCT,
                                /* Ø */
                                /* 176 */ PUNCT,
                                /* ∞ */
                                /* 177 */ PUNCT,
                                /* ± */
                                /* 178 */ PUNCT,
                                /* ≤ */
                                /* 179 */ PUNCT,
                                /* ≥ */
                                /* 180 */ PUNCT,
                                /* ¥ */
                                /* 181 */ PUNCT,
                                /* µ */
                                /* 182 */ PUNCT,
                                /* ∂ */
                                /* 183 */ PUNCT,
                                /* ∑ */
                                /* 184 */ PUNCT,
                                /* ∏ */
                                /* 185 */ PUNCT,
                                /* π */
                                /* 186 */ PUNCT,
                                /* ∫ */
                                /* 187 */ PUNCT,
                                /* ª */
                                /* 188 */ PUNCT,
                                /* º */
                                /* 189 */ PUNCT,
                                /* Ω */
                                /* 190 */ PUNCT,
                                /* æ */
                                /* 191 */ PUNCT,
                                /* ø */

                                /* 192 */ ALPHU,
                                /* ¿ */
                                /* 193 */ ALPHU,
                                /* ¡ */
                                /* 194 */ ALPHU,
                                /* ¬ */
                                /* 195 */ ALPHU,
                                /* √ */
                                /* 196 */ ALPHU,
                                /* ƒ */
                                /* 197 */ ALPHU,
                                /* ≈ */
                                /* 198 */ ALPHU,
                                /* ∆ */
                                /* 199 */ ALPHU,
                                /* « */
                                /* 200 */ ALPHU,
                                /* » */
                                /* 201 */ ALPHU,
                                /* … */
                                /* 202 */ ALPHU,
                                /*   */
                                /* 203 */ ALPHU,
                                /* À */
                                /* 204 */ ALPHU,
                                /* Ã */
                                /* 205 */ ALPHU,
                                /* Õ */
                                /* 206 */ ALPHU,
                                /* Œ */
                                /* 207 */ ALPHU,
                                /* œ */
                                /* 208 */ ALPHU,
                                /* – */
                                /* 209 */ ALPHU,
                                /* — */
                                /* 210 */ ALPHU,
                                /* “ */
                                /* 211 */ ALPHU,
                                /* ” */
                                /* 212 */ ALPHU,
                                /* ‘ */
                                /* 213 */ ALPHU,
                                /* ’ */
                                /* 214 */ ALPHU,
                                /* ÷ */

                                /* 215 */ PUNCT,
                                /* ◊ */

                                /* 216 */ ALPHU,
                                /* ÿ */
                                /* 217 */ ALPHU,
                                /* Ÿ */
                                /* 218 */ ALPHU,
                                /* ⁄ */
                                /* 219 */ ALPHU,
                                /* € */
                                /* 220 */ ALPHU,
                                /* ‹ */
                                /* 221 */ ALPHU,
                                /* › */
                                /* 222 */ ALPHU,
                                /* ﬁ */

                                /* 223 */ ALPHL,
                                /* ﬂ */
                                /* 224 */ ALPHL,
                                /* ‡ */
                                /* 225 */ ALPHL,
                                /* · */
                                /* 226 */ ALPHL,
                                /* ‚ */
                                /* 227 */ ALPHL,
                                /* „ */
                                /* 228 */ ALPHL,
                                /* ‰ */
                                /* 229 */ ALPHL,
                                /* Â */
                                /* 230 */ ALPHL,
                                /* Ê */
                                /* 231 */ ALPHL,
                                /* Á */
                                /* 232 */ ALPHL,
                                /* Ë */
                                /* 233 */ ALPHL,
                                /* È */
                                /* 234 */ ALPHL,
                                /* Í */
                                /* 235 */ ALPHL,
                                /* Î */
                                /* 236 */ ALPHL,
                                /* Ï */
                                /* 237 */ ALPHL,
                                /* Ì */
                                /* 238 */ ALPHL,
                                /* Ó */
                                /* 239 */ ALPHL,
                                /* Ô */
                                /* 240 */ ALPHL,
                                /*  */
                                /* 241 */ ALPHL,
                                /* Ò */
                                /* 242 */ ALPHL,
                                /* Ú */
                                /* 243 */ ALPHL,
                                /* Û */
                                /* 244 */ ALPHL,
                                /* Ù */
                                /* 245 */ ALPHL,
                                /* ı */
                                /* 246 */ ALPHL,
                                /* ˆ */

                                /* 247 */ PUNCT,
                                /* ˜ */

                                /* 248 */ ALPHL,
                                /* ¯ */
                                /* 249 */ ALPHL,
                                /* ˘ */
                                /* 250 */ ALPHL,
                                /* ˙ */
                                /* 251 */ ALPHL,
                                /* ˚ */
                                /* 252 */ ALPHL,
                                /* ¸ */
                                /* 253 */ ALPHL,
                                /* ˝ */
                                /* 254 */ ALPHL,
                                /* ˛ */
                                /* 255 */ ALPHL
                                /* \377 */
};
