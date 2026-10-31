"""Generate the original monochrome keyboard icon without imaging dependencies."""
from pathlib import Path
import struct

sizes = [16, 20, 24, 32, 40, 48]
images = []
for size in sizes:
    pixels = bytearray()
    glyph = ['01110', '11011', '11011', '11111', '11011', '11011', '11011']
    for y in range(size - 1, -1, -1):
        for x in range(size):
            inside = 1 <= x < size - 1 and 1 <= y < size - 1
            gx = int((x / size - .23) / .54 * 5)
            gy = int((y / size - .2) / .6 * 7)
            ink = 0 <= gx < 5 and 0 <= gy < 7 and .23 <= x / size < .77 and .2 <= y / size < .8 and glyph[gy][gx] == '1'
            color = 0 if ink or not inside else 255
            pixels.extend([color, color, color, 255])
    mask = bytes(((size + 31) // 32) * 4 * size)
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, len(pixels), 0, 0, 0, 0)
    images.append(header + pixels + mask)
offset = 6 + 16 * len(sizes)
output = bytearray(struct.pack('<HHH', 0, 1, len(sizes)))
for size, image in zip(sizes, images):
    output.extend(struct.pack('<BBBBHHII', size, size, 0, 0, 1, 32, len(image), offset))
    offset += len(image)
for image in images:
    output.extend(image)
(Path(__file__).resolve().parents[1] / 'assets' / 'keyboard.ico').write_bytes(output)
