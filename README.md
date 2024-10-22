inkplate-dashboard
==================

Project to display a simple dashboard on an Inkplate 10 displaying articles
from an RSS feed and the current weather, provided by the
[MET Weather API](https://api.met.no/).

## Server

The server generates the dashboard as an HTML page, and uses Chrome to
screenshot it and generate a PNG which can be sent to the Inkplate 10.


### Quick setup

For a local setup, you will need Google Chrome or Chromium installed
on your machine.

To run the server:

- Create a stub configuration by copying `config.example.toml` to `config.toml`
- Setup your poetry environment
- Run the application

```
cp config.exemple.toml config.toml
poetry shell
poetry install
uvicorn inkplate_dashboard.app:app
```

You should be able to access the dashboard on the following URLs:

- Live HTML version: http://localhost:8000/live/png
- Live PNG version: http://localhost:8000/live/png
- Cached PNG version: http://localhost:8000/

Note: if chrome is not found, update the paths in `inkplate_dashboard/chrome.py`

### Docker setup

To build and run locally:

```
docker build -t inkplate_dashboard .
docker run -v $(pwd)/config.example.toml:/app/config.toml:ro \
    -p 8000:8000 \
    inkplate_dashboard
```
