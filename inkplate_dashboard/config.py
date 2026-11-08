import tomllib
from datetime import tzinfo
from enum import StrEnum
from typing import BinaryIO
from zoneinfo import ZoneInfo

from pydantic import BaseModel, HttpUrl, PositiveInt


class UnitEnum(StrEnum):
    metric = "metric"
    imperial = "imperial"


class CacheConfiguration(BaseModel):
    path: str
    weather_ttl: PositiveInt = 6600  # 2h - 10min


class DisplayConfiguration(BaseModel):
    rss_url: HttpUrl
    location: tuple[float, float]
    locale: str
    timezone: str
    units: UnitEnum
    refresh_interval_sec: PositiveInt
    weather_hours_interval: PositiveInt = 4

    def tz(self) -> tzinfo:
        return ZoneInfo(self.timezone)


class Configuration(BaseModel):
    cache: CacheConfiguration
    display: DisplayConfiguration


def parse_config(fp: BinaryIO) -> Configuration:
    return Configuration(**tomllib.load(fp))
