# Apache Traffic Server Documentation

This directory contains the source code for Traffic Server documentation.

# Build

## Package Requirements
Traffic Server documentation is built using the Sphinx documentation generator.
The Sphinx build tool is distributed as a Python package. A Pipfile is provided
to conveniently configure a Python virtual environment with the needed Sphinx
packages.

In addition to the Sphinx Python package requirements, building the
documentation will also require Java and graphviz system packages to be
installed.

## Build Steps
Building the docs is a relatively simple matter of passing
`-DENABLE_DOCS=ON` to cmake (docs generation is off by default), and
then using the `generate_docs` build target. The build steps for the
`generate_docs` target will install a Pipenv using `docs/Pipfile` and do
what is necessary to build and install the docs. Thus the following
steps should build the docs:

```sh
cmake -B docs-build -DENABLE_DOCS=ON
cmake --build docs-build --target generate_docs
```

The generated HTML docs will be installed in the cmake build's `doc/docbuild/html`
directory, `docs-build/doc/docbuild/html` per the above example.

Once the build completes, you can use Python's
[http.server](https://docs.python.org/3/library/http.server.html) module to
create a local test HTTP server to view the built documentation. **Note:**
http.server is only designed for test purposes, not for production use.

```bash
cd docs-build/doc/docbuild/html
python3 -m http.server 8888
Serving HTTP on 0.0.0.0 port 8888 (http://0.0.0.0:8888/) ...
```

You can then view the rendered HTML using your browser of choice and navigating
to the `http://0.0.0.0:8888/` URL.
