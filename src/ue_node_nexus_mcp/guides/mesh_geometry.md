# Mesh geometry authoring

Use `mesh_geometry_get` and `mesh_geometry_build` for native StaticMesh geometry.
This workflow produces editable derivative assets from preserved sources. Material
graphs remain authored with `.nexus`; geometry recipes are JSON objects retained in
the generated asset's package metadata and returned by `mesh_geometry_get`.

## Input and coordinate contract

- Input is exactly one `grid` or `source_asset` plus its `source_revision`.
- `grid.size=[width,depth,0]` uses centimetres; `grid.cells=[nx,ny,0]` creates a
  centred XY plane with upward normals. Each axis has 1..512 cells.
- Existing mesh inputs use editable LOD0 MeshDescription, including UV and material
  attributes. Results have one LOD and complex-as-simple collision for static use.
- All positions, deltas, masks and target edge lengths use asset-local centimetres.
  Apply the actor/component transform when converting world requests to this space.
- `max_triangles` defaults to 200000, maximum 500000. Compute operations have a
  20-second budget checked between operations/remesh passes. Mesh build/save time
  is reported separately within total operation time; this is not a hard real-time API.

## Recipe operations

`lattice`: dimensions `[nx,ny,nz]`, each 2..32; sparse `offsets` contain
`index=[i,j,k]` and `delta=[x,y,z]`. Unlisted controls remain at rest.
`interpolation` is `linear` or `cubic` (default). Cubic B-spline control offsets
are smoothly weighted; they are not point-interpolating handles.
The fitted control cage uses 1% padding. Query `lattice_dimensions` with
`mesh_geometry_get` to retrieve rest controls. Flat index is `k+nz*(j+ny*i)`.

`remesh`: `target_edge_length`, `iterations` 1..20, `lock_boundary` default true.
UV seams and material/group boundaries are constrained; surface projection retains
the deformed shape. Optional `region={min:[...],max:[...]}` freezes outside edges.
`automatic=true` runs only when max edge exceeds 1.5 times target or max triangle
aspect exceeds 3. The chosen target and passes may leave constrained edges longer
than target; inspect returned quality rather than assuming a guaranteed edge length.

`noise`: `amplitude`, `scale`, `octaves` 1..8, integer `seed`, `persistence` 0..1,
`direction` z/normal. `sampling_offset` shifts coordinates before noise sampling,
useful for adjacent translated terrain patches. Consistent sampling also requires
matching mesh orientation/scale when converting from world space.

Lattice and noise accept `lock_boundary` and an ellipsoidal smooth `mask`:
`center`, positive `radius`, optional `invert`. The mask is evaluated in the
current input coordinates of that operation. Invert protects a centre platform.

Order operations explicitly: lattice -> remesh -> noise is a useful terrain recipe.
Noise beyond the sampling density cannot be represented: choose remesh edge lengths
small enough for the shortest desired detail scale.

## Publication, retries and editing

1. Read source revision, or describe a grid. Keep the complete recipe as authoring data.
2. Call `mesh_geometry_build(output_asset, recipe, dry_run=true)` for evaluated geometry
   statistics and output conflict checks. No output asset is created.
3. Execute with `dry_run=false, save=true`. Verify `ok`, `ready`, `saved` and quality.
4. Assign the output mesh through existing actor/component operations or scene editing.
   Undo of scene assignments restores the previous mesh reference; source assets remain intact.

Existing outputs are preserved. An identical recipe retry reuses the output only
when its recorded geometry revision still matches. A changed recipe or user-edited
output reports conflict: select a new output path, rebuild from the original source,
then replace the scene reference. This prevents cumulative noise and repeated deformation.
Save failure explicitly retains `applied=true, saved=false`; replay can retry saving.
No full-text height/vertex merge is implied by storing the recipe.

Checks reject non-finite/degenerate geometry, detected self intersections and reversed
triangle orientations during lattice/noise deformation. Boundary/seam constraints and
triangle budgets are enforced. Use smaller deformation steps for rotations exceeding
90 degrees between corresponding triangle normals.

Every operation returns per-stage triangle/vertex counts, edge/aspect statistics and
timings. `[NexusGeometry]` records requests, stages and rejections in the editor log.
