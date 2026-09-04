# Cameras

A setup carries a numbered list of cameras, up to 70 of them. A [camera trigger](AREAS.md) hands one of them control by index, and the camera's **type** decides what it then does.

| Type | Name | What it does | Example |
|---|---|---|---|
| 1 | Pivot | mounted at a spot, turns to follow Banjo | `MM_MUMBOS_MOUNTAIN` camera 9 |
| 2 | Static | position and angle both fixed; nothing follows anything | `SM_SPIRAL_MOUNTAIN` camera 1 |
| 3 | Dolly | slides along the line toward Banjo, staying between two distances | `SM_SPIRAL_MOUNTAIN` camera 0 |
| 4 | Random | frames nothing; re-tunes how far back the ordinary camera sits | `SM_SPIRAL_MOUNTAIN` camera 38 |

## Where a camera sits and what it looks at

Every camera except type 4 carries a **position** and a **pitch, yaw and roll**. What those two mean depends on the type:

- On a **static** camera that's all there is. The camera sits at the position, points at the angles, and never moves. Nothing tracks Banjo. Trigger zones don't select these - scripted code cuts to one by index. Freezeezy Peak's camera 46 is one: a static at `(-3682, 1765, -5278)`, pitch 332 and yaw 131, that Wozza's code cuts to by number for both of his meetings, as a bear and as a walrus. It hangs about 960 units above him and 1,900 away, and no trigger zone anywhere selects it.
- On a **pivot** camera the position is the mount and the angles are only where it starts. From there it turns to keep Banjo in frame.
- On a **dolly** camera the position anchors the line it slides along, and the angles seed it the same way.

## Tuning how it moves

Pivot and dolly cameras carry four tuning numbers, in two pairs, plus the dolly's distances. They are authored per camera, and the dynamic camera picks them up when the zone hands over:

| Setting | What it does |
|---|---|
| **horizontal / vertical speed** | how quickly the camera slides to keep up, sideways and up-down as separate rates |
| **rotation / acceleration** | how fast it turns toward where it wants to point, and how sharply it gets up to that rate |
| **close / far distance** (dolly only) | the band it holds between. It slides in when Banjo is nearer than the close distance and out when he is past the far one |

Small values make the camera drift; large ones make it snap. Retail mostly does not tune them at all: eighteen of the game's 26 pivot cameras carry an identical set - horizontal 1.75, vertical 3.75, rotation 2.75, acceleration 12 - and four more share a second set of 0.70, 2.33, 4.00 and 16.00. Only four cameras in the whole game are one-offs, Mumbo's Mountain camera 9 among them at 1.31, 3.53, 4.00 and 16.00. If you have no feel for these numbers yet, the first set is what the game reaches for by default.

## Flags

Pivot cameras carry three flags; dolly cameras carry the first and the last:

- **hits** runs line-of-sight correction, so the camera is pushed out of geometry rather than ending up inside a wall. Leave it on unless the camera has a clear view by construction.
- **vfix** (pivot only) holds the authored height instead of tracking Banjo's. Use it when he can climb inside the shot and you want the camera to stay put. Every one of the game's 26 pivot cameras sets it, so a retail pivot never changes height.
- **bee** marks the camera as still applying during the bee transformation. A zone whose camera lacks it is ignored while flying as the bee.

## Handing over

The zone-to-camera handoff is `ba_camera.c`: dolly and pivot cameras drive the dynamic camera into their own states. A zone pointing at an index with no camera behind it falls back to the free camera - which is what you get if you delete a camera and leave its triggers behind.

Type 4 holds one number and no position. The handoff reads that number, applies it, and reports that no camera node took over, so you stay on the ordinary camera with its distances changed underneath you. Each world carries a table of zoom presets, keyed by map and by that number. A row holds three settings, one per step of the zoom button: how far back the camera sits, and how high it rides, at closest, medium and furthest.

Spiral Mountain is the whole idea in one row. Everywhere else in the map, the closest zoom sits 550 units back and 175 up. Its 48 water cylinders all select camera 38, camera 38 holds the number 1, and the map's row 1 reads 800 and 375. Swim, and the camera drifts 250 units further out and 200 higher without you touching the zoom button; climb out and it closes back in. Gobi's Valley carries four such rows and Rusty Bucket Bay five, so a world can re-tune its camera room by room.

## Reading them in Lightbulb

The Cameras tab lists every camera with its type and position, and filters by type. Each one draws in the viewport as a camera gizmo, colored by index from Rare's own eight-color debug palette (the `RGB_VALUES` table retail still carries in `src/core1/debugtext.c`). Every trigger volume that selects a camera is drawn in that same color. Selecting a camera shows its type, position and angles, and says how many trigger volumes point at it - or warns you when none do. Lightbulb doesn't yet show the speed, acceleration or distance settings.