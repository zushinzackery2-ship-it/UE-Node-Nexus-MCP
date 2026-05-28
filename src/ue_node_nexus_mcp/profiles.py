from __future__ import annotations

RESPONSE_MINIMAL = "minimal"
RESPONSE_FULL = "full"
VALID_RESPONSE_MODES = {RESPONSE_MINIMAL, RESPONSE_FULL}
DEFAULT_RESPONSE_MODE = RESPONSE_MINIMAL


class ProfileArgumentError(ValueError):
    pass


def parse_response_mode(value: str) -> str:
    normalized = value.strip().lower()
    if normalized not in VALID_RESPONSE_MODES:
        raise ProfileArgumentError(f"unknown response mode: {value}")
    return normalized


def consume_profile_args(argv: list[str]) -> tuple[str | None, list[str]]:
    response_mode = None
    remaining = [argv[0]]
    index = 1
    while index < len(argv):
        arg = argv[index]
        if arg == "--response-mode":
            index += 1
            if index >= len(argv):
                raise ProfileArgumentError("--response-mode requires minimal or full")
            response_mode = parse_response_mode(argv[index])
        elif arg.startswith("--response-mode="):
            response_mode = parse_response_mode(arg.split("=", 1)[1])
        else:
            remaining.append(arg)
        index += 1
    return response_mode, remaining


def read_profile_env(env: dict[str, str]) -> str | None:
    response_mode = None

    env_response = env.get("UE_NEXUS_RESPONSE_MODE")
    if env_response:
        response_mode = parse_response_mode(env_response)

    return response_mode
