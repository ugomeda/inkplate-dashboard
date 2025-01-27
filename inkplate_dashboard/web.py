from importlib import resources

import sass
from starlette.concurrency import run_in_threadpool
from starlette.endpoints import HTTPEndpoint
from starlette.requests import Request
from starlette.responses import HTMLResponse, PlainTextResponse

from inkplate_dashboard.chrome import screenshot_display
from inkplate_dashboard.display import generate_html


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
        return HTMLResponse(await run_in_threadpool(generate_html, request.app.display))


class DisplayPngEndpoint(HTTPEndpoint):
    async def get(self, request: Request) -> PlainTextResponse:
        image, _hash = await run_in_threadpool(screenshot_display)
        return PlainTextResponse(image, media_type="image/png")
