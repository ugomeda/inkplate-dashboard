import tomllib
from datetime import tzinfo
from enum import StrEnum
from typing import BinaryIO
from zoneinfo import ZoneInfo

from pydantic import BaseModel, Field, HttpUrl, PositiveInt


class UnitEnum(StrEnum):
    metric = "metric"
    imperial = "imperial"


class CacheConfiguration(BaseModel):
    path: str
    weather_ttl: PositiveInt = 6600  # 2h - 10min
    rss_feed_ttl: PositiveInt = 1800  # 30 minutes
    rss_article_ttl: PositiveInt = 7200  # 2h
    rss_image_ttl: PositiveInt = 7200  # 2h


class Feed(BaseModel):
    url: HttpUrl
    ignore_elements: list[str] = Field(default_factory=list)
    summary_words: tuple[PositiveInt, PositiveInt] = (70, 100)
    user_agent: str


class DisplayConfiguration(BaseModel):
    feeds: list[Feed]
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
