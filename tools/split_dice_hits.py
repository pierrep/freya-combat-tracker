#!/usr/bin/env python3
"""Cut a recording of dice into one clip per hit, named for the app.

The app plays one short clip each time a die hits the table, another die, or
tips onto its face (see data/sounds/README.md). This finds each hit in a longer
recording (a take of single drops, or a Freesound roll), cuts from just before
it to just before the next, and writes them into the sounds folder under the
names the app reads:

    <out>/<surface>/<die>_<hit>_<force>_<take>.wav
    <out>/dice/<die>_<other>_<force>_<take>.wav        (--surface dice)

Examples:
    # Twenty drops of a d20 onto wood, sorted into soft, medium and hard:
    tools/split_dice_hits.py d20_drops.wav --surface wood --die d20 --hit face

    # See what it finds first, without writing anything:
    tools/split_dice_hits.py roll.flac --surface felt --list

    # A CC-BY pack: say who made it, and the credit goes in CREDITS.txt.
    tools/split_dice_hits.py dice_01.wav --surface wood --die any \\
        --credit '"Dice Rolls" by dermotte, freesound.org/s/... (CC-BY 4.0)'

Reads WAV itself (PCM or float, any rate, any channels); anything else (MP3,
FLAC, M4A, OGG) needs ffmpeg on the PATH. No other dependencies.
"""

import argparse
import array
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import wave

DICE = ("d4", "d6", "d8", "d10", "d12", "d20", "any")
HITS = ("corner", "edge", "face", "settle")
FORCES = ("soft", "medium", "hard")


