# Building Docker Images in Practice (1): Hello World

**English** | [中文](zh/docker-image-build-hello-world.md) | [日本語](ja/docker-image-build-hello-world.md)

In the previous note, I learned how to list and remove local images. Today I built my own image from a Dockerfile for the first time.

The program is only two lines of shell, but the point of the experiment is not the program itself. It is to observe how a Dockerfile becomes an image, and how an image in turn becomes a running container.

## 1. Preparing a Minimal Project

The lab environment is still macOS + OrbStack, and the base image is `alpine:latest`.

The project contains just two files:

```text
docker-image-lab/
└── 01-hello/
    ├── Dockerfile
    └── hello.sh
```

`hello.sh`:

```sh
#!/bin/sh
echo "Hello from my first Docker Image!"
pwd
```

`Dockerfile`:

```dockerfile
FROM alpine:latest

WORKDIR /app

COPY hello.sh .

CMD ["sh", "hello.sh"]
```

This Dockerfile ultimately produces two kinds of build output: the files the container needs to see, and the default configuration used when the container starts.

## 2. What Each of the Four Dockerfile Instructions Does

### `FROM`: Choosing the Starting Point

```dockerfile
FROM alpine:latest
```

`FROM` specifies the base image the new image is built on. Here, Alpine's existing filesystem layers and related configuration are reused; if the image is not available locally, the builder can pull it from a registry.

Building `hello-lab` does not modify the original Alpine image. Whatever is new becomes build output that belongs to the new image.

### `WORKDIR`: Setting the Default Working Directory

```dockerfile
WORKDIR /app
```

`WORKDIR` sets the working directory for the relevant instructions that follow, and it also becomes the default working directory when the container starts. If `/app` does not exist, it is created during the build, so there is no need to write this first:

```dockerfile
RUN mkdir -p /app
```

### `COPY`: Putting Files into the Image

```dockerfile
COPY hello.sh .
```

The basic structure of `COPY` is:

```dockerfile
COPY <source> <destination>
```

The source comes from the build context, while the destination belongs to the filesystem of the image being built. The destination `.` here is a relative path, resolved against the previously set `WORKDIR /app`, so the result is:

```text
/app/hello.sh
```

The file's contents are now part of the image; the running container no longer refers to the original `hello.sh` on macOS. This makes `COPY` completely different from a bind mount:

- `COPY` incorporates file contents into the image at build time.
- A bind mount mounts an external path into the container at run time.

### `CMD`: Recording the Default Startup Command

```dockerfile
CMD ["sh", "hello.sh"]
```

`CMD` does not execute `hello.sh` while the image is being built. It writes the default command into the image config, to be used later when a container is created:

```text
docker build  ──→  stores the Cmd configuration
docker run    ──→  starts a process using Cmd
```

Because `sh hello.sh` is invoked explicitly, the shell reads the script's contents, so `hello.sh` itself does not need execute permission. Running the script directly instead would also require the correct execute permission and interpreter setup.

## 3. Exec Form vs. Shell Form

Commands in a Dockerfile come in two common forms.

Exec form uses a JSON array:

```dockerfile
CMD ["sh", "hello.sh"]
```

The first item in the array is the executable, and the rest are the arguments passed to it. By default, this form does not wrap the whole command in another shell, nor does it perform shell variable expansion, which makes it the better choice for starting a container's main process.

Shell form is a single command string:

```dockerfile
CMD sh hello.sh
```

In a Linux container, it is typically executed in a way similar to:

```text
/bin/sh -c "sh hello.sh"
```

Shell form allows shell syntax such as variable expansion and pipes, but the extra shell process can also interfere with how signals are received and forwarded.

The simple distinction I have settled on for now:

```text
Exec Form   states the program and its arguments explicitly
Shell Form  hands a command string to a shell to interpret first
```

## 4. What the Build Context Is

Change into the project directory and build the image:

```bash
cd "$HOME/docker-image-lab/01-hello"
docker build -t hello-lab:v1 .
```

The `.` at the end of the command means the current directory is the build context for this build:

```text
docker build -t hello-lab:v1 .
                                 ↑
                          Build Context
```

The builder can read the local files needed by instructions such as a regular `COPY` or `ADD` only from the build context. `COPY hello.sh .` finds its source file precisely because `hello.sh` is in the current context.

The build context is not the image's filesystem, and not every file in the context automatically ends up in the final image. A file becomes part of the build output only after the corresponding build instruction processes it.

Real projects should also avoid feeding logs, build artifacts, version-control directories, or sensitive files into the build as unnecessary input; `.dockerignore` can exclude them. This directory contains only two files, so no elaborate configuration is needed for now.

The other parts of the build command mean:

- `build`: run a build according to the Dockerfile.
- `-t hello-lab:v1`: give the result an image name and tag.
- `.`: specify the build context, which also makes the builder look for a `Dockerfile` in the current directory by default.

