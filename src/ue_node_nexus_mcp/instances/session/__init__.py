"""Per-MCP bindings backed by the user-level manager."""

from .binding import EditorSession

instance_manager = EditorSession()

__all__ = ["EditorSession", "instance_manager"]
