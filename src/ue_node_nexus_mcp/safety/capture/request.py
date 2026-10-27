"""The viewport_capture request contract shared by its wrapper, payload schema and native handler.

``filename`` names the screenshot without directory or extension. The editor
writes ``Saved/Screenshots/<platform>/<filename>_<capture_id>.png`` and returns
``file_path`` with ``capture_id``; the receipt sits beside it as
``<file_path>.capture.json``.
"""

from __future__ import annotations

import re
from typing import Any

FILENAME_PATTERN = r"^[A-Za-z0-9_-]{1,100}$"
FILENAME_MAX_LENGTH = 100
DEFAULT_FILENAME = "UeNodeNexus"
TARGETS = ("level", "active", "pie")
TIMEOUT_MINIMUM = 1
TIMEOUT_MAXIMUM = 120

FILENAME_RULE = "filename must be 1..100 letters, digits, '_' or '-' without directory or extension"
TARGET_RULE = "target must be one of level/active/pie"
TIMEOUT_RULE = "timeout_seconds must be a number from 1 to 120"


def validate(filename: Any, target: Any, timeout_seconds: Any, show_ui: Any, dry_run: Any) -> None:
    """Raise ``ValueError`` for the first violated field, in the native handler's order."""
    if filename is not None and (not isinstance(filename, str) or re.fullmatch(FILENAME_PATTERN, filename) is None):
        raise ValueError(FILENAME_RULE)
    if target not in TARGETS:
        raise ValueError(TARGET_RULE)
    number = isinstance(timeout_seconds, (int, float)) and not isinstance(timeout_seconds, bool)
    if not number or not TIMEOUT_MINIMUM <= timeout_seconds <= TIMEOUT_MAXIMUM:
        raise ValueError(TIMEOUT_RULE)
    for field, value in (("show_ui", show_ui), ("dry_run", dry_run)):
        if not isinstance(value, bool):
            raise ValueError(f"{field} must be a boolean")


def augment(operation: str, schema: dict[str, Any]) -> None:
    if operation != "viewport_capture":
        return
    fields = schema["properties"]
    fields["filename"].update(
        pattern=FILENAME_PATTERN, minLength=1, maxLength=FILENAME_MAX_LENGTH,
        description=f"Screenshot name without directory or extension; omitted uses {DEFAULT_FILENAME}. The PNG is "
                    "Saved/Screenshots/<platform>/<filename>_<capture_id>.png; data returns file_path and capture_id "
                    "for viewport_capture_status")
    fields["target"]["description"] = ("level is the level editor viewport, active the last focused panel, "
                                       "pie the running game viewport")
    fields["timeout_seconds"].update(minimum=TIMEOUT_MINIMUM, maximum=TIMEOUT_MAXIMUM,
                                     description="Deadline in seconds for the admitted capture job to complete")
