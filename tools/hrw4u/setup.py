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
from setuptools import setup, find_packages

setup(
    name="hrw4u",
    version="1.0.0",
    description="HRW4U CLI tool",
    author="Leif Hedstrom",
    package_dir={"": "build"},
    packages=find_packages(where="build"),
    include_package_data=True,
    entry_points={"console_scripts": ["hrw4u = hrw4u.__main__:main",]},
    install_requires=[
        "antlr4-python3-runtime==4.13.*",
    ],
    python_requires=">=3.9",
    classifiers=[
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: Implementation :: PyPy",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: OS Independent",
    ],
)
