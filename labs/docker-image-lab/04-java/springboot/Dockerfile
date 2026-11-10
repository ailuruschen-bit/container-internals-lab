# Spring Boot image: Maven build stage + non-root JRE runtime stage.
#   docker build -t spring-lab:v1 .
#   docker run -d --name springboot1 -p 127.0.0.1:8081:8080 spring-lab:v1
# The /messages endpoints need PostgreSQL; run the full stack with ../../05-compose.

# === Stage 1: Build ===
FROM maven:3.9-eclipse-temurin-21 AS builder
WORKDIR /build

# Copy only the POM first and resolve dependencies in their own layer,
# so source-only changes reuse the cached dependency download.
COPY pom.xml .
RUN mvn -B dependency:go-offline

# Source changes invalidate only the steps from here on.
COPY src ./src
RUN mvn -B package -DskipTests

# === Stage 2: Runtime ===
FROM eclipse-temurin:21-jre

# Dedicated unprivileged user with a fixed UID/GID.
RUN groupadd --gid 10001 app && \
    useradd --uid 10001 --gid 10001 --no-create-home app

WORKDIR /app

# Only the packaged jar is carried over; Maven and the sources stay in the build stage.
COPY --from=builder --chown=10001:10001 /build/target/*.jar app.jar

# Run as the numeric UID/GID so the identity does not depend on name lookup.
USER 10001:10001

# Documents the port the app listens on; publishing still requires -p or Compose ports.
EXPOSE 8080
ENTRYPOINT ["java", "-jar", "app.jar"]