One more point that is easy to confuse: `-t` in `docker build` means `--tag`, whereas `-t` in `docker run` means `--tty`. The same short option can mean different things in different subcommands.

## 5. From Build to Run

The whole process can be represented as:

```text
Dockerfile + hello.sh
          │
          ▼
     docker build
          │
          ▼
 Image: hello-lab:v1
          │
          ▼
      docker run
          │
          ▼
  Container Process
```

The build stage produces the filesystem contents and stores the image's runtime configuration. The run stage then uses the image to create the container's writable layer, network, and process environment, and finally executes the startup command.

Using the image's default `CMD`:

```bash
docker run --rm hello-lab:v1
```

Expected output:

```text
Hello from my first Docker Image!
/app
```

This verifies three things at once:

- `hello.sh` has been copied into the image.
- `CMD` was executed when the container started.
- `/app`, set by `WORKDIR`, became the default working directory.

`--rm` means the container is removed automatically once it stops; the image used to create it is not removed.

## 6. Overriding the Image's Default `CMD`

Specify a different command after the image name:

```bash
docker run --rm hello-lab:v1 echo "CMD overridden"
```

The output becomes:

```text
CMD overridden
```

The basic structure of `docker run` is:

```text
docker run [OPTIONS] IMAGE [COMMAND] [ARG...]
```

This image does not set an `ENTRYPOINT`, so the `COMMAND` after the image name replaces the original `CMD`.

In the same way, the default command can be replaced with an interactive shell:

```bash
docker run --rm -it hello-lab:v1 sh
```

Once inside, inspect the image's contents manually:

```sh
pwd
ls -l /app
cat /app/hello.sh
sh /app/hello.sh
```

This time `hello.sh` is not executed automatically, because `sh` has already replaced the default `CMD`. Once I learn about `ENTRYPOINT`, I will need to revisit how it combines with `CMD` and the command given at run time.

## 7. A Tag Is Just a Reference Name for an Image

The image reference produced by this build is:

```text
hello-lab:v1
```

It can be broken down simply as:

```text
repository : tag
hello-lab  : v1
```

If the tag is omitted, Docker tries `latest` by default:

```bash
docker run hello-lab
```

This actually looks for `hello-lab:latest`. Only `hello-lab:v1` exists right now, and the presence of `v1` locally does not mean Docker will fall back to it.

An additional tag can be added to an existing image:

```bash
docker image tag hello-lab:v1 hello-lab:latest
docker image ls
```

After that, the following command can find `latest`:

```bash
docker run --rm hello-lab
```

This step neither rebuilds the image nor copies its entire filesystem; it only adds another reference name. The same image can have multiple tags, and `latest` is just an ordinary tag with no guarantee that it points to the most recently built content.

## 8. Inspecting the Build Output

Now I can apply the `inspect` command from the previous note to an image I built myself:

```bash
docker image inspect hello-lab:v1
```

To view only the image config:

```bash
docker image inspect hello-lab:v1 \
  -f '{{json .Config}}'
```

To view the working directory and the default command separately:

```bash
docker image inspect hello-lab:v1 \
  -f '{{.Config.WorkingDir}}'

docker image inspect hello-lab:v1 \
  -f '{{json .Config.Cmd}}'
```

This time, the Dockerfile instructions map to the image output as follows:

| Dockerfile instruction | Build output |
|---|---|
| `FROM` | Starts from the base image's filesystem layers and configuration |
| `WORKDIR` | Sets the default working directory, creating it if necessary |
| `COPY` | Adds `hello.sh` to the image filesystem |
| `CMD` | Writes the default startup command into the image config |

So, for now, this image can be understood simply as:

```text
Docker Image
   ├── Filesystem Layers
   │      └── /app/hello.sh
   └── Image Config
          ├── WorkingDir: /app
          └── Cmd: ["sh", "hello.sh"]
```

## 9. What I Understood Today

A Dockerfile is the build specification for an image, an image is the finished build output, and a container is a running instance created from an image:

```text
Dockerfile
    │ docker build
    ▼
Docker Image
    ├── Filesystem contents
    └── Default runtime configuration
    │ docker run
    ▼
Container
    ├── Creates a runtime environment from the image
    ├── Adds its own writable layer
    └── Starts the actual process
```

One image can be used to create many containers, and a container can override some of the image's default configuration when it starts.

Once the build succeeds, the locally built image can still run even if the original Dockerfile and `hello.sh` are no longer kept. What the container really depends on is the build output, not a fresh read of the original project directory.

One last important realization: a Dockerfile should not be thought of simply as a shell script that runs from top to bottom. Different instructions produce different kinds of results:

```text
COPY  processes file contents at build time
CMD   records the default runtime configuration at build time
RUN   executes commands at build time (saved for a later experiment)
```

What I completed today is the smallest end-to-end image build loop. Next, I plan to use a Dockerfile to build a custom Nginx web image and keep observing `RUN`, the build cache, and how static assets get into the image.
