from dataclasses import dataclass
from datetime import datetime

from babel.dates import format_date, get_timezone
from jinja2 import Environment, PackageLoader, select_autoescape

from inkplate_dashboard.cache import AppCache
from inkplate_dashboard.config import DisplayConfiguration
from inkplate_dashboard.sources.rss import HeadlineEntry, get_headlines
from inkplate_dashboard.sources.weather import WeatherData, get_weather
from inkplate_dashboard.template import TemplateFilters


@dataclass
class EpaperContext:
    config: DisplayConfiguration
    weather: WeatherData
    headlines: list[HeadlineEntry]
    date: str
    battery_icon: str


def generate_html(
    app_cache: AppCache, display: DisplayConfiguration, voltage: float | None
) -> str:
    # Load RSS and weather
    headlines = get_headlines(str(display.rss_url))
    weather = get_weather(app_cache, display, 3, 0)

    # Get the date
    now = datetime.now(tz=get_timezone(display.timezone))
    date_str = format_date(now, format="full", locale=display.locale)

    # Get battery level
    battery_icon = "battery-warning"
    if voltage is not None:
        if voltage >= 4.2:
            battery_icon = "battery-full"
        elif voltage >= 4.0:
            battery_icon = "battery-medium"
        elif voltage >= 3.8:
            battery_icon = "battery-low"
        else:
            battery_icon = "battery"

    # Generate HTML
    env = Environment(
        loader=PackageLoader("inkplate_dashboard", "templates"),
        autoescape=select_autoescape(),
    )
    template = env.get_template("display.html")

    return template.render(
        filters=TemplateFilters(display),
        data=EpaperContext(
            config=display,
            weather=weather,
            headlines=headlines,
            date=date_str,
            battery_icon=battery_icon,
        ),
    )
