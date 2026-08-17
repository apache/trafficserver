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

from tools.uranium.services import ATS, ATSFactory, Curl


class ParentRetryScenario:
    """Verify ATS accepts the parent retry configuration."""

    def __init__(self, ats_factory: ATSFactory, curl: Curl) -> None:
        self._curl = curl
        self._ats = self.configure_ats(ats_factory)

    @staticmethod
    def configure_ats(ats_factory: ATSFactory) -> ATS:
        """Configure a deliberately unavailable retryable parent."""

        ats = ats_factory.create("ts-child")
        ats.parent_config.add_line(
            'dest_domain=. method=get parent="localhost:8081" '
            'parent_retry=unavailable_server_retry unavailable_server_retry_responses="502,503"')
        return ats

    def run(self) -> None:
        """Start ATS and exercise the parsed parent configuration."""

        self._ats.start()
        result = self._curl.get(self._ats, options=f"--verbose")
        assert result.returncode == 0, result.output


def test_parent_retry(ats_factory: ATSFactory, curl: Curl) -> None:
    """The unavailable-server retry parent setting is accepted at runtime."""

    ParentRetryScenario(ats_factory, curl).run()
