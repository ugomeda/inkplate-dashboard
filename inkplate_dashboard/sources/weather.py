from dataclasses import dataclass
from datetime import datetime
from typing import cast

import requests
from babel.dates import format_time, get_timezone

from inkplate_dashboard.config import DisplayConfiguration, UnitEnum

MET_API_URL = "https://api.met.no/weatherapi/locationforecast/2.0/compact"


def celcius_to_fahrenheit(temp: float) -> float:
    return (9 / 5) * temp + 32


@dataclass
class WeatherObservation:
    date: datetime
    date_short_locale: str
    icon: str

    temperature: str
    wind: str
    rain: str | None


def get_weather(display: DisplayConfiguration) -> list[WeatherObservation]:
    # Fetch JSON
    request = requests.get(
        MET_API_URL,
        headers={"user-agent": "inkplate-dashboard meda.ugo@gmail.com"},
        params={
            "lat": f"{display.location[0]:.2f}",
            "lon": f"{display.location[1]:.2f}",
        },
    )
    request.raise_for_status()
    data = request.json()

    # Compile results
    result = []
    for ts in data["properties"]["timeseries"][0::4]:
        ts_data = ts["data"]

        if "next_1_hours" in ts_data and "next_6_hours" in ts_data:
            date = datetime.fromisoformat(ts["time"])

            # Convert data based on locality
            temperature_celcius = cast(
                float, ts_data["instant"]["details"]["air_temperature"]
            )
            wind_speed_mps = cast(float, ts_data["instant"]["details"]["wind_speed"])
            rain_mm = cast(
                float,
                ts_data["next_6_hours"]["details"].get("precipitation_amount", 0.0),
            )

            rain = None
            if display.units == UnitEnum.metric:
                temperature = f"{round(temperature_celcius)}°"
                wind_speed = f"{round(wind_speed_mps * 3.6)}km/h"
                if rain_mm > 0:
                    rain = f"{rain_mm:.1f}mm"
            elif display.units == UnitEnum.imperial:
                temperature = f"{round(celcius_to_fahrenheit(temperature_celcius))}°"
                wind_speed = f"{round(wind_speed_mps * 2.237)}mph"
                if rain_mm > 0:
                    rain = f"{rain_mm / 25.4:.3f}in"
            else:
                raise Exception("Unexpected unit type")

            result.append(
                WeatherObservation(
                    date=date,
                    date_short_locale=format_time(
                        date,
                        format="short",
                        tzinfo=get_timezone(display.timezone),
                        locale=display.locale,
                    ),
                    icon=ts_data["next_1_hours"]["summary"]["symbol_code"],
                    temperature=temperature,
                    wind=wind_speed,
                    rain=rain,
                )
            )

    return result
