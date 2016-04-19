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

-include /etc/os-release
DISTRIBUTION := $(NAME)$(VERSION_ID)

# Search for a program in $PATH (direct from the GNU make manual.
pathsearch = $(firstword $(wildcard $(addsuffix /$(1),$(subst :, ,$(PATH)))))

APT := $(call pathsearch,apt-get)
DNF := $(call pathsearch,dnf)
YUM := $(call pathsearch,yum)

ifndef DISTRIBUTION
ifneq ($(APT),)
DISTRIBUTION := DebianGeneric
else ifneq ($(DNF),)
DISTRIBUTION := RedHatGeneric
else ifneq ($(YUM),)
DISTRIBUTION := RedHatGeneric
endif
endif

PACKAGES_Fedora23 := python-pip python-virtualenv python-devel libffi-devel redhat-rpm-config
PACKAGES_RedHatGeneric := python-pip python-virtualenv python-devel libcffi-devel
PACKAGES_DebianGeneric := python-pip python-virtualenv python-dev libffi-dev

PACKAGES := $(PACKAGES_$(DISTRIBUTION))

VIRTUALENV_DIR := virtualenv

NOSETESTS := ./nosetests

# Run all tests.
.PHONY: test
test: $(VIRTUALENV_DIR)
	@$(NOSETESTS) --with-xunit --nocapture --verbose --logging-level=INFO

# Scan and list the tests.
.PHONY: list
list: $(VIRTUALENV_DIR)
	@$(NOSETESTS) -sv --collect-only

# Construct the virtualenv.
.PHONY: $(VIRTUALENV_DIR)
$(VIRTUALENV_DIR): $(VIRTUALENV_DIR)/.done

$(VIRTUALENV_DIR)/.done:
	@if [ ! -d $(VIRTUALENV_DIR) ]; then\
		virtualenv $(VIRTUALENV_DIR);\
	fi
	$(MAKE) update
	@echo "Virtualenv ready!"

# Update the virtualenv with the latest TSQA package and dependencies.
.PHONY: update
update:
	@rm -f $(VIRTUALENV_DIR)/.done
	@. $(VIRTUALENV_DIR)/bin/activate && \
		$(VIRTUALENV_DIR)/bin/pip install --upgrade pip && \
		$(VIRTUALENV_DIR)/bin/pip install --upgrade -r requirements.txt && \
		touch $(VIRTUALENV_DIR)/.done

# Install TSQA bootstrap dependencies.
.PHONY: bootstrap
bootstrap:
	@echo package list is $(PACKAGES)
	@if [ -e /etc/debian_version ]; then \
		$(APT) install -y $(PACKAGES) ; \
	fi
	@if [ -e /etc/redhat-release ]; then \
		$(YUM) install -y $(PACKAGES) ; \
	fi

.PHONY: clean
clean:
	rm -rf virtualenv
