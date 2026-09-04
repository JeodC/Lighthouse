# Warps

A **warp** is two halves in two different setups: an **exit** in the map you leave, and an **entrance** in the map you arrive at. Both have to exist or the warp misbehaves.

## The half you leave: exits

An exit is a category-3 node, and it fires on **touch**. Each frame an actor's marker is tested against the nearby nodes whose *marker id* field names its kind, using the node's radius as a box half-extent on all three axes. Category 4, the contact trigger, is dispatched the same way but has nothing to do with warping - see [Areas](AREAS.md). The marker id is what lets an exit answer to Banjo while an orange pad answers to a thrown orange. Mumbo's Mountain has three of those pads, exit ids `0x2`, `0x3` and `0x4`, and all three carry marker id 12 - `MARKER_C_ORANGE_PROJECTILE`. Banjo's own marker never matches that, so walking onto one of these never reaches the handler.

**An exit's id indexes a table of handlers**: every door, hut, lobby link and Nintendo-logo transition is one entry - `warp_mmEnterMumbosHut`, `warp_lairEnterCCLobbyFromCCLevel`, and so on. The handler carries the destination map and entrance, so the same exit id always leads to the same place. Romhacks re-point them through the game config instead; Lightbulb's **Game Config** window has a Warp overrides list built from the exits in the level you have open.

A global-timer stamp stops an exit re-firing every frame, and the game keeps that stamp **in the node's own yaw and scale fields**. Those two are scratch on an exit, overwritten the first time anyone touches it, so there is nothing to set there - a warp takes its facing from the entry point at the other end.

## The half you arrive at: entrances

Every transition carries an **entrance number**. On arrival the game turns that number into an entry-point actor id and looks for a node carrying it: entrance 1 wants actor `0x01`, entrances 4-13 want `0x76`-`0x7F`, entrance `0x12` wants `0x103`, and so on. Banjo is placed at that node, facing its yaw.

**An entrance is a pair of nodes, not one.** A second table turns the same entrance number into an entry *camera* actor id - `0x80` for entrance 0, counting up from there, with other ids further down the table - and the load also spawns the camera-controller actor `0x66`. One node puts Banjo somewhere; the other tells the camera where to be, taking a distance from its scale field.

### When it goes wrong

If the entry point the entrance asks for is missing, the game walks entrance numbers `0` to `0x1D` in order and takes the first one the map does have. So:

| The destination map has | What happens |
|---|---|
| the entry point the exit asks for | Banjo arrives where you meant |
| some other entry point in the `0`-`0x1D` range | Banjo arrives at *that* one instead - the warp works, but comes out in the wrong place |
| no entry point in that range | Banjo is left at the void default and falls forever (the port logs a warning) |

Entrance numbers of `0x80` and above are a different mechanism - they carry their own position and need no node. `0x63` and `0x65` are answered before the lookup happens too.

Lightbulb checks the entry point for you. Edit a warp destination in **Game Config** and it reads that map's setup and says which of the three rows above you land in. It doesn't yet check the camera half of the pair.

## A whole warp: Banjo's front door

Both halves, in both directions, are four records.

**Going in.** Spiral Mountain carries an exit with id `0x118` at `(3926, -425, 6761)`, radius 157. Touching it runs `warp_smEnterBanjosHouse`, which asks for `0x8C01` - map `0x8C` and entrance `1`. Banjo's house is map `0x8C`, and it carries an actor node with id `0x1` at `(0, 0, 157)` facing yaw 180. Entrance 1 asks for actor `0x1`, so that node is where Banjo lands, facing into the room.

**Coming back.** The house carries one exit of its own, id `0x119` at `(-7, 74, 452)`, running `warp_smExitBanjosHouse` for `0x112` - map `0x1`, entrance `0x12`. Entrance `0x12` asks for actor `0x103`, and Spiral Mountain has one, at `(3955, -490, 6484)` facing yaw 180 - about 280 units from the exit you walked in through, and 65 units below it.

## Adding a warp to a new area

1. Place an **exit node** where the player should touch it, and give it the id of a handler that leads where you want - or add a **warp override** in Game Config to re-point an existing one.
2. In the destination map, place an **entry point** actor node for the entrance number that exit uses, facing where Banjo should be looking.
3. Place the matching **entry camera** node beside it.
4. Add a return exit the same way, and an entry point back in the first map.
