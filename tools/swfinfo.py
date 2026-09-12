#!/usr/bin/env python3
"""Inspect a SWF: stage size, frame rate, and tag inventory.

Run this against the game's own SWF (from your Sonny Legacy Collection install)
to get the exact numbers the replica needs -- the stage rect and frame rate are
stored in the SWF header, so there is no guessing about resolution or tick rate.

    python3 tools/swfinfo.py "/path/to/Sonny.swf"

Nothing is copied or redistributed; it only reports what is in the file.
"""
import collections
import struct
import sys
import zlib

# Tag ids worth calling out when planning extraction.
TAG_NAMES = {
    0: "End", 9: "SetBackgroundColor", 21: "DefineBitsJPEG2",
    35: "DefineBitsJPEG3", 20: "DefineBitsLossless", 36: "DefineBitsLossless2",
    14: "DefineSound", 19: "SoundStreamBlock", 39: "DefineSprite",
    59: "DoInitAction", 12: "DoAction", 82: "DoABC", 76: "SymbolClass",
    87: "DefineBinaryData", 2: "DefineShape", 22: "DefineShape2",
    32: "DefineShape3", 83: "DefineShape4", 37: "DefineEditText",
    48: "DefineFont2", 75: "DefineFont3", 88: "DefineFontName",
    69: "FileAttributes", 77: "Metadata",
}


class Bits:
    def __init__(self, data, pos=0):
        self.data, self.pos, self.bit = data, pos, 0

    def ub(self, n):
        v = 0
        for _ in range(n):
            byte = self.data[self.pos]
            v = (v << 1) | ((byte >> (7 - self.bit)) & 1)
            self.bit += 1
            if self.bit == 8:
                self.bit, self.pos = 0, self.pos + 1
        return v

    def sb(self, n):
        v = self.ub(n)
        if n and (v >> (n - 1)) & 1:
            v -= 1 << n
        return v

    def align(self):
        if self.bit:
            self.bit, self.pos = 0, self.pos + 1


def read_swf(path):
    raw = open(path, "rb").read()
    if len(raw) < 8:
        raise SystemExit("not a SWF: file too short")
    sig, version, length = raw[:3], raw[3], struct.unpack_from("<I", raw, 4)[0]
    if sig == b"FWS":
        body = raw[8:]
    elif sig == b"CWS":
        body = zlib.decompress(raw[8:])
    elif sig == b"ZWS":
        try:
            import lzma
            # SWF LZMA: 4-byte compressed length, 5-byte props, then the stream.
            props, rest = raw[12:17], raw[17:]
            filters = [{"id": lzma.FILTER_LZMA1, "dict_size":
                        struct.unpack_from("<I", props, 1)[0]}]
            body = lzma.LZMADecompressor(format=lzma.FORMAT_RAW,
                                         filters=filters).decompress(rest)
        except Exception as exc:
            raise SystemExit("LZMA SWF could not be decompressed: %s" % exc)
    else:
        raise SystemExit("not a SWF (signature %r)" % sig)
    return sig.decode(), version, length, body


def main(path):
    sig, version, length, body = read_swf(path)

    b = Bits(body)
    nbits = b.ub(5)
    xmin, xmax, ymin, ymax = (b.sb(nbits) for _ in range(4))
    b.align()
    frame_rate = struct.unpack_from("<H", body, b.pos)[0] / 256.0
    frame_count = struct.unpack_from("<H", body, b.pos + 2)[0]
    pos = b.pos + 4

    print("signature      : %s (v%d, %s)" % (sig, version,
          "compressed" if sig != "FWS" else "uncompressed"))
    print("declared size  : %d bytes" % length)
    print("stage          : %dx%d px" % ((xmax - xmin) // 20, (ymax - ymin) // 20))
    print("frame rate     : %g fps" % frame_rate)
    print("frame count    : %d" % frame_count)

    counts, bytes_by_tag = collections.Counter(), collections.Counter()
    while pos + 2 <= len(body):
        code_and_len = struct.unpack_from("<H", body, pos)[0]
        pos += 2
        tag, tag_len = code_and_len >> 6, code_and_len & 0x3F
        if tag_len == 0x3F:
            tag_len = struct.unpack_from("<I", body, pos)[0]
            pos += 4
        counts[tag] += 1
        bytes_by_tag[tag] += tag_len
        pos += tag_len
        if tag == 0:
            break

    print("\ntags (count, bytes):")
    for tag, n in counts.most_common():
        print("  %-22s %5d  %9d" % (TAG_NAMES.get(tag, "Tag%d" % tag), n,
                                    bytes_by_tag[tag]))
    print("\nactionscript   : %s" % ("AS3 (DoABC present)" if counts[82]
                                     else "AS1/AS2 (DoAction/DoInitAction)"))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
