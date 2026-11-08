import calendar
import logging
import re
from dataclasses import dataclass
from datetime import datetime, tzinfo
from typing import Any, cast
from urllib.parse import urljoin

import feedparser
import requests
import trafilatura
from bs4 import BeautifulSoup
from playwright.sync_api import Browser, TimeoutError, sync_playwright

from inkplate_dashboard.cache import AppCache, CacheNS
from inkplate_dashboard.config import DisplayConfiguration, Feed

logger = logging.getLogger(__name__)


@dataclass
class RSSArticle:
    title: str
    url: str
    website: str
    summary: str
    image: str | None
    published_at: datetime | None
    language: str | None


@dataclass
class Article:
    title: str
    website: str
    image: bytes | None
    favicon: bytes | None
    summary: str
    published_at: datetime | None
    language: str | None


def _filter_rss_article(article: RSSArticle) -> bool:
    # filters out lemonde live articles which renders as garbage
    if "/live/" in article.url:
        return False

    return True


def clean_html(text: str | None) -> str:
    if not text:
        return ""

    return BeautifulSoup(text, "html.parser").get_text(" ", strip=True)


def _normalize_whitespace(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def _shorten_summary(text: str, min_words: int, max_words: int) -> str:
    text = _normalize_whitespace(text)
    if not text:
        return ""

    words = text.split()
    if len(words) <= max_words:
        return text

    sentences = re.split(r"(?<=[.!?…])\s+", text)

    result: list[str] = []
    word_count = 0

    for sentence in sentences:
        sentence_words = sentence.split()
        if result and word_count + len(sentence_words) > max_words:
            break

        result.append(sentence)
        word_count += len(sentence_words)
        if word_count >= min_words:
            break

    if word_count >= min_words:
        return " ".join(result)

    return " ".join(words[:max_words]).rstrip(" ,;:") + "…"


def _rss_image(entry: dict[str, Any]) -> str | None:
    # media:content
    for media in entry.get("media_content", []):
        url = media.get("url")
        if url:
            return cast("str", url)

    # media:thumbnail
    for media in entry.get("media_thumbnail", []):
        url = media.get("url")
        if url:
            return cast("str", url)

    # enclosure
    for link in entry.get("links", []):
        content_type = link.get("type", "")
        if content_type.startswith("image/"):
            return cast("str", link.get("href"))

    return None


def _get_cached_rss_content(
    cache: AppCache, feed: Feed
) -> tuple[bytes, dict[str, str]]:
    feed_url = str(feed.url)
    cached = cache.get(CacheNS.rss_feed, feed_url)
    if cached is not None:
        logger.info("using cache for %s", feed_url)
        return cast("tuple[bytes, dict[str, str]]", cached)

    response = requests.get(
        feed_url, timeout=15, headers={"User-Agent": feed.user_agent}
    )
    response.raise_for_status()
    value = (response.content, dict(response.headers))

    cache.set(CacheNS.rss_feed, feed_url, value)

    return value


def _rss_published_at(entry: dict[str, Any], tz: tzinfo) -> datetime | None:
    parsed = entry.get("published_parsed") or entry.get("updated_parsed")
    if parsed is None:
        return None

    return datetime.fromtimestamp(calendar.timegm(parsed), tz=tz)


def _rss_language(feed: Any, entry: dict[str, Any]) -> str | None:
    return entry.get("language") or feed.feed.get("language") or None


def _load_rss_feed(
    cache: AppCache, config: DisplayConfiguration, feed: Feed
) -> list[RSSArticle]:
    content, headers = _get_cached_rss_content(cache, feed)
    rss_content = feedparser.parse(content, response_headers=headers)

    return [
        RSSArticle(
            title=clean_html(entry.get("title", "")),
            url=entry["link"],
            website=clean_html(rss_content.feed.get("title", "")),
            summary=clean_html(entry.get("summary") or entry.get("description") or ""),
            image=_rss_image(entry),
            published_at=_rss_published_at(entry, config.tz()),
            language=_rss_language(rss_content, entry),
        )
        for entry in rss_content.entries
    ]


def _page_get_meta(
    page: BeautifulSoup, selectors: list[str], default: str | None
) -> str | None:
    for selector in selectors:
        element = page.select_one(selector)
        if element is None:
            continue

        value = element.get("content")
        if value:
            return _normalize_whitespace(str(value))

    return default


def _page_get_favicon(base_url: str, page: BeautifulSoup) -> str | None:
    selectors = [
        'link[rel="icon"]',
        'link[rel="shortcut icon"]',
        'link[rel="apple-touch-icon"]',
    ]

    for selector in selectors:
        element = page.select_one(selector)
        if element is None:
            continue

        href = cast("str | None", element.get("href"))
        if href:
            return urljoin(base_url, href)

    return None


def _page_extract_text(config: Feed, html: str, url: str) -> str:
    page = BeautifulSoup(html, "html.parser")

    for selector in config.ignore_elements:
        for element in page.select(selector):
            element.decompose()

    result = trafilatura.bare_extraction(
        str(page),
        url=url,
        include_comments=False,
        include_tables=False,
        favor_precision=True,
    )

    if result is None:
        return ""

    return _normalize_whitespace(result.text or "")  # type:ignore


def _page_get_html(cache: AppCache, browser: Browser, url: str, locale: str) -> str:
    cached = cache.get(CacheNS.rss_article, url)
    if cached is not None:
        logger.info("used cache for article %s", url)
        return cast("str", cached)

    context = browser.new_context(locale=locale)
    try:
        page = context.new_page()
        page.goto(url, wait_until="domcontentloaded", timeout=30_000)

        try:
            page.wait_for_selector("article", timeout=8_000)
        except TimeoutError:
            page.wait_for_timeout(2_000)

        html = page.content()
    finally:
        context.close()

    cache.set(CacheNS.rss_article, url, html)

    return html


def _load_image(cache: AppCache, feed: Feed, url: str | None) -> bytes | None:
    if url is None:
        return None

    cached = cache.get(CacheNS.rss_image, url)
    if cached is not None:
        logger.info("used cache for image %s", url)
        return cast("bytes", cached)

    req = requests.get(url, timeout=15, headers={"User-Agent": feed.user_agent})
    req.raise_for_status()

    cache.set(CacheNS.rss_image, url, req.content)
    return req.content


def render_article(
    config: DisplayConfiguration,
    feed: Feed,
    browser: Browser,
    cache: AppCache,
    rss_article: RSSArticle,
) -> Article:
    try:
        html = _page_get_html(cache, browser, rss_article.url, config.locale)
        parsed_page = BeautifulSoup(html, "html.parser")

        # Extract metas
        title = _page_get_meta(
            parsed_page, ['meta[property="og:title"]'], rss_article.title
        )
        website = _page_get_meta(
            parsed_page, ['meta[property="og:site_name"]'], rss_article.website
        )
        image = _page_get_meta(
            parsed_page,
            ['meta[property="og:image"]', 'meta[name="twitter:image"]'],
            rss_article.image,
        )
        favicon = _page_get_favicon(rss_article.url, parsed_page)

        # Extract body
        text = _page_extract_text(feed, html, rss_article.url)
        text = _normalize_whitespace(text)
        if len(text.split()) < 50:  # default to RSS summary if extraction failed
            text = rss_article.summary

        return Article(
            title=title or "",
            website=website or "",
            image=_load_image(cache, feed, image),
            favicon=_load_image(cache, feed, favicon),
            summary=_shorten_summary(
                text, feed.summary_words[0], feed.summary_words[1]
            ),
            published_at=rss_article.published_at,
            language=rss_article.language,
        )

    except TimeoutError:
        logger.exception("error while fetching article %s", rss_article.url)

        return Article(
            title=rss_article.title,
            website=rss_article.website,
            image=_load_image(cache, feed, rss_article.image),
            favicon=None,
            summary=_shorten_summary(
                rss_article.summary, feed.summary_words[0], feed.summary_words[1]
            ),
            published_at=rss_article.published_at,
            language=rss_article.language,
        )


def fetch_news(cache: AppCache, config: DisplayConfiguration) -> list[Article]:
    result: list[Article] = []

    with sync_playwright() as playwright:
        browser = playwright.firefox.launch(headless=True)

        try:
            for feed in config.feeds:
                articles = _load_rss_feed(cache, config, feed)
                if not articles:
                    raise RuntimeError(f"No articles found in {feed.url}")

                article = next(
                    article for article in articles if _filter_rss_article(article)
                )
                result.append(render_article(config, feed, browser, cache, article))
        finally:
            browser.close()

    return result
