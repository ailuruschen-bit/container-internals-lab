# Building Docker Images in Practice (3): Cache, ARG/ENV, and .dockerignore

**English** | [中文](zh/docker-image-build-cache-args-ignore.md) | [日本語](ja/docker-image-build-cache-args-ignore.md)

The previous two experiments answered "how do I build an image?" This time I looked more closely at the build process itself: why Docker can speed up repeated builds, how to pass different variables to the build stage and the run stage, and how to control which files are in the build context.

These three experiments look independent, but they all affect the same thing: **which inputs take part in an image build, and which steps have to be re-executed when those inputs change.**

## 1. Observing the Build Cache

The first experiment used a Dockerfile similar to this:

```dockerfile
FROM alpine:latest

RUN apk add --no-cache curl

WORKDIR /app

COPY message.txt .

CMD ["cat", "message.txt"]
```

Add `--progress=plain` when building:

```bash
docker build \
  --progress=plain \
  -t cache-lab:v1 .
```

The default progress display refreshes dynamically, whereas `plain` prints every build step as ordinary text, which makes it easier to see whether each step hits the cache.

On the first build, every step has to actually run, and Docker saves the build results that can be reused. Running the same command again with neither the Dockerfile nor the build inputs changed produces output containing something like:

```text
CACHED
```

`--progress=plain` only changes how output is displayed; it does not turn the cache on or off. The build cache is an integral part of the builder.

### Why the Cache Gets Invalidated

Each build step in a Dockerfile depends on the result of the previous step and on the inputs of the current instruction:

```text
FROM
  ↓
RUN apk add curl
  ↓
WORKDIR /app
  ↓
COPY message.txt .
  ↓
CMD ...
```

If only `message.txt` is modified, the input to `COPY` changes and its previous result can no longer be used. The `RUN apk add` step before it is unaffected and can still hit the cache, but the steps after `COPY` depend on the new upper-layer result and usually have to be recomputed along this new path as well.

The order of a Dockerfile therefore directly affects how fast repeated builds are. A practical rule of thumb:

> Put steps that are relatively stable and expensive to run first; put content that changes often last.

For example, since application source code changes often, `COPY . .` should not come before the installation of stable system dependencies:

```dockerfile
FROM alpine:latest

RUN apk add --no-cache curl

WORKDIR /app
COPY message.txt .

CMD ["cat", "message.txt"]
```

This way, modifying `message.txt` does not require reinstalling `curl`.

Dependencies in real projects can be split further. For example, copy the dependency manifest and install dependencies first, then copy the frequently changing source code last. The expensive dependency installation step then has to be re-executed only when the dependency manifest changes.

To discard the existing cache explicitly, use:

```bash
docker build --no-cache -t cache-lab:v1 .
```

Everyday builds should not disable the cache out of habit, though. A more valuable approach is to arrange the Dockerfile so that the scope of cache invalidation matches the scope of the actual file changes as closely as possible.

## 2. `ARG`: Passing Build-Time Parameters

The second experiment starts by declaring a build argument in the Dockerfile:

```dockerfile
FROM alpine:latest

ARG BUILD_LABEL=experiment

RUN echo "build label: $BUILD_LABEL"
```

The default value of the `ARG` is `experiment`. In a plain build, subsequent build instructions can read this value:

```bash
docker build \
  --progress=plain \
  -t lab-env:v1 .
```

The default can also be overridden on the command line:

```bash
docker build \
  --build-arg BUILD_LABEL=release \
  --progress=plain \
  -t lab-env:v2 .
```

What happens here is a new build-argument input, so steps that use `BUILD_LABEL` may lose their existing cache as a result.

The scope of `ARG` is the image build process. Having appeared during the build stage does not automatically make it an environment variable at container run time.

## 3. `ENV`: Storing the Image's Default Environment Variables

The same Dockerfile can also include:

```dockerfile
ENV RUN_MODE=production

CMD ["sh", "-c", "echo RUN_MODE=$RUN_MODE; echo BUILD_LABEL=${BUILD_LABEL:-unset}"]
```

Put together, the Dockerfile can be simplified to:

```dockerfile
FROM alpine:latest

ARG BUILD_LABEL=experiment
ENV RUN_MODE=production

RUN echo "build label: $BUILD_LABEL"

CMD ["sh", "-c", "echo RUN_MODE=$RUN_MODE; echo BUILD_LABEL=${BUILD_LABEL:-unset}"]
```

Run the image:

```bash
docker run --rm lab-env:v1
```

The result looks like this:

```text
RUN_MODE=production
BUILD_LABEL=unset
```

This shows that:

