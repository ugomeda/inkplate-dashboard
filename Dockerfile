ARG BASE_IMAGE=python:slim-bookworm
ARG PYTHON_VERSION="3.11"

FROM ${BASE_IMAGE} as poetry
ARG PYTHON_VERSION

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        python${PYTHON_VERSION} \
        python3-pip \
        python3-venv \
        python3-dev \
        build-essential \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /app/poetry/ \
    && /app/poetry/bin/pip install poetry poetry-plugin-export


FROM poetry as builder
ARG PYTHON_VERSION

WORKDIR /app/
COPY pyproject.toml poetry.lock README.md /app/

RUN /app/poetry/bin/poetry export --only=main --output=requirements.txt
RUN python${PYTHON_VERSION} -m pip wheel -r requirements.txt --require-hashes -w /app/dist/

RUN python${PYTHON_VERSION} -m venv /app/venv
RUN /app/venv/bin/pip install /app/dist/*.whl


FROM ${BASE_IMAGE} as runner
ARG PYTHON_VERSION

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        python${PYTHON_VERSION} \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/venv/ /app/venv/
RUN ["/app/venv/bin/python3", "-m", "playwright", "install", "chromium", "--no-shell", "--with-deps"]

COPY inkplate_dashboard /app/inkplate_dashboard

CMD ["/app/venv/bin/python3", "-m", "uvicorn", "inkplate_dashboard.app:app", "--host", "0.0.0.0"]
