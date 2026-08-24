# Custom Models

This doc explains the model container, how a model gets wired into the game, and what produces one. The animation half has [its own guide](CUSTOM%20ANIMATIONS.md); [#3](#3-building-a-model) covers the Blender plugin that builds the model half.

---

## 1. The model container

A Banjo-Kazooie model is not one file but a family of sibling resources sharing a base path. This is the same shape Torch gives every extracted game model (`assets/model/ASSET_<id>_<label>` plus suffixes), and custom models use it identically:

| Entry | Type | Contents |
|---|---|---|
| `<name>` | `BKMO` | the model header: geo type, tri/vert counts, presence flags, bounding box + cull sphere, the display list (raw N64 command words), and the bone table |
| `<name>_VTX` | `OVTX` | vertex count + N64 `Vtx` records (16 bytes each: position, flag, UV, color/normal) |
| `<name>_GEO` | `OBLB` | the geometry layout: a small command tree (`BONE`, `LOADDL`, ...) that binds ranges of the display list to bones |
| `<name>_tex_<i>` | `OTEX` | one texture: format, width, height, raw pixels in native N64 encoding |

Every resource starts with the standard 64-byte header (the same one every entry in `bk.o2r` carries), so the engine's factories pick them up like any extracted asset. The authoritative format documentation is the reading code: [`src/port/Resource/Importers/ModelFactory.cpp`](../../src/port/Resource/Importers/ModelFactory.cpp) with the structs in [`include/model.h`](../../include/model.h), and Torch's `docs/otr_format/` for the vertex/texture/blob bodies.

Three container details matter to anyone producing one:

- **Images bind by path, palettes don't.** A `G_SETTIMG` in the display list whose address is `0xFF000000 | i` resolves at load time to `<model path>_tex_<i>` - which is what makes each image individually replaceable (and HD-replaceable through the `alt/` layer, see [TEXTURE PACKS.md](TEXTURE%20PACKS.md)). A CI texture's palette instead stays inside the model's own texture blob, where the display list points segment 2 at a byte offset. `ModelFactory` never reads the `_tex_<i>_TLUT` resources Torch writes beside a model, so repainting one of those changes nothing in game. To recolor a model's palettized texture you redraw it larger and let it take the HD path, where no palette is involved.
- **The geo layout references display-list command *indices*.** Each `BONE` node names a bone's animation matrix and a `LOADDL` child pointing at where that bone's geometry starts in the command array. Command order is load-bearing.
- **The bone table is mandatory for anything animated.** The header ends with an animation scale and a flat bone list (rest position, id, parent). The animated-actor path dereferences the skeleton without a null check - a boneless model in an animated slot crashes the game. Bones are what [animation YAML](CUSTOM%20ANIMATIONS.md) addresses by id.

BK's skinning is **rigid**: there are no vertex weights, and each *vertex* follows exactly one bone ([`rendernormal.c:30`](../../src/core2/model/rendernormal.c#L30) is the whole of it - one matrix per entry, applied to a rest position, written into every vertex that shares it). A seam closes by a triangle's corners following different bones, not by blending, so a "bend" is really mesh segments riding a chain of bones. Joints go on natural geometry rings so the seams hide.

---

## 2. Wiring a model into the game

Ship your container at the model's own path (`assets/model/ASSET_<id>_<label>` and siblings) in a mod `.o2r`. Later archives win per entry path, so the base model is shadowed, no code involved. The catch: every vanilla animation that plays on that actor still addresses the *original* skeleton's bone ids, so your bone table has to answer to them (the safe move is extending the original skeleton rather than inventing one).

A five-minute experiment makes both halves of that concrete - the ease *and* the catch. Swap Banjo for the blue Jinjo: copy the Jinjo's whole family out of `bk.o2r` (`assets/model/ASSET_3C0_JINJO_BLUE` plus its `_GEO`, `_VTX`, and four `_tex_*`/`_TLUT` pairs - eleven entries), rename each to the matching `assets/model/ASSET_34E_BANJOKAZOOIE_HIGH_POLY` name, `torch pack` the folder into an `.o2r`, drop it in `mods/`. It *works* - a blue Jinjo stands in Banjo's place, because the container is self-contained: a model finds its geometry, vertices, and textures through its own path, so a renamed family travels intact.

Then walk around, and there's the catch: the Jinjo's legs hold their T-pose while it slides along, and there's no Kazooie at all. The walk animation is addressing *Banjo's* bone ids - and although the Jinjo's skeleton happens to number its 31 bones 1-31 too, an id is just a number each model assigns to its own limbs. The ids the walk moves aren't the Jinjo's legs, and Kazooie's bones don't exist on it, so those channels animate nothing. No crash - unaddressed bones simply hold their rest pose. That's the skeleton contract in one image: a drop-in replacement is only a real replacement when its bone table answers the vanilla animations id-for-id.

Loose files become an archive with Torch's packer - it zips a staging folder verbatim, so the folder's layout is the archive's layout:

```
torch pack <staging-folder> <name>.o2r o2r
```

---

## 3. Building a model

[Fast64](https://github.com/HarbourMasters/fast64)'s HarbourMasters fork carries a **BK64** game mode that reads and writes the container in #1 - it emits the resource family itself, so nothing sits between Blender and the archive.

Install the fork (zip the repo, then Blender's *Preferences -> Add-Ons -> Install*) and set the game to BK64 in the Fast64 tab; the model and animation tools appear in the 3D view sidebar under a BK64 tab. It reads a model or skeleton out of a resource family or a `.bin` - mesh, UVs, vertex colors, textures, materials, collision, collision shapes, mesh lists, and the armature with its original bone ids - and writes any of that back to either container. Animations go both ways too, matched by bone id, one action at a time or every action on the rig at once.

Import the original model first and #2's catch answers itself: the bone table arrives with its ids intact, and the animation scale comes with it, so the vanilla animations find the right limbs and move them the right distance. Fit your mesh to that armature, set Resource Path to the vanilla model's own path, export, and pack the export folder with `torch pack` as above.

The plugin's [own README](https://github.com/HarbourMasters/fast64/blob/main/fast64_internal/hm64/bk64/README.md) is the reference for the Blender side - materials and draw layers, the two rigging methods, geo nodes, collision, animated textures, and the two things that don't survive a round trip.

**Torch has no model import**, so the `torch modding export` flow the [animation](CUSTOM%20ANIMATIONS.md) and [dialog](CUSTOM%20DIALOG.md) guides start from has no model path. Models come through Blender, and Torch's only job here is `torch pack`.