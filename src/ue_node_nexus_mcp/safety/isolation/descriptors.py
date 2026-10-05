"""Read Unreal descriptors with comments and trailing commas, retaining JSON errors."""

from __future__ import annotations

import json
from pathlib import Path

from ...instances.errors import require


def normalize(source: str) -> str:
    characters = list(source)
    tokens = []
    index = 0
    while index < len(source):
        character = source[index]
        if character.isspace():
            index += 1
            continue
        start = index
        if character == '"':
            _, index = json.decoder.scanstring(source, index + 1, True)
            tokens.append(('"', start))
            continue
        if source.startswith("//", index):
            index = source.find("\n", index + 2)
            if index < 0:
                index = len(source)
        elif source.startswith("/*", index):
            end = source.find("*/", index + 2)
            if end < 0:
                raise json.JSONDecodeError("Unterminated descriptor comment", source, index)
            index = end + 2
        else:
            tokens.append((character, index))
            index += 1
            continue
        for position in range(start, index):
            if characters[position] not in "\r\n":
                characters[position] = " "
    for position, (token, offset) in enumerate(tokens):
        if token == "," and position > 0 and position + 1 < len(tokens):
            previous, following = tokens[position - 1][0], tokens[position + 1][0]
            if previous not in "{[,:" and following in "}]":
                characters[offset] = " "
    return "".join(characters)


def read_descriptor(path: Path) -> dict:
    try:
        data = json.loads(normalize(path.read_text(encoding="utf-8-sig")))
    except (OSError, ValueError) as error:
        require(False, "isolation_descriptor_invalid", "Unreal descriptor could not be parsed",
                path=str(path), reason=str(error))
    require(isinstance(data, dict), "isolation_descriptor_invalid", "Unreal descriptor must be an object", path=str(path))
    return data
