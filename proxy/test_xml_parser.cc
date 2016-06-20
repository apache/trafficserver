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

#include <stdio.h>
#include "InkXml.h"
#include "ts/ink_assert.h"

int
main()
{
  InkXmlAttr *a1   = new InkXmlAttr("Name", "Matt");
  InkXmlAttr *a2   = new InkXmlAttr("Title", "Engineer");
  InkXmlAttr *a3   = new InkXmlAttr("Company", "Inktomi");
  InkXmlObject *o1 = new InkXmlObject("Employee");

  ink_release_assert(strcmp(o1->object_name(), "Employee") == 0);

  o1->add_attr(a1);
  ink_release_assert(o1->first() == a1);

  o1->add_attr(a2);
  ink_release_assert(o1->next(o1->first()) == a2);

  o1->add_attr(a3);
  ink_release_assert(o1->next(o1->next(o1->first())) == a3);

  o1->add_tag("Email", "matt@inktomi");
  ink_release_assert(strcmp(o1->tag_value("Email"), "matt@inktomi") == 0);

  InkXmlConfigFile f1("logs.config");
  f1.add_object(o1);
  f1.display();

  return 0;
}
