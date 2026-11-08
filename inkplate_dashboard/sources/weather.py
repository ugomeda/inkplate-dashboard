import logging
from dataclasses import dataclass
from datetime import date, datetime, time, timedelta, tzinfo
from typing import Any, cast

import requests

from inkplate_dashboard.cache import AppCache, CacheNS
from inkplate_dashboard.config import DisplayConfiguration

logger = logging.getLogger(__name__)

METNO_URL = "https://api.met.no/weatherapi/locationforecast/2.0/compact"


@dataclass
class WeatherForecast:
    temperature: float
    conditions: str  # MET.no symbol_code, e.g. "partlycloudy_day"
    wind_speed: float  # m/s
    wind_from_direction: int  # degrees
    precipitation: float  # mm
    at: datetime


@dataclass
class WeatherDailyForecast:
    min_temperature: float
    max_temperature: float
    conditions: str
    at: datetime


@dataclass
class WeatherData:
    current: WeatherForecast
    hourly: list[WeatherForecast]
    daily: list[WeatherDailyForecast]
    daily_min_temperature: float
    daily_max_temperature: float
    hourly_has_rain: bool


def _parse_time(value: str, tz: tzinfo) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(tz)


def _get_weather_forecast(
    timeseries: list[dict[str, Any]], tz: tzinfo
) -> WeatherForecast:
    return WeatherForecast(
        temperature=timeseries[0]["data"]["instant"]["details"]["air_temperature"],
        conditions=timeseries[0]["data"]["next_1_hours"]["summary"]["symbol_code"],
        wind_speed=timeseries[0]["data"]["instant"]["details"]["wind_speed"],
        wind_from_direction=round(
            timeseries[0]["data"]["instant"]["details"]["wind_from_direction"]
        ),
        precipitation=sum(
            forecast["data"]["next_1_hours"]["details"]["precipitation_amount"]
            for forecast in timeseries
        ),
        at=_parse_time(timeseries[0]["time"], tz),
    )


def _get_weather_daily_forecast(
    date_: date, timeseries: list[dict[str, Any]], tz: tzinfo
) -> WeatherDailyForecast:
    temperatures = [
        forecast["data"]["instant"]["details"]["air_temperature"]
        for forecast in timeseries
    ]

    # Take the conditions around the middle of the day (8h-20h)
    midday_forecast = min(
        [ts for ts in timeseries if "next_12_hours" in ts["data"]],
        key=lambda item: abs(
            _parse_time(item["time"], tz) - datetime.combine(date_, time(hour=8), tz)
        ),
    )

    return WeatherDailyForecast(
        min_temperature=min(temperatures),
        max_temperature=max(temperatures),
        conditions=midday_forecast["data"]["next_12_hours"]["summary"]["symbol_code"],
        at=_parse_time(midday_forecast["time"], tz),
    )


def _load_weather(cache: AppCache, location: tuple[float, float]) -> dict[str, Any]:
    cached = cache.get(CacheNS.weather, str(location))
    if cached:
        logger.info("loaded from cache")
        return cast("dict[str, Any]", cached)

    response = requests.get(
        METNO_URL,
        params={
            "lat": round(location[0], 4),
            "lon": round(location[1], 4),
        },
        headers={
            "User-Agent": "github.com/ugomeda/inkplate-dashboard",
            "Accept": "application/json",
        },
        timeout=15,
    )
    response.raise_for_status()

    cache.set(CacheNS.weather, str(location), response.json())

    return cast("dict[str, Any]", response.json())


def get_weather(
    cache: AppCache,
    config: DisplayConfiguration,
    hourly_count: int,
    daily_count: int,
) -> WeatherData:
    tz = config.tz()
    timeseries = _load_weather(cache, config.location)["properties"]["timeseries"]
    if not timeseries:
        raise RuntimeError("MET.no returned an empty forecast")

    # Current forecast
    now = datetime.now(tz)
    start_index = next(
        i for i, item in enumerate(timeseries) if _parse_time(item["time"], tz) > now
    )

    current_forecast = _get_weather_forecast([timeseries[start_index]], tz)

    # Get the hourly forecasts
    hourly_forecasts: list[WeatherForecast] = []
    for hourly_index in range(hourly_count):
        hourly_forecasts.append(
            _get_weather_forecast(
                [
                    timeseries[
                        start_index
                        + 1
                        + hourly_index * config.weather_hours_interval
                        + hour
                    ]
                    for hour in range(config.weather_hours_interval)
                ],
                tz,
            )
        )

    # Get the daily forecasts
    daily_forecasts: list[WeatherDailyForecast] = []
    for daily_index in range(daily_count):
        day = (now + timedelta(days=daily_index + 1)).date()
        day_timeseries = [
            data for data in timeseries if _parse_time(data["time"], tz).date() == day
        ]

        daily_forecasts.append(_get_weather_daily_forecast(day, day_timeseries, tz))

    return WeatherData(
        current=current_forecast,
        hourly=hourly_forecasts,
        daily=daily_forecasts,
        daily_min_temperature=min(
            (day.min_temperature for day in daily_forecasts), default=0.0
        ),
        daily_max_temperature=max(
            (day.max_temperature for day in daily_forecasts), default=0.0
        ),
        hourly_has_rain=any(hourly.precipitation > 0 for hourly in hourly_forecasts),
    )
