# .dockerignore experiment.
# Build and run, then check that debug.log is missing from /app:
#   docker build -t ignore-lab:v1 . && docker run --rm ignore-lab:v1

FROM alpine:latest

WORKDIR /app

# Copies everything left in the build context after .dockerignore filtering.
COPY . .

CMD ["ls", "-la", "/app"]
