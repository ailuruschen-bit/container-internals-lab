# ARG vs ENV experiment.
#   docker build --progress=plain -t lab-env:v1 .
#   docker build --build-arg BUILD_LABEL=release --progress=plain -t lab-env:v2 .
#   docker run --rm lab-env:v1
#   docker run --rm -e RUN_MODE=development lab-env:v1

FROM alpine:latest

# Build-time only: readable by later build steps, not kept in the runtime environment.
ARG BUILD_LABEL=experiment

# Stored in the image config as a default environment variable for containers.
ENV RUN_MODE=production

RUN echo "Building image with label: $BUILD_LABEL"

# Expected output: RUN_MODE=production, BUILD_LABEL=unset.
CMD ["sh", "-c", "echo RUN_MODE=$RUN_MODE; echo BUILD_LABEL=${BUILD_LABEL:-unset}"]
