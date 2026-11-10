# Docker Images in Practice (5): Version Replacement, Rollback, and Registries

**English** | [中文](zh/13-docker-image-version-rollback-registry.md) | [日本語](ja/13-docker-image-version-rollback-registry.md)

After building the Spring Boot image, I went on to experiment with two questions closer to the delivery process: how application versions are updated and rolled back, and how a finished image is passed between environments.

The most important realization this time: a containerized application is usually not updated inside its existing container. Instead, a new container is created from a new image, and a registry stores and distributes those different image versions.

## 1. A Version Update Does Not Modify the Existing Container

This time I built two Spring Boot images in turn:

```text
spring-lab:v1
spring-lab:v2
```

If the production container is currently running `v1`, the basic way to release `v2` is not to enter the old container and overwrite the JAR, but to stop and remove the old container and then create a new one from the new image:

```bash
docker stop spring-prod
docker rm spring-prod

docker run -d \
  --name spring-prod \
  -p 127.0.0.1:8081:8080 \
  spring-lab:v2
```

What happens here is a replacement of the running instance:

```text
Old container: spring-lab:v1  ──→  stopped and removed

New container: spring-lab:v2  ──→  created and started
```

The original `spring-lab:v1` image can still be kept. As long as the required runtime configuration remains compatible, the same procedure can stop `v2` and create a container from `v1` again, rolling the application back.

After the experiment on my machine, `spring-prod` is currently running:

```text
spring-lab:v2
```

## 2. Why Stateless Services Are Easy to Replace

If an application instance does not write business state that must be kept long term into its own container's writable layer, the container itself can be replaced:

```text
Container
    ├── Application: from the image
    ├── Runtime configuration: injected at startup
    └── Persistent state: external database, volume, or other service
```

After an application container is removed, a new container can read the external state again and continue serving. For such a stateless application, moving between versions mostly comes down to "which image to create the new instance from."

But "just swap the image" still depends on a few conditions:

- Configuration, passwords, and environment differences can be supplied again at run time.
- Data is not stored only in the old container's writable layer.
- The external data structures used by the old and new versions are compatible with each other.
- The old version's image can still be obtained.

In particular, rolling back an image rolls back only the application; it does not automatically roll back the database. If the new version ran an incompatible database migration, the old program may not work correctly even if the old image is run again.

### A Simple Replacement Is Not a Zero-Downtime Release

On a single host with only one container, where the old and new containers both need the same host port, the old container must be stopped before the new one can start, which can cause a brief outage.

Keeping the service available throughout a release usually requires:

- Running multiple application replicas at the same time.
- Having old and new versions use different internal instances or ports.
- Health-checking the new instances.
- Shifting traffic gradually through a load balancer or orchestration system.

So containers make version replacement simple, but rolling releases, automatic rollback, and zero downtime still need a higher-level deployment mechanism.

## 3. Tags and Rollback-Ready Versions

`v1` and `v2` make the version relationship easy to see:

```text
spring-lab:v1
spring-lab:v2
```

But a tag is essentially a name that can be re-pointed at different image content. If the image is rebuilt and the same `v2` tag is reused, the `v2` seen later is not necessarily what was released the first time.

To make rollback more reliable, you can:

- Use a non-repeating version tag for each release, such as a version number or commit ID.
- Record the image digest actually used at deployment time.
- Avoid relying only on `latest`, which is vague and can change.

Tags are convenient for humans to read; digests are better for pinning down the exact image content. Either way, the prerequisite is that old versions are still kept locally or in a registry and have not been deleted by a cleanup policy.

## 4. What Problem a Registry Solves

An image that exists only in the Docker Engine on a development machine cannot be used directly by other servers. A registry is an image storage and distribution service: Docker clients can push images to it and pull images from it.

The relationship among the three can be shown simply as:

```text
Development or CI environment
    │ docker push
    ▼
Registry
    │ docker pull
    ▼
Test or production environment
```

A registry does not compile applications, and it does not replace running containers automatically just because it received a new image. Its job is to store, name, and distribute image content; building and deployment are still done by the Docker client, CI/CD, or an orchestration platform.

## 5. Running a Registry Locally

This experiment used the official `registry:3` image directly:

```bash
docker run -d \
  --name registry-lab \
  -p 127.0.0.1:5000:5000 \
  registry:3
```

The registry inside the container listens on 5000, and OrbStack publishes it to `127.0.0.1:5000` on macOS.

The repositories from this experiment can be listed through the registry API:

```bash
curl http://127.0.0.1:5000/v2/_catalog
```

On my machine it currently returns:

```json
{"repositories":["spring-lab"]}
```

Then list the tags in `spring-lab`:

```bash
curl http://127.0.0.1:5000/v2/spring-lab/tags/list
```

The current result is:

```json
{"name":"spring-lab","tags":["v1"]}
```

These HTTP APIs are useful for observing the results of an experiment; day-to-day uploading and downloading of images still uses `docker push` and `docker pull`.

## 6. Why the Image Gets a Registry Prefix First

A full image reference can be simplified to:

