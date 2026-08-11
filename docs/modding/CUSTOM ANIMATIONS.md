# Making an Animation Mod

The *Fast Swim* Banjo-Tooie backport was created by creating modified animation data and pointing the game to said data when both A and B are held while swimming. It's a good example because it's easy to demonstrate in Lighthouse and it's half of a *combined animation set*: all it does is overlay Banjo's leg bone kicks onto Kazooie's wing stroke. Here's how it's done.

---

## 1. Export the animations to YAML

```
torch modding export <baserom.z64> -s <lighthouse> -d <workdir>
```

This decodes every asset into an editable form and writes a `modding.yml` manifest listing them (the same export the language-pack guide starts with - but leave `dialog_pack` **off**; that flag is for language packs). Animations are one file per asset, and for this example we need two:

```
assets/anim/ASSET_3F_BSSWIM_DIVE_MOVE.yaml
assets/anim/ASSET_71_BSSWIM_DIVE_SLOW.yaml
```

Only assets listed in `modding.yml` are re-encoded from your yamls; everything else parses straight from the ROM. Delete the yamls you aren't editing and prune their `modding.yml` lines to match, leaving just yours.

---

## 2. The keyframe format

An animation YAML is a frame range plus a list of **elements** - one per (bone, channel) pair. The top of the paddle kick:

```yaml
ASSET_71_BSSWIM_DIVE_SLOW:
  StartFrame: 0
  EndFrame: 40
  Elements:
    - BoneIndex: 1
      TransformType: 0
      Data:
        - [1, 1, 0, -588]
        - [1, 1, 40, -588]
    ...
    - BoneIndex: 20
      TransformType: 1
      Data:
        - [1, 1, 0, 630]
        - [1, 1, 5, -870]
        - [1, 1, 12, -3813]
        - [1, 1, 25, -5715]
        - [1, 1, 35, -90]
        - [1, 1, 40, 630]
```

**`BoneIndex`** is a bone id in the target model's skeleton (a bone appears once per channel it animates). **`TransformType`** picks the channel:

| Type | Channel | Value means |
|---|---|---|
| 0 / 1 / 2 | rotation X / Y / Z | `value / 64` = degrees |
| 3 / 4 / 5 | scale X / Y / Z | `value / 64` = factor (so `64` = 1.0) |
| 6 / 7 / 8 | translation X / Y / Z | `value / 64` × the model's animation scale = world units |

Each **`Data`** row is one keyframe: `[smooth, smooth, frame, value]`. Frames run from `StartFrame` to `EndFrame`; the two leading flags control keyframe smoothing - the game uses `1, 1` (smooth interpolation) on virtually every key, and you should keep them for new keys too. Between keyframes the game interpolates; channels you don't supply sit at their defaults (rotation 0, scale 1.0, translation 0).

Read the bone-20 element above with the table: it's rotation Y, swinging from `630` down to `-5715` and back across the 40-frame cycle. Note that the playback *speed* isn't in this file: the game maps the frame range onto however long the action lasts, so keyframe positions set the motion's rhythm within the cycle, not its duration.

Two rules the engine enforces:

- **Keep one bone's elements contiguous.** The player walks the list accumulating channels and applies them when the bone id *changes* - interleaving bones (6, 20, 6) silently mangles the result. Export order is always grouped; preserve it when adding elements.
- **Don't retype ids you don't know.** Bone ids come from the model being animated. You learn them from code that already uses them, or by nudging one channel at a time. For Banjo's skeleton, [`FastSwim.cpp`](../../src/port/Enhancements/Backports/FastSwim.cpp) documents the leg chains outright: **right leg `6, 7, 10, 12, 14`, left leg `20, 21, 24, 26, 28`**, with 14 and 28 the feet. And ids are per-model: the same numbers mean different limbs on a different skeleton, which is why a straight model swap T-poses - see the Jinjo experiment in [CUSTOM MODELS.md §2](CUSTOM%20MODELS.md#2-wiring-a-model-into-the-game).

---

## 3. The edit: a harder kick

The thighs are the top of each leg chain - bones **6** (right) and **20** (left) - and the kick lives in their rotation-Y channels. Multiply every value in those two elements by 1.5, keeping the frames:

```yaml
    - BoneIndex: 20
      TransformType: 1
      Data:
        - [1, 1, 0, 945]      # was 630
        - [1, 1, 5, -1305]    # was -870
        - [1, 1, 12, -5720]   # was -3813
        - [1, 1, 25, -8573]   # was -5715
        - [1, 1, 35, -135]    # was -90
        - [1, 1, 40, 945]     # was 630
```

Same treatment for bone 6's rotation-Y element, and that's the whole mod: each thigh now swings to ~134° instead of ~89°. Values are just numbers to the engine - overshoot them and the legs windmill, which is a perfectly good way to confirm your mod is loading.

---

## 4. Build the mod

Re-encode the archive with your edits:

```
torch modding import o2r <baserom.z64> -s <lighthouse> -d <workdir>
```

The importer re-encodes your edited yamls, parses everything else straight from the ROM, and produces a full game archive. Your mod is just the changed entries, so pull them out and pack them alone:

1. A `.o2r` is a zip - open the freshly built archive with any archive tool and copy your entries (here `assets/anim/ASSET_71_BSSWIM_DIVE_SLOW`) into an empty staging folder, keeping the internal path.
2. Pack the staging folder with Torch:

   ```
   torch pack <staging-folder> mymod.o2r o2r
   ```

   `pack` zips the folder verbatim - whatever paths are inside the folder are the paths in the mod.

Drop `mymod.o2r` into Lighthouse's `mods/` folder. Archives load after the base game and **the later archive wins per entry path**, so your entry shadows the original. The in-game Mods menu toggles and reorders packs; see [TEXTURE PACKS.md §4](TEXTURE%20PACKS.md#4-install-it) for the full load-order rules.

Now test it: find water, dive, and paddle with A - the exaggerated kick is Banjo's ordinary slow swim.

---

## 5. Combined animation sets

Underwater movement is really a *pair* of animations on a single player skeleton. That's right, Banjo and Kazooie are joined for eternity, inseparable until Banjo-Tooie or some mod allows it. Banjo's paddle kick works the leg bones while Kazooie's wings are static, and Kazooie's wing stroke works the wing bones while Banjo's legs are static. Therefore, it's relatively simple to combine the two to get them to work in tandem. See [`FastSwim.cpp`](../../src/port/Enhancements/Backports/FastSwim.cpp) for the combined animation.

The enhancement does the combining in code, each frame, at independent speeds. But you can also build the combination as **pure data** - this is what the second yaml from step 1 is for. Both animations drive the same skeleton, and the wing stroke already carries a (near-static) element for every leg channel the kick animates - eighteen elements across the ten leg bones. So, in `ASSET_3F_BSSWIM_DIVE_MOVE.yaml`, replace the `Data` rows of each leg element with the rows from the matching `BoneIndex` + `TransformType` element in the kick yaml, **doubling every frame number** so the kick's 0-40 cycle spans the stroke's 0-80 range. Keep the stroke's element order - you're only swapping rows. Import and pack exactly as in step 4, shipping `assets/anim/ASSET_3F_BSSWIM_DIVE_MOVE`, and Banjo kicks while Kazooie strokes on the ordinary underwater B swim - no code, no enhancement.

The difference between data and code side: the data version locks the kick to the stroke's rhythm (one kick per stroke - to kick twice, keep the original frames and repeat the rows shifted by +40 instead of doubling), where the enhancement samples the kick at its own speed. And you won't swim any faster; the velocity boost is code, not animation.
