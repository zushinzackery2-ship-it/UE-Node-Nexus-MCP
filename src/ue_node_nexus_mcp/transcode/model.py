"""Document object model shared by the text parser, the raw codecs and diff.

Both directions (UE raw export and ``.nexus`` text) are converted into this
model, so comparisons and the canonical emitter only ever see one shape.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Iterator, Union

HEADER_KEYS = ("nexus", "asset", "class", "schema")


@dataclass
class Header:
    nexus: int = 1
    asset: str = ""
    cls: str = ""
    schema: str = ""
    extra: dict[str, str] = field(default_factory=dict)


@dataclass
class Prop:
    key: str
    value: str
    line: int = 0
    type_name: str | None = None
    default: str | None = None


@dataclass
class Decl:
    id: str
    type_name: str
    args: list[tuple[str | None, str]] = field(default_factory=list)
    default: str | None = None
    props: list[tuple[str, str | None]] = field(default_factory=list)
    pos: tuple[int, int] | None = None
    flags: list[str] = field(default_factory=list)
    annotations: dict[str, str] = field(default_factory=dict)
    modifier: str | None = None
    line: int = 0
    meta: dict[str, Any] = field(default_factory=dict)

    @property
    def opaque(self) -> bool:
        return self.type_name == "@opaque"

    @property
    def inherited(self) -> bool:
        return self.type_name == "@inherited"

    def positional(self) -> list[str]:
        return [value for key, value in self.args if key is None]

    def keyed(self) -> dict[str, str]:
        return {key: value for key, value in self.args if key is not None}

    def prop_map(self) -> dict[str, str | None]:
        return dict(self.props)

    def prop_flags(self) -> list[str]:
        return [key for key, value in self.props if value is None]

    def prop_values(self) -> dict[str, str]:
        return {key: value for key, value in self.props if value is not None}

    def marker_arg(self) -> str | None:
        """Payload of ``@opaque(X)`` / ``@inherited(X)`` / ``Object(X)``."""
        positional = self.positional()
        return positional[0] if positional else None

    def has_flag(self, flag: str) -> bool:
        return flag in self.flags


@dataclass
class Link:
    src: str
    src_pin: str | None
    dst: str
    dst_pin: str | None
    line: int = 0

    def key(self) -> tuple[str, str, str, str]:
        return (self.src, self.src_pin or "", self.dst, self.dst_pin or "")


@dataclass
class Bare:
    text: str
    line: int = 0


Entry = Union[Prop, Decl, Link, Bare]


@dataclass
class Section:
    name: str
    args: str = ""
    entries: list[Entry] = field(default_factory=list)
    line: int = 0
    meta: dict[str, Any] = field(default_factory=dict)

    def props(self) -> list[Prop]:
        return [entry for entry in self.entries if isinstance(entry, Prop)]

    def decls(self) -> list[Decl]:
        return [entry for entry in self.entries if isinstance(entry, Decl)]

    def links(self) -> list[Link]:
        return [entry for entry in self.entries if isinstance(entry, Link)]

    def bares(self) -> list[Bare]:
        return [entry for entry in self.entries if isinstance(entry, Bare)]

    def prop_map(self) -> dict[str, Prop]:
        return {prop.key: prop for prop in self.props()}

    def decl_map(self) -> dict[str, Decl]:
        return {decl.id: decl for decl in self.decls()}

    def header(self) -> str:
        return f"[{self.name}]" if not self.args else f"[{self.name} {self.args}]"

    def identity(self) -> tuple[str, str]:
        return (self.name, self.args.strip())


@dataclass
class Document:
    header: Header = field(default_factory=Header)
    sections: list[Section] = field(default_factory=list)

    def section(self, name: str, args: str | None = None) -> Section | None:
        for section in self.sections:
            if section.name == name and (args is None or section.args.strip() == args.strip()):
                return section
        return None

    def find_sections(self, name: str) -> list[Section]:
        return [section for section in self.sections if section.name == name]

    def ensure_section(self, name: str, args: str = "") -> Section:
        existing = self.section(name, args)
        if existing is not None:
            return existing
        section = Section(name, args)
        self.sections.append(section)
        return section

    def iter_decls(self) -> Iterator[tuple[Section, Decl]]:
        for section in self.sections:
            for decl in section.decls():
                yield section, decl

    def section_map(self) -> dict[tuple[str, str], Section]:
        return {section.identity(): section for section in self.sections}
