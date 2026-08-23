# Making an Audio Mod

The game's audio comes in two kinds and they mod very differently.

A **sound effect** is one file. Drop an mp3 in next to it, run the import, done.

**Music** is a MIDI, and dropping one in is only half the job. Banjo plays a sequence differently from anything else that reads MIDI: the program numbers mean its own instruments, and *the map decides which of your channels are audible*. Get that wrong and a perfectly good track arrives thin and quiet with no error anywhere. Part two is mostly about that.

---

# Part one: sound effects

## 1. Find the sound

Sounds are in `bk.o2r` at `assets/sfx/<id>_<NAME>`, named by the id the game plays them with:

```
assets/sfx/000_BLOOP
assets/sfx/0C2_GRUBLIN_EGH
assets/sfx/3FA_HONEYCOMB_TALKING
```

Lightbulb's **Sounds** panel lists them, plays them, and tells you what you need before you touch anything:

```
Length      0.42 s
Plays at    12375 Hz, mono
Loops       no
Played by   4 actors
```

**"Played by" is the number that matters.** A sound played by one actor belongs to that actor. `002_CLAW_SWIPE` is played by twenty-five, and replacing it changes every one of them. Lightbulb marks those *Used by multiple actors*. `0C2_GRUBLIN_EGH` is played by four (grublin, grublinhood, gruntling, seamangrublin), which is the whole Grublin family.

If you would rather grep than click, the counts come from the decomp: an actor that plays a sound names its `sfx_e` enumerator, so `grep -rn SFX_C2 src/` finds the callers.

## 2. Export

```
torch modding export <baserom.z64> -s <lighthouse> -d <workdir>
```

Each sound comes out as three files:

| File | What it is |
|---|---|
| `0C2_GRUBLIN_EGH.yaml` | everything you might change: envelope, pitch, loop, chain |
| `0C2_GRUBLIN_EGH.wav` | the sound decoded, to listen to or edit |
| `0C2_GRUBLIN_EGH.bin` | the encoded sample: codebook, loop state, ADPCM |

Only assets listed in `modding.yml` are re-encoded on import; everything else is read straight from the ROM. Delete the files you are not editing and prune `modding.yml` to match.

## 3. Drop your audio in

Copy your file next to the yaml and name it in the `Sample` key:

```yaml
Sample: sm64-mario-pain.mp3
```

`Sample: raw` (the default) rebuilds from the `.bin` byte for byte. Anything else is read as a filename sitting beside the yaml.

**Supported:** `.wav`, `.mp3`, `.flac`

**Rate, channels and length do not matter.** Whatever you hand it is mixed to mono, resampled to the slot's rate, and the envelope is fitted to its length. No need to prepare a 22050 Hz mono file, and no need to match the length of the sound you are replacing.

Two things happen automatically, both reported in the log:

- **Your audio is resampled to the slot's rate**: see #5. You supply audio at ordinary pitch and it comes out at ordinary pitch.
- **Leading and trailing silence are trimmed.** The game gives a sound a fixed time to play. Set `TrimSilence: false` to keep it. Looped sounds are never trimmed, since their loop points are counted in samples.

## 4. The envelope

A voice plays for as long as its **envelope**, not for as long as its sample. Hand the slot a longer sound and the game stops partway through; hand it a shorter one and the voice holds after the audio ends. In the vanilla banks the two match to within a millisecond.

**Torch fits it for you** when `Sample` names an audio file, and says so:

```
sfx/0C2_GRUBLIN_EGH envelope fitted to the new sample -- DecayTime 231811 -> 311493 (0.315s)
```

`AttackTime` and `ReleaseTime` are left alone, so the shape of the attack and the fade-out survive; decay takes up the slack. Sounds left on `Sample: raw` are never touched. Set `FitEnvelope: false` to use your numbers exactly as written. A deliberately clipped sound is a legitimate effect.

### Working it out yourself

