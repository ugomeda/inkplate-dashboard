import hashlib
from enum import StrEnum
from typing import Any, assert_never

from diskcache import Cache

from inkplate_dashboard.config import CacheConfiguration


class CacheNS(StrEnum):
    weather = "weather"


def _key(namespace: CacheNS, value: str) -> str:
    digest = hashlib.sha256(value.encode()).hexdigest()
    return f"{namespace}:{digest}"


class AppCache:
    def __init__(self, config: CacheConfiguration):
        self.cache = Cache(config.path)
        self.config = config

    def _ttl(self, namespace: CacheNS) -> int:
        match namespace:
            case CacheNS.weather:
                return self.config.weather_ttl
            case _:
                assert_never(namespace)

    def get(self, namespace: CacheNS, key: str) -> Any | None:
        return self.cache.get(_key(namespace, key))

    def set(self, namespace: CacheNS, key: str, value: Any) -> None:
        self.cache.set(_key(namespace, key), value, expire=self._ttl(namespace))

    def delete(self, namespace: CacheNS, key: str) -> None:
        self.cache.delete(_key(namespace, key))
