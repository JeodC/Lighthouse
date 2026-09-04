# Areas

Three node categories mark out **areas** rather than points: camera triggers (9), enemy boundaries (7), and flag volumes (A). They have nothing to show in game, and all three work the same way. Contact triggers (4) mark out a region too, by a different mechanism, and this page covers them as well.

## How an area is built

At map load each node registers a **cylinder** - its position and horizontal radius - into a per-map list under its id. Cylinders that share an id and whose circles touch in X/Z merge into one **zone**.

**An area of any shape is several overlapping nodes sharing an id.** To cover an L-shaped room, drop three or four nodes down its length rather than reaching for one enormous radius - the radius maxes out at 511 units anyway.

Spiral Mountain's water is the extreme case. Camera trigger id 38 is not one big cylinder but **48** of them, 45 at radius 500 and three trimmed smaller, laid end to end along the water. Every one of them selects camera 38, so as far as the game is concerned they are a single zone shaped like the water.

## Changing the camera in one spot (category 9)

The id is an index into the [camera list](CAMERAS.md). Every frame the player's position is tested against the zones: inside the cylinder's radius horizontally, with the cylinder's center Y within 150 units of the player. The winning zone's camera takes over; standing in no zone means the ordinary free camera, and flying skips the lookup entirely.

**Land or water.** A camera trigger carries one more setting, deciding which of those two states it answers in. Banjo counts as swimming from the moment he enters water until he is back on stable ground, climbing or flying:

| Setting | On land | Swimming |
|---|---|---|
| 0 | yes, within 150 units | yes, at any height |
| 1 | yes, within 150 units | no |
| 2 | no | yes, at any height |
| 3 | no | yes, within 150 units |

Spiral Mountain uses three of the four. Of its 48 water cylinders, 41 are setting 2 - swimming only, any depth - so that camera holds however deep you dive. A separate run of nine zones, ids 20 to 28, climbs from height 400 to 923 and is setting 1 throughout, so none of those fire while swimming. Lightbulb doesn't show this setting yet.

Each camera index also has a runtime enable flag, all on at load, which actors and cutscenes turn off and back on.

When the camera changes somewhere you didn't expect, find the category-9 node whose cylinder you stepped in, read its id, and look it up in the camera list. Lightbulb colors a camera and every volume that selects it the same color.

## Starting and stopping an area camera (category 4)

A **contact trigger** is the other way a region changes the camera, and it works nothing like the cylinders above. It is a category-3 node's sibling: a box, fired once when something touches it, using its radius as a half-extent on all three axes. Where a category-9 zone holds the camera for as long as you stand in it, a contact trigger flips a switch as you cross it and leaves it flipped.

Retail uses the ids in four groups:

| Ids | What |
|---|---|
| `0x16`-`0x29` | start the area camera - dynamic camera mode 12 - for area id `- 0x16` |
| `0x2A` | end the area camera |
| `0x46`-`0x4B` | treasure hunt step progress (TTC's x-marks-the-spot game) |
| `0x4C`, `0x4D` | Mumbo transformation boundary - warn, then detransform |

Every other id in the table does nothing.

Spiral Mountain uses only the first two rows: three nodes with id `0x16` turn area camera 0 on, and twelve with id `0x2A` turn it off again.

## Fencing an enemy in (category 7)

An enemy looks up which boundary zone it's standing in and records it, and its movement code then refuses any position outside that zone. This is why an enemy gives up a chase at an invisible line. Mumbo's Mountain shows the shape of it. Its four Grublins each stand inside a boundary zone built from overlapping 500-unit cylinders - the one at `(4025, 23, 3145)` sits inside two cylinders of zone id 2, roughly 400 units from either center. That pair of circles is the whole of its patrol.

An enemy placed outside every boundary isn't fenced at all. If you move an enemy, check it's still inside its zone.

## Telling a collectible which one it is (category A)

The id here is a value collectibles read to learn *their own identity*. A jiggy finds the flag zone it stands in and its save flag is the zone's value plus 1; an empty honeycomb subtracts 99; a Mumbo token subtracts 199.

Mumbo's Mountain has four jiggies and 23 flag volumes. Its highest jiggy, at `(373, 4221, -906)`, stands 10 units from the center of a flag volume with id 1 and radius 127 - so its save flag is 2, and it has 117 units of slack before it would fall out of that volume.