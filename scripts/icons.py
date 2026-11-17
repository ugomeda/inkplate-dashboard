import argparse
import re
from pathlib import Path

from PIL import Image


def convert_icon(path: Path, size: int) -> bytes:
    image = Image.open(path).convert("RGBA")
    image = image.resize((size, size), Image.Resampling.LANCZOS)
    alpha = image.getchannel("A")
    image = alpha.point(lambda value: 255 if value >= 128 else 0).convert("1")

    # Adafruit GFX drawBitmap format: MSB-first, row-aligned.
    bitmap = bytearray()
    for y in range(size):
        for x in range(0, size, 8):
            byte = 0
            for bit in range(8):
                if x + bit < size and image.getpixel((x + bit, y)):
                    byte |= 0x80 >> bit

            bitmap.append(byte)

    return bytes(bitmap)


def generate_header(sources: list[Path], output: Path, size: int) -> None:
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        f"inline constexpr int ICON_SIZE = {size};",
        "",
    ]

    for source in sources:
        name = re.sub(r"\W+", "_", source.stem).upper()
        bitmap = convert_icon(source, size)

        lines.append(f"inline constexpr uint8_t ICON_{name}[] = {{")

        for offset in range(0, len(bitmap), 12):
            chunk = bitmap[offset : offset + 12]
            values = ", ".join(f"0x{byte:02X}" for byte in chunk)
            lines.append(f"    {values},")

        lines.extend(["};", ""])

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("icons", nargs="+", type=Path)
    parser.add_argument("--size", type=int, default=36)
    args = parser.parse_args()

    generate_header(args.icons, args.output, args.size)
