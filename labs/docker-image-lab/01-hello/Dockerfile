# Minimal image: copy a shell script into Alpine and run it by default.

# Base image: reuse Alpine's filesystem layers and config as the starting point.
FROM alpine:latest

# Default working directory for later instructions and for the container.
# Created automatically if it does not exist.
WORKDIR /app

# Copy from the build context into the image; "." resolves to /app.
COPY hello.sh .

# Recorded in the image config, not executed at build time.
# Exec form: "sh" reads the script, so hello.sh needs no execute bit.
CMD ["sh", "hello.sh"]