def read_wav(path):
    """(samples, rate), mono floats -1..1, for PCM 8/16/24/32 or float 32/64."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    fmt = None
    body = None
    at = 12
    while at + 8 <= len(data):
        tag = data[at:at + 4]
        size = struct.unpack_from("<I", data, at + 4)[0]
        start = at + 8
        if tag == b"fmt ":
            tag_format, channels, rate = struct.unpack_from("<HHI", data, start)
            bits = struct.unpack_from("<H", data, start + 14)[0]
            if tag_format == 0xFFFE and size >= 26:
                tag_format = struct.unpack_from("<H", data, start + 24)[0]
            fmt = (tag_format, channels, rate, bits)
        elif tag == b"data":
            body = data[start:start + size]
        at = start + size + (size & 1)
    if fmt is None or body is None:
        raise ValueError("no sound in it")
    tag_format, channels, rate, bits = fmt
    width = bits // 8
    frame = width * channels
    count = len(body) // frame
    if tag_format == 3 and bits in (32, 64):
        values = array.array("f" if bits == 32 else "d")
        values.frombytes(body[:count * frame])
    elif tag_format == 1 and bits == 16:
        values = array.array("h")
        values.frombytes(body[:count * frame])
        values = [v / 32768.0 for v in values]
    elif tag_format == 1 and bits == 8:
        values = [(b - 128) / 128.0 for b in body[:count * frame]]
    elif tag_format == 1 and bits == 24:
        raw = body[:count * frame]
        values = []
        for i in range(0, len(raw), 3):
            v = raw[i] | (raw[i + 1] << 8) | (raw[i + 2] << 16)
            if v & 0x800000:
                v -= 1 << 24
            values.append(v / 8388608.0)
    elif tag_format == 1 and bits == 32:
        values = array.array("i")
        values.frombytes(body[:count * frame])
        values = [v / 2147483648.0 for v in values]
    else:
        raise ValueError("a WAV encoding that isn't PCM or float")
    if sys.byteorder != "little" and isinstance(values, array.array):
        values.byteswap()
    if channels == 1:
        return [float(v) for v in values], rate
    mono = []
    for i in range(0, len(values) - channels + 1, channels):
        mono.append(sum(values[i:i + channels]) / channels)
    return mono, rate


def read_any(path):
    if path.lower().endswith(".wav"):
        try:
            return read_wav(path)
        except ValueError:
            pass  # an encoding ffmpeg may still read
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        sys.exit(f"{path}: not a WAV this can read, and ffmpeg isn't installed to convert it")
    probe = subprocess.run([ffmpeg, "-hide_banner", "-i", path], capture_output=True, text=True)
    match = re.search(r"(\d+) Hz", probe.stderr)
    rate = int(match.group(1)) if match else 48000
    out = subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-i", path, "-ac", "1", "-ar", str(rate),
                          "-f", "f32le", "-"], capture_output=True, check=True)
    values = array.array("f")
    values.frombytes(out.stdout[:len(out.stdout) // 4 * 4])
    if sys.byteorder != "little":
        values.byteswap()
    return list(values), rate


def envelope(samples, block):
    return [max((abs(v) for v in samples[i:i + block]), default=0.0) for i in range(0, len(samples), block)]


def find_hits(samples, rate, threshold_db, sensitivity, min_gap):
    """Frame indices of each hit's start."""
    block = max(1, rate // 1000)  # 1 ms
    env = envelope(samples, block)
    peak = max(env, default=0.0)
    if peak <= 0.0:
        return []
    floor = peak * 10 ** (threshold_db / 20.0)
    history = 20  # ms before, for "louder than just before"
    gap = max(1, int(min_gap * 1000))
    hits = []
    last = -gap
    for i, level in enumerate(env):
        if level < floor or i - last < gap:
            continue
        before = env[max(0, i - history):i]
        background = sum(before) / len(before) if before else 0.0
        if level >= sensitivity * background:
            # The hit's start: back to where it rose out of the background.
            start = i
            while start > 0 and env[start - 1] > max(background * 1.5, floor * 0.25) and i - start < 3:
                start -= 1
            hits.append(start * block)
            last = i
    return hits


def cut(samples, rate, start, end, max_length):
    pre = int(0.002 * rate)
    begin = max(0, start - pre)
    stop = min(end - int(0.001 * rate), begin + int(max_length * rate), len(samples))
    clip = list(samples[begin:stop])
    if not clip:
        return clip
    peak = max(abs(v) for v in clip)
    # The quiet after the ring.
    quiet = peak * 0.003
    while len(clip) > 16 and abs(clip[-1]) < quiet:
        clip.pop()
    fade_out = min(len(clip), int(0.005 * rate))
    for k in range(fade_out):
        clip[len(clip) - 1 - k] *= k / fade_out
    fade_in = min(len(clip), max(1, int(0.0005 * rate)))
    for k in range(fade_in):
        clip[k] *= k / fade_in
    return clip


def write_wav(path, clip, rate, bits):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(bits // 8)
        w.setframerate(rate)
        if bits == 16:
            data = array.array("h", (max(-32767, min(32767, round(v * 32767))) for v in clip))
            if sys.byteorder != "little":
                data.byteswap()
            w.writeframes(data.tobytes())
        else:
            out = bytearray()
            for v in clip:
                n = max(-8388607, min(8388607, round(v * 8388607))) & 0xFFFFFF
                out += bytes((n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF))
            w.writeframes(bytes(out))


def next_take(folder, prefix):
    taken = set()
    if os.path.isdir(folder):
        for name in os.listdir(folder):
            match = re.fullmatch(re.escape(prefix) + r"_(\d+)\.wav", name, re.IGNORECASE)
            if match:
                taken.add(int(match.group(1)))
    take = 1
    while take in taken:
        take += 1
    return take


def force_of(peak, loudest, fixed):
    if fixed != "auto":
        return fixed
    ratio = peak / loudest if loudest > 0 else 0.0
    if ratio >= 0.55:
        return "hard"
    return "medium" if ratio >= 0.22 else "soft"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog=__doc__.split("\n\n", 1)[1])
    parser.add_argument("inputs", nargs="+", help="recordings to cut (WAV; others need ffmpeg)")
    parser.add_argument("--surface", required=True, choices=("wood", "felt", "dice"),
                        help="the table it landed on, or dice for two dice knocking")
    parser.add_argument("--die", default="any", choices=DICE, help="which die (default any)")
    parser.add_argument("--other", default="any", choices=DICE, help="with --surface dice: the other die")
    parser.add_argument("--hit", default="face", choices=HITS, help="how it lands (default face)")
    parser.add_argument("--force", default="auto", choices=("auto",) + FORCES,
                        help="auto sorts the hits by how loud they are against the loudest (default)")
    parser.add_argument("--out", default=os.path.join(here, "..", "data", "sounds"),
                        help="the sounds folder (default data/sounds)")
    parser.add_argument("--threshold", type=float, default=-30.0,
                        help="dB below the loudest hit to count as a hit (default -30)")
    parser.add_argument("--sensitivity", type=float, default=3.0,
                        help="how much louder than the moment before a hit must be (default 3)")
    parser.add_argument("--min-gap", type=float, default=0.04, help="seconds between hits at least (default 0.04)")
    parser.add_argument("--max-length", type=float, default=0.5, help="seconds a clip runs at most (default 0.5)")
    parser.add_argument("--start", type=float, default=0.0, help="ignore the recording before this (seconds)")
    parser.add_argument("--end", type=float, default=None, help="and after this")
    parser.add_argument("--bits", type=int, default=16, choices=(16, 24))
    parser.add_argument("--list", action="store_true", help="only show the hits found")
    parser.add_argument("--credit", default=None, help="a line for CREDITS.txt (for CC-BY recordings)")
    args = parser.parse_args()

    written = []
    for path in args.inputs:
        samples, rate = read_any(path)
        first = int(args.start * rate)
        last = int(args.end * rate) if args.end is not None else len(samples)
        samples = samples[first:last]
        hits = find_hits(samples, rate, args.threshold, args.sensitivity, args.min_gap)
        clips = []
        for i, start in enumerate(hits):
            end = hits[i + 1] if i + 1 < len(hits) else len(samples)
            clip = cut(samples, rate, start, end, args.max_length)
            if len(clip) > rate // 200:  # 5 ms at least
                clips.append((start, clip, max(abs(v) for v in clip)))
        loudest = max((peak for _, _, peak in clips), default=0.0)
        print(f"{path}: {len(clips)} hit(s) at {rate} Hz")
        for start, clip, peak in clips:
            force = force_of(peak, loudest, args.force)
            db = 20 * math.log10(peak / loudest) if peak > 0 and loudest > 0 else -99
            when = (start + first) / rate
            if args.surface == "dice":
                folder = os.path.join(args.out, "dice")
                prefix = f"{args.die}_{args.other}_{force}"
            else:
                folder = os.path.join(args.out, args.surface)
                prefix = f"{args.die}_{args.hit}_{force}"
            if args.list:
                print(f"  {when:7.3f} s  {len(clip) / rate * 1000:5.0f} ms  {db:6.1f} dB  -> {prefix}")
                continue
            take = next_take(folder, prefix)
            target = os.path.join(folder, f"{prefix}_{take}.wav")
            write_wav(target, clip, rate, args.bits)
            written.append(target)
            print(f"  {when:7.3f} s  {db:6.1f} dB  {os.path.relpath(target)}")
    if written and args.credit:
        with open(os.path.join(args.out, "CREDITS.txt"), "a", encoding="utf-8") as credits:
            names = ", ".join(sorted({os.path.basename(os.path.dirname(p)) + "/" + os.path.basename(p)
                                      for p in written}))
            credits.write(f"{args.credit}\n    {names}\n")
    if written:
        print(f"{len(written)} clip(s) written. Reload recordings on the Options page to hear them.")


if __name__ == "__main__":
    main()
