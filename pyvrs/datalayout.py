#!/usr/bin/env python3
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

# import the native pybind11 VRS bindings (which pyvrs wraps)
import json
import pprint
import re
from typing import Any, Dict

from . import RecordType


class VRSDataLayout:
    def __init__(
        self,
        members: Dict[str, Any],
        data_layout: str,
        data_layout_type: RecordType,
        index: int,
    ) -> None:
        self.__dict__["members"] = members
        self.__dict__["data_layout"] = data_layout
        self.__dict__["data_layout_type"] = data_layout_type
        self.__dict__["index"] = index

    def __str__(self) -> str:
        if self.data_layout is None:
            return "(null)"
        data_layout = json.loads(self.data_layout)["data_layout"]
        list = []
        for field in data_layout:
            d = {}
            for k, v in field.items():
                if k not in ["offset", "size", "index"]:
                    d[k] = type_conversion(v) if k == "type" else v

            if len(d) > 0:
                list.append(dict_to_str(d))

        s = "\n".join([""] + list)
        return s

    def __getattr__(self, name: str):
        if name in self.__dict__["members"]:
            return self.__dict__["members"].get(name)
        raise AttributeError

    def __setattr__(self, name: str, value: Any) -> None:
        if name in self.__dict__["members"]:
            self.__dict__["members"].get(name).set(value)
            return
        raise AttributeError

    def __getitem__(self, name):
        return self.__getattr__(name)

    def __setitem__(self, name, value):
        return self.__setattr__(name, value)


def dict_to_str(d: Dict[str, str]) -> str:
    list = []
    if len(d) < 4:
        return "  " + pprint.pformat(d)
    for k, v in d.items():
        list.append(f"    '{k}': '{v}',")
    s = "\n".join(["  {"] + list + ["  }"])
    return s


def type_conversion(value: str) -> str:
    if value == "DataPieceString":
        return "str"
    type_map = {
        "float": "float",
        "double": "float",
        "string": "str",
        "uint8_t": "int",
        "int8_t": "int",
        "uint16_t": "int",
        "int16_t": "int",
        "uint32_t": "int",
        "int32_t": "int",
        "uint64_t": "int",
        "int64_t": "int",
        "Point2Di": "(int, int)",
        "Point3Df": "(float, float, float)",
        "Point4Df": "(float, float, float, float)",
    }
    m = re.match(r"(?P<datapiece_typename>\w+)<(?P<typename>\w+)>", value)
    datapiece_typename = m.group("datapiece_typename")
    typename = m.group("typename")

    if datapiece_typename == "DataPieceValue":
        return type_map[typename]
    if datapiece_typename in ["DataPieceVector", "DataPieceArray"]:
        return f"List[{type_map[typename]}]"
    if datapiece_typename == "DataPieceStringMap":
        return f"Dict[str, {type_map[typename]}]"
    return "None"