You need this if you are hand-building a sample (#8) or overriding the fit. Envelope times are stored *unpitched*: the game divides them by the slot's pitch ratio when it plays, the same ratio it applies to the sample, so the two stay in step.

```
total microseconds = clip_seconds x 1,000,000 x pitch
                     where pitch = 2 ^ ((KeyBase - 60) / 12)

DecayTime = total - AttackTime - ReleaseTime
```

A 0.5607 s clip in a slot with `KeyBase` 50:

```
pitch = 2 ^ ((50 - 60) / 12) = 0.561
total = 0.5607 x 1,000,000 x 0.561 = 314,694 us
DecayTime = 314,694 - 0 - 3,200 = 311,494
```

| KeyBase | pitch | | KeyBase | pitch |
|---|---|---|---|---|
| 60 | x1.000 | | 48 | x0.500 |
| 55 | x0.749 | | 45 | x0.420 |
| 50 | x0.561 | | 42 | x0.354 |
| | | | 36 | x0.250 |

## 5. Pitch

Export a sound, open the `.wav`, and it sounds like the game; but its sample rate is not 22050 Hz. `0C2_GRUBLIN_EGH` comes out at 12375 Hz, `000_BLOOP` at 7796.

That is deliberate. The samples in the ROM are stored *high-pitched* and the game brings them down through `KeyBase`: 60 means play as stored, and every 12 below that is an octave down. If the export wrote the stored bytes at their nominal rate you would get a chipmunk version of every sound in the game.

Both directions use the rate the game actually plays at. You hear the truth on export, you supply ordinary-pitched audio on import, and **you should not need to touch `KeyBase`**. Setting it to 60 to "fix" the pitch appears to work, but `KeyBase` is read elsewhere in the audio path.

## 6. The other fields

Rare reused most of `ALKeyMap` for sound-effect bookkeeping rather than for key ranges, so the export writes what the fields do:

| Field | What it does |
|---|---|
| `KeyBase`, `Detune` | pitch; see #5 |
| `ChainNext` | plays another sound after this one, by id; 0 for none |
| `ChainDelayFrames` | how long to wait before that one |
| `VolumeGroup` | which of 64 volume groups the sound belongs to |
| `ReverbSend` | reverb amount, 0–15 |
| `SamplePan`, `SampleVolume` | as they sound |

If Lightbulb shows a **Chain** row, the sound is one link of a sequence. Replacing a single link on its own will sound wrong.

These meanings are sound-effect only. Under `assets/instruments/` the same bytes are genuine key and velocity ranges, which is why Lightbulb shows *Key range* there instead.

## 7. Build and pack

```
torch modding import o2r <baserom.z64> -s <lighthouse> -d <workdir>
```

The log tells you what it did:

```
sfx/0C2_GRUBLIN_EGH trimmed 0.254s of silence from the start and 0.047s from the end
encoded sfx/0C2_GRUBLIN_EGH from sm64-mario-pain.mp3 -- 6939 samples, 31.6 dB
sfx/0C2_GRUBLIN_EGH envelope fitted to the new sample -- DecayTime 231811 -> 311493 (0.315s)
```

**The dB figure is encode quality**, measured against the same decoder the game uses. Above roughly 20 dB is fine. Single digits mean something is wrong with the input: near-silence, or a clip that is mostly noise.

Then pull your entry out of the built archive and pack it alone:

```
torch pack <staging-folder> mymod.o2r o2r
```

Drop it in `mods/`.

## 8. Building the sample by hand

You do not need Torch's encoder. `Sample: raw` reads the `.bin` verbatim, so if you can produce one, either with the SDK tools, your own encoder, or by editing an exported one, Torch will use it untouched. Little-endian throughout:

```
u32  order              prediction order; 2 in every vanilla sound
u32  npredictors        how many codebook entries follow
u32  bookCount          = order x npredictors x 8
s16  book[bookCount]    the codebook, Q11 (2048 = 1.0)
u32  stateCount         16 for a looped sound, 0 otherwise
s16  state[stateCount]  decoder state at the loop point
u32  dataSize
u8   data[dataSize]     the ADPCM frames
```

- **`dataSize` must be a whole number of nine-byte frames.** Pad with silence rather than leaving a partial frame.
- **The codebook has to suit your sample.** Reusing the one from the sound you are replacing predicts your audio badly and spends the 4-bit residual range on error, which is audible as roughness.

---

# Part two: music

## 9. Find the track

Music can be found in your `bk.o2r` at `assets/comusic/COMUSIC_<id>_<NAME>`:

```
assets/comusic/COMUSIC_2_MM
assets/comusic/COMUSIC_22_MMM
assets/comusic/COMUSIC_C_TREASURE_TROVE_COVE
```

Export gives you a standard MIDI file per track:

```
torch modding export <baserom.z64> -s <lighthouse> -d <workdir>
```

Format 1, one chunk per channel, at the sequence's own resolution. Open it in anything like Sekaiju, a DAW, or a plain MIDI player.

## 10. Channels are zones

A world plays *one* sequence, and where you stand decides which channels of it are audible. Walk from the Mumbo's Mountain entrance to Conga's Tree and the game does not change track, it mutes some channels and unmutes others.

Mumbo's Mountain, for example:

| Where you are | Channels heard |
|---|---|
| Main area (spawn, most of the map) | 0, 1, 2, 3, 4, 5, 12 |
| Outside Ticker's Tower | main, plus 8 and 14 |
| Conga's tree | 6, 7, 10, 11, 12 |
| Outside Mumbo's hut | 6, 7, 12, 13, 15 |
| Underwater | 9 |

Rare wrote channels 0–5 as a self-contained arrangement (drumkit, marimba, bassoon, trombone, bass, clarinet) and reserved the rest for the zone variations: taiko and shakers for Conga and Mumbo areas, snare rolls and "hups" for Ticker's Tower, a harp for underwater. Channel 12 (bird calls) is in every surface zone, so it is the one channel always audible.

**Put your arrangement on the channels the map's main zone plays**, and use the others only for material meant for those specific places. 46 of the game's maps gate music this way; where a map defines no zones, all sixteen channels play all the time.

## 11. Instruments

A program change selects one of Banjo's own 85 instruments. **These are not General MIDI numbers.** A file written against a GM soundfont will point most channels at the wrong instrument, and any program above 85 is skipped by the player, so those channels go silent.

Some example instruments in your `bk.o2r`:

```
assets/instruments/program1_marimba_keys0-76
assets/instruments/program12_harmonica_keys55-66
assets/instruments/program73_drum_kit
```

The number is the one you type in a sequencer. The key range appears when an instrument is multisampled and says which notes that sample covers. Lightbulb's Sounds panel lists and plays them all. Replacing an instrument is possible but blunt: they are shared across the whole soundtrack. Program 1 is used by 69 of the game's own tracks, for example, so swapping it changes all of them.

## 12. Loops

The game loops per track, and there are two ways to say where.

**Markers**: put `loopStart` and `loopEnd` in the track. This is the Final Fantasy VII convention that sequencers adopted; Sekaiju loops playback between them natively, so you hear in the editor what the game will do. Torch exports vanilla tracks this way.

**Or the file name**: `... LP 384.mid` loops back to tick 384. This is what N64MidiTool does, where the loop point is set at injection time rather than stored in the file, so files prepared that way work unchanged. Markers win if both are present.

A track with neither plays once and stops.

## 13. Volume

Each of the 176 slots has its own master volume, and most sit well below full scale. The median is 20000 of 32767, and only three slots use the full range. Mumbo's Mountain plays at 20000, about 4 dB down.

Set volume with a marker:

```
volume 32767
```

## 14. Controllers

Only some do anything:

| CC | Effect |
|---|---|
| 7 | channel volume |
| 10 | pan |
| 16 | priority |
| 64 | sustain |
| 91 | reverb send |

**CC 11 (expression) is ignored.** Dynamics written in expression will not be heard. Use CC 7 or note velocity.

Two ranges do something other than make sound:

- **CC 106–119 signal the game**, not the synth. The organ puzzle in Mad Monster Mansion waits on one. Sekaiju's auto-repeat feature uses CC 111, which lands squarely in this range, so don't use it.
- **CC 126 and 127 mute and unmute a channel.** In General MIDI these are Mono Mode and Poly Mode; here they will silence part of your track.

Torch warns about both on import. Anything else is ignored harmlessly.

## 15. What the game's own music looks like

Useful for judging whether a track will sit right before you build anything. Medians across the 87 slots a world actually plays, from the game and three shipped romhacks:

| | vanilla | romhacks |
|---|---|---|
| tempo | 120 bpm | 101–120 bpm |
| note onsets per second | 15.0 | 11–15 |
| simultaneous voices | 5.3 | 4.4–6.7 |
| note velocity, mean | 96 | 73–91 |
| CC7, mean | 86 | 80–86 |
| instruments used | 6 | 6–8 |

The synth has **24 voices**. Loud comes from density rather than level: a thin arrangement pinned to velocity 127 still sounds quieter than a full one at velocity 90, and pinning it destroys the dynamics as well.

Resolution is unconstrained. Save as **SMF format 0 or 1**; format 2 is not supported, and neither is SMPTE timing.

## 16. Build and pack

Same as sound effects:

```
torch modding import o2r <baserom.z64> -s <lighthouse> -d <workdir>
torch pack <staging-folder> mymod.o2r o2r
```

The log reports what it read:

```
Music: looping 16 track(s) back to tick 384 from the file name
Music: built a sequence from MIDI -- 16 track(s), division 96, 30809 bytes
```

---

*The instrument names come from the `.ins` file Riposte uses when scoring for the game. The decomp names music tracks but not instruments. Thanks also for the annotated Mumbo's Mountain export that made the zone channels legible.*