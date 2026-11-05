"""Material parameter association/index is part of its identity, beside its name."""

from ..errors import SyncError


def parameter_scope(association="global", index=-1) -> str:
    if association == "global" and index == -1:
        return ""
    if association in ("layer", "blend") and isinstance(index, int) and not isinstance(index, bool) and index >= 0:
        return f"{association} {index}"
    raise SyncError("invalid_parameter_identity", "parameter requires global/-1 or layer|blend/nonnegative index",
                    dict(association=association, index=index))


def parameter_identity(scope: str) -> dict:
    if not scope:
        return {}
    parts = scope.split()
    if len(parts) == 2 and parts[0] in ("layer", "blend") and parts[1].isdigit():
        index = int(parts[1])
        if scope == parameter_scope(parts[0], index):
            return dict(association=parts[0], index=index)
    raise SyncError("invalid_parameter_identity", "parameter section expects 'layer N' or 'blend N'", dict(scope=scope))