```text
[registry address/][namespace/]repository[:tag]
```

The local image was originally named:

```text
spring-lab:v1
```

To push it to the local registry, the same image needs an additional tag that includes the service address:

```bash
docker image tag \
  spring-lab:v1 \
  localhost:5000/spring-lab:v1
```

`localhost:5000` tells Docker where the target registry is, and the `spring-lab:v1` that follows identifies the repository and tag within that registry.

`docker image tag` only adds another reference name locally; it neither uploads the image nor copies its entire filesystem:

```text
spring-lab:v1
          └──────────────┐
                         ▼
                   The same image ID
                         ▲
localhost:5000/spring-lab:v1
          └──────────────┘
```

## 7. Push, Remove, and Pull

After adding the registry tag, the command to use is `push`:

```bash
docker push localhost:5000/spring-lab:v1
```

Not `pull`. The two commands move in opposite directions:

```text
docker push  local Docker Engine ──→ Registry
docker pull  Registry ──→ local Docker Engine
```

On push, the registry stores the image manifest, config, and filesystem layers. Layers with identical content that the registry already has do not need to be stored again each time, so the output may show that some layers already exist.

### Removing the Local Image and Pulling It Back

After confirming that no container depends on these images, the local references can be removed:

```bash
docker image rm localhost:5000/spring-lab:v1
docker image rm spring-lab:v1
```

Both tags need to be removed because they may both point to the same local image. If only the registry tag were removed, the short name `spring-lab:v1` would remain and the underlying image data might also still be there, so the subsequent pull would not fully prove that the registry can restore the image.

Pull from the registry:

```bash
docker pull localhost:5000/spring-lab:v1
```

What comes back is the reference with the full registry address:

```text
localhost:5000/spring-lab:v1
```

To keep using the original short name, add the tag again:

```bash
docker image tag \
  localhost:5000/spring-lab:v1 \
  spring-lab:v1
```

Removing a local image does not remove content already pushed to the registry; conversely, removing the registry container or its storage may cause the remote content to be lost.

## 8. Registry Data Needs to Be Persisted Too

The registry stores its data in `/var/lib/registry` inside the container. The official image used here declares a Docker volume for that path, so separate storage exists outside the container's filesystem.

To make the storage's name and lifecycle more explicit, a named volume can be specified when creating the registry:

```bash
docker volume create registry-data

docker run -d \
  --name registry-lab \
  -p 127.0.0.1:5000:5000 \
  --mount type=volume,source=registry-data,target=/var/lib/registry \
  registry:3
```

That way, when the registry container is replaced, the same `registry-data` can be mounted again. A volume, however, only keeps the container's data independent in a single-host experiment; it does not provide backup, high availability, or disaster recovery.

## 9. From a Local Experiment to Multiple Environments

`localhost:5000` is used locally, and only the current host understands that address as its own registry. For development, test, and production environments to share images, the registry needs a network address that all of them can reach, for example:

```text
registry.example.com/team/spring-lab:v1
```

When every environment uses the same full image reference, testing and deployment can revolve around the same build artifact:

```text
Build once
    ↓
Push to the registry
    ↓
Test environment pulls and verifies
    ↓
Production pulls the same version or digest
```

What moves here is not the container running on the development machine, but immutable image content. Each environment still has to provide its own environment variables, secrets, networking, and persistent storage.

An unauthenticated local HTTP registry is only suitable for experiments. Before actually opening one to other machines, you need to consider at least:

- TLS encryption and trusted certificates.
- Authentication and fine-grained access control.
- Image retention and garbage collection policies.
- Persistent storage, backup, and availability.
- Security scanning, auditing, and network access control.

Organizations can run their own Distribution registry or use a mature managed service such as AWS ECR. A managed service reduces the maintenance work for registry servers, storage, and availability, and can integrate with the cloud platform's identity and access management, auditing, and scanning. The Docker client still interacts with it through the same basics: authentication, tagging, pushing, and pulling.

## 10. What I Understood Today

This experiment turned the image from "a build artifact on my machine" into "a version that can be delivered across environments":

```text
Source code
  │ build
  ▼
spring-lab:v1
  │ tag + push
  ▼
Registry
  │ pull
  ├──────────→ Test environment
  └──────────→ Production environment
                  │
                  ├── Create a new container from v2: upgrade
                  └── Create a new container from v1: rollback
```

For a stateless application, the container itself can be replaced by a new version at any time, and the state that really needs to be kept long term lives outside the container. The registry ensures that deployment environments can obtain the specified image without recompiling the project on every server.

Complete version management, therefore, is not just about keeping a few local tags. It means managing all of the following together:

- An explicit image reference or digest for each version.
- Historical images that can still be obtained from the registry.
- The runtime configuration that matches each version.
- Data structure changes that cannot be undone by rolling back the image.
- The release process, from starting the new container to switching traffic over.

Replacing images makes upgrading and rolling back applications simple, but reliable releases still depend on versions that cannot be confused, externalized state, compatible data, and the stability of the registry and the deployment process themselves.
