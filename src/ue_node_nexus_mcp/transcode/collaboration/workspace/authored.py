"""Reuse authored fact lines and comments when semantic merging changes a file."""

from difflib import SequenceMatcher

from ...errors import SyncError
from ...text.emitter import emit, emit_entry
from ...text.parser import parse, strip_comment
from ...text.semantic import equivalent
from ..semantic.decode import to_document
from ..semantic.snapshot import capture
from .projection import content


def facts(document):
    header = emit(type(document)(header=document.header)).splitlines()
    result = [(line, None) for line in header]
    for section in document.sections:
        result.append((section.header(), section.line))
        result.extend((emit_entry(entry), entry.line) for entry in section.entries)
    return result


def source_blocks(original, document):
    lines = original.splitlines(keepends=True)
    tokens = facts(document)
    first_section = min((section.line for section in document.sections), default=len(lines) + 1)
    headers = dict()
    for index, line in enumerate(lines[:first_section - 1]):
        key, separator, _ = strip_comment(line).strip().partition(":")
        if separator:
            headers[key] = index + 1
    blocks, consumed = [], 0
    for token, position in tokens:
        position = position or headers.get(token.partition(":")[0])
        if position is None:
            blocks.append(dict(token=token, leading="", code="", comment=""))
            continue
        line = lines[position - 1]
        code = strip_comment(line)
        comment = line[len(code):].rstrip("\r\n") if code != line else ""
        blocks.append(dict(token=token, leading="".join(lines[consumed:position - 1]), code=line, comment=comment))
        consumed = position
    return blocks, "".join(lines[consumed:])


def merge_text(workspace, original, before, after):
    """Preserve reusable lines; verify the resulting bytes against merged state."""
    source = original.decode("utf-8-sig")
    document, diagnostics = parse(source)
    if diagnostics.has_errors:
        raise SyncError("invalid_document", "stash original text cannot be parsed")
    blocks, tail = source_blocks(source, document)
    desired = [token for token, _ in facts(to_document(after))]
    matcher = SequenceMatcher(a=[block["token"] for block in blocks], b=desired, autojunk=False)
    newline = "\r\n" if "\r\n" in source else "\n"
    output = []
    for operation, start, end, first, last in matcher.get_opcodes():
        current = blocks[start:end]
        if operation == "equal":
            output.extend(block["leading"] + block["code"] for block in current)
            continue
        output.extend(block["leading"] for block in current)
        for index, token in enumerate(desired[first:last]):
            comment = current[index]["comment"] if index < len(current) else ""
            output.append(token + (" " + comment if comment else "") + newline)
    text = "".join(output) + tail
    captured = capture(text, after, workspace.state["id"], after["semantic"]["kind"], workspace.schema)
    if not equivalent(captured["semantic"], after["semantic"]):
        raise SyncError("authored_projection_invalid", "retained original text differs from the merged semantic state",
                        dict(asset=after["semantic"]["header"]["asset"]))
    return (b"\xef\xbb\xbf" if original.startswith(b"\xef\xbb\xbf") else b"") + text.encode("utf-8")


def projection_options(workspace, session):
    """Resolve retained source bytes after any three-layer authored merge."""
    target = workspace.history.entries(session["candidates"].get("files", session["candidates"]["head"]))
    files, authored, snapshots = dict(), dict(), dict()
    for source in session["metadata"].get("authored_sources", []):
        entries = workspace.history.entries(source["tree"])
        files.update(source["files"])
        for asset, identifier in entries.items():
            if asset not in target:
                continue
            before = workspace.store.objects.data(identifier, "snapshot")
            after = workspace.store.objects.data(target[asset], "snapshot")
            original = content(workspace.store, source["originals"][source["files"][asset]])
            if original is None:
                continue
            if equivalent(before["semantic"], after["semantic"]):
                authored[asset] = original
            elif asset not in authored:
                snapshots[asset] = (original, before, after)
    for asset, arguments in snapshots.items():
        if asset not in authored:
            authored[asset] = merge_text(workspace, *arguments)
    return dict(file_map=files, authored=authored)