- `RUN_MODE` is written into the image config by `ENV` and is still present by default when a container is created.
- `BUILD_LABEL` is provided to the build stage only through `ARG` and does not automatically enter the runtime environment.

Nor is `ENV` accessible only at run time. Once declared in the Dockerfile, it can also be read by subsequent build instructions; the key difference from `ARG` is that it is additionally kept as one of the image's default environment variables.

The default can be overridden when running a container:

```bash
docker run --rm \
  -e RUN_MODE=development \
  lab-env:v1
```

This time the container sees:

```text
RUN_MODE=development
```

`-e` (or `--env`) changes the runtime configuration of the new container; it does not go back and modify `RUN_MODE=production` in the original image. The next container created without `-e` still uses the image's default value.

### How `ARG` and `ENV` Relate

| Aspect | `ARG` | `ENV` |
|---|---|---|
| Main purpose | Build parameters | Environment variables for the image and its containers |
| Readable by later build steps in the Dockerfile | Yes | Yes |
| Kept in the container's runtime environment by default | No | Yes |
| How to override from the command line | `docker build --build-arg` | `docker run -e` |

If you really do want to turn a build argument into a default environment variable of the image, the hand-off has to be written out explicitly:

```dockerfile
ARG APP_VERSION
ENV APP_VERSION=$APP_VERSION
```

Only then does `APP_VERSION` end up in the final image config.

Neither `ARG` nor `ENV` is suitable for passing passwords, tokens, or private keys, however. Build arguments may show up in build records or metadata, and `ENV` is stored directly in the image config. Sensitive information should go through dedicated build-secret or runtime-secret mechanisms, rather than relying on variable names to hide it.

## 4. `.dockerignore`: Filtering the Build Context First

The third experiment used:

```dockerfile
FROM alpine:latest

WORKDIR /app

COPY . .

CMD ["ls", "-la", "/app"]
```

`COPY . .` covers a lot: the first `.` means everything available in the build context, and the second `.` means the image's current working directory.

If the project directory also contains logs, temporary files, and local tool directories alongside the source code, copying everything directly can put unneeded files into the image as well.

A `.dockerignore` file can be created at the root of the build context:

```text
.git
*.log
tmp/
secret.txt
```

On the next build, anything matching these rules is excluded while the build context is being prepared, so the subsequent `COPY . .` can no longer copy it.

Its purpose is similar to that of `.gitignore`, in that both use rules to exclude files you do not want processed, but they belong to different tools, and their matching rules should not simply be assumed to be identical.

What `.dockerignore` provides:

- It reduces the amount of context sent to the builder.
- It prevents irrelevant files from being `COPY`ed into the image.
- It reduces `COPY` cache invalidations caused by changes to irrelevant files.
- It lowers the risk of accidentally packaging local logs, version-control directories, or sensitive files.

The last point is especially important, but `.dockerignore` is only one layer of protection against accidental inclusion and should not be treated as a complete secrets-management solution. Real credentials are best kept out of the ordinary build context from the start and never written into image layers.

### How `.dockerignore` Connects to the Cache

This step brings me back to the first experiment.

Suppose the project contains a constantly changing `debug.log`. If it is part of the input to `COPY . .`, every change to the log may cause this layer to lose its cache, even when the application code has not changed at all.

Once `*.log` is added to `.dockerignore`, logs are no longer part of this `COPY`'s input, so their changes stop pointlessly breaking the cache for this step.

So `.dockerignore` does not merely shrink what goes into the image; it also helps the builder judge more accurately which inputs have really changed.

## 5. What I Understood Today

These three rounds of experiments connect into one complete chain of build inputs:

```text
Project directory
    │
    │ filtered by .dockerignore
    ▼
Build Context
    │
    ├── Dockerfile instructions
    ├── Files used by COPY
    └── Arguments from --build-arg
              │
              ▼
       Build cache decisions
              │
              ▼
         Docker Image
              │
              ├── ARG: not in the runtime environment by default
              └── ENV: stored as a default environment variable
                          │
                          ▼
                    docker run -e
                    overrides it when creating a container
```

The build cache is not a simple global switch but a reuse mechanism built on each instruction and its inputs. A change in the result of an earlier step changes the foundation that later steps depend on.

Nor are `ARG` and `ENV` two ways of writing the same kind of variable: `ARG` is mainly about parameterizing the build, while `ENV` takes part in the build and then goes on to become the container's default runtime environment.

`.dockerignore` sits even earlier in the chain. It decides first which local files are eligible to enter the build context, and thereby affects the contents of `COPY`, build efficiency, and cache stability all at once.

This experiment got me to see a Dockerfile as a build process that has inputs and dependencies and can reuse intermediate results, rather than just a few commands arranged in order.
