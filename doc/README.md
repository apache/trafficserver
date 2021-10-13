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
By default, building the Traffic Server documentation is not configured via the
Automake configure script. It is enabled using the `--enable-docs`
configuration switch.

Here is an example shell session that can be used to build the HTML version of
the Traffic Server documentation:

```bash
cd doc

# Create the Python virtual environment with Sphinx and other documentation
# dependencies installed.
pipenv install

# Enter the Python virtual environment.
pipenv shell

# Configure and build an HTML version of the documentation.
cd ../
autoreconf -fi
./configure --enable-docs
cd doc
make -j $(nproc) html
```

Once the build completes, you can use Python's
[http.server](https://docs.python.org/3/library/http.server.html) module to
create a local test HTTP server to view the built documentation. **Note:**
http.server is only designed for test purposes, not for production use.

```bash
cd docbuild/html
python3 -m http.server 8888
Serving HTTP on 0.0.0.0 port 8888 (http://0.0.0.0:8888/) ...
```

You can then view the rendered HTML using your browser of choice and navigating
to the `http://0.0.0.0:8888/` URL.
