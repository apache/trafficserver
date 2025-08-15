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

# Separate package dirs for forward (hrw4u) and inverse (u4wrh)
PKG_DIR_HRW4U:=$(BUILD_DIR)/hrw4u
PKG_DIR_U4WRH:=$(BUILD_DIR)/u4wrh

VENV_NAME:=hrw4u
ANTLR=antlr

# Scripts (each becomes the package __main__.py)
SCRIPT_HRW4U=scripts/hrw4u
SCRIPT_U4WRH=scripts/u4wrh

# Shared source files for hrw4u (forward)
SRC_FILES_HRW4U=src/visitor.py \
	src/symbols.py \
	src/tables.py \
	src/validation.py \
	src/errors.py \
	src/states.py \
	src/types.py \
	src/debugging.py \
	src/common.py \
	src/collector.py \
	src/script_common.py \

# Inverse helper sources for u4wrh (actual files you have)
INVERSE_SRC_FILES_U4WRH=src/hrw_visitor.py \
	src/hrw_symbols.py \
	src/common.py \
	src/errors.py \
	src/validation.py \
	src/states.py \
	src/types.py \
	src/debugging.py \
	src/tables.py \
	src/script_common.py

# Grammars (forward + inverse)
GRAMMAR_FWD=grammar/hrw4u.g4
GRAMMAR_INV=grammar/u4wrh.g4

# ANTLR outputs (forward -> build/hrw4u, inverse -> build/u4wrh)
ANTLR_FILES_FWD=$(PKG_DIR_HRW4U)/hrw4uLexer.py \
	$(PKG_DIR_HRW4U)/hrw4uParser.py \
	$(PKG_DIR_HRW4U)/hrw4uVisitor.py \
	$(PKG_DIR_HRW4U)/hrw4u.interp \
	$(PKG_DIR_HRW4U)/hrw4u.tokens \
	$(PKG_DIR_HRW4U)/hrw4uLexer.tokens

ANTLR_FILES_INV=$(PKG_DIR_U4WRH)/u4wrhLexer.py \
	$(PKG_DIR_U4WRH)/u4wrhParser.py \
	$(PKG_DIR_U4WRH)/u4wrhVisitor.py \
	$(PKG_DIR_U4WRH)/u4wrh.interp \
	$(PKG_DIR_U4WRH)/u4wrh.tokens \
	$(PKG_DIR_U4WRH)/u4wrhLexer.tokens

# Copy maps
COPIED_SRC_FILES_HRW4U=$(patsubst src/%, $(PKG_DIR_HRW4U)/%, $(SRC_FILES_HRW4U))
COPIED_INV_FILES_U4WRH=$(patsubst src/%, $(PKG_DIR_U4WRH)/%, $(INVERSE_SRC_FILES_U4WRH))

# Main entry point files
MAIN_HRW4U=$(PKG_DIR_HRW4U)/__main__.py
MAIN_U4WRH=$(PKG_DIR_U4WRH)/__main__.py
INIT_HRW4U=$(PKG_DIR_HRW4U)/__init__.py
INIT_U4WRH=$(PKG_DIR_U4WRH)/__init__.py

.PHONY: all gen gen-fwd gen-inv copy-src test clean build package env setup-deps activate update

all: gen

# Orchestrate generation then copy sources and drop __main__.py in each package
gen: gen-fwd gen-inv copy-src $(MAIN_HRW4U) $(MAIN_U4WRH) $(INIT_HRW4U) $(INIT_U4WRH)

# Create __main__.py files only when scripts change
$(MAIN_HRW4U): $(SCRIPT_HRW4U)
	@mkdir -p $(PKG_DIR_HRW4U)
	cp $< $@

$(MAIN_U4WRH): $(SCRIPT_U4WRH)
	@mkdir -p $(PKG_DIR_U4WRH)
	cp $< $@

# Create __init__.py files only when package directories are created
$(INIT_HRW4U): | $(PKG_DIR_HRW4U)
	touch $@

$(INIT_U4WRH): | $(PKG_DIR_U4WRH)
	touch $@

# Generate forward parser/lexer into build/hrw4u
gen-fwd: $(ANTLR_FILES_FWD)

$(ANTLR_FILES_FWD): $(GRAMMAR_FWD)
	@mkdir -p $(PKG_DIR_HRW4U)
	cd grammar && $(ANTLR) -Dlanguage=Python3 -visitor -no-listener -o ../$(PKG_DIR_HRW4U) hrw4u.g4

# Generate inverse parser/lexer into build/u4wrh
gen-inv: $(ANTLR_FILES_INV)

$(ANTLR_FILES_INV): $(GRAMMAR_INV)
	@mkdir -p $(PKG_DIR_U4WRH)
	cd grammar && $(ANTLR) -Dlanguage=Python3 -visitor -no-listener -o ../$(PKG_DIR_U4WRH) u4wrh.g4

# Copy Python sources into their respective package dirs
copy-src: $(COPIED_SRC_FILES_HRW4U) $(COPIED_INV_FILES_U4WRH)

# Pattern rules for copying
$(PKG_DIR_HRW4U)/%: src/%
	@mkdir -p $(PKG_DIR_HRW4U)
	cp $< $@

# Inverse files currently live directly under src/, map them into build/u4wrh/
$(PKG_DIR_U4WRH)/%: src/%
	@mkdir -p $(PKG_DIR_U4WRH)
	cp $< $@

test:
	pytest --tb=short tests

# Build standalone binaries (optional)
build: gen
	pyinstaller --onefile --name hrw4u --strip $(SCRIPT_HRW4U)
	pyinstaller --onefile --name u4wrh --strip $(SCRIPT_U4WRH)

# Wheel packaging (adjust pyproject to include both packages if desired)
package: gen
	@echo "==> Building pip package(s)..."
	python3 -m build --wheel --outdir $(DIST_DIR)

clean:
	rm -rf build dist __pycache__ *.spec *.egg-info
	find tests -name '__pycache__' -type d -exec rm -r {} +

setup-deps: env
	$(PYTHON) -m pip install -r requirements.txt

activate:
	@echo "Run: pyenv activate $(VENV_NAME)"
