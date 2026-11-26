#!/usr/bin/env python3
# Generate internal/jenc/jtables.bin from Python's codecs.
#
# Tables (all little-endian uint32, value 0 = undefined):
#   cp932 single  : 256 entries
#   cp932 double  : lead 0x81..0xFC (124) x trail 0x40..0xFC (189)
#   euc-jis-2004 plane 1 : 94 x 94  (value | 0x80000000 = index of sequence)
#   euc-jis-2004 plane 2 : 94 x 94  (same)
#   jis x 0212 (euc-jp 0x8F) : 94 x 94
#   sequences : count, then (len, runes...) each
#
# Usage: python3 tools/gen_jtables.py internal/jenc/jtables.bin
import struct, sys, zlib

out = []
seqs = []

def put(v):
    out.append(struct.pack('<I', v))

def val(s):
    if len(s) == 1:
        return ord(s)
    seqs.append(s)
    return 0x80000000 | (len(seqs) - 1)

# cp932 single
for b in range(256):
    try:
        s = bytes([b]).decode('cp932')
        put(ord(s) if len(s) == 1 else 0)
    except UnicodeDecodeError:
        put(0)
# cp932 double
for lead in range(0x81, 0xFD):
    for trail in range(0x40, 0xFD):
        try:
            s = bytes([lead, trail]).decode('cp932')
            put(ord(s) if len(s) == 1 else 0)
        except UnicodeDecodeError:
            put(0)
# euc-jis-2004 plane 1 and 2
for prefix in (b'', b'\x8f'):
    for r in range(94):
        for c in range(94):
            bs = prefix + bytes([0xA1 + r, 0xA1 + c])
            try:
                s = bs.decode('euc_jis_2004')
                put(val(s))
            except UnicodeDecodeError:
                put(0)
# jis x 0212 via euc_jp
for r in range(94):
    for c in range(94):
        bs = b'\x8f' + bytes([0xA1 + r, 0xA1 + c])
        try:
            s = bs.decode('euc_jp')
            put(ord(s) if len(s) == 1 else 0)
        except UnicodeDecodeError:
            put(0)
put(len(seqs))
for s in seqs:
    put(len(s))
    for ch in s:
        put(ord(ch))

data = b''.join(out)
with open(sys.argv[1], 'wb') as f:
    f.write(zlib.compress(data, 9))
print(len(data), 'bytes raw,', len(seqs), 'sequences')
