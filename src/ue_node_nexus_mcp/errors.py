from __future__ import annotations


class BridgeError(RuntimeError):
    """Raised when the UE bridge cannot be reached, the selected editor instance
    is gone/ambiguous, or the bridge returns a malformed reply.

    Lives in its own module so the transport and instance layers can raise it
    without importing the higher-level bridge client (avoids an import cycle).
    """
