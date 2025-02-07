from importlib import resources

import sass
from starlette.concurrency import run_in_threadpool
from starlette.endpoints import HTTPEndpoint
from starlette.requests import Request
from starlette.responses import HTMLResponse, PlainTextResponse, Response

from inkplate_dashboard.display import generate_html
from inkplate_dashboard.state import get_state


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
            await run_in_threadpool(generate_html, get_state(request).display)
        )


class DisplayPngEndpoint(HTTPEndpoint):
    async def get(self, request: Request) -> Response:
        state = get_state(request)
        image, etag = await state.chromium.screenshot()
        headers = {
            "etag": etag,
            "Cache-Control": f"max-age={state.display.refresh_interval_sec}",
        }

        # Tell the display to avoid a refresh if the image did not change
        if request.headers.get("if-none-match") == etag:
            return Response(status_code=304, headers=headers)

        return PlainTextResponse(content=image, media_type="image/png", headers=headers)
