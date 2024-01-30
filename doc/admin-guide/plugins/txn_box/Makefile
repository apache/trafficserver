# Makefile for Sphinx documentation

.PHONY: help dirhtml singlehtml

srcdir        ?= .
SPHINXBUILD    = python3 -m sphinx
PAPER         ?= letter
BUILDDIR      ?= docbuild
JAVA          ?= java
PLANTUML_JAR  ?= $(HOME)/bin/plantuml.jar
PLANTUML_ARGS ?=
REALPATH       = $(if $(WINDIR), cygpath --absolute --windows, realpath)
TXB_VERSION   ?= $(shell grep 'release =' conf.py | sed -e 's/release = //' | sed -e 's/"//g')

SBUILD = $(SPHINXBUILD) ${PAPEROPT_letter}
PLANTUML = $(JAVA) -jar $(PLANTUML_JAR) $(PLANTUML_ARGS)
IMAGEDIR = $(srcdir)/pix
BUILD_IMAGEDIR = $(BUILDDIR)/html/_images
UMLDIR = $(srcdir)/uml

$(IMAGEDIR)/%.svg : $(UMLDIR)/%.plantuml
	$(PLANTUML) $< -tsvg -o $(shell $(REALPATH) $(IMAGEDIR))

html: uml ext/local-config.py static docbuild/html/src
	$(SBUILD) -d $(BUILDDIR)/doctrees -b html $(srcdir) $(BUILDDIR)/html
	@echo
	@echo "Build finished. The HTML pages are in $(BUILDDIR)/html."

uml: $(IMAGEDIR)/txn_box_config_schema.svg
uml: $(IMAGEDIR)/config-data.svg

clean:
	-rm -rf html warn.log
	-rm -rf $(BUILDDIR)/doctrees $(BUILDDIR)/html
	-rm pix/*.svg

ext/local-config.py : Makefile ext
	@echo "def setup(app):" > ext/local-config.py
	@echo "   plantuml = '$(JAVA) -jar $(PLANTUML_JAR)'" >> ext/local-config.py
	@echo "   plantuml_output_format = 'svg'" >> ext/local-config.py

ext:
	if [ ! -d "ext" ] ; then mkdir ext; fi

static:
	if [ ! -d "static" ] ; then mkdir static; fi

docbuild/html/src:
	@if [ ! -d docbuild/html/src ] ; then mkdir -p docbuild/html; ln -s ../../doxygen/html docbuild/html/src ; fi

doxygen:
	doxygen

publish: clean doxygen html
	if [ -z $(TXB_VERSION) ] ; then echo "Could not find version" ; exit 1; fi
	( cd docbuild/html ; tar cfh /tmp/txn_box_doc.tar *)
	scp /tmp/txn_box_doc.tar aws-swoc-1:/tmp
	ssh aws-swoc-1 sh bin/txn-box-doc-install.sh $(TXB_VERSION)
