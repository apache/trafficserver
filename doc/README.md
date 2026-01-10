# Apache Traffic Server Documentation

This directory contains the source code for Traffic Server documentation.

# Build

## Package Requirements
Traffic Server documentation is built using the Sphinx documentation generator.
The Sphinx build tool is distributed as a Python package. A `pyproject.toml` is
provided to conveniently configure a Python virtual environment with the needed
Sphinx packages via uv.

In addition to the Sphinx Python package requirements, building the
documentation will also require Java, graphviz, and uv system packages to be
installed.

## Build Steps
Building the docs requires passing `-DENABLE_DOCS=ON` to cmake (docs generation
is off by default), and then using the appropriate build target. The build steps
will automatically install a virtual environment via uv using `doc/pyproject.toml`
and do what is necessary to build the docs.

### Building HTML Documentation
```sh
cmake -B docs-build -DENABLE_DOCS=ON
cmake --build docs-build --target generate_docs
```

The generated HTML docs will be in `docs-build/doc/docbuild/html`.

### Building PDF Documentation
```sh
# Letter paper size (US standard)
cmake --build docs-build --target generate_pdf

# A4 paper size (International standard)
cmake --build docs-build --target generate_pdf_a4
```

The generated PDF will be in `docs-build/doc/docbuild/latex/ApacheTrafficServer.pdf`
(or `docs-build/doc/docbuild/latex-a4/` for A4 format).

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
