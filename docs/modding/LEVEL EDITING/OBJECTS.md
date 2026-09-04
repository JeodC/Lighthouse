# Objects

Scenery, collectibles and lights - the parts of a setup with no behavior of their own.

## Scenery and collectibles

Three record kinds share the objects section, told apart by two flags:

| Kind | What | What you can set |
|---|---|---|
| **Model prop** | static scenery | model id (+ `0x2D1` = the asset id), position, yaw and roll (in 2-degree steps), scale (x100 = 1.0, so 2.55 is the ceiling) |
| **Sprite prop** | notes, eggs, feathers, grass tufts | sprite id (+ `0x572` = the asset id), position, scale, mirror flag, an animation phase, and three small RGB-subtract values (0-7 each) that tint the sprite |
| **Actor prop** | a spawned actor's runtime record | position and flags; the marker pointer is filled at runtime |

Model props are pure decoration with collision - no behavior. Anything that *does* something is an actor, and actors are placed by nodes, not props. See [Actors](ACTORS.md).

**One model, thirty props.** Freezeezy Peak places model `0x55`, the decorative icicles, thirty times; and no two copies are alike. Every one carries its own scale, running from 44 up to 131 - so the smallest is 0.44 times the model's built size and the largest 1.31 times, just under a threefold spread out of one mesh. The yaws are scattered right across the circle, and while 21 stand upright, nine are rolled off vertical by 2 to 26 degrees. That is the whole job of a model prop: the mesh is authored once, and the setup decides where each copy stands, how big it is and which way it leans.

The sprite prop's animation value is a **phase**, not a starting frame: it offsets one sprite's animation against another's, so identical sprites placed together don't animate in step. It counts in 32nds of a cycle, so 16 puts one sprite half a cycle behind its neighbor. It also picks the mirroring and the direction the cycle runs.

Treasure Trove Cove does this at scale. Sprite `0xE` is the red feather (`ASSET_580_SPRITE_RED_FEATHER`), it appears 59 times across the map, and the phases walk 0, 2, 4, 6 and on up to 30 - every other value the field can hold.

## Point lights

The lighting section is a list of point lights: position, an inner and outer fade radius, and an RGB color. They tint **actors** that come near them - the glow Banjo picks up walking through one. They don't light the map itself, and models load no lights of their own (see [CUSTOM MODELS.md](../CUSTOM%20MODELS.md)).

Only five maps in the game carry a lighting section. An actor is only lit if its world opted it in when registering it, and every registration that does so is in Clanker's Cavern, Rusty Bucket Bay or the intro cutscenes. A light reaches a model only when the model is inside its outer radius, and at most 16 reach one model at a time. A model on this path with no light in range is drawn **black**.

Lightbulb doesn't draw or edit lights yet.

## Scenery that moves

A model prop can be handed to code that moves it, and the pairing is made by *position*: place one of a short list of actor nodes near the prop, and at load the game attaches that behavior to the nearest model prop within 500 units. The prop keeps no record of it.

| Actor node | Effect |
|---|---|
| `0x37` water bobber | the prop floats and bobs on water |
| `0x13` sinking bobber | the prop sinks while Banjo stands on it |
| `0x38` Tumblar movement | Tumblar, in `MMM_TUMBLARS_SHED` only |
| `0xF9`-`0x100` | Clanker's moving parts, in `CC_INSIDE_CLANKER` only |

`MMM_TUMBLARS_SHED` is the whole idea in one room. Its setup holds one model prop, at `(17, 0, -32)`, and one `0x38` node, at `(23, 128, -17)`. They are 129 units apart, comfortably inside the 500, and that distance is the only thing joining them.

The bottom two rows only take effect in the map they name, so copying one elsewhere does nothing. Lightbulb draws both halves as ordinary records and doesn't pair them up.

## Collectibles and their save flags

A collectible doesn't know which one it is. It looks up the flag volume it's standing in and takes its identity from that. Move one without its volume and it starts setting a different save flag, or stops counting at all. [Areas](AREAS.md) covers the volumes.
