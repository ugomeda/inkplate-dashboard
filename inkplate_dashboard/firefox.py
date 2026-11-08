import asyncio
import contextlib
import hashlib
import io
from collections.abc import AsyncGenerator
from dataclasses import dataclass

from PIL import Image
from playwright.async_api import Browser, async_playwright
from starlette.applications import Starlette

from inkplate_dashboard.displays import Display


def _convert_image(screenshot: bytes, display: Display) -> tuple[bytes, str]:
    # Build a 256-color palette from the display configuration
    colors = [rgb for rgb, _ in display.palette]
    palette_data = [channel for rgb in colors for channel in rgb]

    palette_img = Image.new("P", (1, 1))
    palette_img.putpalette((palette_data * (768 // len(palette_data) + 1))[:768])

    # Crop and quantize
    img = Image.open(io.BytesIO(screenshot)).convert("RGB")
    img = img.crop((0, 0, display.width, display.height))

    img = img.quantize(
        palette=palette_img,
        dither=Image.Dither.FLOYDSTEINBERG,
    )

    # Normalize duplicated palette entries to 0..N-1
    img.frombytes(bytes(index % len(colors) for index in img.tobytes()))

    # Preserve the output format expected by each display
    if display.png_mode == "L":
        img = img.convert("L")

    img = img.rotate(display.rotate, expand=True)

    hash_value = hashlib.sha256(img.tobytes()).hexdigest()

    output = io.BytesIO()
    img.save(output, "PNG")

    return output.getvalue(), hash_value


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
