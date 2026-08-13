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

from uranium_testkit.services import ATS, Curl


def test_traffic_server_starts(ats: ATS, curl: Curl) -> None:
    """Verify that Traffic Server starts with its default test configuration."""

    ats.start()

    result = curl.get(f"http://127.0.0.1:{ats.http_port}")

    assert result.returncode == 0, result.output
