"""Preserve author aliases verified by native CDO property/subobject identity."""


def retain_aliases(document, previous):
    section = document.section("components")
    if section is None or previous is None:
        return
    old = previous["semantic"]["sections"].get("components:", dict()).get("entities", dict())
    inherited = [entity["alias"] for entity in old.values() if entity["type"] == "@inherited"]
    replacements = dict()
    for decl in section.decls():
        matches = [name for name in inherited if name in decl.meta.get("native_aliases", [])]
        if decl.inherited and len(matches) == 1:
            replacements[decl.id] = matches[0]
            decl.id = matches[0]
    for decl in section.decls():
        decl.args = [(key, replacements.get(value, value) if key == "parent" else value) for key, value in decl.args]
