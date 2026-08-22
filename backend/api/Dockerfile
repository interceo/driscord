# The API image is assembled from a runtime base that already carries the
# Python dependencies, so building it fetches nothing from a package index.
#
#   RUNTIME_IMAGE  pinned base: python-slim with requirements.txt installed,
#                  the unprivileged driscord user, port and entrypoint.
#
# It defaults to the homelab registry and is overridden by digest from the
# infrastructure build policy. The base is rebuilt when requirements.txt
# changes; the check below fails the build instead of shipping an image whose
# dependencies silently lag behind the file.
ARG RUNTIME_IMAGE=registry.registry.svc.cluster.local:5000/driscord-api-runtime-base@sha256:6fb5177cd181aa494a1d35fcb5a38a991145eec26057821ef5a8eacd6f2a8b35

FROM ${RUNTIME_IMAGE} AS runtime

WORKDIR /app

COPY backend/api/ ./

# Offline check against the shipped file: with no index available pip either
# reports every requirement as satisfied by the base, or fails the build.
RUN pip install --no-index --no-cache-dir -r requirements.txt

EXPOSE 8000

CMD ["sh", "-c", "exec uvicorn main:app --host 0.0.0.0 --port ${API_PORT}"]
