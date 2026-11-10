# Building Docker Images in Practice (4): Java and Multi-stage Builds

**English** | [中文](zh/12-docker-image-build-java-multistage.md) | [日本語](ja/12-docker-image-build-java-multistage.md)

This round of experiments moves on to building Java application images: first compiling and running in a single image, then using a multi-stage build to separate the build environment from the runtime environment, then looking at how `ENTRYPOINT` passes arguments, and finally applying the same approach to a minimal Spring Boot project.

The most obvious change this time is that the Dockerfile no longer just copies in a ready-made program; it starts doing real build work.

## 1. Single-Stage Build: Compiling Java Inside the Image

The first version uses a JDK base image and completes source compilation, JAR packaging, and execution in the same build stage.

The Dockerfile can be simplified to:

```dockerfile
FROM eclipse-temurin:21-jdk

WORKDIR /app

COPY Hello.java .

RUN javac Hello.java && \
    jar --create \
        --file app.jar \
        --main-class Hello \
        Hello.class

CMD ["java", "-jar", "app.jar"]
```

This is the first time I actually used `RUN`.

### `RUN` Executes During the Build

```dockerfile
RUN javac Hello.java && \
    jar --create --file app.jar --main-class Hello Hello.class
```

The builder runs `javac` and `jar` during the build:

```text
Hello.java
    │ javac
    ▼
Hello.class
    │ jar
    ▼
app.jar
```

Once the command finishes, the generated `Hello.class` and `app.jar` become part of this build stage's filesystem result.

This differs from the `CMD` I learned earlier:

```text
RUN  ──→ runs at docker build time to produce image content
CMD  ──→ runs at docker run time to start the container process
```

So the Java program is not started through `CMD` while the image is being built. Only later, when a container is created, is the following executed:

```text
java -jar app.jar
```

### Building and Running

```bash
docker build -t java-lab:v1 .
docker run --rm java-lab:v1
```

This version already runs correctly, but the final image is still based on the full JDK image. The JDK includes the compiler and other development tools, and a container that only runs an already compiled JAR usually has no need to keep those build tools around.

In my lab, `java-lab:v1` was roughly:

```text
746 MB
```

That raises the next question: can I use the JDK at build time while keeping only what is needed to run the Java program in the final image?

## 2. Multi-stage Builds: Separating Compilation from Execution

A multi-stage build allows one Dockerfile to contain several `FROM` instructions. Each `FROM` starts a new build stage.

This experiment can be written as:

```dockerfile
FROM eclipse-temurin:21-jdk AS builder

WORKDIR /app
COPY Hello.java .

RUN javac Hello.java && \
    jar --create \
        --file app.jar \
        --main-class Hello \
        Hello.class


FROM eclipse-temurin:21-jre

WORKDIR /app

COPY --from=builder /app/app.jar .

CMD ["java", "-jar", "app.jar"]
```

This Dockerfile contains two stages:

```text
builder stage                  final runtime stage
JDK                            JRE
  │                              │
  ├── COPY Hello.java            │
  ├── javac                      │
  └── jar → app.jar ─────────────┤ COPY --from=builder
                                 │
                                 └── CMD java -jar app.jar
```

### `FROM ... AS` Names a Build Stage

```dockerfile
FROM eclipse-temurin:21-jdk AS builder
```

`builder` is the name of this build stage. It is not an alias for any particular image layer.

A stage can still produce multiple layers through several steps such as `COPY` and `RUN`; `AS builder` names the whole build environment so that later stages can refer to it.

Stages can also be referenced by index if they are not named, but a name does not change when new stages are inserted into the Dockerfile, and it makes the intent clearer.

## 3. `COPY --from` Takes Only the Artifacts That Are Needed

The second stage runs:

```dockerfile
COPY --from=builder /app/app.jar .
```

Two different filesystem views are involved here:

- `/app/app.jar` is the source file in the `builder` stage.
- The destination `.` belongs to the current JRE runtime stage and is resolved against the current `WORKDIR /app`.

The result is:

```text
builder:/app/app.jar
          │
          │ COPY --from=builder
          ▼
final:/app/app.jar
```

`Hello.java`, `Hello.class`, `javac`, and the other JDK tools from the build stage do not come along into the final image with this `COPY`. Only the explicitly copied `app.jar` is carried over to the runtime stage.

The difference from an ordinary `COPY` is:

```text
COPY source target
    source comes from the build context

COPY --from=builder source target
    source comes from another build stage's filesystem
```

## 4. What the Final Image Actually Keeps

When the whole Dockerfile is built by default, the last stage becomes the output image.

