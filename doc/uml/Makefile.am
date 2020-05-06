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

.PHONY: jar-check

if BUILD_DOCS
images := $(patsubst %.uml,images/%.svg,$(wildcard *.uml))
images += $(patsubst %.plantuml,images/%.svg,$(wildcard *.plantuml))
PLANTUML_JAR := $(shell ../ext/plantuml_fetch.sh | tail -1)

all-am: jar-check $(images)
endif

# These are handle when the make target comes from the base doc makefile.
# In cases the .SVG files are built.

html-local: all-am

dirhtml: all-am

singlehtml: all-am

epub: all-am

latex: all-am

man: all-am

images/%.svg : %.uml
	$(JAVA) -jar $(PLANTUML_JAR) -o images -tsvg $<

images/%.svg : %.plantuml
	$(JAVA) -jar $(PLANTUML_JAR) -o images -tsvg $<

clean-local:
	rm -f images/*.svg

jar-check:
	@if [ -z "$(PLANTUML_JAR)" ] ; then \
		echo plantuml jar file variable not set.; \
		exit 1; \
	elif [ ! -r "$(PLANTUML_JAR)" ] ; then \
		echo plantuml jar \'${PLANTUML_JAR}\' not readable.; \
		exit 1; \
	elif [ -d "${PLANTUML_JAR}" ] ; then \
		echo plantuml jar not downloaded to \'${PLANTUML_JAR}\'.; \
		exit 1; \
	fi
