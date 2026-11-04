# Building Docker Images in Practice (2): Customizing the Nginx Home Page

**English** | [中文](zh/docker-image-build-nginx.md) | [日本語](ja/docker-image-build-nginx.md)

Last time, I started from `alpine` and built a minimal Hello World image. This time, instead of starting from a general-purpose Linux base image and installing software on it, I built my own Nginx web image directly on top of `nginx:alpine`.

The Dockerfile has only two instructions this time, yet it made two things much clearer to me: how a child image inherits the runtime configuration of its base image, and how `COPY` changes the final filesystem view without modifying the base image.

## 1. Preparing the Project

The project directory can stay very simple:

```text
docker-image-lab/
└── 02-nginx/
    ├── Dockerfile
    └── index.html
```

`index.html` is the custom page to be served, for example:

```html
<!doctype html>
<html lang="zh-CN">
  <head>
    <meta charset="UTF-8">
    <title>My Nginx Image</title>
  </head>
  <body>
    <h1>Hello from my own Nginx image!</h1>
  </body>
</html>
```

The Dockerfile:

```dockerfile
FROM nginx:alpine

COPY index.html /usr/share/nginx/html/index.html
```

Compared with last time, I did not write `WORKDIR` or `CMD` myself. The base image already provides the Nginx program, an entry-point script, a default configuration, and a default web root, so I only need to replace the page content I actually care about.

## 2. Why `CMD` Can Be Omitted

`FROM nginx:alpine` does more than take the filesystem layers of the base image; it also uses the base image's config as the starting point.

The key configuration of the `nginx:alpine` image currently on my machine includes:

```text
Entrypoint: ["/docker-entrypoint.sh"]
Cmd:        ["nginx", "-g", "daemon off;"]
Ports:      80/tcp
```

The new Dockerfile does not set `ENTRYPOINT` or `CMD` again, so the resulting `my-nginx:v1` keeps using this configuration.

When the container finally starts, the two can be thought of, for now, as being combined like this:

```text
/docker-entrypoint.sh nginx -g "daemon off;"
└──── ENTRYPOINT ────┘ └──────── CMD ────────┘
```

The entry-point script first runs the initialization logic provided by the official Nginx image and then executes the Nginx command given by `CMD`. `daemon off;` keeps Nginx running in the foreground; as long as the Nginx process does not exit, the container keeps running.

So the idea that "Nginx starts automatically because `CMD` is inherited" points in the right direction, but the more complete statement is: **the inherited `ENTRYPOINT` and `CMD` together form the container's default startup behavior.**

### Where Port 80 Comes From

The image configuration also contains:

```json
{"80/tcp":{}}
```

This comes from the base image's port declaration and indicates that the image is expected to serve on the container's `80/tcp`. The declaration itself, however, neither makes Nginx start listening nor publishes the container port to the host automatically.

- Whether Nginx listens on port 80 is determined by the Nginx configuration.
- `ExposedPorts` in the image is metadata about the expected port.
- Whether the host can reach the service through a given port also depends on whether options such as `-p` are used when running the container.

## 3. Customizing the Default Home Page with `COPY`

The default static content directory of the official Nginx image is:

```text
/usr/share/nginx/html
```

It already contains a default `index.html`. This build runs:

```dockerfile
COPY index.html /usr/share/nginx/html/index.html
```

The source file comes from this build's context, while the destination is the location of the default home page in the new image's filesystem.

From the perspective of the directory view the container ultimately sees, the custom page replaces the base image's default page. From the perspective of image layers, however, Docker does not go back and modify the existing layers of `nginx:alpine`:

```text
my-nginx:v1
    ├── New layer: custom index.html
    └── Base layers: nginx:alpine
                    └── Original default index.html
```

The upper layer provides a new file at the same path, so reading `/usr/share/nginx/html/index.html` returns the custom content. The old file from the base image still remains in its original read-only layer; it is simply hidden by the upper-layer file in the final merged view.

This also means that building `my-nginx:v1` does not change the local `nginx:alpine`. A container started from the original image later will still show that image's own default page.

## 4. Building the Custom Image

Change into the project directory:

```bash
cd "$HOME/docker-image-lab/02-nginx"
```

