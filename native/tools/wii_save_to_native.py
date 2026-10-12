#!/usr/bin/env python3
"""Convert a Wii (big-endian) Super Mario Galaxy GameData.bin to the native
little-endian layout the Petari native port reads and writes.

The layout is the same; only multi-byte fields change byte order. Field widths
follow the game's own serializers (Petari src/Game/System): GameDataHolder
chunks PLAY (u8, u32, u16), FLG1 (u16 words), PCE1 (16 x u16), SPN1 (u8 count,
per galaxy u16 hash, u16 size, u8 scenarios, u8 0, per scenario u16 size then
bytes), VLE1 (u16 hash, u16 value pairs), GALA (u16 count, content header of
u16 pairs, records: u16 name hash, u8, u8, 8 x u16 coins); ConfigDataHolder
chunks CONF (u8), MII (bytes), MISC (u8, s64); sysconf SYSC (content header,
s64, s64, u32 by attribute offset). The file header, index and checksum
(MR::calcCheckSum over host u16 words) are rebuilt for the host order.
The input is only read; the output must not exist yet.
"""
import struct, sys
from pathlib import Path

def swap(buf, off, n):
    buf[off:off + n] = buf[off:off + n][::-1]

def swap_u16s(buf, start, end):
    for o in range(start, end - 1, 2):
        swap(buf, o, 2)

def content_header(buf, at):
    """BinaryDataContentHeaderSerializer: u16 count, u16 data size, (u16 hash, u16 offset)*.
    Returns (header bytes, data size, offsets) read big-endian, and swaps it in place."""
    count, size = struct.unpack_from('>HH', buf, at)
    offsets = [struct.unpack_from('>HH', buf, at + 4 + 4 * i) for i in range(count)]
    swap_u16s(buf, at, at + 4 + 4 * count)
    return 4 + 4 * count, size, offsets

def convert_payload(sig, buf, start, end):
    if sig == 'PLAY':
        swap(buf, start + 1, 4)
        if end - start >= 7:
            swap(buf, start + 5, 2)
    elif sig in ('FLG1', 'PCE1', 'VLE1'):
        swap_u16s(buf, start, end)
    elif sig == 'SPN1':
        pos = start + 1
        for _ in range(buf[start]):
            block = struct.unpack_from('>H', buf, pos + 2)[0]
            scenarios = buf[pos + 4]
            at = pos + 6
            for _ in range(scenarios):
                size = struct.unpack_from('>H', buf, at)[0]
                swap(buf, at, 2)
                at += size
            swap(buf, pos, 2)
            swap(buf, pos + 2, 2)
            pos += block
    elif sig == 'GALA':
        galaxies = struct.unpack_from('>H', buf, start)[0]
        swap(buf, start, 2)
        header, size, offsets = content_header(buf, start + 2)
        names = sorted(offsets, key=lambda e: e[1])
        if len(offsets) != 4:
            raise ValueError('GALA attributes %d' % len(offsets))
        # By the game's order: mGalaxyName (u16), mPowerStarFlag (u8),
        # mFirstPlayFlag (u8), mMaxCoinNum (8 x u16).
        name_off, star_off, first_off, coin_off = [o for _, o in offsets]
        rec = start + 2 + header
        for i in range(galaxies):
            r = rec + i * size
            swap(buf, r + name_off, 2)
            swap_u16s(buf, r + coin_off, r + coin_off + 16)
    elif sig in ('CONF', 'MII '):
        pass
    elif sig == 'MISC':
        swap(buf, start + 1, 8)
    elif sig == 'SYSC':
        header, size, offsets = content_header(buf, start)
        data = start + header
        widths = [8, 8, 4]  # mTimeAnnounced, mTimeSent, mSentBytes, in serializer order
        for (_, off), w in zip(offsets, widths):
            swap(buf, data + off, w)
    else:
        raise ValueError('unknown chunk ' + sig)

def convert_holder(buf, start, end):
    """BinaryDataChunkHolder: u8 1, u8 count, 2 pad, then (u32 sig, u32 hash, u32 size) chunks."""
    if not any(buf[start:end]):
        return []
    if buf[start] != 1:
        raise ValueError('holder header %s' % buf[start:start + 4].hex())
    seen, off = [], start + 4
    for _ in range(buf[start + 1]):
        sig_raw, _h, size = struct.unpack_from('>3I', buf, off)
        sig = sig_raw.to_bytes(4, 'big').decode('latin-1')
        convert_payload(sig, buf, off + 12, off + size)
        for k in range(3):
            swap(buf, off + 4 * k, 4)
        seen.append(sig)
        off += size
    return seen

def check_sum(data):
    total = inverse = 0
    for (word,) in struct.iter_unpack('<H', data[: len(data) // 2 * 2]):
        total = (total + word) & 0xFFFF
        inverse = (inverse + (~word & 0xFFFF)) & 0xFFFF
    return total << 16 | inverse

def convert(data):
    buf = bytearray(data)
    stored, version, count, size = struct.unpack_from('>4I', buf, 0)
    entries = []
    for i in range(count):
        name, off = struct.unpack_from('>12sI', buf, 16 + 16 * i)
        entries.append((name.split(b'\0', 1)[0].decode('ascii'), off))
    order = sorted(entries, key=lambda e: e[1])
    report = {}
    for i, (name, off) in enumerate(order):
        end = order[i + 1][1] if i + 1 < len(order) else size
        report[name] = convert_holder(buf, off, end)
    for i in range(count):
        swap(buf, 16 + 16 * i + 12, 4)
    for k in range(1, 4):
        swap(buf, 4 * k, 4)
    struct.pack_into('<I', buf, 0, check_sum(bytes(buf[4:size])))
    return bytes(buf), report

if __name__ == '__main__':
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    if dst.exists():
        sys.exit('output exists: %s' % dst)
    out, report = convert(src.read_bytes())
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(out)
    for name, sigs in report.items():
        print(name, ' '.join(sigs) if sigs else '(empty)')

