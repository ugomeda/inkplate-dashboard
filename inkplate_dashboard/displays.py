from dataclasses import dataclass

from inkplate_dashboard.config import DisplayProfile


@dataclass
class Display:
    template: str
    width: int
    height: int
    weather_days: int
    weather_hours: int


DISPLAYS = {
    DisplayProfile.inkplate10: Display(
        template="inkplate10/display.html",
        width=825,
        height=1200,
        weather_days=0,
        weather_hours=3,
    ),
    DisplayProfile.spectra6: Display(
        template="spectra6/display.html",
        width=1600,
        height=1200,
        weather_days=5,
        weather_hours=6,
    ),
}
