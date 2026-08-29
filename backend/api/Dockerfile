ARG RUNTIME_IMAGE=registry.registry.svc.cluster.local:5000/driscord-api-runtime-base@sha256:6fb5177cd181aa494a1d35fcb5a38a991145eec26057821ef5a8eacd6f2a8b35

FROM ${RUNTIME_IMAGE} AS runtime

WORKDIR /app

COPY backend/api/ ./

RUN pip install --no-index --no-cache-dir -r requirements.txt

EXPOSE 8000

CMD ["sh", "-c", "exec uvicorn main:app --host 0.0.0.0 --port ${API_PORT}"]
