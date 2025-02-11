from datetime import datetime, timedelta
from importlib import resources

import sass
from babel.dates import get_timezone
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
        display = get_display(request)

        # Fetch the image
        extra_headers = {}
        if BATTERY_HEADER in request.headers:
            extra_headers[BATTERY_HEADER] = request.headers[BATTERY_HEADER]

        image, etag = await get_chromium(request).screenshot(
            extra_headers=extra_headers
        )

        # Caculate the next refresh, we want to force an update a bit
        # after midnight to force an update of the date in the header
        now = datetime.now(tz=get_timezone(display.timezone))
        tomorrow = (now + timedelta(days=1)).replace(
            hour=0, minute=1, second=0, microsecond=0
        )
        time_until_tomorrow = int((tomorrow - now).total_seconds())
        next_refresh = min(time_until_tomorrow, display.refresh_interval_sec)

        headers = {
            "etag": etag,
            "x-inkplate-next-refresh": str(next_refresh),
        }

        # Tell the display to avoid a refresh if the image did not change
        if request.headers.get("if-none-match") == etag:
            return Response(status_code=304, headers=headers)

        return PlainTextResponse(content=image, media_type="image/png", headers=headers)
