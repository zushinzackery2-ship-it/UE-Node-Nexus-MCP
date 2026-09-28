"""One identity for the Blueprint class spellings accepted by the schema."""

from .call_host import call_spelling, host_name
from ..schema.names import K2_ALIASES
from ..schema.catalog import reference


def node_name(text: str) -> str:
    name = host_name(K2_ALIASES.get(text, text))
    if name.startswith("EdGraphNode_"):
        name = name[len("EdGraphNode_"):]
    return call_spelling(name)


def node_identity(text: str, schema=None) -> str:
    if text.startswith("/Script/BlueprintGraph.K2Node_"):
        spelling = call_spelling(text)
        if spelling != text:
            return spelling
    if text.startswith("/"):
        return reference(schema, "k2node", text, node_name(text)) if schema is not None else text
    return node_name(text)


def create_class(text: str) -> str:
    name = K2_ALIASES.get(text, text)
    if name == "EdGraphNode_Comment" or text == "Comment":
        return "/Script/UnrealEd.EdGraphNode_Comment"
    if text in K2_ALIASES and text != "CallFunction":
        return "/Script/BlueprintGraph." + name
    return text
