import io
import itertools
import os
import subprocess
import tempfile

from PIL import Image

from inkplate_dashboard.config import DisplayConfiguration

GOOGLE_CHROME_PATHS = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/usr/bin/chromium",
]


def get_chrome_path() -> str:
    for chrome_path in GOOGLE_CHROME_PATHS:
        if os.path.exists(chrome_path):
            return chrome_path

    raise Exception("Could not find Chrome/Chromium path")


def screenshot_display(display: DisplayConfiguration) -> bytes:
    with tempfile.TemporaryDirectory() as tmp_dir:
        screenshot_path = os.path.join(tmp_dir, "screenshot.png")
        subprocess.run(
            [
                get_chrome_path(),
                "--headless=old",
                "--disable-gpu",
                "--high-dpi-support=1",
                "--no-sandbox",
                "--force-device-scale-factor=1",
                "--disable-lcd-text",  # B&W display
                f"--screenshot={screenshot_path}",
                f"--window-size={display.width},{display.height}",
                "--timeout=10000",  # load fonts
                "http://127.0.0.1:8000/live/html",
            ],
            check=False,
        )

        # Reduce the palette to 8 colors to reduce file size and control
        # dithering
        palette = list(
            itertools.chain(
                *[
                    [
                        round((i + 1 / 2) * (256 / 8)),
                        round((i + 1 / 2) * (256 / 8)),
                        round((i + 1 / 2) * (256 / 8)),
                    ]
                    for i in range(8)
                ]
            )
        )
        palette_img = Image.new("P", (1, 1))
        palette_img.putpalette(palette * 32)
        img = Image.open(screenshot_path).convert("RGB")
        img = img.quantize(kmeans=0, palette=palette_img).convert("L")

        output = io.BytesIO()
        img.save(output, "png")

        return output.getvalue()
