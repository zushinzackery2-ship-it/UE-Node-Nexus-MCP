"""Value contracts of Niagara's native type aliases, scoped to Niagara values."""

KINDS = frozenset(("niagara_system", "niagara_emitter"))
VALUE_TYPES = {"vector": "Vector3f", "vector2": "Vector2f", "vector4": "Vector4f",
               "color": "LinearColor", "quat": "Quat4f"}


def value_type(type_name):
    # Niagara Color is FLinearColor and its vectors/quaternions contain floats;
    # the identically named engine property types can have a different layout.
    return VALUE_TYPES.get(type_name.lower(), type_name) if isinstance(type_name, str) else type_name
