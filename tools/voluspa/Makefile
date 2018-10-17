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

APP          := voluspa
RELEASE      ?= 0
VERSION_BASE := 24A
BUILD_NUMBER ?= devel
VERSION      := $(VERSION_BASE)$(BUILD_NUMBER)
GIT_VERSION  := $(shell git describe --always --long)
PKG          := github.com/apache/trafficserver/tools/voluspa
LDFLAGS      := -ldflags "-w -X $(PKG).Version=$(VERSION) -X $(PKG).GitVersion=$(GIT_VERSION)"
DIST         := dist
ARCHIVE      := $(APP)-$(VERSION)
CURRENT_SCHEMA := schema_v1.json
DEST         ?= .
GO           ?= go

COMMON_GO_FILES := *.go */*.go internal/util/*/*go */*/*.go

GO_PKG       := $(shell go list ./... | egrep -v 'vendor|cmd|other')
GO_PKG_DIRS  := $(subst $(shell go list -e .),.,$(shell go list ./... | egrep -v 'vendor|other'))

CMD_SOURCES     := $(shell find cmd -name main.go)
DEV_TARGETS     := $(patsubst cmd/%/main.go,%,$(CMD_SOURCES))
DIST_TARGETS    := voluspa
LINUX_TARGETS   := $(patsubst %,dist/linux-x86_64/%,$(DIST_TARGETS))
DARWIN_TARGETS  := $(patsubst %,dist/darwin-x86_64/%,$(DIST_TARGETS))

all: default

generate:
	go generate ./...

default: $(DEV_TARGETS)

sanity_tests: $(APP)
	@make -C tests test

test: $(APP) sanity_tests
	$(GO) test -cover $(GO_PKG_DIRS)

lint:
	-$(GO) vet ./... 2>&1
	-golint $(GO_PKG) | egrep -v 'vendor|unexport'

metalint:
	-gometalinter --config .gometalint.conf --deadline=60s -e 'vendor' ./...

junit:
	@mkdir -p .out/test-results/ # where rio junit support looks by default
	$(GO) test -v $(GO_PKG_DIRS) 2>&1 | go-junit-report > .out/test-results/junit-report.xml

cover:
	# go get github.com/wadey/gocovmerge github.com/axw/gocov/... github.com/matm/gocov-html
	# based on https://github.com/golang/go/issues/6909#issuecomment-233493644
	@echo Generating an overall coverage report
	@rm -f *.coverprofile
	go list -f '{{if or (len .TestGoFiles) (len .XTestGoFiles)}}go test -test.timeout=120s -covermode=count -coverprofile={{.Name}}_{{len .Imports}}_{{len .Deps}}.coverprofile -coverpkg ./... {{.ImportPath}}{{end}}' $(GO_PKG) | sh -x
	gocovmerge `ls *.coverprofile` > cover.out
	@rm -f *.coverprofile
	@mkdir -p .out/coverage/
	gocov convert cover.out | gocov-html > .out/coverage/merged-coverage.html
	@[[ $$(uname) == 'Darwin' ]] && open .out/coverage/merged-coverage.html || true

voluspa-covered: cmd/voluspa/*.go $(COMMON_GO_FILES)
	@# build a test version of the voluspa command with coverage enabled
	@# e.g.: ./voluspa-covered -test.coverprofile cover.out .../*.conf
	go test -o $(DEST)/$@ -c -tags testrunmain -coverpkg ./... $(PKG)/cmd/voluspa

gocov: .out/coverage/coverage.html

.out/coverage/coverage.json: voluspa-covered
	@mkdir -p .out/coverage/
	gocov test $(GO_PKG) > .out/coverage/coverage.json
	@make -C tests coverage
	gocov convert tests/regress/profile*.out > tests/regress/system.json
	gocov convert tests/other/profile*.out > tests/other/system.json
	cat .out/coverage/coverage.json tests/other/system.json tests/regress/system.json | gocov-merge > .out/coverage/coverage.json

.out/coverage/coverage.html: .out/coverage/coverage.json
	gocov-html < .out/coverage/coverage.json > .out/coverage/coverage.html

dist: artifactory_targets $(DIST)/artifactory/version.txt $(DIST)/artifactory/versions.sh

clean:
	@rm -rf $(DIST) out out_remap RPMS .dist .out
	@rm -f $(DEV_TARGETS) $(TOOLS_TARGETS) $(OTHER_TARGETS) voluspa-covered
	@make -C tests clean

%: cmd/%/*.go $(COMMON_GO_FILES)
	$(GO) build -o $(DEST)/$@ $(LDFLAGS) cmd/$@/*.go

$(DIST)/linux-x86_64/%:
	GOOS=linux GOARCH=amd64 $(GO) build -o $@ $(LDFLAGS) cmd/$(notdir $@)/*.go

$(DIST)/darwin-x86_64/%:
	GOOS=darwin GOARCH=amd64 $(GO) build -o $@ $(LDFLAGS) cmd/$(notdir $@)/*.go

$(DIST)/freebsd-x86_64/%:
	GOOS=freebsd GOARCH=amd64 $(GO) build -o $@ $(LDFLAGS) cmd/$(notdir $@)/*.go

$(DIST)/artifactory:
	@mkdir -p $@

$(DIST)/artifactory/$(APP)-$(VERSION)-darwin-x86_64: $(DIST)/artifactory dist/darwin-x86_64/voluspa
	cp dist/darwin-x86_64/voluspa $@

$(DIST)/artifactory/$(APP)-$(VERSION)-linux-x86_64: $(DIST)/artifactory dist/linux-x86_64/voluspa
	cp dist/linux-x86_64/voluspa $@

$(DIST)/artifactory/$(CURRENT_SCHEMA)-$(VERSION): $(DIST)/artifactory $(CURRENT_SCHEMA)
	cp $(CURRENT_SCHEMA) $@

$(DIST)/artifactory/version.txt:
	@mkdir -p dist/artifactory
	echo $(VERSION) > $@

artifactory_targets: $(DIST)/artifactory/$(APP)-$(VERSION)-darwin-x86_64 $(DIST)/artifactory/$(APP)-$(VERSION)-linux-x86_64 $(DIST)/artifactory/$(CURRENT_SCHEMA)-$(VERSION)

$(DIST)/artifactory/versions.sh: $(DIST)/artifactory/$(APP)-$(VERSION)-darwin-x86_64 $(DIST)/artifactory/$(APP)-$(VERSION)-linux-x86_64
	echo VOLUSPA_VERSION=$(VERSION) > $@
	echo >> $@
	sha1sum $(DIST)/artifactory/$(APP)-$(VERSION)-linux-x86_64 | awk 'BEGIN { FS = " " } ; { printf "linux_SHA1=%s\n", $$1 }' >> $@
	sha1sum $(DIST)/artifactory/$(APP)-$(VERSION)-darwin-x86_64 | awk 'BEGIN { FS = " " } ; { printf "darwin_SHA1=%s\n", $$1 }' >> $@
	sha1sum $(DIST)/artifactory/$(CURRENT_SCHEMA)-$(VERSION) | awk 'BEGIN { FS = " " } ; { printf "schema_SHA1=%s\n", $$1 }' >> $@

update-deps:
	-grep ImportPath Godeps/Godeps.json | cut -f 4 -d \" | grep -v voluspa | xargs go get -u
	@rm -rf Godeps vendor/*
	godep save $(GO_PKG_DIRS)

.PHONY: all clean test linux dist sanity_tests
