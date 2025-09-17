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

# Separate package dirs for forward (hrw4u), inverse (u4wrh), and LSP (hrw4u_lsp)
PKG_DIR_HRW4U:=$(BUILD_DIR)/hrw4u
PKG_DIR_U4WRH:=$(BUILD_DIR)/u4wrh
PKG_DIR_LSP:=$(BUILD_DIR)/hrw4u_lsp

VENV_NAME:=hrw4u
ANTLR=antlr

# Scripts (each becomes the package __main__.py)
SCRIPT_HRW4U=scripts/hrw4u
SCRIPT_U4WRH=scripts/u4wrh
SCRIPT_LSP=scripts/hrw4u-lsp
SCRIPT_KG=scripts/hrw4u-kg

# Shared source files (will go in hrw4u package)
SHARED_FILES=src/common.py \
	src/debugging.py \
	src/errors.py \
	src/states.py \
	src/tables.py \
	src/types.py \
	src/validation.py

# Shared utility modules (used by both hrw4u and u4wrh packages)
UTILS_FILES=src/symbols_base.py \
	src/visitor_base.py \
	src/generators.py \
	src/interning.py

# hrw4u-specific source files (no LSP modules needed)
SRC_FILES_HRW4U=src/visitor.py \
	src/symbols.py \
	src/suggestions.py \
	src/kg_visitor.py

ALL_HRW4U_FILES=$(SHARED_FILES) $(UTILS_FILES) $(SRC_FILES_HRW4U)

# Package-specific source files for u4wrh (inverse) - only u4wrh-specific files
SRC_FILES_U4WRH_ONLY=src/hrw_visitor.py \
	src/hrw_symbols.py
ALL_U4WRH_FILES=$(SRC_FILES_U4WRH_ONLY)

# hrw4u_lsp uses ALL LSP files - no shared LSP modules needed in hrw4u
SRC_FILES_LSP_ALL=src/lsp/__init__.py \
	src/lsp/documentation.py \
	src/lsp/completions.py \
	src/lsp/strings.py \
	src/lsp/hover.py \
	src/lsp/types.py
ALL_LSP_FILES=$(SRC_FILES_LSP_ALL)

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


# Copy maps - create proper directory structure for all packages
COPIED_FILES_HRW4U=$(patsubst src/%, $(PKG_DIR_HRW4U)/%, $(ALL_HRW4U_FILES))
COPIED_FILES_U4WRH=$(patsubst src/%, $(PKG_DIR_U4WRH)/%, $(ALL_U4WRH_FILES))
COPIED_FILES_LSP=$(patsubst src/%, $(PKG_DIR_LSP)/%, $(ALL_LSP_FILES))

# Main entry point files
MAIN_HRW4U=$(PKG_DIR_HRW4U)/__main__.py
MAIN_U4WRH=$(PKG_DIR_U4WRH)/__main__.py
MAIN_LSP=$(PKG_DIR_LSP)/__main__.py
INIT_HRW4U=$(PKG_DIR_HRW4U)/__init__.py
INIT_U4WRH=$(PKG_DIR_U4WRH)/__init__.py
INIT_LSP=$(PKG_DIR_LSP)/__init__.py

.PHONY: all gen gen-fwd gen-inv copy-src test clean build package env setup-deps activate update

all: gen

# Orchestrate generation then copy sources and drop __main__.py in each package
gen: gen-fwd gen-inv copy-src $(MAIN_HRW4U) $(MAIN_U4WRH) $(MAIN_LSP) $(INIT_HRW4U) $(INIT_U4WRH) $(INIT_LSP)

# Create __main__.py files only when scripts change
$(MAIN_HRW4U): $(SCRIPT_HRW4U)
	@mkdir -p $(PKG_DIR_HRW4U)
	cp $< $@

$(MAIN_U4WRH): $(SCRIPT_U4WRH)
	@mkdir -p $(PKG_DIR_U4WRH)
	cp $< $@

$(MAIN_LSP): $(SCRIPT_LSP)
	@mkdir -p $(PKG_DIR_LSP)
	cp $< $@

# Create __init__.py files only when package directories are created
$(INIT_HRW4U): | $(PKG_DIR_HRW4U)
	touch $@

$(INIT_U4WRH): | $(PKG_DIR_U4WRH)
	touch $@

$(INIT_LSP): | $(PKG_DIR_LSP)
	touch $@

# Generate forward parser/lexer into build/hrw4u and build/hrw4u-lsp
gen-fwd: $(ANTLR_FILES_FWD)

$(ANTLR_FILES_FWD): $(GRAMMAR_FWD)
	@mkdir -p $(PKG_DIR_HRW4U)
	cd grammar && $(ANTLR) -Dlanguage=Python3 -visitor -no-listener -o ../$(PKG_DIR_HRW4U) hrw4u.g4

# LSP no longer generates its own ANTLR files - it imports from hrw4u

# Generate inverse parser/lexer into build/u4wrh
gen-inv: $(ANTLR_FILES_INV)

$(ANTLR_FILES_INV): $(GRAMMAR_INV)
	@mkdir -p $(PKG_DIR_U4WRH)
	cd grammar && $(ANTLR) -Dlanguage=Python3 -visitor -no-listener -o ../$(PKG_DIR_U4WRH) u4wrh.g4

# Copy Python sources into their respective package dirs
copy-src: $(COPIED_FILES_HRW4U) $(COPIED_FILES_U4WRH) $(COPIED_FILES_LSP)

# Pattern rules for copying files - create subdirectories as needed
$(PKG_DIR_HRW4U)/%: src/%
	@mkdir -p $(dir $@)
	cp $< $@

$(PKG_DIR_U4WRH)/%: src/%
	@mkdir -p $(dir $@)
	cp $< $@

$(PKG_DIR_LSP)/%: src/%
	@mkdir -p $(dir $@)
	cp $< $@

test:
	pytest --tb=short tests

# Build standalone binaries (optional)
build: gen
	pyinstaller --onefile --name hrw4u --strip $(SCRIPT_HRW4U)
	pyinstaller --onefile --name u4wrh --strip $(SCRIPT_U4WRH)
	pyinstaller --onefile --name hrw4u-lsp --strip $(SCRIPT_LSP)
	pyinstaller --onefile --name hrw4u-kg --strip $(SCRIPT_KG)

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
