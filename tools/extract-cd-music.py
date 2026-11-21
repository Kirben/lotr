"""Extract Enhanced CD background tracks and scripted cues from a raw BIN/CUE image."""

import argparse
import re
import wave
from pathlib import Path


SECTOR_BYTES = 2352
TRACKS = (2, 3, 4)


def read_cue(cue):
    image = None
    tracks = {}
    for line in cue.read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        file_entry = re.fullmatch(r'FILE\s+"([^"]+)"\s+BINARY', line, re.I)
        if file_entry:
            if image is not None:
                raise ValueError("Expected a single BIN file")
            image = cue.parent / file_entry[1]
        track = re.fullmatch(r"TRACK\s+(\d+)\s+(\S+)", line, re.I)
        if track:
            number, mode = int(track[1]), track[2].upper()
            tracks[number] = (mode, None)
        index = re.fullmatch(r"INDEX\s+01\s+(\d+):(\d+):(\d+)", line, re.I)
        if index:
            minutes, seconds, sectors = map(int, index.groups())
            if seconds >= 60 or sectors >= 75 or not tracks:
                raise ValueError("Invalid INDEX 01 position")
            tracks[number] = mode, (minutes * 60 + seconds) * 75 + sectors
    if image is None or not tracks or tracks.get(1, (None,))[0] != "MODE1/2352":
        raise ValueError("Expected a single mixed-mode Enhanced CD BIN/CUE")
    positions = [entry[1] for entry in tracks.values()]
    if any(position is None for position in positions) or positions != sorted(set(positions)):
        raise ValueError("Expected increasing INDEX 01 positions")
    if any(tracks.get(number, (None,))[0] != "AUDIO" or number + 1 not in tracks
           for number in TRACKS):
        raise ValueError("Expected background audio tracks 02 through 04")
    if image.stat().st_size % SECTOR_BYTES or positions[-1] * SECTOR_BYTES >= image.stat().st_size:
        raise ValueError("BIN size does not match the cue sheet")
    return image, tracks


def extract(image, tracks, destination):
    with image.open("rb") as source:
        ranges = [(f"cd-track-{number:02}.wav", tracks[number][1], tracks[number + 1][1])
                  for number in TRACKS]
        # DOS SOUND durations come from the executable cue table.
        cues = (
            (0, 2, 239),
            (1, 3, 570),
            (2, 4, 535),
            (3, 5, 547),
            (4, 6, 337),
            (5, 7, 11),
            (6, 8, 16),
            (7, 9, 6),
            (8, 10, 24),
            (9, 11, 11),
            (0x0A, 12, 11),
        )
        for cue, number, seconds in cues:
            if tracks.get(number, (None,))[0] != "AUDIO" or number + 1 not in tracks:
                continue
            first = tracks[number][1]
            end = first + seconds * 75
            if end > tracks[number + 1][1]:
                raise ValueError(f"Track {number:02} is too short for cue {cue:02X}")
            ranges.append((f"cd-cue-{cue:02X}.wav", first, end))
        for name, first, end in ranges:
            remaining = (end - first) * SECTOR_BYTES
            target = destination / name
            if target.exists():
                with wave.open(str(target), "rb") as song:
                    if (song.getnchannels(), song.getsampwidth(), song.getframerate(), song.getnframes()) != (
                        2, 2, 44100, remaining // 4
                    ):
                        raise ValueError(f"Existing track has an unexpected format or length: {target}")
                print(f"Using existing {target}")
                continue
            source.seek(first * SECTOR_BYTES)
            output = target.open("xb")
            try:
                with output, wave.open(output, "wb") as song:
                    song.setparams((2, 2, 44100, remaining // 4, "NONE", "not compressed"))
                    while remaining:
                        block = source.read(min(remaining, 1024 * SECTOR_BYTES))
                        if not block:
                            raise OSError("BIN ended before the track boundary")
                        song.writeframesraw(block)
                        remaining -= len(block)
            except BaseException:
                target.unlink()
                raise
            print(f"Extracted {target}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cue", type=Path)
    parser.add_argument("data_directory", type=Path)
    args = parser.parse_args()
    if not args.data_directory.is_dir():
        parser.error("Data directory must already exist")
    try:
        extract(*read_cue(args.cue), args.data_directory)
    except (OSError, ValueError, wave.Error) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
