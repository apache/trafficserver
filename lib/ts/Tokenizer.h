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

/***************************************/

#ifndef _TOKENIZER_H_
#define _TOKENIZER_H_

/****************************************************************************
 *
 *  Tokenizer.h - A string tokenzier
 *
 *
 *
 ****************************************************************************/

/**********************************************************
 *  class Tokenizer
 *
 *  Tokenizes a string, and then allows array like access
 *
 *  The delimiters are determined by the string passed to the
 *   the constructor.
 *
 *  There are three memory options.
 *     SHARE_TOKS - this modifies the original string passed in
 *          through Intialize() and shares its space.   NULLs
 *          are inserted into string after each token.  Choosing
 *          this option means the user is reponsible for not
 *          deallocating the string storage before deallocating
 *          the tokenizer object
 *     COPY_TOKS - this option copies the orginial string and
 *          leaves the original unchanged.  The deallocation of the
 *          original string and the deallocation of the Tokenizer
 *          object are now independent.
 *     Note: If neither SHARE_TOKS or COPY_TOKS is selected, COPY_TOKS
 *          is the default
 *     ALLOW_EMPTY_TOKENS: If multiple delimiters appear next to each
 *          other, each delimiter creates a token someof which
 *          will be zero length.  The default is to skip repeated
 *          delimiters
 *
 *  Tokenizer(const char* StrOfDelimit) - a string that contains
 *     the delimiters for tokenizing.  This string is copied.
 *
 *  Intialize(char* str, TokenizerOpts opt) - Submits a string
 *     to be tokenized according to the memory options listed above
 *
 *  ReUse() - Allows the object to be reused for a new string
 *     After ReUse() is called, Initialize() can be called safely
 *     again
 *
 *  operator[index] - returns a pointer to the number token given
 *     by index.  If index > numTokens-1, NULL is returned.
 *     Because of way tokens are stored, this is O(n) operation
 *     It is very fast though for the first 16 tokens and
 *     is intended to be used on a small number of tokens
 *
 *  iterFirst(tok_iter_state* state) - Returns the first
 *     token and intializes state argument for subsequent
 *     calls to iterNext.  If no tokens exist, NULL is
 *     returned
 *
 *  iterNext(tok_iter_state* state) - Returns the next token after
 *     what arg state returned next last time.   Returns NULL if no
 *     more tokens exists.
 *
 *  Note: To iterate through a list using operator[] takes O(n^2) time
 *      Using iterFirst, iterNext the running time is O(n), so use
 *      the iteration where possible
 *
 *  count() - returns the number of tokens
 *
 *  setMaxTokens() - sets the maximum number of tokens.  Once maxTokens
 *                     is reached, delimiters are ignored and the
 *                     last token is rest of the string.  Negative numbers
 *                     mean no limit on the number of tokens
 *
 *  getMaxTokens() - returns maxTokens.  UINT_MAX means no limit
 *
 *  Print() - Debugging method to print out the tokens
 *
 *******************************************************************/

#include "ts/ink_apidefs.h"

#define COPY_TOKS (1u << 0)
#define SHARE_TOKS (1u << 1)
#define ALLOW_EMPTY_TOKS (1u << 2)
#define ALLOW_SPACES (1u << 3)

#define TOK_NODE_ELEMENTS 16

struct tok_node {
  char *el[TOK_NODE_ELEMENTS];
  tok_node *next;
};

struct tok_iter_state {
  tok_node *node;
  int index;
};

class Tokenizer
{
public:
  inkcoreapi Tokenizer(const char *StrOfDelimiters);
  inkcoreapi ~Tokenizer();

  unsigned Initialize(char *str, unsigned options);
  inkcoreapi unsigned Initialize(const char *str); // Automatically sets option to copy
  const char *operator[](unsigned index) const;

  void
  setMaxTokens(unsigned max)
  {
    maxTokens = max;
  };

  unsigned
  getMaxTokens() const
  {
    return maxTokens;
  };

  unsigned count() const;
  void Print(); // Debugging print out

  inkcoreapi const char *iterFirst(tok_iter_state *state);
  inkcoreapi const char *iterNext(tok_iter_state *state);

private:
  Tokenizer &operator=(const Tokenizer &);
  Tokenizer(const Tokenizer &);
  int isDelimiter(char c);
  void addToken(char *startAddr, int length);
  void ReUse();
  char *strOfDelimit;
  tok_node start_node;
  unsigned numValidTokens;
  unsigned maxTokens;
  int options;
  bool quoteFound;

  // State about where to add the next token
  tok_node *add_node;
  int add_index;
};

#endif
