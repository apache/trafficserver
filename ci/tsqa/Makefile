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

.PHONY: test list clean update bootstrap

VIRTUALENV_DIR = virtualenv

# Run all tests.
test: $(VIRTUALENV_DIR)
	@. $(VIRTUALENV_DIR)/bin/activate && $(VIRTUALENV_DIR)/bin/nosetests --with-xunit -sv --logging-level=INFO

# Scan and list the tests.
list: $(VIRTUALENV_DIR)
	@. $(VIRTUALENV_DIR)/bin/activate && $(VIRTUALENV_DIR)/bin/nosetests -sv --collect-only

# Construct the virtualenv.
$(VIRTUALENV_DIR): $(VIRTUALENV_DIR)/.done

$(VIRTUALENV_DIR)/.done:
	@if [ ! -d $(VIRTUALENV_DIR) ]; then\
		virtualenv $(VIRTUALENV_DIR);\
	fi
	$(MAKE) update
	@echo "Virtualenv ready!"

# Update the virtualenv with the latest TSQA package and dependencies.
update:
	@rm -f $(VIRTUALENV_DIR)/.done
	@. $(VIRTUALENV_DIR)/bin/activate && \
		$(VIRTUALENV_DIR)/bin/pip install --upgrade -r requirements.txt && \
		touch $(VIRTUALENV_DIR)/.done

# Install TSQA bootstrap dependencies.
bootstrap:
	@if [ -e /etc/debian_version ]; then \
		apt-get install -y python-pip python-virtualenv python-dev libffi-dev ; \
	fi
	@if [ -e /etc/redhat-release ]; then \
		yum install -y python-pip python-virtualenv python-devel libcffi-devel ; \
	fi

clean:
	rm -rf virtualenv
