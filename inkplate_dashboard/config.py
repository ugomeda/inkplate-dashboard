import tomllib
from enum import Enum
from typing import BinaryIO

from pydantic import BaseModel, HttpUrl


class UnitEnum(str, Enum):
    metric = "metric"
    imperial = "imperial"


class DisplayConfiguration(BaseModel):
    rss_url: HttpUrl
    location: tuple[float, float]
    locale: str
    timezone: str
    units: UnitEnum


class Configuration(BaseModel):
    display: DisplayConfiguration


def parse_config(fp: BinaryIO) -> Configuration:
    return Configuration(**tomllib.load(fp))
