# Level Editing: The Setup File

A Banjo-Kazooie map is two kinds of asset: the **level models** (`assets/level/`, the geometry you walk on) and one **setup file** per map (`assets/setup`). The setup file is everything *placed in* the world: scenery props, collectible sprites, actor spawns, camera definitions, point lights, and the invisible marker nodes. Those nodes decide where the camera changes, where enemies may roam, and which save flag a jiggy sets. This doc is the reference for what's in that file and what each piece does.

`torch modding export` writes each setup out as a readable yaml, but nothing imports it back - editing the yaml does nothing. **Lightbulb**, the level explorer packaged with Lighthouse and launched with `Lighthouse --editor`, is being built for this work and already draws everything below. A finished setup edit ships like any other mod: the changed `assets/setup/...` entry in an `.o2r`, later archive wins.

---

## 1. The file

A setup file is a stream of tagged sections ([`Setup.cpp`](../../editor/src/importer/Setup.cpp) walks it with the decomp's own readers):

| Tag | Section |
|---|---|
| `0x01` | Objects - the cube grid holding all props and nodes |
| `0x02` | Unused, skipped by the reader |
| `0x03` | Camera nodes |
| `0x04` | Lighting |
| `0x00` | End of file |

The file opens with the grid bounds: min and max cell coordinates on each axis. Cells are 1000x1000x1000 world units; everything in section 1 is bucketed into this grid so the engine can query "what's near Banjo" without walking the whole map.

## 2. Props: what you can see

Three record kinds share the objects section, 12 bytes each, told apart by two flag bits ([`prop.h`](../../include/prop.h)):

| Kind | What | Fields |
|---|---|---|
| **Model prop** | static scenery - trees, fences, furniture | model id (+ `0x2D1` = the asset id), position, yaw and roll (a byte each, x2 = degrees), scale (x100 = 1.0, so 2.55 is the ceiling) |
| **Sprite prop** | billboard - notes, eggs, feathers, grass tufts | sprite id (+ `0x572` = the asset id), position, scale, mirror flag, animation frame, and three 3-bit RGB-subtract values that tint the sprite |
| **Actor prop** | a spawned actor's runtime record | position and flags; the marker pointer is filled at runtime |

Model props are pure decoration with collision - no behavior. Anything that *does* something is an actor, and actors are placed by nodes, not props.

## 3. Nodes: what you can't

A node ([`NodeProp`](../../include/prop.h), 20 bytes) is an invisible point with a category, an id, and a few packed parameters:

| Field | Meaning |
|---|---|
| position | s16 x/y/z |
| radius | 9 bits - a horizontal radius for the volume categories (the volume is a cylinder), a box half-extent for the contact categories; for an actor spawn it lands in the actor's `actorTypeSpecificField` as a free parameter ([`actor_cubepropsystem.c:1530`](../../src/core2/actor_cubepropsystem.c#L1530)) |
| category | 6 bits - what kind of node this is |
| id | 16 bits - actor id, camera index, warp index, flag value... meaning set by the category |
| marker id | which actor kind can set this node off - see the contact triggers in [#5](#5-warps-and-contact-triggers-categories-3-and-4) |
| yaw | 9 bits, in degrees |
| scale | 23 bits - for an actor spawn, x100 = 1.0 ([`actor_cubepropsystem.c:1531`](../../src/core2/actor_cubepropsystem.c#L1531)); other systems repurpose it (a camera-controller node's scale is read back as a distance, and one dynamic camera mode switches on it outright - [`dynamicCam12.c:238`](../../src/core2/nc/dynamicCam12.c#L238)) |
| chain links | two 12-bit values: the node's own chain id and the chain id of the *next* node - the linked list the path system is built from ([#8](#8-paths)). Every actor spawn resolves its chain and keeps the id as `secondaryId` ([`gccube.c:1279`](../../src/core2/gccube.c#L1279)) |
| volume flag | 2 bits, camera triggers only - see [#6](#6-the-three-volume-systems) |

Seven categories mean something:

| Category | Name | What it does |
|---|---|---|
| 3 | Warp | touch its box and the warp function named by id runs - see [#5](#5-warps-and-contact-triggers-categories-3-and-4) |
| 4 | Contact trigger | touch its box and the trigger function named by id runs - see [#5](#5-warps-and-contact-triggers-categories-3-and-4) |
| 6 | Actor | spawns the actor named by id - see [#4](#4-actor-nodes-and-the-invisible-actors) |
| 7 | Enemy boundary | a cylinder volume enemies are bound to - see [#6](#6-the-three-volume-systems) |
| 8 | Path | a spline control point, strung to the next by its chain links - see [#8](#8-paths) |
| 9 | Camera trigger | a cylinder volume that picks the camera - see [#6](#6-the-three-volume-systems) |
| A | Flag | a cylinder volume that tells collectibles which save flag they are - see [#6](#6-the-three-volume-systems) |

A record whose category is none of these is usually **not a node at all**. The same 20-byte slot also stores scripted-path waypoints ([#8](#8-paths)), whose bytes are a float and packed speed/animation fields. Read as a node they produce junk categories and ids, which is why the contact dispatch treats unknown categories as no-ops ([`warp_dispatch.c:582`](../../src/core2/map/warp_dispatch.c#L582)). Across all 129 retail setups the real categories hold 3928 actor, 3390 camera trigger, 2244 path, 2087 enemy boundary, 832 contact trigger, 400 warp, and 236 flag nodes.

## 4. Actor nodes, and the invisible actors

A category-6 node spawns its id as an actor, at the node's position and yaw. Most need no explanation - a Grublin node puts a Grublin there. The ones that do are the actors with no model, which exist to give a *place* behavior:

**Climb markers.** A climbable tree or pole is a pair of nodes: `0x26` (`ACTOR_26_CLIMB_BASE`) at the bottom, and a top marker - `0x27` or `0x28` - above it. The base actor finds the nearest top of either kind ([`climbBase.c:28`](../../src/core2/ch/climbBase.c#L28)) and the two ends define the climbable span; the base node's radius field is the climb radius. The two top kinds differ in one thing: reach the top of a `0x28` climb moving upward and Banjo climbs out onto whatever is above ([`bs/climb.c:171`](../../src/core2/bs/climb.c#L171)). A `0x27` top just ends the pole. Nothing renders for any of this in game - Lightbulb substitutes pole markers.

**Entry points.** Every map transition carries an *exit id*, and on load the game maps it to an entry-node id: exit 1 -> node `0x01`, 4-13 -> `0x76`-`0x7F`, and so on ([`func_803084F0`](../../src/core2/gccube.c#L2274)). Banjo is then placed at that node, facing its yaw ([`ba_lookdir.c:52`](../../src/core2/ba/ba_lookdir.c#L52)). If the requested entry node is missing, every exit id is tried in order as a fallback. If none resolves, the player is left at the void default (the port logs a warning). A second table maps the same exit id to an entry *camera* node in the `0x80`-`0x8D` range ([`camera_nodemanager.c:20`](../../src/core2/nc/camera_nodemanager.c#L20)), where the load also spawns the camera-controller actor `0x66`. So a working map entrance is a node pair: one for Banjo, one for the camera.

**Walk-in nodes** (`0x184`-`0x186`). When Banjo exits within 500 units of one (1000 for `0x186`), the transition plays the walk-out with the node's yaw and scale as parameters, or walks him to the node outright for `0x184` ([`cutscene_skip.c:235`](../../src/core2/map/cutscene_skip.c#L235)).

**Map trigger nodes.** Ids `0x018`, `0x18A`, `0x18B`, `0x192`-`0x194` each have a handler pair in a per-map trigger table ([`cutscene_triggers.c:33`](../../src/core2/map/cutscene_triggers.c#L33)). `0x18B`, for instance, watches for the player dropping below the node's height. Lightbulb has stand-in models for these and the entry markers ([`ActorOverrides.cpp`](../../editor/src/ActorOverrides.cpp)).

## 5. Warps and contact triggers (categories 3 and 4)

These two fire on **touch**. Each frame, an actor's marker is tested against the nearby nodes whose *marker id* field names its kind, using the node's radius as a box half-extent on all three axes. On contact the node is dispatched by category ([`func_80307CA0`](../../src/core2/gccube.c#L2050) -> [`func_80334448`](../../src/core2/map/warp_dispatch.c#L553)). The marker id is what lets a warp answer to Banjo while an orange pad answers to a thrown orange.

**A warp node's id indexes a function table** of about 130 named handlers ([`sWarpFunctions`](../../src/core2/map/warp_dispatch.c#L317)): every door, hut, lobby link, and Nintendo-logo transition is one entry - `warp_mmEnterMumbosHut`, `warp_lairEnterCCLobbyFromCCLevel`, and so on. The handler carries its destination map and exit id, which is why the same warp id always leads to the same place (romhacks re-point them through the BKCF config instead). A global-timer stamp on the node keeps a warp from re-firing every frame.

**A contact trigger's id indexes a second table** ([`sRadiusTriggers`](../../src/core2/map/warp_dispatch.c#L398)), and the retail entries group cleanly:

| Ids | What |
|---|---|
| `0x16`-`0x29` | start the area camera - dynamic camera mode 12 - for area id `- 0x16` ([`dynamicCam12.c:217`](../../src/core2/nc/dynamicCam12.c#L217)) |
| `0x2A` | end the area camera ([`dynamicCam12.c:211`](../../src/core2/nc/dynamicCam12.c#L211)) - the most-placed trigger in the game, 458 of the 832 |
| `0x46`-`0x4B` | Treasure hunt step progress (TTC's x marks the spot game) |
| `0x4C`, `0x4D` | Mumbo transformation boundary - warn, then detransform |

Everything else in the table is a no-op. Banjo's Backpack calls category 4 "magic boundary or camera trigger", which is these ids exactly.

## 6. The three volume systems

Categories 7, 9, and A have nothing to show at all. All three work the same way: at map load the node registers a **cylinder** (its position and horizontal radius) into a per-map list under its id ([`func_803303B8`](../../src/core2/actor_cubepropsystem.c#L1540)). Cylinders sharing an id whose circles touch in X/Z merge into one **zone** ([`gccube.c:1481`](../../src/core2/gccube.c#L1481)), so a zone covering an odd-shaped area is just several overlapping nodes with the same id.

### Camera triggers (category 9)

The id is an index into the camera-node section ([#7](#7-camera-nodes)). Every frame the player's position is tested against the zones ([`func_80306EF4`](../../src/core2/gccube.c#L1805)): inside a cylinder's radius horizontally, with the cylinder's center Y within 150 units of the player (the node's 2-bit volume flag can lift the vertical check). The winning zone's camera node takes over. Standing in no zone means the ordinary free camera, and flying skips zone lookup entirely ([`ba_health.c:43`](../../src/core2/ba/ba_health.c#L43)).

Two more switches on this system: each camera index has a runtime enable flag, all on at load ([`gccube.c:755`](../../src/core2/gccube.c#L755)). Actors and cutscenes can turn a zone off and back on. The 2-bit volume flag also classes a cylinder as applying only to certain lookups. The query runs in a different class while Banjo is in water ([`playerutils.c:276`](../../src/core2/playerutils.c#L276)), so one spot can use different camera zones swimming and on land.

To answer "why did the camera change *there*": find the category-9 node whose cylinder you stepped in, read its id, and look that id up in the camera section. The node's type is what the camera did.

### Enemy boundaries (category 7)

An enemy looks up which boundary zone it's standing in and records it ([`actor_array.c:967`](../../src/core2/actor_array.c#L967)), and its movement code refuses any position outside that zone ([`actor_array.c:1527`](../../src/core2/actor_array.c#L1527)). This is why an enemy gives up chasing at an invisible line - it reached the edge of its boundary cylinders.

### Flag volumes (category A)

The id here is a value collectibles read to learn *which one they are*. A jiggy locates itself in a flag zone and its save-flag id is the zone's value + 1 ([`jiggy.c:45`](../../src/core2/ch/jiggy.c#L45)); an empty honeycomb subtracts `0x63` ([`honeycomb.c:56`](../../src/core2/ch/honeycomb.c#L56)). A Mumbo token subtracts 199 ([`mumbotoken.c:53`](../../src/core2/ch/mumbotoken.c#L53)). The consequence for an editor: move a jiggy without its flag volume and it starts setting a different save bit - or none.

## 7. Camera nodes

Section 3 is the list of cameras the trigger zones select between, four types ([`camera.h`](../../include/core2/camera.h)):

| Type | Name | Fields | Behavior |
|---|---|---|---|
| 1 | Fixed | position, horizontal/vertical speed, rotation, acceleration, pitch/yaw/roll, flags | a camera mounted at its position, turning to follow Banjo at the authored speeds |
| 2 | Static | position, pitch/yaw/roll | position and rotation both authored; nothing tracks Banjo. Not selected by trigger zones - scripted code cuts to one by index (`ncStaticCamera_setToNode`): the parade, Furnace Fun, the Grunty fight, molehill lessons. Lightbulb also uses these to aim its viewport on load |
| 3 | Dolly | the fixed-camera fields plus a close and far distance | follows Banjo along the line to him, held between the two distances |
| 4 | Random | one flag | selects a fallback camera behavior |

Types 1 and 3 carry three flags ([`camera.h`](../../include/core2/camera.h)): **hits** runs line-of-sight correction so the camera doesn't sit inside geometry, **vfix** (type 1 only) holds the authored height instead of tracking Banjo's, and **bee** marks the node as still applying during the bee transformation. A zone whose node lacks it is ignored while flying as the bee ([`ba_camera.c:87`](../../src/core2/ba/ba_camera.c#L87)).

The zone-to-camera handoff is [`ba_camera.c:73`](../../src/core2/ba/ba_camera.c#L73): type 3 and type 1 nodes drive the dynamic camera into their states. A zone pointing at an invalid node falls back to the free camera.

## 8. Paths

The 20-byte node slot actually stores **two record types**, and the path system is built from both. Category-8 nodes are the *control points* - positions, strung together by their chain links. Interleaved with them are *scripted waypoints* (Banjo's Backpack calls them SNodes). Same slot, different layout: a float for the fraction along the path, plus packed speed, animation id, and pause time for that leg.

At map load the path builder walks every chain ([`spline_pathfollow.c:525`](../../src/core2/spline_pathfollow.c#L525)): it collects each chain's records in link order, splits control points from script records, and bakes the control points into a spline with the script entries alongside. An actor spawned on a chain binds to its path through the chain id it kept at spawn. That is how a Snippet patrols, how platforms ride their loops, and how the per-leg speed and pause values reach them.

## 9. Lighting

Section 4 is a list of point lights: position, an inner and outer fade radius, and an RGB color. These recolor vertices around them (`gclights_recolor_vertices`) - the warm pools around torches are this. No relation to model rendering (models load no lights; see [CUSTOM MODELS.md](CUSTOM%20MODELS.md)).

## 10. Reading a setup in Lightbulb

Lightbulb loads the real file through the game's own readers, so what it shows is what the game will do. The Layers menu maps to the categories above - Models, Sprites, Actors, Entries, Warps, Cam Markers, Enemies, Paths, Triggers, Flags, Cameras - plus the grid boundary box and a radius gizmo for the selected node (red for enemy boundaries, green for flag volumes). Camera gizmos and camera-trigger cubes are colored by camera node index using Rare's own eight-color debug palette (the `RGB_VALUES` table retail still carries in `src/core1/debugtext.c`). A camera and every volume that selects it share a color. Nodes with no in-game visual get stand-in models; anything still unaccounted for draws as a gray pyramid - a node the editor has no better picture for yet.