Stages and layers need to be kept apart here; it is not accurate to say that "only the last layer is kept":

```text
Final image
    ├── Base image layers of the last stage
    ├── Runtime configuration such as WORKDIR
    ├── The layer holding the copied app.jar
    └── Image config such as CMD
```

The final runtime image still contains the several layers that the last stage itself needs. What does not make it into the final image is the full filesystem of the earlier `builder` stage, along with its build tools.

So a multi-stage build does not merge everything and then remove the JDK. It starts over from a lighter runtime base image and copies over only the artifacts that are needed:

```text
Single-stage
JDK + source + class + JAR + build tools

Multi-stage final result
JRE + JAR
```

In my lab, the two images were:

```text
java-lab:v1    about 746 MB
java-lab:v2    about 475 MB
```

The size dropped not because the JAR got smaller, but because the final image is now based on the JRE and no longer carries the full JDK build environment.

## 5. The Earlier Stage Is Not in the Image, but Is Its Cache Still There?

The earlier `builder` stage does not become part of the final image's filesystem, but that does not mean every build has to compile from scratch.

BuildKit can keep the results of each step of earlier stages in its cache. When the Dockerfile, the source code, and other relevant inputs have not changed, a later build may still show:

```text
CACHED
```

So both of the following can be true at the same time:

```text
The builder stage is not part of the final runtime image
                ＋
The builder stage's build results can be reused from the cache
```

The cache, however, is not something permanently attached to the final image. It falls under the current builder's cache management and may be gone because of manual cleanup, garbage collection, switching builders, or moving to another machine. Reusing it on other machines or in CI requires explicitly importing, exporting, or sharing the build cache.

So the more accurate statement is not "the earlier stage's cache is never lost," but:

> Although earlier stages do not enter the final image, they keep speeding up later builds as long as the relevant build cache is still available.

## 6. Verifying the Final Image with `history`

Build the multi-stage version:

```bash
docker build \
  --progress=plain \
  -t java-lab:v2 .
```

Then look at the final image's history:

```bash
docker image history java-lab:v2
```

The top of the output on my machine mainly shows:

```text
CMD ["java" "-jar" "app.jar"]
COPY /app/app.jar .
WORKDIR /app
```

Below that is the history of the final JRE base image, not the file layers produced by `javac` and `jar` in the JDK build stage.

This confirms that the final image does come from the last, runtime stage and received only the `app.jar` output by the `builder` stage.

You can also check the final startup command:

```bash
docker image inspect java-lab:v2 \
  -f '{{json .Config.Cmd}}'
```

Output:

```json
["java","-jar","app.jar"]
```

Run the container:

```bash
docker run --rm java-lab:v2
```

The container only needs to run the already built JAR; it does not recompile the source at startup.

## 7. The Understanding So Far

I now see building a Java application image as two distinct responsibilities:

```text
Build Stage
    ├── Needs the JDK
    ├── Compiles the source
    └── Produces the JAR
             │
             │ only the build artifact is passed on
             ▼
Runtime Stage
    ├── Needs only the JRE
    ├── Holds the JAR
    └── Starts the Java process
```

A single-stage Dockerfile is shorter, but it puts every tool needed both to produce the program and to run it into the same final image.

A multi-stage build lets different stages use different base images: the build stage uses a JDK with the complete toolchain, the runtime stage uses a JRE that meets only the program's runtime needs, and `COPY --from` passes the artifact across explicitly.

The benefit is not just a smaller image. The final runtime environment is also closer to what is really needed, without source code, compilers, or other build-only files, and the responsibilities of building and running are more clearly separated.

On this foundation, I went on to move the Java startup command from `CMD` to `ENTRYPOINT` and tried building a real Spring Boot project.

## 8. Pinning the Java Program with `ENTRYPOINT`

The previous version used:

```dockerfile
CMD ["java", "-jar", "app.jar"]
```

Any arguments passed after the image name replace the entire `CMD`:

```bash
docker run --rm java-lab:v2 hello
```

Docker then tries to run `hello` as the container's program, instead of passing it to `app.jar`.

To pin the Java program itself while keeping its arguments variable, the Dockerfile can be changed to:

```dockerfile
ENTRYPOINT ["java", "-jar", "app.jar"]
CMD ["default"]
```

With both in exec form, the default startup command can be thought of as:

```text
ENTRYPOINT                         CMD
["java", "-jar", "app.jar"]  +  ["default"]
                    │
                    ▼
java -jar app.jar default
```

`ENTRYPOINT` pins the program to run, and `CMD` provides the default arguments here.

### Using the Default Arguments

