"""Generate the royal-blue Just Type keyboard icon without external imaging dependencies."""
from pathlib import Path
import struct

sizes = [16, 20, 24, 32, 40, 48]
images = []
for size in sizes:
    pixels = bytearray()
    glyph = ['01110', '11011', '11011', '11111', '11011', '11011', '11011']
    r_corner = max(2.0, size * 0.22)
    for y in range(size - 1, -1, -1):
        for x in range(size):
            cx = x + 0.5
            cy = y + 0.5
            dx = max(0.0, max(r_corner - cx, cx - (size - r_corner)))
            dy = max(0.0, max(r_corner - cy, cy - (size - r_corner)))
            dist = (dx * dx + dy * dy) ** 0.5
            if dist > r_corner + 0.5:
                pixels.extend([0, 0, 0, 0])
                continue
            gx = int((x / size - .23) / .54 * 5)
            gy = int((y / size - .2) / .6 * 7)
            ink = 0 <= gx < 5 and 0 <= gy < 7 and .23 <= x / size < .77 and .2 <= y / size < .8 and glyph[gy][gx] == '1'
            if ink:
                pixels.extend([255, 255, 255, 255])  # White glyph (BGRA)
            else:
                pixels.extend([235, 99, 37, 255])    # Royal Blue #2563EB (BGRA)
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
