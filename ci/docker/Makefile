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
	@echo 'fedora26         create ATS docker image for Fedora 27'
	@echo 'fedora27         create ATS docker image for Fedora 27'
	@echo 'fedora28         create ATS docker image for Fedora 28'
	@echo 'centos6          create ATS docker image for Centos 6'
	@echo 'centos7          create ATS docker image for Centos 7'
	@echo 'all              build all images'

all: fedora centos

# Fedora Docker images
fedora: fedora26 fedora27 fedora28

fedora26:
	docker build -t ats_$@ --build-arg OS_VERSION=26 --build-arg OS_TYPE=fedora yum/

fedora27:
	docker build -t ats_$@ --build-arg OS_VERSION=27 --build-arg OS_TYPE=fedora yum/

fedora28:
	docker build -t ats_$@ --build-arg OS_VERSION=28 --build-arg OS_TYPE=fedora yum/

# Centos Docker images
centos: centos6 centos7

centos6:
	docker build -t ats_$@ --build-arg OS_VERSION=6 --build-arg OS_TYPE=centos yum/

centos7:
	docker build -t ats_$@ --build-arg OS_VERSION=7 --build-arg OS_TYPE=centos yum/