```bash
docker run --rm java-lab:v3
```

Output in my lab:

```text
Java application started!
Argument: default
```

### Passing New Arguments with `docker run`

```bash
docker run --rm java-lab:v3 Codex
```

The output becomes:

```text
Java application started!
Argument: Codex
```

`Codex` after the image name replaces `CMD ["default"]` and is then appended as an argument to the existing exec-form `ENTRYPOINT`:

```text
java -jar app.jar Codex
```

So the more accurate statement is not "whatever follows `docker run` is always passed to Java," but:

> When an image has an exec-form `ENTRYPOINT`, the arguments given on the run command replace `CMD` and are passed to the `ENTRYPOINT` as arguments.

## 9. Overriding the Default Entry Point with `--entrypoint`

To replace the default program as well, use:

```bash
docker run --rm \
  --entrypoint java \
  java-lab:v3 \
  -version
```

This time the container runs:

```text
java -version
```

rather than:

```text
java -jar app.jar default
```

`--entrypoint java` overrides the image's `ENTRYPOINT` for this new container. When this option is used, the image's original `CMD` is no longer appended automatically; `-version` after the image name becomes the argument to the new entry point.

Both the "override" and the "dropped CMD" apply only to this container's configuration; they do not modify the original `java-lab:v3` image. The next time the image is run normally, it uses its original `ENTRYPOINT` and `CMD` again.

To check the two settings stored in the image, run:

```bash
docker image inspect java-lab:v3 \
  -f 'Entrypoint={{json .Config.Entrypoint}} Cmd={{json .Config.Cmd}}'
```

The result on my machine:

```text
Entrypoint=["java","-jar","app.jar"] Cmd=["default"]
```

### My Current Understanding of `CMD` and `ENTRYPOINT`

| Setting | Role in this experiment |
|---|---|
| `ENTRYPOINT` | Pins the Java program the container mainly runs |
| `CMD` | Provides replaceable default arguments to the entry-point program |
| Arguments after the image name | Replace `CMD` and are passed to the default `ENTRYPOINT` |
| `--entrypoint` | Replaces the default entry point for this container; the original `CMD` no longer takes part automatically |

This combination suits images where "the program is fixed and the arguments change often." It does not mean every image must use both `ENTRYPOINT` and `CMD`; the choice still depends on whether users are expected to override the program or only its arguments.

## 10. Applying Multi-stage Builds to Spring Boot

After the plain Java JAR experiments, I built a minimal Spring Boot project.

A Spring Boot project no longer calls `javac` and `jar` directly. Instead, Maven resolves dependencies, compiles the source, runs the build lifecycle, and produces an executable JAR. The build stage therefore uses a base image with Maven and a JDK, while the runtime stage still uses only a JRE.

The project structure can be simplified to:

```text
spring-lab/
├── Dockerfile
├── pom.xml
└── src/
    └── main/
        ├── java/
        └── resources/
```

The corresponding Dockerfile is roughly:

```dockerfile
FROM maven:3.9-eclipse-temurin-21 AS build

WORKDIR /build

COPY pom.xml .
RUN mvn dependency:go-offline

COPY src ./src
RUN mvn clean package -DskipTests


FROM eclipse-temurin:21-jre

WORKDIR /app

COPY --from=build /build/target/*.jar app.jar

EXPOSE 8080

ENTRYPOINT ["java", "-jar", "app.jar"]
```

These are still the same two stages I already understand:

```text
Maven + JDK Build Stage
    ├── Reads pom.xml
    ├── Downloads dependencies
    ├── Compiles the Spring Boot project
    └── target/*.jar
                 │
                 │ COPY --from=build
                 ▼
JRE Runtime Stage
    ├── app.jar
    └── java -jar app.jar
```

## 11. What Maven Is Responsible for in This Spring Boot Project

This Spring Boot project uses Maven, so the build process relies heavily on Maven, but that does not mean Spring Boot can only be used with Maven. Spring Boot supports Gradle just as well; the more accurate statement is: **for this project, which is managed by `pom.xml`, dependency resolution and the build process are driven by Maven.**

After reading `pom.xml`, Maven first assembles the build model that actually takes effect for the project. Important sources include:

- The coordinates, properties, and build configuration declared by the project itself.
- Configuration inherited from the parent POM through `<parent>`.
- Dependency version management provided by `<dependencyManagement>`.
- Direct dependencies declared in `<dependencies>`, along with their transitive dependencies.
- Maven plugins configured in, or inherited into, `<build>`.

