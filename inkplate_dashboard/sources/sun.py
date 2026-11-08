import logging
from dataclasses import dataclass
from datetime import datetime
from typing import Any, cast

import requests

from inkplate_dashboard.cache import AppCache, CacheNS
from inkplate_dashboard.config import DisplayConfiguration

SUNRISE_URL = "https://api.met.no/weatherapi/sunrise/3.0/sun"

logger = logging.getLogger(__name__)


@dataclass
class SunData:
    sunrise: datetime
    sunset: datetime


def _cached_request(
    cache: AppCache, config: DisplayConfiguration, now: datetime
) -> dict[str, Any]:
    params = {
        "lat": str(round(config.location[0], 4)),
        "lon": str(round(config.location[1], 4)),
        "date": now.date().isoformat(),
    }
    cache_key = str(params)

    cached = cache.get(CacheNS.sun, cache_key)
    if cached is not None:
        logger.info("using cache for sunset/sunrise")
        return cast("dict[str, Any]", cached)

    response = requests.get(
        SUNRISE_URL,
        params=params,
        headers={
            "User-Agent": "github.com/ugomeda/inkplate-dashboard",
            "Accept": "application/json",
        },
        timeout=15,
    )
    response.raise_for_status()
    data = response.json()

    cache.set(CacheNS.sun, cache_key, data)

    return cast("dict[str, Any]", data)


def fetch_sun(
    cache: AppCache,
    config: DisplayConfiguration,
    now: datetime,
) -> SunData:
    data = _cached_request(cache, config, now)
    properties = data["properties"]
    assert properties["body"] == "Sun"  # sanity check

    sunrise = properties["sunrise"]["time"]
    sunset = properties["sunset"]["time"]

    return SunData(
        sunrise=datetime.fromisoformat(sunrise).astimezone(config.tz()),
        sunset=datetime.fromisoformat(sunset).astimezone(config.tz()),
    )
