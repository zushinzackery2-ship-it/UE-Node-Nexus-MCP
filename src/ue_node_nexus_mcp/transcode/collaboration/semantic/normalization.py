"""Re-encode authored values with published types, pin contracts and identities."""

from copy import deepcopy

from ...diff.common import same_class
from ...text.model import Section
from ...storage.io import digest
from .decode import to_document
from .encode import encode, family_for
from .identity import Identities


def matched_entity(identities, scope, decl):
    physical = decl.meta.get("physical") or decl.meta.get("guid")
    old_id = decl.meta.get("semantic_id")
    entities = identities.sections.get(scope, dict()).get("entities", dict())
    identifier = old_id if old_id in entities else None
    if not identifier and physical:
        owner = decl.meta.get("owner")
        identifier = identities.by_owner.get((owner, physical)) if owner else identities.by_physical.get(physical)
    identifier = identifier or identities.by_alias.get((scope, decl.id))
    binding = identities.old_bindings.get(identifier, dict())
    if physical and binding.get("physical") not in (None, "", physical):
        return None, None
    return identifier, entities.get(identifier)


def normalize_snapshot(snapshot: dict | None, reference: dict | None = None, schema=None) -> dict | None:
    """Keep the authored state; borrow only the canonical representation facts.

    A physical replacement never inherits an old identity just because it has
    the same alias. Unpublished declarations do inherit their first UE binding.
    """
    if snapshot is None:
        return None
    reference = reference or snapshot
    document = to_document(snapshot, include_defaults=True)
    kind = snapshot["semantic"]["kind"]
    identities = Identities(reference, "normalize", document.header.asset)
    for section in document.sections:
        scope = identities.scope(section)
        before = identities.sections.get(scope, dict())
        for prop in section.props():
            known = before.get("props", dict()).get(prop.key)
            if known:
                prop.type_name = known.get("type", prop.type_name)
        for decl in section.decls():
            identifier, entity = matched_entity(identities, scope, decl)
            if entity is None or not same_class(schema, family_for(kind, section.name), decl.type_name, entity["type"]):
                continue
            binding = identities.old_bindings.get(identifier, dict())
            decl.meta = dict(decl.meta, **deepcopy(binding.get("meta", dict())))
            decl.meta["semantic_id"] = identifier
            if binding.get("physical"):
                decl.meta["guid"] = binding["physical"]
            decl.type_name = entity["type"]
            # Omitted layout means retain the editor position; only an explicit
            # position is a writable layout change (the diff uses the same rule).
            if decl.pos is None and entity.get("position") is not None:
                decl.pos = tuple(entity["position"])
    # The raw envelope always emits this property section, even when empty.
    if document.section("asset") is None and "asset:" in identities.sections:
        document.sections.insert(0, Section("asset", ""))
    if schema:
        document.header.schema = schema.key
    semantic, bindings, locations = encode(document, kind, reference, "normalize", schema)
    return dict(snapshot, semantic=semantic, semantic_hash=digest(semantic), bindings=bindings,
                schema_key=document.header.schema, locations=locations)