If the project inherits from `spring-boot-starter-parent`, it gets a set of defaults provided by Spring Boot, such as managed versions of common dependencies, compiler settings, and some plugin configuration. "Inheritance" here is not Java class inheritance; it means Maven combines parent and child configuration into an effective POM while resolving the POM.

What is declared in `<dependencies>` is also better not lumped together as "modules." Maven deals with artifacts, most commonly JARs. A direct dependency may in turn reference other artifacts, and Maven computes the final set of dependencies according to rules such as transitive resolution, version management, and scope.

During resolution, Maven usually checks the local repository first; POMs, JARs, and plugins not found locally are then downloaded from the configured remote repositories. By default, the local repository is at:

```text
~/.m2/repository
```

So Maven does more than "download libraries." It also interprets the POM, resolves the dependency graph, runs the lifecycle and plugins, and finally produces the build artifacts.

For a Spring Boot project, `mvn package` runs the lifecycle steps Maven needs up to the `package` phase. With the Spring Boot Maven Plugin configured, it also repackages the ordinary JAR into an executable JAR that can be started with:

```text
java -jar app.jar
```

## 12. Two Ways to Write the Maven Build Stage

After this experiment, I realized there are at least two common ways to write the build stage. Both ultimately build the project with Maven; the difference is whether Maven is provided by the base image or launched through the project's own wrapper.

### Option 1: A Base Image That Includes Maven

The current experiment uses:

```dockerfile
FROM maven:3.9-eclipse-temurin-21 AS build
```

This base image already has Maven and a JDK, so it can run directly:

```dockerfile
RUN mvn dependency:go-offline
RUN mvn clean package -DskipTests
```

This calls the global `mvn` installed in the base image. The advantage is a simple Dockerfile with no wrapper to bootstrap; the Maven version is determined by the chosen Maven image tag.

This approach should still use an explicit base image version. With a broad tag that keeps moving, the actual Maven or JDK version may still change when the image is rebuilt at a different time.

### Option 2: The Maven Wrapper on a JDK Image

A project with the Maven Wrapper enabled usually contains:

```text
spring-lab/
├── .mvn/
│   └── wrapper/
│       └── maven-wrapper.properties
├── mvnw
├── mvnw.cmd
├── pom.xml
└── src/
```

Where:

- `mvnw` is the launcher script for Linux and macOS.
- `mvnw.cmd` is the launcher script for Windows.
- `.mvn/wrapper/maven-wrapper.properties` records the Maven distribution the wrapper uses, its download URL, and related settings.
- Depending on the wrapper type, there may also be a `maven-wrapper.jar` or downloader source code.

The wrapper does not commit a complete Maven installation into the project. When `./mvnw` runs, it looks for the version specified by the project's wrapper configuration and, if it is not available locally, downloads and uses that Maven distribution.

In a multi-stage Dockerfile, a plain JDK image can be used instead:

```dockerfile
FROM eclipse-temurin:21-jdk AS build

WORKDIR /build

COPY .mvn .mvn
COPY --chmod=0755 mvnw .
COPY pom.xml .

RUN ./mvnw dependency:go-offline

COPY src ./src
RUN ./mvnw clean package -DskipTests


FROM eclipse-temurin:21-jre

WORKDIR /app
COPY --from=build /build/target/*.jar app.jar

EXPOSE 8080
ENTRYPOINT ["java", "-jar", "app.jar"]
```

Here the global `mvn` is never called; the project's own `./mvnw` is always used instead. That way, the development machine, CI, and the Docker build stage can all use the specified Maven version according to the same wrapper configuration.

`mvnw` must be executable. The example sets the permission at copy time with `COPY --chmod=0755`; alternatively, the executable bit can be stored correctly in the repository.

The first run of the wrapper usually also needs network access to download the Maven distribution. Depending on its type, the wrapper may rely on the wrapper JAR, a Java downloader, or tools such as `curl` and `wget`, so it cannot be assumed that any slimmed-down JDK image has everything needed to bootstrap it. After choosing a base image, it still has to be verified that the wrapper actually starts.

### The Maven Distribution and Project Dependencies Live in Different Cache Directories

The Maven distribution downloaded by the wrapper is stored by default in:

```text
~/.m2/wrapper/dists
```

The project dependencies and plugins resolved by Maven are stored by default in:

```text
~/.m2/repository
```

If the Docker build stage runs as root by default, `~` is usually `/root`, so the corresponding paths are:

```text
/root/.m2/wrapper/dists
/root/.m2/repository
```

If the base image switches to a different build user, or Maven options change the local repository location, the actual paths change as well. `~/.m2/repository` is therefore a default convention, not an immutable fixed location.

