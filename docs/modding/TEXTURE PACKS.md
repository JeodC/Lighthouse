# Making a Texture Pack

A texture pack is an `.o2r` archive that replaces any of the game's textures with your own artwork - same-resolution redraws or high-resolution (HD) upscales, from a single recolored egg to all 11,972 replaceable images. Players drop it into their `mods/` folder and it applies on top of the base game, toggleable in-game.

Packs are built end to end with **[Retro](https://github.com/HarbourMasters/retro)**, HarbourMasters' OTR/O2R generation tool. No scripts, no manual archive surgery: Retro extracts every texture to editable PNGs, you paint, Retro packs the changes. This guide walks through one pack end to end - recoloring the blue eggs green - and covers the format rules along the way.

---

## 1. Extract the game's textures

Open Retro and go **Create OTR / O2R → Replace Textures**. It asks whether you already have a texture replacement folder - answer **No** the first time, then:

1. Select your generated `bk.o2r` (the one your Lighthouse install plays from, so the entry names match exactly).
2. Pick an output folder.

Retro writes a `bk/` folder there containing every texture as a PNG, at the same relative path it has inside the archive, plus a **`manifest.json`** recording each texture's hash, format, and size. The US v1.0 archive yields **11,972 textures**.

Two rules about this folder:

- **Don't touch `manifest.json`.** It is how Retro knows a texture's native format and whether you changed it. Packing refuses to run without it.
- **Don't move or rename the PNGs.** The relative path *is* the archive entry name; a moved file no longer matches the manifest and is silently dropped at pack time.

(Re-running the extraction into the same folder deletes and recreates it - keep your edits elsewhere until they're final, or extract once and back the folder up.)

### How the files are named

Entry names follow the base game's own layout - `assets/<category>/ASSET_<id>_<label>` plus a suffix for each image the asset owns:

| Pattern | What it is | Example |
|---|---|---|
| `…_tex_<i>` | the i-th texture of a model or level | `assets/model/ASSET_491_NOTE_DOOR_tex_1` |
| `…_<frame>_<chunk>` | one chunk of a sprite frame | `assets/sprite/ASSET_36D_BLUE_EGG_0_0` |
| `…_TLUT` | the palette of a CI4/CI8 texture | `assets/sprite/ASSET_36D_BLUE_EGG_0_TLUT` |

The `ASSET_<id>` numbers are the game's own asset ids, so anything you find in the asset table or other docs maps straight onto a folder path.

---

## 2. Edit the textures

Edit any PNG in place with whatever editor you like. At pack time Retro compares every file's hash against the manifest and **stages only the ones you changed** - you never trim the folder down by hand, and unedited files cost nothing.

What the folder holds, by native format:

| Format | Count | | Format | Count |
|---|---:|---|---|---:|
| RGBA16 | 5,805 | | I4 | 266 |
| CI4 | 4,581 | | I8 | 195 |
| RGBA32 | 569 | | IA8 | 119 |
| CI8 | 437 | | | |

For the RGBA and I/IA formats there's nothing to know - the PNG is the image, edit it. The CI formats need a word.

### CI textures and their TLUTs

Nearly half the game's textures are **palettized** (CI4/CI8): the texture stores per-pixel *indices*, and a separate 16- or 256-entry palette - the `_TLUT` sibling - supplies the colors. Retro extracts the two halves as two PNGs and does not join them back up: the texture comes out as an indexed PNG with a neutral grayscale ramp for a palette, and the `_TLUT` PNG is a tiny strip holding the actual colors. So yes - some five thousand of your extracted PNGs open **black and white**. Nothing is damaged; you're looking at raw palette slots drawn without their colors.

Everything about working on them follows from one fact: **at pack time Retro reads back only the indices** - which slot each pixel uses - and in-game the colors still come from the TLUT. Three workflows cover every case:

- **Recoloring: edit the `_TLUT` strip and nothing else.** This is our egg example: the blue egg is sprite `ASSET_36D_BLUE_EGG`, and repainting the blue entries in its `_TLUT` PNGs green recolors the egg everywhere that sprite is drawn - a handful of pixels changed in total. The grayscale texture PNG never enters into it.
- **Redrawing in place: give the PNG its real palette first.** The TLUT strip *is* the texture's palette, in order - slot `N` in the PNG is color `N` in the strip. Import the strip's colors as the texture PNG's colormap (any palette-aware editor can; GIMP imports a palette from an image, Aseprite loads one from a file) and the grayscale art snaps into full color for you to paint on. Two rules: stay in indexed-color mode, and keep the slots where they are - paint with the existing entries rather than reordering or appending, because only the indices survive the round trip. (A CI texture saved un-paletted at its original size no longer matches its manifest format, and Retro skips it with a console note.)
- **Skipping palettes entirely: upscale it.** Resize the texture (2x is plenty) and paint in full RGBA. A resized texture takes the HD path below, where the image ships as true color and the palette machinery isn't consulted at all. For any substantial redraw this is the path to take - it's what the big community packs do wholesale.

### HD textures

Any texture may be replaced at **higher than native resolution** - redraw it at 2x, 4x, whatever your artwork needs, keeping the original aspect ratio (clean integer multiples keep the pixels aligned with how the game tiles the original). Retro detects the size change and re-encodes the image as raw RGBA32 with the scale factors recorded in the resource header; the engine then maps it onto the original texture coordinates at draw time. This works for every format - and since an upscaled CI texture is stored as RGBA32, HD redraws are free of the palette rules above.

This includes the **font sheets and HUD sprites**: the dialog and bold fonts are sprites like any other, and the port resolves their HD versions through the same replacement layer, so a texture pack can carry high-resolution fonts. (For fonts in *language packs* - which replace glyphs at SD inside the pack itself - see [LANGUAGE PACKS.md §5](LANGUAGE%20PACKS.md#5-fonts-and-glyphs); an HD sheet at `alt/assets/lang/<region>/…` composes on top of the pack's SD one automatically.)

---

## 3. Pack it

Back in Retro: **Create OTR / O2R → Replace Textures**, and this time answer **Yes**, selecting your `bk/` folder. Retro stages every changed texture; the bottom bar's **Finalize OTR / O2R** button opens the review list.

Before finalizing, turn on **"Prepend `alt/`"**. This prefixes every entry with `alt/`, and you want it for two reasons:

1. **It makes the pack toggleable.** Lighthouse resolves every texture through an alternate-assets layer: with **Alternate Assets** enabled (it is by default), the engine first looks for `alt/` + the normal path and falls back to the original when there's no replacement. Players can flip the whole pack on and off live from the mod menu, no restart.
2. **It fails safe.** If an entry is ever malformed, an `alt/` lookup just falls back to the stock texture. A replacement at the *bare* path instead shadows the original outright - if it doesn't parse, that texture is simply gone from the game.

Then **Finalize** and save as `<yourpack>.o2r`. Keep the `.o2r` extension - it's the only one the mod menu discovers. (Compression is optional and off by default; enabling it just makes the file smaller.)

---

## 4. Install it

Drop the `.o2r` anywhere under Lighthouse's `mods/` folder (it's scanned recursively; the folder is created next to `bk.o2r` on first run). The pack appears in the in-game **Mods** menu, enabled by default.

When multiple mods replace the same texture, **the later-loaded archive wins**. The menu lets you drag enabled mods into the order you want; a few placements are fixed around that:

| Loads | What |
|---|---|
| first | `bk.o2r` (base game) |
| then | enabled mods, in menu order |
| then | loose folders under `mods/` (unpacked archives, a development convenience) |
| last | `mods/~lang/` language packs (always active, never listed in the menu) |

Two special cases worth knowing: while a **romhack** is active, plain mods are skipped - a pack that should apply under romhacks too belongs in `mods/~shared/`, which always loads. And the **Alternate Assets** toggle governs every `alt/` entry from every mod at once, so a disabled toggle silently benches your pack - it's the first thing to check when textures "don't work".

---

## 5. Advanced: additive art and externally-named packs

*(Everything in this section needs a Retro build newer than v0.2.1 - the BK64 additions currently in source.)*

**Additive textures.** Some art the port looks for exists in no archive at all - the path is probed at draw time under `alt/`, and simply misses until a pack supplies it:

| Path shape | What it adds |
|---|---|
| `assets/sprite/<chunk>_<BLUE⎮GREEN⎮ORANGE⎮PURPLE⎮YELLOW>` | full-color art per Jinjo color, replacing the runtime tint of the shared gray head sprite |
| `assets/boldfont/<mask chunk>_<sphere hex id>` | a pre-textured bold-font glyph for one world sphere, replacing the runtime mask-plus-fill composite |
| `assets/lang/<region>/<asset path>` | a variant used only while that language is active |

Retro recognizes these shapes: name the file accordingly (or alias onto the path, below) and it derives the size and format from the entry the path decorates, staging it even though it matches no manifest entry. Additive art always ships as true-color raw - author it at a clean integer multiple of its template's size.

**Font glyph art.** Extraction records each font chunk's *drawn tile region* alongside its stored size, so glyph art authored at k× the tile - which is how font sheets are usually drawn - is padded onto the full stored canvas automatically, and bold-font glyphs additionally land on the aligned stride the game uploads with. Author at an integer multiple of either the tile or the full chunk and the packer sorts it out.

**`aliases.json`.** To repack a pack whose files aren't named by archive path (an emulator-era Rice-named pack, say), drop an `aliases.json` next to `manifest.json`, mapping each source image - path relative to the folder, extension included - onto its archive target; use a list when one image feeds several targets:

```json
{
  "Areas/Gobi's Valley/Banjo-Kazooie#00A0F146#0#3_all.png": "assets/level/ASSET_1476_GV_MEMORY_GAME_OPA_tex_4",
  "UI/Jinjos/Banjo-Kazooie#B36AB2E1#0#2_all.png": "assets/sprite/ASSET_7DF_UNNAMED_0_0_BLUE"
}
```

Copy the pack's files into the extraction folder, add the map, and the normal repack path handles the rest - aliased targets follow every rule above, additive shapes included. Building the map is the real work (matching a thousand-file pack onto asset paths is pack-specific research), but once made it's plain shareable data that anyone can reuse.

---

## Gotchas

- **Never put a plain PNG at a game texture path.** Game textures are binary resources; Retro converts your PNGs into them at pack time. A raw `.png` file placed in an archive by hand doesn't parse - harmless under `alt/` (falls back), asset-destroying at the bare path. If you build archives outside Retro, this is the mistake to avoid.
- **A CI texture saved un-paletted at its original size is skipped** (console note at pack time). Re-save as indexed, or resize it onto the HD path.
- **Extracting from a modded archive** is unsupported territory - Retro warns about this itself. Always extract from a clean `bk.o2r`.
- **Retro version:** written against Retro **v0.2.1**. The flow above (Replace Textures → manifest folder → Finalize) is the tool's core and stable across recent versions.
