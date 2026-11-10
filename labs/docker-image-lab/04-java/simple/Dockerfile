# Single-stage Java build: compile and run in the same JDK image.
# Works, but the final image still contains the full JDK and build files.
#   docker build -t java-lab:v1 . && docker run --rm java-lab:v1
FROM eclipse-temurin:21-jdk

WORKDIR /app

COPY Hello.java .

# Compile and package into an executable jar whose manifest names the main class.
RUN javac Hello.java && \
    jar --create --file app.jar --main-class Hello Hello.class

CMD ["java", "-jar", "app.jar"]
