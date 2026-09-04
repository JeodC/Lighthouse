# Reference

The format itself. The other pages cover what to do with it.

## The file

A setup file is a stream of tagged sections (`Setup.cpp` walks it with the decomp's own readers):

| Tag | Section |
|---|---|
| `0x01` | Objects - the cube grid holding all props and nodes |
| `0x02` | Unused, skipped by the reader |
| `0x03` | Camera nodes |
| `0x04` | Lighting |
| `0x00` | End of file |

One more shape exists: a cube can list its nodes as 12-byte records instead of the 20-byte ones described below, and the reader accepts either. Lightbulb reads what the game reads, so both open.

The file opens with the grid bounds: min and max cell coordinates on each axis. Cells are 1000x1000x1000 world units; everything in section 1 is bucketed into this grid so the engine only tests what's near Banjo.

## Node fields

A node is an invisible point with a category, an id, and a few parameters:

| Field | Meaning |
|---|---|
| position | whole-number x/y/z |
| radius | up to 511 units - a horizontal radius for the volume categories (the volume is a cylinder), a box half-extent for the contact categories; for an actor spawn it lands in the actor's `actorTypeSpecificField` as a free parameter |
| category | what kind of node this is |
| id | actor id, camera index, warp index, flag value... meaning set by the category |
| marker id | which actor kind can set this node off. Lightbulb doesn't read it yet |
| yaw | 0-359 degrees |
| scale | for an actor spawn, x100 = 1.0; other systems repurpose it (a camera-controller node's scale is read back as a distance, and one dynamic camera mode switches on it outright - `dynamicCam12.c`) |
| chain links | the node's own chain number (up to 4095) and that of the *next* node - the linked list the path system is built from. Every actor spawn resolves its chain and keeps the id as `secondaryId` |
| land/water setting | camera triggers only, four values - see [Areas](AREAS.md). Lightbulb doesn't read it yet |

## Categories

| Category | Name | Page |
|---|---|---|
| 3 | Warp (an exit) | [Warps](WARPS.md) |
| 4 | Contact trigger | [Areas](AREAS.md) |
| 6 | Actor | [Actors](ACTORS.md) |
| 7 | Enemy boundary | [Areas](AREAS.md) |
| 8 | Path | [Paths](PATHS.md) |
| 9 | Camera trigger | [Areas](AREAS.md) |
| A | Flag | [Areas](AREAS.md) |

Categories 0, 1, 2 and 5 are read and dispatched the same way, but every one of them lands on an empty case. They still take up a slot, which matters for the limit below. Retail ships two of them, both in `CCW_AUTUMN`, both at `(2366, 0, 2931)` with radius 50 and matching in every other field - a record the dispatch reads and does nothing with.

A record whose category is none of these is **not a node at all**. The same slot also stores scripted path waypoints, which the game tells apart by a flag, not by the category. Read as a node, a waypoint shows a junk category and id, sometimes landing on a real category value by chance. Lightbulb goes by the flag.

## Limits

**Nodes are re-ordered inside a cube.** On load, each cube's nodes are partitioned. Categories 6 to A and every script waypoint fill from the back of the array; categories 0 to 5 fill from the front. Only that front group is scanned when something touches a node. File order is not kept.

**A cube holds at most 31 nodes in that front group.** The count was a 5-bit field on the N64, so a 32nd wrapped it. The port widens the field and warns instead. Inert categories count toward it, so a cube can reach the limit without holding 31 working exits.

## Reading a setup in Lightbulb

The Layers menu maps to the categories above - Models, Sprites, Actors, Entries, Warps, Cam Markers, Enemies, Paths, Triggers, Flags, Cameras - plus the grid boundary box and a radius gizmo for the selected node (red for enemy boundaries, green for flag volumes).

Script waypoints appear in the Objects list as their own kind, with chain id and fraction; selecting one shows every field from [Paths](PATHS.md), grayed where the apply switches leave it inactive.

Cameras and the volumes that select them share a color (see [Cameras](CAMERAS.md)). Nodes with no in-game visual get stand-in models; anything still unaccounted for draws as a gray pyramid.
