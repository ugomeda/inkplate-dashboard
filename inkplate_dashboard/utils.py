from typing import cast

from starlette.requests import Request

from inkplate_dashboard.chrome import ChromiumInstance
from inkplate_dashboard.config import DisplayConfiguration
from inkplate_dashboard.constants import BATTERY_HEADER


def get_chromium(request: Request) -> ChromiumInstance:
    return cast(ChromiumInstance, request.state.chromium)


def get_display(request: Request) -> DisplayConfiguration:
    return cast(DisplayConfiguration, request.app.state.display)


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
