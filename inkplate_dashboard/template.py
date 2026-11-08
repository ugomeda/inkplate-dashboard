import base64
from datetime import datetime

from babel import Locale
from babel.dates import format_date, format_time

from inkplate_dashboard.config import DisplayConfiguration, UnitEnum


class TemplateFilters:
    def __init__(self, config: DisplayConfiguration):
        self.locale = config.locale.replace("-", "_")
        self.units = config.units

    def temperature(self, value: float) -> str:
        if self.units == UnitEnum.imperial:
            value = (9 / 5) * value + 32

        return f"{round(value)}°"

    def wind_speed(self, value: float) -> str:
        if self.units == UnitEnum.imperial:
            return f"{round(value * 2.236936)}mph"

        return f"{round(value * 3.6)}km/h"

    def rain(self, value: float) -> str:
        if self.units == UnitEnum.imperial:
            return f"{value / 25.4:.3f}in"

        return f"{value:.1f}mm"

    def format_day(self, value: datetime, format: str) -> str:
        return format_date(value, locale=self.locale, format=format)

    def format_time(self, value: datetime, format: str) -> str:
        return format_time(value, locale=self.locale, format=format)

    def format_weather_hour(self, value: datetime) -> str:
        locale = Locale.parse(self.locale)
        short_pattern = str(locale.time_formats["short"])

        # AM/PM format -> "2 PM"
        if "h" in short_pattern or "K" in short_pattern:
            return format_time(value, "h a", locale=self.locale)

        # 24h format -> "14 h"
        return format_time(value, "HH 'h'", locale=self.locale)

    def image_data_uri(self, data: bytes | None) -> str | None:
        if data is None:
            return None

        if data.startswith(b"\x89PNG\r\n\x1a\n"):
            content_type = "image/png"
        elif data.startswith(b"\xff\xd8\xff"):
            content_type = "image/jpeg"
        elif data.startswith(b"GIF8"):
            content_type = "image/gif"
        elif data.lstrip().startswith(b"<svg"):
            content_type = "image/svg+xml"
        else:
            content_type = "application/octet-stream"

        encoded = base64.b64encode(data).decode("ascii")
        return f"data:{content_type};base64,{encoded}"
