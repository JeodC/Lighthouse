# Paths

A path is how you make an actor go somewhere on its own, for example the magic carpets in Gobi's Valley. It's a chain of records, each pointing at the next. Three kinds go in it:

| Record | What it is for |
|---|---|
| **Actor spawn** | the actor that rides the path. It goes first. |
| **Control point** | a place the route passes through. A category-8 node. |
| **Script waypoint** | an instruction for one part of the route: stop here, go this fast, play this animation, face that way. |

## The route

The spawn node comes first, then the control points in order. The actor follows a smooth curve through them rather than straight lines, so a loop needs three or four points, not a dozen. The spawn node has to be the first link in the chain - that's how the game knows which route the actor rides. With no waypoints at all, the actor just travels the route at normal speed, facing where it's going.

**How the chain is written.** Every record carries two numbers: its own, and the next record's. They are map-wide rather than per path, so no two records anywhere in the map share a number, and a chain ends at a next of 0. The game finds the head of each path by looking for the record that nothing else points at, so if some other record names your spawn node as its next, the path starts in the wrong place.

## Waypoints

A waypoint says what happens at one point along the route. Its **fraction** is where: `0` is the start, `1` is the end, `0.5` is halfway. When the actor passes that point the waypoint fires. Waypoints can sit anywhere in the chain - the game sorts them by fraction. Several at the same fraction fire in the order they appear.

A waypoint can do any of these:

- **Stop for a while.** A pause, in seconds. The actor waits, then carries on.
- **Change speed.** `16` is normal, `48` is quick, `4` is a crawl. The actor keeps that speed until another waypoint changes it.
- **Play an animation**, for a given number of seconds: once, once backwards, looping, looping backwards, or frozen on its last frame.
- **Face a direction.** Either face along the route, or face a yaw and pitch given on the waypoint. There are in-between settings that fix one but leave the other free.
- **Blend toward a later waypoint**, so the heading or the speed changes gradually between the two instead of switching at once. A separate smooth-turn setting makes the actor turn toward its heading over time rather than snap.

Pause, speed and animation each have an on/off switch on the waypoint. Retail levels are full of waypoints carrying a speed or a pitch with the switch off; the game ignores those, and Lightbulb grays them out. Facing and blending have no switch - they always happen when the waypoint fires.

## Reading a vanilla one: a magic carpet

Open `GV_GOBIS_VALLEY` in Lightbulb and find the `MAGIC_CARPET_2` spawn at `(-4461, 1600, 4141)`. It heads one chain of 35 records: the spawn, seven waypoints, and 27 control points that climb from height 1366 to 2350 and come back to where they started.

Every one of its waypoints does the same thing - **set a speed**:

| Fraction | Speed |
|---|---|
| 0.000 | 80 |
| 0.071 | 400 |
| 0.150 | 68 |
| 0.415 | 380 |
| 0.434 | 48 |
| 0.849 | 70 |
| 0.866 | 10 |

The fractions are **spread along the route**, so each one fires as the carpet reaches that part of the loop: it sets off at 80, jumps to 400 a fourteenth of the way round, settles to 68, and is down to 10 on the last stretch. Each waypoint also carries a pause of a second and a yaw, and the game uses **neither** - only the speed switch is on, so the rest sits there inert. The pairs at 0.415 and 0.434, and at 0.849 and 0.866, are stored the *other way round* in the chain; the game sorts by fraction, so chain order never has to match route order.

## Waypoints made in other editors

Waypoints have no position of their own - they reuse a node's slot to hold a float and four packed words, so nothing is drawn for them in the viewport and reading one as a node shows a junk category and id. Other level editors read the record differently from the game, so check an imported level's waypoints in Lightbulb before trusting them.

Programmers: the exact layout is `Struct_glspline_t1` in `spline_pathfollow.c`.
