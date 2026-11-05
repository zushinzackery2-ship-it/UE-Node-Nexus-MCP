# Material instance parameters

Material instance overrides keep the parameter name, association and index.
Global parameters use `[scalar]`, `[vector]`, `[texture]`, `[switch]` and
`[component_mask]`. Layer and blend parameters use the same sections with a
zero-based index, for example `[scalar layer 0]` or `[vector blend 1]`.
The same parameter name can appear in different scopes.

```ini
[scalar]
Roughness = 0.5

[scalar layer 0]
Roughness = 0.25

[scalar layer 1]
Roughness = 0.75

[component_mask]
Channels = (R=True,G=False,B=True,A=False)
```

Omitting an override restores inheritance in that scope. Scalars and vector
channels require finite numbers; vectors and component masks require all four
RGBA channels. Switches and masks accept `True`/`False` or `1`/`0`. The native
writer checks that the parent exposes the selected parameter before applying it.
