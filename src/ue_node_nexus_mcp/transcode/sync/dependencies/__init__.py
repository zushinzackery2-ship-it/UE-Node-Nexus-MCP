"""Dependencies from parsed document values, ordered before their consumers."""

from .collect import dependency_kinds, document_dependencies
from .order import order_assets

__all__ = ("dependency_kinds", "document_dependencies", "order_assets")
