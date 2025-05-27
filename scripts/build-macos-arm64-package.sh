#!/bin/bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Parse command line argument
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 [test|prod]"
    exit 1
fi

if [ "$1" == "test" ]; then
    TWINE_REPOSITORY="https://test.pypi.org/legacy/"
else
    TWINE_REPOSITORY="https://upload.pypi.org/legacy/"
fi


# Use clang and not gcc
export CC=/usr/bin/cc
export CXX=/usr/bin/c++

# Build vrs using the following
# cd /tmp; git clone https://github.com/facebookresearch/vrs.git -b v1.0.4 \
#     && mkdir vrs_Build && cd vrs_Build \
#     && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DCMAKE_OSX_ARCHITECTURES="arm64" ../vrs/ \
#     && sudo make -j2 install

cd ..

export ARCHFLAGS="-arch arm64"
export CMAKE_ARGS="-DCMAKE_PREFIX_PATH=/Users/${USER}/homebrew"

# Set environment variables for cibuildwheel
export CIBW_PLATFORM="macos"
export CIBW_OUTPUT_DIR="dist"
export CIBW_ARCHS="arm64"
export CIBW_BUILD_VERBOSITY="3"
export CIBW_BUILD="cp39-*64 cp310-*64 cp311-*64 cp312-*64"
export CIBW_BEFORE_BUILD_MACOS="arch -arm64 brew install boost cmake fmt glog jpeg-turbo libpng lz4 xxhash zstd opus"
export CIBW_SKIP="*-manylinux_i686 *musllinux*"

# Build wheels for all specified versions
python -m pip install cibuildwheel==2.17.0
python -m cibuildwheel --output-dir dist

# # Upload to PyPI
twine upload --repository-url "$TWINE_REPOSITORY" dist/*

# # Clean up
rm -rf dist
