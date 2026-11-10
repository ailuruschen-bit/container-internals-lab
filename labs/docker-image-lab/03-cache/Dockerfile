# Build cache experiment.
# Build twice with `docker build --progress=plain -t cache-lab:v1 .`
# and look for CACHED steps; then edit message.txt and build again.

FROM alpine:latest

# Stable and relatively expensive: placed before frequently changing content,
# so editing message.txt does not force curl to be reinstalled.
RUN apk add --no-cache curl

WORKDIR /app

# Changes often: this step and everything after it are rebuilt when message.txt changes.
COPY message.txt .

CMD ["cat", "message.txt"]
