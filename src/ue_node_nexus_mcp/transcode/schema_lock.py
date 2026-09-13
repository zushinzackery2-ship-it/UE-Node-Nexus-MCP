"""Compatibility imports for the unified schema package."""

from .schema.lock import ClassInfo, SchemaLock, find_any_schema_lock, load_schema_lock

__all__ = ["ClassInfo", "SchemaLock", "find_any_schema_lock", "load_schema_lock"]
