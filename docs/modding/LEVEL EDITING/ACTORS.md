# Actors

Anything that moves, talks, hurts you or can be collected is an **actor**, and an actor is placed by a node, not a prop.

## Placing one

A category-6 node spawns its id as an actor, at the node's position and yaw: a Grublin node puts a Grublin there. Two of the node's other fields are handed to the actor as free parameters, and what they mean is up to that actor:

- **radius** in the actor's `actorTypeSpecificField`
- **scale** is x100 = 1.0, the same as a prop's. Leave it at 0 for default size

The node's own chain number also reaches the actor, which is how a path finds its rider. See [Paths](PATHS.md).

Whether an actor appears at all depends on the map registering its kind. Each world registers the kinds it supports as it loads, and only then does the map spawn its nodes. Lightbulb's *Unregistered actors* layer shows spawns whose map never registers them. Lighthouse globalizes actors, like romhacks do.

**Every actor in the map spawns at load**, all cubes at once. A crowded map is crowded from the first frame. Actors will be culled and may not update until they are within the player's range.

## What the free parameter is for: Boggy's slalom

Freezeezy Peak builds Boggy's course out of 78 actor nodes that are identical but for one number. Two actor ids, `0x161` and `0x162`, are the left and right posts of a gate; each is placed 39 times, and the only field that differs between one flag and the next is the radius - **1 on the first pair, counting up to 39 on the last**.

That number is the gate's place in the race. A flag hands it straight to the race code when somebody passes between the posts, which records the gate as the player's or Boggy's and spawns the gates four places further along, so the course streams in ahead of you rather than existing all at once.

The race also counts its flags before it will run: it waits for all 78 to register, and holds if any are missing. The parameter is therefore carrying the entire structure of the race.

## Actors that find each other: Mr. Vile's game

Some actors are placed knowing that another actor will come looking for them. Bubblegloop Swamp's Mr. Vile arena (`BGS_MR_VILE`) is three actor ids:

| Node | Where | How many |
|---|---|---|
| `0x139` `YUMBLIE` | a grid across the floor, roughly 200 units apart, all at height -100 | 30 |
| `0x138` `VILE_GAME_CTRL` | `(-397, 125, -1257)`, off to one side | 1 |
| `0x13A` `MR_VILE` | `(0, 0, 0)`, facing yaw 180 | 1 |

Every one of the 30 holes is the same record: radius 50, yaw 0, no chain, no parameter. On its first update each one searches the live actor list for the **nearest** `VILE_GAME_CTRL` and keeps that as its controller; when it pops up it hands the controller its own position. The controller never reads the setup - it learns the board from whoever reports in. So adding a hole is placing one more `0x139` anywhere on the floor.

The rest is in code. No more than 12 holes are up at once, and whether a hole comes up as a Yumblie or a Grumblie is rolled as it rises: never a Grumblie in round one, three in ten in round two, half in round three, and the same climb again through the rematch set. A hole waits 1 to 10 seconds underground, holds 5 to 10 above, and sits out 10 to 20 after being eaten. Mr. Vile himself goes for the nearest piece of the wanted kind, and when there is none he wanders a box 500 units either side of his own node - a smaller box than the grid, which reaches 800. The camera for the game is computed rather than taken from a camera node.

## A number that indexes a table: the jigsaw podium

Every jiggy podium in Gruntilda's Lair is one actor node - `0x3B7`, or `0x3BC` for the second style of podium - and its radius is the **puzzle number**, 1 to 11. Eleven nodes across eight lair maps carry the whole set.

The number is an index into a table (`jigsawpicture.c`) holding three things per puzzle: the cost in jiggies, how much room the save file gives the count, and which save flag it is. The costs run 1, 2, 5, 7, 8, 9, 10, 12, 15, 25 and 4. A romhack's game config can override every row- that table is what Lightbulb's **Game Config** window edits.

The number does two more jobs. For puzzles 1 to 9, finishing the picture sets level flag `number + 0x1B`, which is the flag that world's door watches; 10 opens Grunty's door and 11 grants double health instead. The order the pieces fly in is seeded from it, so each picture assembles the same way every time.

A number outside 1 to 11 has no row, and the podium treats it as costing nothing. A twelfth world needs a twelfth row in the table before it needs a podium.

**Changing one.** Say Mumbo's Mountain should cost 4 jiggies instead of 1. None of that change is in the setup - the podium node stays exactly as it is, radius 1 still meaning row 1 - and it lands in three other places instead.

