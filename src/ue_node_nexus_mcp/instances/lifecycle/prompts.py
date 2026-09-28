"""What an editor is waiting on, from its Guard and from its windows."""

from __future__ import annotations


def observe(record: dict, rows: list[dict], guard: dict | None) -> dict:
    """Write one observation's windows and prompt into ``record``.

    ``guard`` is the Guard status of this observation, or None when it did not
    answer. The Guard records every editor message dialog from PostConfigInit on;
    a native MessageBox shown before it loaded, or by code that bypasses the
    editor's dialog handler, is only visible as a ``#32770`` window of the process.
    Every field is written each time, so an answered prompt clears what it reported.
    """
    titles = [row["title"] for row in rows if row["visible"]]
    dialog = guard.get("blocking_dialog") if guard and guard.get("waiting_for_user") else None
    if dialog is None:
        native = next((row for row in rows if row["dialog"]), None)
        if native:
            dialog = dict(source="window", title=native["title"], message=native.get("message", ""), visible=native["visible"])
    record.update(window_visible=bool(titles), window_titles=titles, windows=rows,
                  blocking_dialog=dialog, waiting_for_user=dialog is not None)
    return record


def exited(item: dict) -> None:
    """An exit ends every prompt; the one still open says what the process was waiting on."""
    if item.get("blocking_dialog"):
        item["exited_while_waiting"] = item["blocking_dialog"]
    item.update(ready=False, window_visible=False, window_titles=[], windows=[], waiting_for_user=False, blocking_dialog=None)
