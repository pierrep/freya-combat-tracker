# Dice recordings

Put short WAV clips of single dice hits here and the dice play them. Each hit in
a throw (a landing, a bounce, two dice knocking, a die tipping onto its face)
plays the closest clip there is. A kind of hit with no clip at all is silent,
and with no clips the dice make no sound, so a few face landings on each table
are the place to start.

The app reads the first of these folders that has a WAV in it:

1. `$FREYA_SOUNDS_DIR`
2. `sounds` in your data folder (Options → Saved data), for your own set
3. this folder, `data/sounds` in the source tree
4. `share/freya-combat-tracker/sounds` after `cmake --install`

After adding files, press **Reload recordings** on the Options page (no restart
needed), then **Play test throw** to hear them. The note under the choice says
how many clips it found and names any file it skipped.

## Names

```
wood/d20_face_hard_2.wav    a d20 landing flat on wood, struck hard, take 2
wood/d6_corner_soft_1.wav   a d6 landing on a corner, softly
felt/any_face_medium_1.wav  any die landing flat on felt
wood/d12_settle_1.wav       a d12 tipping onto its face at the end
dice/d6_d20_hard_1.wav      a d6 and a d20 knocking together (any table)
dice/any_any_medium_1.wav   any two dice knocking
```

The words can come in any order, split by `_`, `-` or spaces, and other words
are ignored (so a Freesound file can keep its title):

| Part | Words | Left out |
|---|---|---|
| Table | the folder `wood`, `felt` or `dice`, or a word: wood, felt, tray, mat | required |
| Die | d4, d6, d8, d10, d12, d20 (d100 counts as d10), any | any |
| Hit | corner, edge, face, settle | face |
| Force | soft, medium, hard | medium |
| Take | a number | 1 |

Dice-on-dice clips go in `dice/` and need no hit. Name one or two dice.

## Which clip a hit gets

1. The same table, die, hit and force. One of its takes is chosen at random,
   never the one that die just played.
2. Otherwise the nearest force, then the nearest kind of hit (corner, edge,
   face, settle in that order), then the nearest die (d4 to d20 in order of
   size), then `any`.
3. With nothing for the table in use, the other table's clips play (a wooden
   clip on felt is dulled and shortened).
4. With nothing of that kind at all (no table hits, say, or no dice
   knocking), that hit is silent.

The force comes from how fast the die hits. The clip's loudness doesn't
matter: every clip is made as loud as the others when it's read, and the hit's
own speed sets how loud it plays. So a "soft" clip should *sound* soft (duller,
shorter, less ring), not just be quieter.

Clips are trimmed to start at the hit and cut at 2 seconds. Any sample rate,
bit depth (8, 16, 24, 32-bit, or float) and channel count is fine (stereo is
mixed to mono, and the app places each hit left or right itself).

## Cutting clips from a longer recording

`tools/split_dice_hits.py` finds each hit in a recording, cuts it out, sorts it
into soft, medium and hard by loudness, and writes it here with the right name:

```
tools/split_dice_hits.py drops.wav --surface wood --die d20 --hit face --list   # look first
tools/split_dice_hits.py drops.wav --surface wood --die d20 --hit face
tools/split_dice_hits.py knocks.wav --surface dice --die d6 --other d20
```

It reads WAV itself; MP3, FLAC, M4A and OGG need ffmpeg. Use `--start` and
`--end` to take part of a file, `--threshold` (dB below the loudest, default
-30) and `--sensitivity` if it finds too many or too few hits, and
`--force hard` to name them all one force. `--credit "..."` adds a line to
`CREDITS.txt`.

## Recording your own

- **Room:** quiet, not echoey (a room with soft furnishings or curtains). Turn
  off fans and fridges if you can.
- **Microphone:** 20 to 40 cm from where the dice land, pointing at it. Any
  decent microphone works; a phone works in a pinch (turn off its noise
  suppression if it has a setting). Mono is fine.
- **Level:** set it on the hardest throw so the loudest hit peaks around −6 dB.
  Never clip.
- **Format:** WAV, 48 kHz, 24-bit if your recorder allows it.
- **Single hits:** for table hits, hold the die a few centimetres up and drop it
  so it lands once, then catch or still it before it bounces again. Leave a
  second of quiet between drops. Drop from about 2 cm (soft), 8 cm (medium) and
  20 cm (hard). Aim for 3 or more takes of each.
- **Corners, edges, faces:** drop the die corner-down, edge-down and flat. The
  flat landing (face) matters most; if you only record one kind, record that.
- **Settles:** tip a die over from resting on an edge so it falls onto a face.
- **Dice knocking:** hold one die still on the table and flick another into it,
  or tap two together just above the table.
- **Each die:** d4, d6, d8, d10, d12 and d20 if you have the time; otherwise a
  d6 and a d20 cover the small and large ends, and the rest fall back to them.
- **Both tables:** a wooden table and a felt tray or mat.

A good starting set is d6 and d20 face drops at three heights on wood, five
takes each (30 drops), then the same on felt. Record each set as one file and
let the splitter cut it up.

## Licences

Recordings from elsewhere keep their licence. CC0 needs nothing. CC-BY needs
credit wherever the app is shared: list the author, the title, the link and the
licence in `CREDITS.txt` (the splitter's `--credit` does this). Leave out
anything licensed NC (non-commercial) unless the app will never be sold.