The cost is row 1 of the table, edited in Lightbulb's **Game Config** window. The same row says how much room the save file gives the count, and row 1 gives it one bit: a count of 4 needs three. Lightbulb marks the field *too small* when they disagree. Widening it where it sits is not an option, since the counts are packed shoulder to shoulder from `0x5D` to `0x82` and three bits at `0x5D` overwrite Treasure Trove Cove's. The last flag the game names is `0x123` and the file holds 296 bits, so `0x124` to `0x127` are unassigned; move the row's flag there.

The pieces are geometry, and the picture *is* the pieces. Each one is a vertex group in the lair map model - the lobby's map, not the podium - numbered 400 plus the piece's index for this puzzle. An unplaced piece draws at opacity 0 and a placed one at 255, so the completed picture is simply every group visible at once. The Mumbo's Mountain lobby has one, group 400; a cost of 4 wants 400 through 403, each carrying its own slice of the image. In Blender that is a vertex group named `bk64_mesh_400` and so on, on the lobby's map model - see the plugin's [Mesh Lists](https://github.com/HarbourMasters/fast64/blob/main/fast64_internal/hm64/bk64/README.md#mesh-lists) section. A new picture is the same groups with a different texture.

Leave the geometry alone and the puzzle still works: the lookup tolerates a missing group, so the door opens at four jiggies and three of them just never appear on the wall. That half of the job is a model edit, and it belongs in Blender - Lightbulb edits setups and the game config, not geometry.

## The invisible actors

Some actors have no model at all. They exist to give a *place* a behavior, and the clearest case is one actor reading several of them.

**Wozza's retreat.** Freezeezy Peak places Wozza (`0x1F3`) at `(-5130, 800, -4499)`, and he never stands there. As he spawns he looks up four other actor ids in the same setup by number, keeps their positions, and moves himself onto one of them:

| Node | Position | Action |
|---|---|---|
| `0x35B` | `(-5037, 800, -4348)` | stands here - 177 units from his own node |
| `0x359` | `(-5546, 792, -4170)` | first stop when he runs |
| `0x35A` | `(-5855, 793, -4058)` | second stop, where he waits |
| `0x35C` | `(-6144, 795, -3948)` | what he turns to face while waiting |

Those four are the whole route: about 540 units to the first stop, 330 to the second, 310 to the last, and the cave warp (`warp_fpEnterWozzasCave`, id `0xFD`) sits some 340 units past the end of it. So he runs roughly 1,200 units west toward his cave and stops short of the exit. None of the four has a model.

**Climb markers.** A climbable tree or pole is a pair of nodes: `0x26` (`ACTOR_26_CLIMB_BASE`) at the bottom, and a top marker - `0x27` or `0x28` - above it. The base actor finds the nearest top of either kind and the two ends define the climbable span; the base node's radius is the climb radius. The two top kinds differ in one thing: reach the top of a `0x28` climb moving upward and Banjo climbs out onto whatever is above. A `0x27` top just ends the pole. Since these don't render in-game, Lightbulb uses placeholder models for ease of use.

**Entry points.** Where Banjo lands when he arrives from another map. These come in pairs with a camera node - see [Warps](WARPS.md).

**Bobbers and movers.** A handful of ids hand a nearby piece of scenery over to code that moves it - floating platforms, Tumblar, Clanker's parts. See [Scenery that moves](OBJECTS.md#scenery-that-moves).

**Walk-in nodes** (`0x184`-`0x186`). When Banjo exits within 500 units of one (1000 for `0x186`), the transition plays the walk-out with the node's yaw and scale as parameters, or walks him to the node outright for `0x184`. `0x185` is the one retail actually leans on - Spiral Mountain puts one at `(0, 1784, -3659)` facing yaw 180, about 200 units from the warp that runs `warp_lairEnterLairFromSMLevel`, so the two sit together as an exit and its walk-out. Retail ships no `0x186` at all.

**Map trigger nodes.** Ids `0x018`, `0x18A`, `0x18B`, `0x192`-`0x194` each have a handler pair in a per-map trigger table. `0x18B` compares only the player's height against the node's - no radius, no horizontal distance - and lifts the player back up to that height when they fall below it. Rusty Bucket Bay's engine room puts one at `(-61, -4792, 2570)`, so only the `-4792` matters: it is a floor under the whole room, not a spot in it. Lightbulb has stand-in models for these and the entry markers.

## Making one move

An actor that follows a route is the first link of a path chain. See [Paths](PATHS.md).