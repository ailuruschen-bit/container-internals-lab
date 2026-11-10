# Multi-stage build: compile with the JDK, ship only the jar on a JRE.
#   docker build --progress=plain -t java-lab:v2 . && docker run --rm java-lab:v2

# Stage 1 (builder): has the compiler; discarded from the final image.
FROM eclipse-temurin:21-jdk AS builder

WORKDIR /app

COPY Hello.java .

RUN javac Hello.java && \
    jar --create --file app.jar --main-class Hello Hello.class

# Stage 2 (runtime): smaller JRE base without build tools.
FROM eclipse-temurin:21-jre

WORKDIR /app

# Take only the build artifact from the builder stage.
COPY --from=builder /app/app.jar .

CMD ["java", "-jar", "app.jar"]
