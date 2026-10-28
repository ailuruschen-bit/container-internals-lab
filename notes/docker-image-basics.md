# Docker Image Basics: Learning to Inspect Images First

**English** | [中文](zh/docker-image-basics.md) | [日本語](ja/docker-image-basics.md)

Today I started learning about Docker images. I have not covered much yet: mainly listing local images, inspecting an image's details, and removing images I no longer need.

This note records only what I have actually understood so far. I will extend it as I learn about image layers, pulling, tags, and builds.

## 1. Listing Local Images

```bash
docker image ls
```

This command lists the images currently stored by the Docker Engine. It also has a common shorthand:

```bash
docker images
```

The output looks roughly like this:

```text
REPOSITORY   TAG       IMAGE ID       CREATED       SIZE
alpine       latest    294b683cb724   5 weeks ago   14.6MB
```

The fields mean:

- `REPOSITORY`: the name of the repository the image belongs to, such as `alpine`.
- `TAG`: the image tag, such as `latest`.
- `IMAGE ID`: the identifier of the local image; only its first part is shown by default.
- `CREATED`: when the image was created, not when I downloaded it.
- `SIZE`: the image size as shown in the current Docker environment.

An image can usually be referenced as `repository:tag`:

```text
alpine:latest
nginx:alpine
```

If the tag is omitted, Docker normally uses `latest`. But `latest` is just an ordinary tag name; it does not mean Docker works out which version is the newest.

To see the full ID, use:

```bash
docker image ls --no-trunc
```

To look for a specific image, specify its name directly:

```bash
docker image ls alpine
```

What this returns are images already present locally in the Docker Engine, not search results from a remote registry.

## 2. Inspecting an Image's Details

```bash
docker image inspect alpine
```

`inspect` returns a block of JSON. Compared with the summary from `image ls`, it is much closer to the image metadata Docker actually stores.

An image can also be inspected by its tagged name or by its image ID:

```bash
docker image inspect alpine:latest
docker image inspect 294b683cb724
```

The outermost level of the output is an array, because the command can inspect several images at once:

```bash
docker image inspect alpine nginx:alpine
```

### Fields I Currently Focus On

| Field | My understanding |
|---|---|
| `Id` | The full content identifier of the local image's configuration object |
| `RepoTags` | The repository names and tags that point to this image |
| `RepoDigests` | Content-digest references corresponding to the image as obtained from a registry |
| `Created` | The creation time recorded in the image metadata |
| `Os` | The operating system the image targets, such as `linux` |
| `Architecture` | The CPU architecture the image targets, such as `arm64` |
| `Size` | The image size, usually expressed in bytes in the JSON |
| `Config` | The default runtime configuration the image prepares for containers |
| `RootFS.Layers` | Digests of the layers that make up the image's root filesystem |

`Config` contains several more important pieces of information:

- `Env`: environment variables preset by the image.
- `Cmd`: the default command used when no other command is specified.
- `Entrypoint`: the entry-point program configured by the image.
- `WorkingDir`: the default working directory after the container starts.
- `User`: the user the image runs as by default.
- `ExposedPorts`: the service ports declared by the image.

Not every image sets all of these fields. A field that is not configured may be empty or may not appear in the output at all.

`ExposedPorts` is only information provided by the image; it does not automatically publish ports to the host environment. Actually setting up port mapping still requires options such as `-p` when running the container.

This made me confirm one thing again: an image is not just a set of files; it also carries the default configuration used when creating containers. When `docker run` starts a container, it uses both the image's filesystem contents and this configuration, and command-line options can override some of these defaults.

## 3. Extracting Specific Information with `inspect -f`

The full JSON is detailed, but sometimes I only want one field. That is what `-f`, short for `--format`, is for:

```bash
docker image inspect -f '{{.Id}}' alpine
```

What follows `-f` is a Go template. In its most basic form, it starts from the current object, represented by `.`, and walks down the JSON field hierarchy.

For example, to see the OS and CPU architecture:

```bash
docker image inspect -f '{{.Os}}/{{.Architecture}}' alpine
```

The output might be:

```text
linux/arm64
```

To see the default command:

```bash
docker image inspect -f '{{json .Config.Cmd}}' alpine
```

To see the default working directory:

```bash
docker image inspect -f '{{.Config.WorkingDir}}' alpine
```

To see the list of image tags:

```bash
docker image inspect -f '{{json .RepoTags}}' alpine
```

`json` is used here so that arrays and nested objects are printed as valid JSON rather than in the template's default rendering.

So my current understanding of `inspect -f` is:

```text
inspect       retrieves the complete object
    ↓
--format      selects and arranges output with a template
    ↓
{{.Config.Cmd}}  reads a value along the field hierarchy
```

If I am not sure of a field path, I can look at the full `inspect` JSON once and then write the template based on its nesting.

## 4. Removing Local Images

```bash
docker image rm alpine:latest
```

It has two equivalent forms:

```bash
docker image remove alpine:latest
docker rmi alpine:latest
```

The remove command accepts an image name, a tagged name, or an image ID, and it can take multiple targets at once:

```bash
docker image rm alpine:latest nginx:alpine
```

What gets removed here is the local image or tag in the current Docker Engine; nothing in the remote registry is deleted.

Tags and the image itself also need to be distinguished. If other tags still refer to the same image, removing one tag may only drop that one name, and the underlying image data does not necessarily disappear right away.

If a container still depends on the image, Docker may refuse to remove it and report a conflict. In that case, I should first check whether the related containers are still needed, rather than reaching for a forced removal as soon as I see the error:

```bash
docker ps -a
```

`docker image rm` does provide `-f` / `--force`, but that should be a deliberate choice made after understanding the references involved, not a default action.

## 5. Today's Basic Understanding

For now, I think of an image as two parts:

```text
Docker Image
    ├── Filesystem contents
    └── Default configuration for creating containers
```

Mapped to the commands I used today:

```text
docker image ls       list which images exist locally
docker image inspect  see what an image records
inspect -f            extract the fields I need from the details
docker image rm       remove a local image or tag
```

This is only the beginning of learning about images. Why images are layered, how tags differ from digests, what is actually downloaded during a pull, and how a Dockerfile produces an image are topics I will add as I continue.
