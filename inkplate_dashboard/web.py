from importlib import resources

import sass
from starlette.concurrency import run_in_threadpool
from starlette.endpoints import HTTPEndpoint
from starlette.requests import Request
from starlette.responses import HTMLResponse, PlainTextResponse, Response

from inkplate_dashboard.constants import BATTERY_HEADER
from inkplate_dashboard.display import generate_html
from inkplate_dashboard.utils import (
    get_chromium,
    get_display,
    get_voltage,
)


def compile_styles() -> str:
    file = resources.files("inkplate_dashboard").joinpath("styles/styles.scss")
    with file.open("r") as fd:
        return sass.compile(string=fd.read())


class StylesEndpoint(HTTPEndpoint):
    async def get(self, request: Request) -> PlainTextResponse:
        return PlainTextResponse(
            await run_in_threadpool(compile_styles), media_type="text/css"
        )


class DisplayHtmlEndpoint(HTTPEndpoint):
    async def get(self, request: Request) -> HTMLResponse:
        return HTMLResponse(
            await run_in_threadpool(
                generate_html, get_display(request), get_voltage(request)
            )
        )


class DisplayPngEndpoint(HTTPEndpoint):
    async def get(self, request: Request) -> Response:
        extra_headers = {}
        if BATTERY_HEADER in request.headers:
            extra_headers[BATTERY_HEADER] = request.headers[BATTERY_HEADER]

        image, etag = await get_chromium(request).screenshot(
            extra_headers=extra_headers
        )
        headers = {
            "etag": etag,
            "Cache-Control": f"max-age={get_display(request).refresh_interval_sec}",
        }

        # Tell the display to avoid a refresh if the image did not change
        if request.headers.get("if-none-match") == etag:
            return Response(status_code=304, headers=headers)

        return PlainTextResponse(content=image, media_type="image/png", headers=headers)
