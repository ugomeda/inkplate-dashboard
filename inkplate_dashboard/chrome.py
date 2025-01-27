import hashlib
import io
import itertools
import os
import subprocess
import tempfile

from PIL import Image

GOOGLE_CHROME_PATHS = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/usr/bin/chromium",
]


def get_chrome_path() -> str:
    for chrome_path in GOOGLE_CHROME_PATHS:
        if os.path.exists(chrome_path):
            return chrome_path

    raise Exception("Could not find Chrome/Chromium path")


def screenshot_display() -> tuple[bytes, str]:
    """Makes a screenshot of the html view and return the PNG
    image and a hash.
    """
    with tempfile.TemporaryDirectory() as tmp_dir:
        screenshot_path = os.path.join(tmp_dir, "screenshot.png")
        subprocess.run(
            [
                get_chrome_path(),
                "--headless=new",
                "--disable-gpu",
                "--high-dpi-support=1",
                "--no-sandbox",
                "--force-device-scale-factor=1",
                "--disable-lcd-text",  # B&W display
                f"--screenshot={screenshot_path}",
                "--window-size=825,1200",
                "--virtual-time-budget=10000",
                "--timeout=5000",
                "http://127.0.0.1:8000/live/html",
            ],
            check=False,
            timeout=10,
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
        img = img.rotate(90, expand=1)

        hash = hashlib.sha256(img.tobytes()).hexdigest()

        output = io.BytesIO()
        img.save(output, "png")

        return output.getvalue(), hash
