ARG PYTHON_VERSION=3.15

FROM python:${PYTHON_VERSION}-slim-bookworm AS builder

# Install uv
COPY --from=ghcr.io/astral-sh/uv:latest /uv /uvx /usr/local/bin/
ENV UV_PYTHON_DOWNLOADS=0 \
    UV_LINK_MODE=copy

# Install dependencies
WORKDIR /app
COPY pyproject.toml uv.lock README.md ./
RUN --mount=type=cache,target=/root/.cache/uv \
    uv sync --locked --no-dev --no-install-project


FROM python:${PYTHON_VERSION}-slim-bookworm AS runner

RUN apt-get update \
    && apt-get install -y --no-install-recommends libatomic1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
ENV PATH="/app/.venv/bin:$PATH"
COPY --from=builder /app/.venv /app/.venv
RUN python -m playwright install firefox --with-deps
COPY inkplate_dashboard /app/inkplate_dashboard

CMD ["python", "-m", "uvicorn", "inkplate_dashboard.app:app", "--host", "0.0.0.0"]
