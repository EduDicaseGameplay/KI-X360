"""Convert P6 PPM files to PNG (no dependencies). Usage: ppm2png.py FILE.ppm..."""
import struct, sys, zlib

def png(path_in):
    data = open(path_in, 'rb').read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    pix = parts[3]
    raw = b''.join(b'\0' + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path_in[:-4] + '.png', 'wb').write(out)

for p in sys.argv[1:]:
    png(p)
