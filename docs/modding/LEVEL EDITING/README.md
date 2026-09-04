# Level Editing

Everything *placed in* a Banjo-Kazooie map lives in one **setup file** per map (`assets/setup`), separate from the geometry you walk on (`assets/level`). Scenery, collectibles, enemies, cameras, lights, and the invisible markers that decide where the camera changes or which save flag a jiggy sets - all of that is the setup.

**Lightbulb**, the level explorer packaged with Lighthouse and launched with `Lighthouse --editor`, draws every one of them. It reads the real file through the game's own readers, so what it shows is what the game will do. A finished setup edit ships like any other mod: the changed `assets/setup/...` entry in an `.o2r`, later archive wins.

## What do you want to do?

| Goal | Page |
|---|---|
| Put scenery, a collectible or a light in the world | [Objects](OBJECTS.md) |
| Put an enemy, a character, a climbable pole in the world | [Actors](ACTORS.md) |
| Add a warp, or send an existing one somewhere new | [Warps](WARPS.md) |
| Change the camera in one spot, fence an enemy in, fix a jiggy's save flag | [Areas](AREAS.md) |
| Aim a camera, or change how it follows Banjo | [Cameras](CAMERAS.md) |
| Make something travel a route on its own | [Paths](PATHS.md) |
| Make a platform float, sink or move | [Objects](OBJECTS.md#scenery-that-moves) |
| Look up the file format, field by field | [Reference](REFERENCE.md) |

## Props and nodes

Everything in a setup is either a **prop** or a **node**.

A **prop** is something you can see: a piece of scenery, or a sprite such as a note or an egg. It has a position and not much else, and it does nothing.

A **node** is an invisible point with a *category* and an *id*. The category says what kind of thing it is - an actor spawn, a warp, a camera trigger - and the id says which one. Nodes are where all the behavior lives. Almost everything on the list above is a node.

## Before you move something

- **Collectibles read their save flag from the area they stand in**, not from themselves. Move a jiggy out of its flag volume and it starts setting a different flag, or none. See [Areas](AREAS.md).
- **A warp needs a matching entrance at the other end.** Sending a warp to a map with no entry point for it drops Banjo somewhere else in that map, or into the void. See [Warps](WARPS.md).
