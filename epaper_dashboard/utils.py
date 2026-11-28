from typing import cast

from starlette.exceptions import HTTPException
from starlette.requests import Request

from epaper_dashboard.cache import AppCache
from epaper_dashboard.config import DisplayConfiguration
from epaper_dashboard.constants import BATTERY_HEADER
from epaper_dashboard.firefox import FirefoxInstance


def get_firefox(request: Request) -> FirefoxInstance:
    return cast("FirefoxInstance", request.state.firefox)


def get_display(request: Request, display_id: str) -> DisplayConfiguration:
    try:
        return cast("DisplayConfiguration", request.app.state.displays[display_id])
    except KeyError:
        raise HTTPException(status_code=404, detail="Display not found") from None


def get_cache(request: Request) -> AppCache:
    return cast("AppCache", request.app.state.cache)


def get_voltage(request: Request) -> float | None:
    voltage = request.headers.get(BATTERY_HEADER)
    if voltage is None:
        return None
    if not voltage.endswith("V"):
        return None

    try:
        return float(voltage[:-1])
    except ValueError:
        return None
