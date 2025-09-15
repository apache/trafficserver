#!/usr/bin/env bash
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

set -e

VENV_NAME="hrw4u"

if ! which antlr; then
    echo "Make sure antlr is installed, e.g. brew install antlr"
    echo "Once its in your path, re-run this script."
    exit
fi

eval "$(pyenv init --path)"
eval "$(pyenv init -)"
# eval "$(pyenv virtualenv-init -)"

if pyenv virtualenvs | grep hrw4u; then
    pyenv uninstall -f "$VENV_NAME"
else
    echo "==> Creating virtualenv $VENV_NAME..."
    pyenv virtualenv "$VENV_NAME"
fi

echo "==> Activating virtualenv..."
pyenv activate "$VENV_NAME"


echo "==> Installing dependencies..."
pip install --upgrade pip
pip install -r requirements.txt

echo "==> Done. To activate manually: pyenv activate $VENV_NAME"

# Probably need for running in the local build tree
export PYTHONPATH=./build:${PYTHONPATH}
