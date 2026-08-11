# Custom Models

Lighthouse can render a fully custom, fully animated character in place of a game model. The shipped proof is the **boot-sequence mascot**: with the boot sequence set to Default, the N64 cube that walks across the startup screen is replaced by a lighthouse character - custom mesh, custom textures, a custom rig, and two custom animations. Every resource involved is public, inside `lighthouse.o2r`:

```
models/lighthouse        the model container
models/lighthouse_GEO    its geometry layout
models/lighthouse_VTX    its vertex buffer
models/lighthouse_tex_0..2   its textures
anim/lighthouse_walk     hop cycle       (60 frames)
anim/lighthouse_shrug    lean-and-look   (160 frames)
```

This doc explains the model container those files use, how a model gets wired into the game, and - honestly - where the tooling for producing one stands today. The animation half is fully covered by public tools and has [its own guide](CUSTOM%20ANIMATIONS.md); the model half currently is not, and this doc is written so that gap is a known, closable one rather than a mystery.

---

## 1. The model container

A Banjo-Kazooie model is not one file but a family of sibling resources sharing a base path. This is the same shape Torch gives every extracted game model (`assets/model/ASSET_<id>_<label>` plus suffixes), and custom models use it identically:

| Entry | Type | Contents |
|---|---|---|
| `<name>` | `BKMO` | the model header: geo type, tri/vert counts, presence flags, bounding box + cull sphere, the display list (raw N64 command words), and the bone table |
| `<name>_VTX` | `OVTX` | vertex count + N64 `Vtx` records (16 bytes each: position, flag, UV, color/normal) |
| `<name>_GEO` | `OBLB` | the geometry layout: a small command tree (`BONE`, `LOADDL`, …) that binds ranges of the display list to bones |
| `<name>_tex_<i>` | `OTEX` | one texture: format, width, height, raw pixels in native N64 encoding |

Every resource starts with the standard 64-byte header (the same one every entry in `bk.o2r` carries), so the engine's factories pick them up like any extracted asset. The authoritative format documentation is the reading code: [`src/port/Resource/Importers/ModelFactory.cpp`](../../src/port/Resource/Importers/ModelFactory.cpp) with the structs in [`include/model.h`](../../include/model.h), and Torch's `docs/otr_format/` for the vertex/texture/blob bodies.

Three container details matter to anyone producing one:

- **Textures bind by path, not by embedding.** A `G_SETTIMG` in the display list whose address is `0xFF000000 | i` resolves at load time to `<model path>_tex_<i>` - which is also what makes each texture individually replaceable (and HD-replaceable through the `alt/` layer, see [TEXTURE PACKS.md](TEXTURE%20PACKS.md)).
- **The geo layout references display-list command *indices*.** Each `BONE` node names a bone's animation matrix and a `LOADDL` child pointing at where that bone's geometry starts in the command array. Command order is load-bearing.
- **The bone table is mandatory for anything animated.** The header ends with an animation scale and a flat bone list (rest position, id, parent). The animated-actor path dereferences the skeleton without a null check - a boneless model in an animated slot crashes the game. Bones are what [animation YAML](CUSTOM%20ANIMATIONS.md) addresses by id.

BK's skinning is **rigid**: there are no vertex weights, each triangle belongs to exactly one bone, and a "bend" is really several mesh segments riding a chain of bones. Joints go on natural geometry rings so the seams hide.

---

## 2. Wiring a model into the game

Two mechanisms, for two situations:

**Replace a vanilla model outright** - ship your container at the model's own path (`assets/model/ASSET_<id>_<label>` and siblings) in a mod `.o2r`. Later archives win per entry path, so the base model is shadowed, no code involved. The catch: every vanilla animation that plays on that actor still addresses the *original* skeleton's bone ids, so your bone table has to answer to them (the safe move is extending the original skeleton rather than inventing one).

A five-minute experiment makes both halves of that concrete - the ease *and* the catch. Swap Banjo for the blue Jinjo: copy the Jinjo's whole family out of `bk.o2r` (`assets/model/ASSET_3C0_JINJO_BLUE` plus its `_GEO`, `_VTX`, and four `_tex_*`/`_TLUT` pairs - eleven entries), rename each to the matching `assets/model/ASSET_34E_BANJOKAZOOIE_HIGH_POLY` name, `torch pack` the folder into an `.o2r`, drop it in `mods/`. It *works* - a blue Jinjo stands in Banjo's place, because the container is self-contained: a model finds its geometry, vertices, and textures through its own path, so a renamed family travels intact.

