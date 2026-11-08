import asyncio
import contextlib
import hashlib
import io
import itertools
from collections.abc import AsyncGenerator
from dataclasses import dataclass

from PIL import Image
from playwright.async_api import Browser, async_playwright
from starlette.applications import Starlette

from inkplate_dashboard.displays import Display


def _convert_image(screenshot: bytes, display: Display) -> tuple[bytes, str]:
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
    img = img.crop((0, 0, display.width, display.height))
    img = img.quantize(kmeans=0, palette=palette_img).convert("L")

    hash = hashlib.sha256(img.tobytes()).hexdigest()

    output = io.BytesIO()
    img.save(output, "png")

    return output.getvalue(), hash


@dataclass
class FirefoxInstance:
    browser: Browser

    async def screenshot(
        self,
        display_id: str,
        display: Display,
        extra_headers: dict[str, str] | None = None,
    ) -> tuple[bytes, str]:
        page = await self.browser.new_page(
            viewport={"width": display.width, "height": display.height},
            extra_http_headers=extra_headers,
        )
        try:
            await page.goto(
                f"http://127.0.0.1:8000/live/{display_id}/html", timeout=10000
            )
            screenshot = await page.screenshot()
        finally:
            await page.close()

        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, _convert_image, screenshot, display)


@contextlib.asynccontextmanager
async def firefox_lifespan(
    app: Starlette,
) -> AsyncGenerator[dict[str, FirefoxInstance]]:
    async with async_playwright() as playwright:
        browser = await playwright.firefox.launch(headless=True)
        try:
            yield {"firefox": FirefoxInstance(browser)}
        finally:
            await browser.close()
