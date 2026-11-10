# ENTRYPOINT + CMD experiment.
#   docker build -t java-lab:v3 .
#   docker run --rm java-lab:v3                          -> Argument: default
#   docker run --rm java-lab:v3 hello                    -> CMD replaced: Argument: hello
#   docker run --rm --entrypoint java java-lab:v3 -version
#                                                        -> ENTRYPOINT replaced; CMD no longer appended

# Stage 1: compile
FROM eclipse-temurin:21-jdk AS builder

WORKDIR /app

COPY Hello.java .

RUN javac Hello.java && \
    jar --create --file app.jar --main-class Hello Hello.class

# Stage 2: runtime
FROM eclipse-temurin:21-jre

WORKDIR /app

COPY --from=builder /app/app.jar .

# Fixed executable for every container started from this image.
ENTRYPOINT ["java", "-jar", "app.jar"]

# Default arguments appended to ENTRYPOINT; replaced by arguments given to `docker run`.
CMD ["default"]
