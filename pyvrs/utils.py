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

from collections.abc import Callable, Iterable, Mapping
from fnmatch import fnmatch
from typing import Any, TypeVar

from . import recordable_type_id_name

T = TypeVar("T")


def get_recordable_type_id_name(stream_id: str) -> str:
    """Retrieve a human readable string what the recordable is.
    Args:
        stream_id: stream_id you are interested in.
    Returns:
        The name of recordable_type_id for the given stream id.
    """
    return recordable_type_id_name(stream_id)


def _filter_set_on_condition(
    s: set[T], items: Iterable[T], cond_func: Callable[[T, T], bool]
) -> set[T]:
    """Restrict down a set by a list of items and a comparator. For each element in the set, if for
    any item in the items the comparator returns True, the element is retained in the filtered set
    that is returned. Note that this does not mutate the original set, it returns a filtered copy.
    """
    filtered = set()
    for a in s:
        for b in items:
            if cond_func(a, b):
                filtered.add(a)
                break
    return filtered


def filter_by_stream_ids(
    available_stream_ids: set[str], regex_stream_ids: Iterable[str]
) -> set[str]:
    """Filter a set of stream_ids based on the provided regex stream_ids.
    Args:
        available_stream_ids: A set of stream_ids in a file.
        regex_stream_ids: regex of the stream ids that you are interested in.
    Returns:
        A set of stream_ids in available_stream_ids that matches the one of the regex in regex_stream_ids.
    """
    return _filter_set_on_condition(
        available_stream_ids, regex_stream_ids, lambda a, b: fnmatch(a, b)
    )


def filter_by_record_type(
    available_record_types: set[str], requested_record_types: Iterable[str]
) -> set[str]:
    """Filter a set record types based on the provided record types. Filter only succeeds on
    exact matches.
    Args:
        available_record_types: A set of record types in a file.
        requested_record_types: A set of record types you are interested in.
    Returns:
        A set of record types in available_record_types that is also in requested_record_types.
    """
    return _filter_set_on_condition(
        available_record_types, requested_record_types, lambda a, b: a == b
    )


def string_of_set(s: set[str]) -> str:
    """Return a pretty string representation of a set (with elements ordered)."""
    return "{" + ", ".join(sorted(s)) + "}"


def tags_to_justified_table_str(tags: Mapping[str, Any]) -> str:
    """Returns a nice table representation of tags for easy viewing."""
    if len(tags) == 0:
        return "File contains no file tags."
    tag_width = max(len(t) for t in tags)
    table_content = [f"{k.rjust(tag_width)}| {tags[k]}" for k in sorted(tags)]
    table_width = max(len(content) for content in table_content)
    return "\n".join(
        [" FILE TAGS ".center(table_width, "-"), *table_content, "-" * table_width]
    )


def _unique_string_for_key(name: str, type_: str, dict_: Mapping[str, Any]) -> str:
    r"""Builds a unique key for a name type pair."""
    i = 1
    while True:
        # default to name<type>. If we hit an existing key, keep adding brackets, e.g. name<<type>>.
        new_key = name + ("<" * i) + type_ + (">" * i)
        if new_key not in dict_:
            return new_key
        i += 1


def stringify_metadata_keys(
    metadata_dict: dict[tuple[str, str], Any],
) -> dict[str, Any]:
    r"""remove unambiguous types from metadata dicts. If the type is overloaded, converts to a
    key<type> string representation."""
    filtered: dict[str, Any] = {}
    ambiguous_names: set[str] = set()
    removed_types: dict[str, str] = {}

    for (name, type_), value in metadata_dict.items():
        if name in ambiguous_names:
            # this name is already overloaded - build a string from the name and type.
            filtered[_unique_string_for_key(name, type_, filtered)] = value
        elif name in filtered:
            # first time we find a collision. Get the old value and type..
            old_value = filtered.pop(name)
            old_type = removed_types.pop(name)

            # and update the filtered dict with both the old entry and the new.
            filtered[_unique_string_for_key(name, old_type, filtered)] = old_value
            filtered[_unique_string_for_key(name, type_, filtered)] = value

            # blacklist so we don't hit this again.
            ambiguous_names.add(name)
        else:
            # this name seems unambiguous so far, just add it without type...
            filtered[name] = value
            # ...and keep this type around in case we need to disambiguate later.
            removed_types[name] = type_

    return filtered
