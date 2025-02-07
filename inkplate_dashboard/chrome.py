import asyncio
import contextlib
import hashlib
import io
import itertools
from collections.abc import AsyncIterator
from dataclasses import dataclass

from PIL import Image
from playwright.async_api import Browser, async_playwright
from starlette.applications import Starlette


def _convert_image(screenshot: bytes) -> tuple[bytes, str]:
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
    img = Image.open(io.BytesIO(screenshot)).convert("RGB")
    img = img.crop((0, 0, 825, 1200))
    img = img.quantize(kmeans=0, palette=palette_img).convert("L")

    hash = hashlib.sha256(img.tobytes()).hexdigest()

    output = io.BytesIO()
    img.save(output, "png")

    return output.getvalue(), hash


@dataclass
class ChromiumInstance:
    browser: Browser

    async def screenshot(
        self, extra_headers: dict[str, str] | None = None
    ) -> tuple[bytes, str]:
        page = await self.browser.new_page(
            viewport={"width": 825, "height": 1200}, extra_http_headers=extra_headers
        )
        try:
            await page.goto("http://127.0.0.1:8000/live/html", timeout=5000)
            screenshot = await page.screenshot()
        finally:
            await page.close()

        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, _convert_image, screenshot)


@contextlib.asynccontextmanager
async def chrome_lifespan(app: Starlette) -> AsyncIterator[dict[str, ChromiumInstance]]:
    async with async_playwright() as playwright:
        browser = await playwright.chromium.launch(
            channel="chromium",
            args=["--disable-lcd-text", "--high-dpi-support=1", "--disable-gpu"],
        )
        yield {"chromium": ChromiumInstance(browser)}
