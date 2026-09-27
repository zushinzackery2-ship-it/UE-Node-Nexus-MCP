"""One identity for the Blueprint class spellings accepted by the schema."""

from ..bp_call_host import call_spelling, host_name
from ..schema.lock import K2_ALIASES


def node_name(text: str) -> str:
    name = host_name(K2_ALIASES.get(text, text))
    if name.startswith("EdGraphNode_"):
        name = name[len("EdGraphNode_"):]
    return call_spelling(name)


def create_class(text: str) -> str:
    name = K2_ALIASES.get(text, text)
    if name == "EdGraphNode_Comment" or text == "Comment":
        return "/Script/UnrealEd.EdGraphNode_Comment"
    if text in K2_ALIASES and text != "CallFunction":
        return "/Script/BlueprintGraph." + name
    return text