Build the image:

```bash
docker build -t my-nginx:v1 .
```

The current directory is again used as the build context. The builder reads the Dockerfile in it and adds `index.html` to the new image through `COPY`.

After the build, list the local images:

```bash
docker image ls my-nginx
```

At this point, `my-nginx:v1` is a new image, and `nginx:alpine` remains on the machine as a separate base image.

## 5. Verifying Configuration Inheritance with `inspect`

View the full image information:

```bash
docker image inspect my-nginx:v1
```

Check only the inherited `CMD`:

```bash
docker image inspect my-nginx:v1 \
  -f '{{json .Config.Cmd}}'
```

In this experiment, the result was:

```json
["nginx","-g","daemon off;"]
```

Note that the option here is `-f`, short for `--format`, not `if`.

View only the `ENTRYPOINT`:

```bash
docker image inspect my-nginx:v1 \
  -f '{{json .Config.Entrypoint}}'
```

Output:

```json
["/docker-entrypoint.sh"]
```

View the declared ports:

```bash
docker image inspect my-nginx:v1 \
  -f '{{json .Config.ExposedPorts}}'
```

Output:

```json
{"80/tcp":{}}
```

These results show that although the child image only adds one page file, the runtime configuration already prepared by the base image is still there.

If the child Dockerfile added a new `CMD`, it would replace the `CMD` inherited from the base image rather than keeping two default commands side by side.

## 6. Observing the Build Result with `history`

```bash
docker image history my-nginx:v1
```

At the top of the output, there is a record similar to:

```text
COPY index.html /usr/share/nginx/html/index.html
```

Below it is the build history inherited from `nginx:alpine`, including steps such as installing Nginx, setting environment variables, declaring ports, and configuring `ENTRYPOINT` and `CMD`.

This corresponds exactly to the inheritance in the Dockerfile:

```text
COPY in my-nginx:v1
          ↓
Existing history of nginx:alpine
          ↓
Contents of the underlying Alpine image
```

`docker image history` is useful for seeing roughly which build steps an image consists of and how much size each step added. It is not a complete backup of the original Dockerfile, however, and the commands in its output may have been transformed or truncated by the builder.

To see the creation commands in fuller form, use:

```bash
docker image history --no-trunc my-nginx:v1
```

## 7. Starting the Container and Verifying the Page

Publish the container's port 80 to port 8080 on the local machine:

```bash
docker run -d --rm \
  --name my-nginx \
  -p 127.0.0.1:8080:80 \
  my-nginx:v1
```

Request the page:

```bash
curl http://localhost:8080
```

Getting back the contents of the custom `index.html` shows that:

- Nginx runs normally with the inherited startup configuration.
- Nginx listens on port 80 inside the container.
- The custom home page is now part of the new image's filesystem.
- Publishing host port 8080 to container port 80 works.

You can also check the final file directly inside the container:

```bash
docker exec my-nginx \
  cat /usr/share/nginx/html/index.html
```

Stop the container when the experiment is over:

```bash
docker stop my-nginx
```

Because it was started with `--rm`, the container is removed automatically once it stops, while the built `my-nginx:v1` image is kept.

## 8. What I Understood Today

The inheritance and customization in this experiment can be summarized as:

```text
nginx:alpine
    ├── Nginx program and default configuration
    ├── Default static content
    ├── ENTRYPOINT
    ├── CMD
    └── ExposedPorts
            │
            │ FROM
            ▼
my-nginx:v1
    ├── Inherits files and configuration from the base image
    └── COPYs a custom index.html into a new layer
```

This gave me a more concrete understanding of what "building on top of an image" means: it is not only about reusing programs that are already installed, but also about inheriting the default runtime configuration the image provides. As long as those defaults fit the need, the child Dockerfile does not have to declare them again.

At the same time, "replacing a file in the base image" usually does not mean modifying the original read-only layer. It means writing new content at the same path in a new layer, so that the new file is the one visible in the final view.

Compared with my first build of a shell program on Alpine, this one is closer to how images are used in practice: choose an application image whose general-purpose setup is already done, layer only my own content on top of it, and end up with a custom image that can be run again and again.
