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

.PHONY: test clean update

VIRTUALENV_DIR = virtualenv

# Run all tests.
test: $(VIRTUALENV_DIR)
	@source $(VIRTUALENV_DIR)/bin/activate && $(VIRTUALENV_DIR)/bin/nosetests -sv --logging-level=INFO

# Scan and list the tests.
list: $(VIRTUALENV_DIR)
	@source $(VIRTUALENV_DIR)/bin/activate && $(VIRTUALENV_DIR)/bin/nosetests -v --collect-only

# Construct the virtualenv.
$(VIRTUALENV_DIR):
	@if [ ! -d $(VIRTUALENV_DIR) ]; then\
		virtualenv $(VIRTUALENV_DIR);\
	fi
	@# Update the virtualenv with your dep libraries
	source $(VIRTUALENV_DIR)/bin/activate && $(VIRTUALENV_DIR)/bin/pip install -r requirements.txt
	@echo "Virtualenv ready!"

# Update the virtualenv with the latest TSQA package and dependencies.
update: $(VIRTUALENV_DIR)
	source $(VIRTUALENV_DIR)/bin/activate && $(VIRTUALENV_DIR)/bin/pip install --upgrade -r requirements.txt

clean:
	rm -rf virtualenv
