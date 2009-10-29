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

#include "ink_unused.h"        /* MAGIC_EDITING_TAG */
#include "Main.h"
#include "Regex.h"

void
test()
{
#if 0
  char **s;
  MultiRegex regex_table;
  char *strings[] = {
    "booble",
    "ofrobbery",
    "ofrobblescum",
    NULL
  };

  fprintf(stderr, "\n*** Dumping MultiRegex With No Contents ***\n\n");
  regex_table.dump();

  fprintf(stderr, "\n*** Adding Three Patterns ***\n\n");
  regex_table.addPattern("^frobble$", 1.0);
  regex_table.addPattern("^.*frob.*$", 0.0);
  regex_table.addPattern("frobbles", 2.0);
  regex_table.dump();

  for (s = strings; *s != NULL; s++) {
    bool is_match;
    MultiRegexCell *cell;

    is_match = regex_table.match(*s, &cell);

    if (is_match)
      fprintf(stderr, "match(%s) = <priority %f, pattern '%s'>\n", *s, cell->getPriority(), cell->getPattern());
    else
      fprintf(stderr, "match(%s) = <NO MATCH>\n", *s);
  }
#endif
}
