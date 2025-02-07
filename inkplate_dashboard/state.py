from dataclasses import dataclass
from typing import cast

from starlette.requests import Request

from inkplate_dashboard.chrome import ChromiumInstance
from inkplate_dashboard.config import DisplayConfiguration


@dataclass
class State:
    chromium: ChromiumInstance
    display: DisplayConfiguration


def get_state(request: Request) -> State:
    return cast(State, request.state)
