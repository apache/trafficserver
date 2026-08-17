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
"""Public service API for procedural Uranium tests.

Implementations live in focused modules; this facade keeps scenario imports stable.
"""

from .ats import ATS, ATSFactory, ConfigFile, RecordsConfig
from .context import CommandResult, ProceduralContext
from .curl import Curl
from .dns import DNSServer
from .httpbin import HttpBinServer
from .origin import OriginServer
from .process_service import ProcessService
from .service_factory import ServiceFactory
from .service_utils import assert_matches_gold, send_tcp, wait_for_file_lines, wait_for_metric
from .verifier import VerifierServer

__all__ = [
    "ATS",
    "ATSFactory",
    "CommandResult",
    "ConfigFile",
    "Curl",
    "DNSServer",
    "HttpBinServer",
    "OriginServer",
    "ProceduralContext",
    "ProcessService",
    "RecordsConfig",
    "ServiceFactory",
    "VerifierServer",
    "assert_matches_gold",
    "send_tcp",
    "wait_for_file_lines",
    "wait_for_metric",
]
