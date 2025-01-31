from starlette.applications import Starlette
from starlette.routing import Mount, Route
from starlette.staticfiles import StaticFiles

from inkplate_dashboard.chrome import chrome_lifespan
from inkplate_dashboard.config import parse_config
from inkplate_dashboard.web import (
    DisplayHtmlEndpoint,
    DisplayPngEndpoint,
    StylesEndpoint,
)

routes = [
    Route("/live/html", DisplayHtmlEndpoint),
    Route("/live/png", DisplayPngEndpoint),
    Route("/static/styles.css", StylesEndpoint),
    Mount("/static", app=StaticFiles(packages=["inkplate_dashboard"]), name="static"),
]

app = Starlette(routes=routes, lifespan=chrome_lifespan)

with open("config.toml", "rb") as fd:
    config = parse_config(fd)


app.display = config.display  # type: ignore[attr-defined]
