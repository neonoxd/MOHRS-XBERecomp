"""Extract an XBE's DXT1 title/save image to a multi-size Windows icon and resource file.

Uses only Python's standard library; the generated artwork belongs in build output.
  python tools/extract_xbe_icon.py assets/xbe/default.xbe '$$XTIMAGE' out/icons/MOHRS.ico
"""
import math
from pathlib import Path
import struct
import sys


def section_data(path, section):
    data = Path(path).read_bytes()
    if data[:4] != b"XBEH":
        raise ValueError(f"{path}: not an XBE")
    base, = struct.unpack_from("<I", data, 0x104)
    count, table = struct.unpack_from("<II", data, 0x11C)
    for i in range(count):
        _, _, _, offset, size, name = struct.unpack_from("<6I", data, table - base + i * 56)
        name -= base
        if data[name:data.index(b"\0", name)].decode("ascii") == section:
            if offset + size > len(data):
                raise ValueError(f"{path}: truncated {section}")
            return data[offset:offset + size]
    raise ValueError(f"{path}: missing {section}")


def decode_image(data):
    if data[:4] != b"XPR0":
        raise ValueError("Expected an XPR0 image")
    total, header = struct.unpack_from("<II", data, 4)
    fmt, = struct.unpack_from("<I", data, 24)
    width, height = 1 << ((fmt >> 20) & 15), 1 << ((fmt >> 24) & 15)
    if (fmt >> 8) & 255 != 0x0C or width != height or not 16 <= width <= 256:
        raise ValueError("Expected a square DXT1 icon between 16 and 256 pixels")
    if not 32 <= header <= total <= len(data) or header + width * height // 2 > total:
        raise ValueError("Truncated XPR0 texture")

    def rgb565(value):
        r, g, b = value >> 11, (value >> 5) & 63, value & 31
        return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255)

    pixels = [None] * (width * height)
    offset = header
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            c0, c1, indices = struct.unpack_from("<HHI", data, offset)
            offset += 8
            a, b = rgb565(c0), rgb565(c1)
            if c0 > c1:
                colors = [a, b, tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
                          tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,)]
            else:
                colors = [a, b, tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0)]
            for j in range(16):
                pixels[(y + j // 4) * width + x + j % 4] = colors[(indices >> (2 * j)) & 3]
    return width, pixels


def resize(pixels, source, size):
    """Area-average in premultiplied alpha, keeping the original pixels at native size."""
    if size == source:
        return pixels
    scale = source / size
    result = []
    for y in range(size):
        for x in range(size):
            left, top, right, bottom = x * scale, y * scale, (x + 1) * scale, (y + 1) * scale
            color, alpha = [0.0, 0.0, 0.0], 0.0
            for sy in range(math.floor(top), math.ceil(bottom)):
                for sx in range(math.floor(left), math.ceil(right)):
                    weight = (min(right, sx + 1) - max(left, sx)) * (min(bottom, sy + 1) - max(top, sy))
                    pixel = pixels[sy * source + sx]
                    alpha += pixel[3] * weight
                    for channel in range(3):
                        color[channel] += pixel[channel] * pixel[3] * weight
            rgb = tuple(round(value / alpha) for value in color) if alpha else (0, 0, 0)
            result.append(rgb + (round(alpha / (scale * scale)),))
    return result


def bitmap(pixels, size):
    # ICO DIBs are bottom-up BGRA followed by a 1-bit transparency mask, each row DWORD-aligned.
    color = bytearray()
    stride = ((size + 31) // 32) * 4
    mask = bytearray(stride * size)
    for y in range(size):
        for x in range(size):
            r, g, b, a = pixels[(size - 1 - y) * size + x]
            color.extend((b, g, r, a))
            if a == 0:
                mask[y * stride + x // 8] |= 0x80 >> (x % 8)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(color), 0, 0, 0, 0)
    return header + color + mask


def extract(path, section, output):
    source, pixels = decode_image(section_data(path, section))
    sizes = [size for size in (16, 24, 32, 48, 64, 128, 256) if size <= source]
    images = [bitmap(resize(pixels, source, size), size) for size in sizes]
    offset = 6 + len(images) * 16
    directory = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    for size, image in zip(sizes, images):
        directory.extend(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset))
        offset += len(image)
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(directory + b"".join(images))
    # Resource ID 1 is loaded by the runtime's main, splash and compilation windows.
    resource = f'1 ICON "{output.as_posix()}"\n'
    output.with_suffix(".rc").write_text(resource, encoding="utf-8")
    print(f"{path}: {section} ({source}x{source}) -> {output}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    try:
        extract(*sys.argv[1:])
    except (ValueError, OSError, struct.error) as error:
        raise SystemExit(str(error)) from error
