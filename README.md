inkplate-dashboard
==================

Project to display a simple dashboard on an Inkplate 10 displaying articles
from an RSS feed and the current weather, provided by the
[MET Weather API](https://api.met.no/).

## Server

The server generates the dashboard as an HTML page, and uses Chromium to
screenshot it and generate a PNG which can be sent to the Inkplate 10.

### Docker quickstart

An image is provided on [dockerhub](https://hub.docker.com/r/ugomeda/inkplate-dashboard):

```
docker run \
    --pull always \
    -v $(pwd)/config.example.toml:/app/config.toml:ro \
    -p 8000:8000 \
    ugomeda/inkplate-dashboard:latest
```

Then access:

- Live HTML version: http://localhost:8000/live/html
- Live PNG version: http://localhost:8000/live/png

### Run locally

To run the server:

- Create a stub configuration by copying `config.example.toml` to `config.toml`
- Setup your poetry environment
- Install Chromium
- Run the application

```
cp config.exemple.toml config.toml
poetry shell
poetry install
playwright install chromium --no-shell --with-deps
uvicorn inkplate_dashboard.app:app
```

You should be able to access the dashboard on the following URLs:

- Live HTML version: http://localhost:8000/live/html
- Live PNG version: http://localhost:8000/live/png

Note: if chrome is not found, update the paths in `inkplate_dashboard/chrome.py`

## Assets

This projets uses:

- Weather icons: https://github.com/nrkno/yr-weather-symbols
- Icons: https://github.com/lucide-icons/lucide
- textFit: https://github.com/STRML/textFit
- Fonts: https://github.com/fontsource