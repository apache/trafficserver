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

#include "tscore/TestBox.h"
#include "I_EventSystem.h"
#include "MIME.h"

REGRESSION_TEST(MIME)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  MIMEField *field;
  MIMEHdr hdr;
  hdr.create(NULL);

  hdr.field_create("Test1", 5);
  hdr.field_create("Test2", 5);
  hdr.field_create("Test3", 5);
  hdr.field_create("Test4", 5);
  field = hdr.field_create("Test5", 5);

  box.check(hdr.m_mime->m_first_fblock.contains(field), "The field block doesn't contain the field but it should");
  box.check(!hdr.m_mime->m_first_fblock.contains(field + (1L << 32)), "The field block contains the field but it shouldn't");

  int slot_num = mime_hdr_field_slotnum(hdr.m_mime, field);
  box.check(slot_num == 4, "Slot number is %d but should be 4", slot_num);

  slot_num = mime_hdr_field_slotnum(hdr.m_mime, field + (1L << 32));
  box.check(slot_num == -1, "Slot number is %d but should be -1", slot_num);

  hdr.destroy();
}

REGRESSION_TEST(MIME_PARSERS)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  const char *end;
  int value;
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  std::vector<std::pair<const char *, int>> tests = {{"0", 0},
                                                     {"1234", 1234},
                                                     {"-1234", -1234},
                                                     {"2147483647", 2147483647},
                                                     {"-2147483648", 2147483648},
                                                     {"2147483648", INT_MAX},
                                                     {"-2147483649", INT_MIN},
                                                     {"2147483647", INT_MAX},
                                                     {"-2147483648", INT_MIN},
                                                     {"999999999999", INT_MAX},
                                                     {"-999999999999", INT_MIN}};

  for (const auto &it : tests) {
    auto [buf, val] = it;

    end = buf + strlen(buf);
    box.check(mime_parse_int(buf, end) == val, "Failed mime_parse_int");
    box.check(mime_parse_integer(buf, end, &value), "Failed mime_parse_integer call");
    box.check(value == val, "Failed mime_parse_integer value");
  }

  // Also check the date parser, which relies heavily on the mime_parse_integer() function
  const char *date1 = "Sun, 05 Dec 1999 08:49:37 GMT";
  const char *date2 = "Sunday, 05-Dec-1999 08:49:37 GMT";

  int d1 = mime_parse_date(date1, date1 + strlen(date1));
  int d2 = mime_parse_date(date2, date2 + strlen(date2));

  box.check(d1 == d2, "Failed mime_parse_date");

  printf("Date1: %d\n", d1);
  printf("Date2: %d\n", d2);
}

int
main(int argc, const char **argv)
{
  Thread *main_thread = new EThread();
  main_thread->set_specific();
  mime_init();

  return RegressionTest::main(argc, argv, REGRESSION_TEST_QUICK);
}
