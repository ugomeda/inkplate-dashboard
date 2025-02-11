import base64
import io
from dataclasses import dataclass
from datetime import datetime
from xml.etree.ElementTree import Element

import requests
from defusedxml import ElementTree
from PIL import Image, ImageOps

from inkplate_dashboard.constants import FAKE_USER_AGENT

RSS_NS = {"media": "http://search.yahoo.com/mrss/"}
FORMATS = [
    "%a, %d %b %Y %H:%M:%S %z",  # RFC
    "%a, %d %b %Y %H:%M:%S %Z",  # RFC
    "%a, %d %b %y %H:%M:%S %z",  # France Info
]


@dataclass
class HeadlineEntry:
    title: str
    description: str
    image: str | None
    pub_date: datetime

    def is_long(self) -> bool:
        """Small tweak to adapt the size of the large headline."""
        return (len(self.title) * 1.5 + len(self.description)) > 400

    def get_image(self) -> str | None:
        """Fetches the image and adjusts luminosity for better inkplate
        output. Returns the image as a b64 encoded image."""
        if self.image is None:
            return None

        # Fetch image
        req = requests.get(
            self.image, stream=True, headers={"User-Agent": FAKE_USER_AGENT}
        )
        req.raise_for_status()
        req.raw.decode_content = True  # handle spurious Content-Encoding

        # B&W, resize, adjust
        img = Image.open(req.raw).convert("L")  # type: ignore
        img = ImageOps.contain(img, (1000, 1000))
        img = ImageOps.autocontrast(img, cutoff=8)

        # Return output
        stream = io.BytesIO()
        img.save(stream, "jpeg")

        return base64.b64encode(stream.getvalue()).decode()


def parse_date(date_str: str) -> datetime:
    for format in FORMATS:
        try:
            return datetime.strptime(date_str, format)
        except ValueError:
            pass

    raise ValueError(f"{date_str} is not parseable")


def _find_image(entry: Element) -> str | None:
    image_el = entry.find("media:content", RSS_NS)
    if image_el is not None:
        return str(image_el.get("url"))

    enclosure_el = entry.find("enclosure", RSS_NS)
    if enclosure_el is not None:
        return str(enclosure_el.get("url"))

    thumb_el = entry.find("media:thumbnail", RSS_NS)
    if thumb_el is not None:
        return str(thumb_el.get("url"))

    return None


def get_headlines(url: str) -> list[HeadlineEntry]:
    response = requests.get(url, headers={"User-Agent": FAKE_USER_AGENT}, timeout=10)
    response.raise_for_status()

    root = ElementTree.fromstring(response.content)

    result = []
    for entry in root.findall("./channel/item"):
        title_el = entry.find("title")
        description_el = entry.find("description")
        pub_date_el = entry.find("pubDate")

        if title_el is None or description_el is None or pub_date_el is None:
            continue

        result.append(
            HeadlineEntry(
                title=title_el.text or "",
                description=description_el.text or "",
                image=_find_image(entry),
                pub_date=parse_date(pub_date_el.text or ""),
            )
        )

    return result
