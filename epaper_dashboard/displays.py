from dataclasses import dataclass

from epaper_dashboard.config import DisplayProfile


@dataclass
class Display:
    template: str
    width: int
    height: int
    weather_days: int
    weather_hours: int
    rotate: int
    palette: list[tuple[tuple[int, int, int], int]]


DISPLAYS = {
    DisplayProfile.inkplate10: Display(
        template="inkplate10/display.html",
        width=825,
        height=1200,
        weather_days=0,
        weather_hours=3,
        rotate=90,
        palette=[
            ((16, 16, 16), 0x0),
            ((48, 48, 48), 0x1),
            ((80, 80, 80), 0x2),
            ((112, 112, 112), 0x3),
            ((144, 144, 144), 0x4),
            ((176, 176, 176), 0x5),
            ((208, 208, 208), 0x6),
            ((240, 240, 240), 0x7),
        ],
    ),
    DisplayProfile.spectra6: Display(
        template="spectra6/display.html",
        width=1600,
        height=1200,
        weather_days=5,
        weather_hours=6,
        rotate=90,
        palette=[
            ((2, 2, 2), 0x0),  # black
            ((190, 200, 200), 0x1),  # white
            ((205, 202, 0), 0x2),  # yellow
            ((135, 19, 0), 0x3),  # red
            ((5, 64, 158), 0x5),  # blue
            ((39, 102, 60), 0x6),  # green
        ],
    ),
}
