from __future__ import annotations

from .contracts import DEFAULT_FEATURE_GROUPS, FEATURE_GROUPS


class FeatureArgumentError(ValueError):
    pass


def parse_bool(value: str) -> bool:
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on", "enable", "enabled"}:
        return True
    if normalized in {"0", "false", "no", "off", "disable", "disabled"}:
        return False
    raise FeatureArgumentError(f"invalid boolean value: {value}")


def parse_feature_list(value: str) -> set[str]:
    features = {part.strip().lower() for part in value.replace(";", ",").split(",") if part.strip()}
    unknown = features - FEATURE_GROUPS
    if unknown:
        raise FeatureArgumentError(f"unknown feature group(s): {', '.join(sorted(unknown))}")
    return features


def consume_feature_args(argv: list[str]) -> tuple[set[str] | None, set[str], set[str], bool | None, list[str]]:
    explicit_features = None
    enable_features: set[str] = set()
    disable_features: set[str] = set()
    niagara_support = None
    remaining = [argv[0]]
    index = 1
    while index < len(argv):
        arg = argv[index]
        if arg == "--features":
            index += 1
            if index >= len(argv):
                raise FeatureArgumentError("--features requires a comma-separated feature list")
            explicit_features = parse_feature_list(argv[index])
        elif arg.startswith("--features="):
            explicit_features = parse_feature_list(arg.split("=", 1)[1])
        elif arg == "--enable-feature":
            index += 1
            if index >= len(argv):
                raise FeatureArgumentError("--enable-feature requires a feature name")
            enable_features |= parse_feature_list(argv[index])
        elif arg.startswith("--enable-feature="):
            enable_features |= parse_feature_list(arg.split("=", 1)[1])
        elif arg == "--disable-feature":
            index += 1
            if index >= len(argv):
                raise FeatureArgumentError("--disable-feature requires a feature name")
            disable_features |= parse_feature_list(argv[index])
        elif arg.startswith("--disable-feature="):
            disable_features |= parse_feature_list(arg.split("=", 1)[1])
        elif arg == "--niagara-support":
            index += 1
            if index >= len(argv):
                raise FeatureArgumentError("--niagara-support requires true or false")
            niagara_support = parse_bool(argv[index])
        elif arg.startswith("--niagara-support="):
            niagara_support = parse_bool(arg.split("=", 1)[1])
        else:
            remaining.append(arg)
        index += 1
    return explicit_features, enable_features, disable_features, niagara_support, remaining


def resolve_enabled_features(
    explicit_features: set[str] | None = None,
    enable_features: set[str] | None = None,
    disable_features: set[str] | None = None,
    niagara_support: bool | None = None,
) -> set[str]:
    features = set(explicit_features if explicit_features is not None else DEFAULT_FEATURE_GROUPS)
    features |= set(enable_features or set())
    features -= set(disable_features or set())
    if niagara_support is True:
        features.add("niagara")
    elif niagara_support is False:
        features.discard("niagara")
    return features


def read_feature_env(env: dict[str, str]) -> tuple[set[str] | None, set[str], set[str], bool | None]:
    explicit_features = None
    enable_features: set[str] = set()
    disable_features: set[str] = set()
    niagara_support = None

    env_features = env.get("UE_NEXUS_FEATURES")
    if env_features:
        explicit_features = parse_feature_list(env_features)

    env_enable = env.get("UE_NEXUS_ENABLE_FEATURES")
    if env_enable:
        enable_features |= parse_feature_list(env_enable)

    env_disable = env.get("UE_NEXUS_DISABLE_FEATURES")
    if env_disable:
        disable_features |= parse_feature_list(env_disable)

    env_niagara = env.get("UE_NEXUS_NIAGARA_SUPPORT")
    if env_niagara:
        niagara_support = parse_bool(env_niagara)

    return explicit_features, enable_features, disable_features, niagara_support