Whether the global Maven in the base image or the Maven Wrapper is used, dependency resolution goes through the same Maven local repository mechanism. The main difference is "who decides on, and provides, Maven itself":

| Build approach | Where Maven comes from | What mainly determines the Maven version |
|---|---|---|
| Maven base image | Already installed in the image | The base image tag |
| Maven Wrapper | Obtained by the wrapper according to the project configuration | The `.mvn/wrapper` configuration |

Both approaches are used only in the build stage. Because the final stage copies only the JAR, Maven, the wrapper, `~/.m2`, and the project source do not end up in the final JRE image.

## 13. Why Copy `pom.xml` Before the Source Code

The build stage does not start with `COPY . .`; it first handles the dependency descriptor:

```dockerfile
COPY pom.xml .
RUN mvn dependency:go-offline
```

Only then does it copy the frequently changing source code:

```dockerfile
COPY src ./src
RUN mvn clean package -DskipTests
```

This maps directly to the build cache experiment in the previous note.

When `pom.xml` is unchanged but the Java source changes, the dependency download step still has a chance to hit the cache, and only copying the source and packaging need to run again. If the whole project were copied in at the start, any source change would alter the result of that early `COPY`, making the subsequent dependency download step much more likely to lose its cache as well.

The two orders can be compared like this:

```text
Harder to reuse the cache      Easier to reuse the cache

COPY . .                       COPY pom.xml .
RUN mvn package                RUN mvn dependency:go-offline
                               COPY src ./src
                               RUN mvn package
```

`dependency:go-offline` resolves and downloads the project's dependencies and related plugins ahead of time, so that they become part of that build step's cached result. It does not mean the final running container must work offline, nor does it copy the full Maven environment into the final JRE image.

If `pom.xml` changes, the dependency input itself has changed, so re-running the dependency download is a legitimate cache invalidation.

## 14. What the Spring Boot Final Image Keeps

The second `FROM` starts a brand-new runtime stage:

```dockerfile
FROM eclipse-temurin:21-jre
```

The final image does not wholesale include Maven, the project source, or the local dependency repository from the build stage. Only the packaged artifact is copied over from the previous stage:

```dockerfile
COPY --from=build /build/target/*.jar app.jar
```

On my machine, the top of the final history of `spring-lab:v1` mainly contains:

```text
COPY /build/target/*.jar app.jar
EXPOSE 8080
ENTRYPOINT ["java" "-jar" "app.jar"]
```

The JAR copied into the final image is about 23 MB, and the whole `spring-lab:v1` image is about 519 MB. Most of the image size comes from the JRE base image, not from the Maven build stage.

`EXPOSE 8080` only states that the image is expected to serve on the container's `8080/tcp`; it does not publish the port to the host automatically. That still has to be configured explicitly at run time:

```bash
docker run -d \
  --name springboot1 \
  -p 127.0.0.1:8081:8080 \
  spring-lab:v1
```

This publishes port `8081` on macOS to the container's `8080`. Check the logs:

```bash
docker logs springboot1
```

The logs on my machine showed that the embedded Tomcat had started on the container's port 8080. When accessing the root path:

```bash
curl -i http://localhost:8081
```

the current minimal project returned HTTP 404. That does not mean the container or Tomcat failed to start; the application simply does not yet define a controller for `/`. Port connectivity and the Spring Boot startup itself have been verified.

## 15. The Complete Picture from This Round of Java Build Experiments

From the first single-stage Java build to the Maven multi-stage build for Spring Boot, this round of experiments gradually pulled apart four responsibilities:

```text
Build Tool
    Maven / javac / jar
            │
            ▼
Build Artifact
    Executable JAR
            │
            │ COPY --from
            ▼
Runtime Environment
    JRE
            │
            ▼
Container Process
    ENTRYPOINT + CMD or run-time arguments
```

My understanding of containerizing a Java application is no longer "copy the Java project directory into an image and start it," but:

1. Produce a built JAR in a build stage, using either a Maven + JDK base image or a JDK plus the Maven Wrapper.
2. Pass only the JAR on to a leaner runtime stage.
3. Pin the Java launcher with `ENTRYPOINT`, and provide default arguments with `CMD` where needed.
4. Order the Dockerfile so that Maven dependency downloads reuse the build cache as much as possible.
5. Use the same image across environments through run-time port publishing and external configuration.

Multi-stage builds separate the build environment from the runtime environment, `ENTRYPOINT` and `CMD` combine the container's main program with its default arguments, and the order of the Maven dependency steps affects how efficient repeated builds are. Only together do these parts make up a reasonably complete build process for a Java application image.
