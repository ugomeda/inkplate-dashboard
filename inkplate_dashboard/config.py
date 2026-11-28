import tomllib
from datetime import tzinfo
from enum import StrEnum
from typing import BinaryIO
from zoneinfo import ZoneInfo

from pydantic import BaseModel, ConfigDict, Field, HttpUrl, PositiveInt


class UnitEnum(StrEnum):
    metric = "metric"
    imperial = "imperial"


class DisplayProfile(StrEnum):
    inkplate10 = "inkplate10"
    spectra6 = "spectra6"


class CacheConfiguration(BaseModel):
    model_config = ConfigDict(extra="forbid")

    path: str = ".cache"
    weather_ttl: PositiveInt = 6600  # 2h - 10min
    sun_ttl: PositiveInt = 48 * 3600  # 2 days
    rss_feed_ttl: PositiveInt = 1800  # 30 minutes
    rss_article_ttl: PositiveInt = 7200  # 2h
    rss_image_ttl: PositiveInt = 7200  # 2h


class Feed(BaseModel):
    model_config = ConfigDict(extra="forbid")

    url: HttpUrl
    ignored_elements: list[str] = Field(default_factory=list)


class DisplayConfiguration(BaseModel):
    model_config = ConfigDict(extra="forbid")

    profile: DisplayProfile
    feeds: list[Feed]
    location: tuple[float, float]
    locale: str
    timezone: str
    units: UnitEnum
    refresh_interval_sec: PositiveInt
    weather_forecast_step_hours: PositiveInt = 4
    news_user_agent: str
    news_summary_words: PositiveInt

    def tz(self) -> tzinfo:
        return ZoneInfo(self.timezone)


class Configuration(BaseModel):
    model_config = ConfigDict(extra="forbid")

    cache: CacheConfiguration = Field(default_factory=CacheConfiguration)
    displays: dict[str, DisplayConfiguration]


def parse_config(fp: BinaryIO) -> Configuration:
    return Configuration(**tomllib.load(fp))
