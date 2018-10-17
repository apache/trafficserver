#
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
#

test: regress other custom

clean:
	@rm -f */system.json
	@rm -f */profile_*.out

COVERAGE_OPTIONS=-test.run ^TestRunMain$$ -test.coverprofile=system.out

coverage:
	cd regress && OPTIONS="$(COVERAGE_OPTIONS)" VOLUSPA=voluspa-covered ./test.sh
	cd other && OPTIONS="$(COVERAGE_OPTIONS)" VOLUSPA=voluspa-covered ./test.sh
	if [ -x custom ]; then \
		cd custom && OPTIONS="$(COVERAGE_OPTIONS)" VOLUSPA=voluspa-covered ./test.sh; \
	fi;

regress:
	cd regress && ./test.sh

other:
	cd other && ./test.sh

custom:
	if [ -x custom ]; then \
		cd custom && ./test.sh; \
	fi;

regen_baseline:
	cd regress && ./regen_baseline.sh
	cd other && ./regen_baseline.sh
	if [ -x custom ]; then \
		cd custom && ./regen_baseline.sh; \
	fi;

.PHONY: regress other custom
.NOTPARALLEL:
