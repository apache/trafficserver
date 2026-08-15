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

import pytest

from tools.uranium.services import ATS, ATSFactory


class MaxRecordsScenario:
    """Verify traffic_server's maxRecords argument parsing and lower bound."""

    def __init__(self, ats_factory: ATSFactory, value: str, expected: str) -> None:
        self._ats_factory = ats_factory
        self._value = value
        self._expected = expected

    def configure_ats(self) -> ATS:
        """Pass the selected maxRecords value to traffic_server."""

        return self._ats_factory.create("ts", server_args=["--maxRecords", self._value])

    def run(self) -> None:
        """Start ATS and validate its parsing diagnostic."""

        ats = self.configure_ats()
        ats.start()
        output = ats.process_output + ats.traffic_out.read_text(errors="replace")
        assert self._expected in output


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        ("1000", "Passed maxRecords value=1000 is lower than the default value 2048. Default will be used."),
        ("abc", "Invalid 0 value for maxRecords. Default  2048 will be used."),
        ("5000", "NOTE: records parsing completed"),
    ],
)
def test_ts_max_records_param(ats_factory: ATSFactory, value: str, expected: str) -> None:
    """maxRecords accepts large values and safely handles small or invalid ones."""

    MaxRecordsScenario(ats_factory, value, expected).run()
