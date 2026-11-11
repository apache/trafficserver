#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import http.client
import sys


def main() -> None:
    connection = http.client.HTTPConnection("127.0.0.1", int(sys.argv[1]), timeout=15)
    connection.request("GET", "/continuation/headers", headers={"Host": "cont.h2o.example.com"})
    response = connection.getresponse()
    assert response.status == 200, response.status
    assert response.read() == b"okay"
    for name, pattern in (("one", "0123456789abcdef"), ("two", "fedcba9876543210")):
        value = response.getheader(f"x-continuation-padding-{name}")
        assert value is not None and value[9:] == pattern * 1024, f"incomplete {name} header"
        assert len(value[:9]) == 9 and value[8] == "-"
    connection.close()
    print("Both CONTINUATION-carried headers arrived intact")


if __name__ == "__main__":
    main()
