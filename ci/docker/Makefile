#  Makefile to make docker images
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

.DEFAULT_GOAL := help

help:
	@echo 'fedora31         create ATS docker image for Fedora 31'
	@echo 'fedora32         create ATS docker image for Fedora 32'
	@echo 'fedora33         create ATS docker image for Fedora 33'
	@echo 'fedora34         create ATS docker image for Fedora 34'
	@echo 'centos7          create ATS docker image for Centos 7'
	@echo 'centos8          create ATS docker image for Centos 8'
	@echo 'all              build all images'

all: fedora centos

# Fedora Docker images
fedora: fedora31 fedora32 fedora33 fedora34

fedora31:
	docker build -t ats_$@ --build- OS_VERSION=31 --build-arg OS_TYPE=fedora yum/

fedora32:
	docker build -t ats_$@ --build-arg OS_VERSION=32 --build-arg OS_TYPE=fedora yum/

fedora33:
	docker build -t ats_$@ --build-arg OS_VERSION=33 --build-arg OS_TYPE=fedora yum/

fedora34:
	docker build -t ats_$@ --build-arg OS_VERSION=34 --build-arg OS_TYPE=fedora yum/

# Centos Docker images
centos: centos7 centos8

centos7:
	docker build -t ats_$@ --build-arg OS_VERSION=7 --build-arg OS_TYPE=centos yum/

centos8:
	docker build -t ats_$@ --build-arg OS_VERSION=8 --build-arg OS_TYPE=centos yum/
