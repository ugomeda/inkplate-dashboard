from dataclasses import dataclass
from datetime import datetime

from babel.dates import format_date, get_timezone
from jinja2 import Environment, PackageLoader, select_autoescape

from inkplate_dashboard.config import DisplayConfiguration
from inkplate_dashboard.sources.rss import HeadlineEntry, get_headlines
from inkplate_dashboard.sources.weather import WeatherObservation, get_weather


@dataclass
class EpaperContext:
    config: DisplayConfiguration
    weather: list[WeatherObservation]
    headlines: list[HeadlineEntry]
    date: str


def generate_html(display: DisplayConfiguration) -> str:
    # Load RSS and weather
    headlines = get_headlines(str(display.rss_url))
    weather = get_weather(display)

    # Get the date
    now = datetime.now(tz=get_timezone(display.timezone))
    date_str = format_date(now, format="full", locale=display.locale)

    # Generate HTML
    env = Environment(
        loader=PackageLoader("inkplate_dashboard", "templates"),
        autoescape=select_autoescape(),
    )
    template = env.get_template("display.html")

    return template.render(
        data=EpaperContext(
            config=display,
            weather=weather,
            headlines=headlines,
            date=date_str,
        )
    )