Then walk around, and there's the catch: the Jinjo's legs hold their T-pose while it slides along, and there's no Kazooie at all. The walk animation is addressing *Banjo's* bone ids - and although the Jinjo's skeleton happens to number its 31 bones 1-31 too, an id is just a number each model assigns to its own limbs. The ids the walk moves aren't the Jinjo's legs, and Kazooie's bones don't exist on it, so those channels animate nothing. No crash - unaddressed bones simply hold their rest pose. That's the skeleton contract in one image: a drop-in replacement is only a real replacement when its bone table answers the vanilla animations id-for-id.

**Re-point an asset id in code** - the port's hook:

```c
ResourceMgr_RegisterAssetOverride(assetId, "models/yourmodel");
```

This is how the mascot works: [`src/port/Enhancements/Cutscenes/IntroLogos.cpp`](../../src/port/Enhancements/Cutscenes/IntroLogos.cpp) re-points the cube's model (`0x3A6`) and its walk/shrug animations (`0x8F`/`0x90`) at the `models/lighthouse` and `anim/lighthouse_*` resources. The override lets the replacement live at its own path with its own extended skeleton and its own animations - nothing about the vanilla assets is touched. It's a code-side hook, so this route is a port contribution (a PR) rather than a drop-in mod.

Loose files become an archive with Torch's packer - it zips a staging folder verbatim, so the folder's layout is the archive's layout:

```
torch pack <staging-folder> <name>.o2r o2r
```

(That's exactly how `lighthouse.o2r` itself is built: the repo's `port/` directory holds the loose resources, and the `GeneratePortO2R` CMake target packs it.)

---

## 3. How the mascot was made - and what it teaches

The mascot started as an ordinary Blender model, exported through the [Fast64](https://github.com/HarbourMasters/fast64) plugin as an N64 F3D display list with textures. But a Fast64 export is *generic* N64 data, not a BK model - bridging the two took development tooling that reshaped the export into the container above:

- the display list flattened into one command stream, with vertex loads re-addressed into the concatenated `_VTX` buffer and texture loads rewritten to the `_tex_<i>` convention;
- the mesh **split into per-bone segments** (rigid skinning) - the tower cut at stripe rings into a short spine, each eye's pupil split out to its own bone;
- a skeleton hand-built by **extending the N-cube's own 20 bones** with five new ones (two spine, two pupils pivoting at their eyeball centres, one for the light-beam cone), so the vanilla actor's animated path stays happy while the new animations address the new bones;
- textures re-encoded to native formats, and the header's bounding box / cull sphere recomputed from the transformed vertices.

The lessons that generalize - the constraints any Fast64-to-BK conversion must honor:

| Constraint | Why |
|---|---|
| Force unlit materials, white vertex shade | this draw path loads no lights, so Fast64's lit materials render **black**; with lighting off the combiner's SHADE term comes from vertex color, and white makes it neutral |
| Reflective materials need geo type bit `0x4` | it makes the renderer set up the reflection matrix that `G_TEXTURE_GEN` env-mapping needs (the Jiggy's shine works this way - the mascot's glass copies it) |
| No vertex painting | the Fast64 fork doesn't export it, and BK models lean on it for baked shading - plan materials around textures and prim colors |
| Always ship a bone table | animated actors crash on boneless models (§1) |
| One bone per mesh chunk, joints on natural rings | rigid skinning (§1) |

---

## 4. Where the tooling stands

Being straight about the current state:

- **Fast64 does not support BK64.** The README says so where it links the plugin; the export gives you generic F3D geometry, with no BK container, no armature-to-bone-table path, and no vertex colors.
- **Torch has no model import.** Its BK64 `ModelFactory` only exports (code/header/binary); the geo-layout factory's modding import was stubbed and never finished. So unlike [textures (Retro)](TEXTURE%20PACKS.md) and [animations (Torch modding)](CUSTOM%20ANIMATIONS.md), there is **no public tool today** that turns a Blender export into a BK model container - the mascot's conversion was one-off development tooling, and we don't pretend otherwise.

What closes the gap, in order of value: an armature-aware BK64 export in the Fast64 fork (so rigs come from Blender instead of being hand-built), feeding a small, mechanical container converter - Torch being its natural home, next to the anim round-trip that already exists. The container reference in §1 and the constraint table in §3 are this doc's contribution to that work: everything a converter must produce and honor, learned the hard way once so it doesn't have to be learned again.

Until then: custom *animations* on vanilla models are fully moddable today, custom *textures* are fully moddable today, and a custom *model* is a source-level contribution best discussed with the team on the [Discord](https://discord.com/invite/shipofharkinian) first.
