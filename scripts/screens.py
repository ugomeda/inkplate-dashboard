import os
from dataclasses import dataclass
from io import BytesIO

import cairosvg
from PIL import Image, ImageDraw, ImageFont

FONT_REGULAR_NAME = "inter-latin-400-normal.ttf"
FONT_BOLD_NAME = "inter-latin-600-normal.ttf"
BASE_TITLE_SIZE = 70
BASE_HELPTEXT_SIZE = 35
BASE_ICON_SIZE = 256
BASE_TITLE_GAP = 40
BASE_HELPTEXT_GAP = 25


@dataclass
class Screen:
    icon: str
    title: str
    helptext: str | None = None


@dataclass
class Display:
    width: int
    height: int
    output_path: str
    screens: list[str]
    scale: float = 1.0
    rotation: int | None = None


ERROR_SCREENS = {
    "low_battery": Screen(
        icon="battery",
        title="Low battery",
        helptext="Updates are paused, connect\nthe display to power.",
    ),
    "image_decode_failed": Screen(
        icon="image-off",
        title="Image error",
        helptext="Unable to decode dashboard image,\ncheck the server logs.",
    ),
    "updating": Screen(icon="refresh-cw", title="Updating display..."),
    "server_unreachable": Screen(
        icon="screen-share-off",
        title="Server unavailable",
        helptext=("Unable to reach the dashboard server,\ncheck network connectivity."),
    ),
    "server_error": Screen(
        icon="screen-share-off",
        title="Server error",
        helptext=(
            "The dashboard server returned \n"
            "an unexpected response,\n"
            "check the server logs."
        ),
    ),
    "wifi_unavailable": Screen(
        icon="wifi-off",
        title="Connection failed",
        helptext="Unable to connect to the Wi-Fi network.",
    ),
    "unknown_error": Screen(
        icon="circle-exclamation-point",
        title="Unknown error",
        helptext="An unexpected error occurred.",
    ),
}

DISPLAYS = {
    "inplate10": Display(
        width=825,
        height=1200,
        output_path="../firmware/main/screens",
        screens=[
            "low_battery",
            "image_decode_failed",
            "updating",
            "server_unreachable",
            "server_error",
            "wifi_unavailable",
            "unknown_error",
        ],
    ),
    "spectra6": Display(
        width=1600,
        height=1200,
        output_path="../spectra6-test/main/screens",
        screens=[
            "low_battery",
            "image_decode_failed",
            "server_unreachable",
            "server_error",
            "wifi_unavailable",
            "unknown_error",
        ],
        scale=1.5,
        rotation=Image.Transpose.ROTATE_90,
    ),
}


def render_screen(
    display_name: str, display: Display, screen_name: str, screen: Screen
) -> None:
    image = Image.new("RGB", (display.width, display.height), "black")
    draw = ImageDraw.Draw(image)

    title_font = ImageFont.truetype(
        FONT_BOLD_NAME, size=int(BASE_TITLE_SIZE * display.scale)
    )
    help_font = ImageFont.truetype(
        FONT_REGULAR_NAME, size=int(BASE_HELPTEXT_SIZE * display.scale)
    )

    title_bbox = draw.multiline_textbbox(
        (0, 0), screen.title, font=title_font, align="center"
    )
    title_w = title_bbox[2]
    title_h = title_bbox[3]

    help_w = 0
    help_h = 0
    if screen.helptext:
        help_bbox = draw.multiline_textbbox(
            (0, 0),
            screen.helptext,
            font=help_font,
            align="center",
            spacing=6,
        )
        help_w = help_bbox[2]
        help_h = help_bbox[3]

    title_gap = int(BASE_TITLE_GAP * display.scale)
    helptext_gap = int(BASE_HELPTEXT_GAP * display.scale)
    icon_size = int(BASE_ICON_SIZE * display.scale)

    content_height = icon_size + title_gap + title_h
    if screen.helptext:
        content_height += helptext_gap + help_h

    start_y = (display.height - content_height) // 2

    # Icon
    icon_png = cairosvg.svg2png(
        url=os.path.join("icons", f"{screen.icon}.svg"),
        output_width=icon_size,
        output_height=icon_size,
    )
    icon = Image.open(BytesIO(icon_png)).convert("RGBA")
    icon_x = (display.width - icon.width) // 2
    image.paste("white", (icon_x, start_y), icon.getchannel("A"))

    # Title
    title_x = (display.width - title_w) // 2
    title_y = start_y + icon_size + title_gap
    draw.multiline_text(
        (title_x, title_y),
        screen.title,
        font=title_font,
        fill="white",
        align="center",
    )

    # Help text
    if screen.helptext and help_font is not None:
        help_x = (display.width - help_w) // 2
        help_y = title_y + title_h + helptext_gap
        draw.multiline_text(
            (help_x, help_y),
            screen.helptext,
            font=help_font,
            fill="white",
            align="center",
            spacing=6,
        )

    output_path = os.path.join(display.output_path, f"{screen_name}.png")
    image = image.convert("1", dither=Image.Dither.NONE)
    if display.rotation is not None:
        image = image.transpose(display.rotation)
    image.save(output_path, optimize=True)
    print(f"[{display_name}] generated {output_path}")


def main() -> None:
    for display_name, display in DISPLAYS.items():
        for screen_name in display.screens:
            screen = ERROR_SCREENS[screen_name]

            render_screen(
                display_name=display_name,
                display=display,
                screen_name=screen_name,
                screen=screen,
            )


if __name__ == "__main__":
    main()
