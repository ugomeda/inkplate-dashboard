from dataclasses import dataclass
from datetime import datetime

from jinja2 import Environment, PackageLoader, select_autoescape

from inkplate_dashboard.cache import AppCache
from inkplate_dashboard.config import DisplayConfiguration
from inkplate_dashboard.displays import DISPLAYS
from inkplate_dashboard.sources.rss import Article, fetch_news
from inkplate_dashboard.sources.sun import SunData, fetch_sun
from inkplate_dashboard.sources.weather import WeatherData, get_weather
from inkplate_dashboard.template import TemplateFilters


@dataclass
class EpaperContext:
    config: DisplayConfiguration
    weather: WeatherData
    headlines: list[Article]
    now: datetime
    battery_icon: str
    sun: SunData


def generate_html(
    app_cache: AppCache, display: DisplayConfiguration, voltage: float | None
) -> str:
    profile = DISPLAYS[display.profile]
    now = datetime.now(tz=display.tz())

    # Load RSS, weather and sun informations
    headlines = fetch_news(app_cache, display)
    weather = get_weather(
        app_cache, display, profile.weather_hours, profile.weather_days
    )
    sun = fetch_sun(app_cache, display, now)

    # Get battery level
    battery_icon = "battery-exclamation-point"
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
    template = env.get_template(profile.template)

    return template.render(
        filters=TemplateFilters(display),
        data=EpaperContext(
            config=display,
            weather=weather,
            headlines=headlines,
            now=now,
            battery_icon=battery_icon,
            sun=sun,
        ),
    )
