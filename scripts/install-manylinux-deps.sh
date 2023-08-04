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

yum install -y cmake git ninja-build gtest-devel \
    lz4-devel libzstd-devel xxhash-devel libpng-devel

cd /tmp && git clone https://github.com/fmtlib/fmt.git -b 8.1.1 \
    && cd fmt \
    && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DFMT_TEST=OFF .;make -j4 install; rm -rf /tmp/fmt;

cd /tmp && git clone https://github.com/libjpeg-turbo/libjpeg-turbo.git -b 2.1.4 \
    && cd libjpeg-turbo \
    && cmake -DCMAKE_BUILD_TYPE=Release -DWITH_JPEG8=1 -DCMAKE_INSTALL_DEFAULT_PREFIX=/usr .;make -j4 install; rm -rf /tmp/libjpeg-turbo;

cd /tmp && git clone --recursive https://github.com/boostorg/boost.git -b boost-1.81.0 \
    && cd boost \
    && ./bootstrap.sh \
    && ./b2 install \
    && rm -rf /tmp/boost;
