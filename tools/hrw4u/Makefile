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
BUILD_DIR:=build
DIST_DIR:=dist
PKG_DIR:=$(BUILD_DIR)/hrw4u
VENV_NAME:=hrw4u
ANTLR=antlr
PYTHON_VERSION=3.11.8

# Source files
SCRIPT_FILE=scripts/hrw4u

SRC_FILES=src/visitor.py \
          src/symbols.py \
          src/validation.py \
          src/errors.py \
          src/states.py \
          src/types.py
GRAMMAR_FILE=grammar/hrw4u.g4

ANTLR_FILES=$(PKG_DIR)/hrw4uLexer.py \
            $(PKG_DIR)/hrw4uParser.py \
            $(PKG_DIR)/hrw4uVisitor.py \
            $(PKG_DIR)/hrw4u.interp \
            $(PKG_DIR)/hrw4u.tokens \
            $(PKG_DIR)/hrw4uLexer.tokens

COPIED_SRC_FILES=$(patsubst src/%, $(PKG_DIR)/%, $(SRC_FILES))

.PHONY: all gen test clean build package env setup-deps activate update

all: gen

copy-src: $(COPIED_SRC_FILES)

$(PKG_DIR)/%: src/%
	@mkdir -p $(PKG_DIR)
	cp $< $@

gen: $(ANTLR_FILES) copy-src

$(ANTLR_FILES): $(GRAMMAR_FILE)
	@mkdir -p $(PKG_DIR)
	cd grammar && $(ANTLR) -Dlanguage=Python3 -visitor -no-listener -o ../$(PKG_DIR) hrw4u.g4
	cp $(SRC_FILES) $(PKG_DIR)
	cp $(SCRIPT_FILE) $(PKG_DIR)/__main__.py
	touch $(PKG_DIR)/__init__.py

test:
	pytest --tb=short tests

build: gen
	pyinstaller --onefile --name hrw4u --strip scripts/hrw4u

package: gen
	@echo "==> Building pip package for $(hrw4u)..."
	python3 -m build --wheel --outdir $(DIST_DIR)

clean:
	rm -rf build dist __pycache__ *.spec *.egg-info
	find tests -name '__pycache__' -type d -exec rm -r {} +

env:
	pyenv install -s $(PYTHON_VERSION)
	pyenv virtualenv -f $(PYTHON_VERSION) $(VENV_NAME)
	pyenv local $(VENV_NAME)

setup-deps: env
	$(PYTHON) -m pip install -r requirements.txt

activate:
	@echo "Run: pyenv activate $(VENV_NAME)"
