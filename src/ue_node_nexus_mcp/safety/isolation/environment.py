"""Reproduce measured rendering inputs, then compare the worker's real state."""

import json
from pathlib import Path
import re

from ...instances.errors import require

CATEGORIES = frozenset(("Engine", "Game", "Input", "Scalability", "DeviceProfiles", "Hardware"))
PREFIXES = ("r.", "sg.", "fx.", "niagara.", "rhi.", "d3d11.", "d3d12.", "p.", "a.", "foliage.", "grass.")
FLAGS = frozenset(("sm5", "sm6", "featureleveles31", "noraytracing", "raytracing", "renderoffscreen",
                   "allowsoftwarerendering", "windowed", "fullscreen", "forceres", "vsync", "novsync", "norhithread", "onethread"))
NUMBERS = frozenset(("resx", "resy", "graphicsadapter", "adapter"))


def arguments(launch):
    require(isinstance(launch, dict) and isinstance(launch.get("arguments"), list)
            and isinstance(launch.get("unsupported"), list), "safety_identity_incomplete", "effective rendering launch measurement is incomplete")
    require(not launch["unsupported"], "safety_launch_unsupported", "source rendering launch options cannot be reproduced",
            arguments=launch["unsupported"])
    result, names = [], set()
    for argument in launch["arguments"]:
        require(isinstance(argument, str) and argument.startswith("-"), "safety_launch_unsupported", "rendering launch argument is invalid")
        name, separator, value = argument[1:].partition("=")
        allowed = name in FLAGS and not separator
        allowed |= name in NUMBERS and bool(re.fullmatch(r"-?\d+", value)) and bool(separator)
        allowed |= name == "featurelevel" and value.lower() in ("es31", "sm5", "sm6") and bool(separator)
        require(allowed and name not in names, "safety_launch_unsupported", "rendering launch argument cannot be reproduced", argument=argument)
        names.add(name)
        result.append(argument)
    return result


def validate(environment):
    require(isinstance(environment, dict) and environment.get("effective_environment_contract") == 1,
            "safety_identity_incomplete", "effective configuration and runtime rendering measurements are required")
    configuration = environment.get("effective_configuration")
    require(isinstance(configuration, dict) and set(configuration) == CATEGORIES,
            "safety_identity_incomplete", "effective configuration requires every runtime ini category")
    for category, sections in configuration.items():
        require(isinstance(sections, dict), "safety_identity_incomplete", "effective ini sections are invalid", category=category)
        for entries in sections.values():
            require(isinstance(entries, list) and all(isinstance(row, dict) and isinstance(row.get("key"), str)
                    and row["key"] and isinstance(row.get("value"), str) for row in entries),
                    "safety_identity_incomplete", "effective ini entries are invalid", category=category)
    variables = environment.get("render_cvars")
    require(isinstance(variables, dict) and variables and all(isinstance(name, str) and name.lower().startswith(PREFIXES)
            and isinstance(value, str) for name, value in variables.items()),
            "safety_identity_incomplete", "runtime rendering console variable measurement is incomplete")
    arguments(environment.get("render_launch"))
    return environment


def launch_arguments(project):
    marker = Path(project).parent / "Saved/Nexus/validation-worker.json"
    if not marker.is_file():
        return []
    data = json.loads(marker.read_text(encoding="utf-8"))
    require(Path(data.get("project_path", "")).resolve() == Path(project).resolve(),
            "isolation_wrong_project", "validation marker belongs to a different project")
    if "environment" not in data:
        return []
    return arguments(validate(data["environment"])["render_launch"])


def verify_worker(expected, worker):
    validate(expected)
    require(worker.get("environment") == expected, "isolation_identity_mismatch",
            "worker effective configuration, rendering variables or launch options differ from the publishing editor")
    reproduction = worker.get("reproduction", {})
    require(reproduction.get("applied") is True and reproduction.get("phase") == "PostConfigInit",
            "isolation_reproduction_failed", "worker did not reproduce the environment before RHI initialization", reproduction=reproduction)
    require(reproduction.get("callbacks_applied") is True and reproduction.get("callbacks_phase") == "EngineLoopInitComplete",
            "isolation_reproduction_failed", "worker did not complete console callbacks after engine initialization", reproduction=reproduction)
