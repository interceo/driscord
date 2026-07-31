FROM python:3.12-slim AS runtime

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    API_PORT=8000

WORKDIR /app

RUN groupadd --system driscord \
    && useradd --system --gid driscord --home-dir /app driscord

COPY backend/api/requirements.txt ./requirements.txt
RUN pip install --no-cache-dir --upgrade pip \
    && pip install --no-cache-dir -r requirements.txt

COPY backend/api/ ./

RUN chown -R driscord:driscord /app
USER driscord

EXPOSE 8000

CMD ["sh", "-c", "exec uvicorn main:app --host 0.0.0.0 --port ${API_PORT}"]
