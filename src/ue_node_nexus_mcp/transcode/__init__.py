"""Text mirror ("Content_Transcoded") codec, lint, diff and sync engine.

The UE editor stays the compiler; ``.nexus`` text files are the source. Bulk
data flows disk-to-disk (UE plugin writes raw exports into the mirror), the MCP
tool results only carry summaries and ``file:line`` diagnostics.
"""
