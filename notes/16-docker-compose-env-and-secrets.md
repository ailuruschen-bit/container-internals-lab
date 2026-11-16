# Docker Compose Configuration in Practice: From `.env` to Secrets

**English** | [中文](zh/16-docker-compose-env-and-secrets.md) | [日本語](ja/16-docker-compose-env-and-secrets.md)

The previous Compose note was already long, so I split environment configuration and sensitive information into a separate note. The problem this experiment set out to solve is concrete: the database name and username can be passed into containers as ordinary configuration, but the password should not be expanded directly into the Compose configuration or the container's environment variables.

## 1. What `.env` Solves Is Variable Substitution

Prepare a `.env` file in the Compose project directory:

```dotenv
DB_NAME=labdb
DB_USERNAME=labuser
```

`compose.yaml` can reference these values with `${VARIABLE}`:

```yaml
services:
  db:
    image: postgres:17-alpine
    environment:
      POSTGRES_USER: ${DB_USERNAME}
      POSTGRES_DB: ${DB_NAME}
```

When running a command, Compose reads the default `.env`, performs variable substitution first, and only then creates the containers. The substituted result can be inspected with:

```bash
docker compose config
docker compose config --environment
```

If the password were also written as `${DB_PASSWORD}`, the full password would appear in the output of `docker compose config`, and once passed into `environment`, it would also become a container environment variable. So `.env` is good for managing differences between environments, but it does not change how sensitive values are passed.

To read substitution variables from another location, use:

```bash
docker compose --env-file ./config/.env.dev up -d
```

`env_file` under a service serves a different purpose: it passes all the variables in the specified file into the container, rather than providing values for `${VARIABLE}` substitution across the whole `compose.yaml`.

## 2. `.env.example` Only Describes the Structure

The actual `.env` should be added to `.gitignore`, and the repository should keep only a template without real sensitive values:

```gitignore
.env
.env.*
!.env.example
```

```dotenv
# .env.example
DB_NAME=labdb
DB_USERNAME=labuser
```

A new environment can copy the template and fill in its own configuration:

```bash
cp .env.example .env
```

This prevents local configuration from being committed by mistake, but `.gitignore` does not encrypt anything. A password that has already been committed cannot be made safe again by deleting it later; it still has to be rotated promptly.

## 3. The Basic Model of Compose Secrets

Using a Compose secret takes two steps:

1. Declare the secret's name and the source of its content under the top-level `secrets`.
2. Explicitly grant access to it in each service that needs it.

```yaml
services:
  db:
    image: postgres:17-alpine
    secrets:
      - db_password

secrets:
  db_password:
    file: ./secrets/db_password.txt
```

At startup, Compose mounts the source file into the authorized containers as a read-only file:

```text
Host ./secrets/db_password.txt
              │
              ▼
Container /run/secrets/db_password
```

The top-level declaration does not automatically give every service the secret. Only services that list it under `secrets` get the corresponding mount.

Nor does a secret automatically become an environment variable. The program in the container must read the file, or provide its own way of "reading configuration from a file."

## 4. PostgreSQL: Reading the Password Through `_FILE`

The entrypoint script of the official PostgreSQL image supports `POSTGRES_PASSWORD_FILE`. So it only needs to be told where the secret file is, without writing a separate script to read it:

```yaml
services:
  db:
    image: postgres:17-alpine
    environment:
      POSTGRES_USER: ${DB_USERNAME}
      POSTGRES_DB: ${DB_NAME}
      POSTGRES_PASSWORD_FILE: /run/secrets/db_password
    secrets:
      - db_password

secrets:
  db_password:
    file: ./secrets/db_password.txt
```

The environment variable here holds only the file path; the actual password still lives in the mounted file:

```text
POSTGRES_PASSWORD_FILE=/run/secrets/db_password
                                      │
                                      ▼
                       Read by the PostgreSQL entrypoint script
```

`_FILE` is a convention adopted by some images, not something Docker provides automatically for every environment variable. When using another image, first check whether it supports this pattern.

Note also that `POSTGRES_PASSWORD_FILE` mainly takes part in the first initialization of an empty data directory. If a database already exists in the named volume, simply changing the secret file does not automatically change the password of the existing database user.

## 5. Spring Boot: Reading the Secret Through a Config Tree

My own Spring Boot application has no `SPRING_DATASOURCE_PASSWORD_FILE`, but Spring Boot supports importing a directory as a config tree.

A config tree treats each file name in the directory as a property name and the file's content as the property value. So the secret's `target` can be used to change the file name inside the container:

```yaml
services:
  app:
    image: spring-lab:dev
    environment:
      SPRING_DATASOURCE_URL: jdbc:postgresql://db:5432/${DB_NAME}
      SPRING_DATASOURCE_USERNAME: ${DB_USERNAME}
      SPRING_CONFIG_IMPORT: configtree:/run/secrets/
    secrets:
      - source: db_password
        target: spring.datasource.password
```

The resulting mount is:

```text
/run/secrets/spring.datasource.password
              │                    │
              │                    └── File content becomes the property value
              └── File name becomes spring.datasource.password
```

After Spring Boot imports `/run/secrets/`, it gets a property value equivalent to the environment variable below, without the password itself ever entering the environment:

```text
SPRING_DATASOURCE_PASSWORD=<password>
```

The same `db_password` secret can be used with different target file names in different services. PostgreSQL keeps the default `db_password`, while Spring Boot renames it through `target` into a property key the config tree recognizes.

## 6. The Combined Compose Configuration

The configuration-related parts of this experiment can be combined as:

```yaml
services:
  app:
    image: spring-lab:dev
    environment:
      SPRING_DATASOURCE_URL: jdbc:postgresql://db:5432/${DB_NAME}
      SPRING_DATASOURCE_USERNAME: ${DB_USERNAME}
      SPRING_CONFIG_IMPORT: configtree:/run/secrets/
    secrets:
      - source: db_password
        target: spring.datasource.password

  db:
    image: postgres:17-alpine
    environment:
      POSTGRES_USER: ${DB_USERNAME}
      POSTGRES_DB: ${DB_NAME}
      POSTGRES_PASSWORD_FILE: /run/secrets/db_password
    secrets:
      - db_password

secrets:
  db_password:
    file: ./secrets/db_password.txt
```

The mapping is:

```text
.env
  ├── DB_NAME      ──→ Compose substitution ──→ Container environment variable
  └── DB_USERNAME  ──→ Compose substitution ──→ Container environment variable

db_password.txt
  └── Compose Secret
          ├── db  /run/secrets/db_password
          └── app /run/secrets/spring.datasource.password
```

## 7. The Security Boundary of Secrets

With secrets in place, `docker compose config` shows only the secret's file source and mount relationships, without expanding the file content into the configuration, and the password does not appear in the container's list of environment variables. This makes it less likely to be exposed by accident in configuration output, environment inspection, or debug logs than using `${DB_PASSWORD}` directly.

In plain local Compose, however, the source file that `file` points to still exists in plaintext on the host. Anyone who can read that file, control the Docker Engine, or get into an authorized container may still be able to read the password. So it is still necessary to:

- Add the real secret files to `.gitignore` and commit only `.example` templates.
- Restrict file permissions on the host and administrative access to Docker.
- Keep secrets out of Dockerfiles, image layers, and application logs.

What secrets improve is how sensitive information gets into containers; they are not a complete system for secret storage, encryption, and rotation.

## 8. The Understanding So Far

```text
Ordinary environment configuration
    └── .env ──→ ${VARIABLE} substitution ──→ Compose config or container environment variables

Sensitive configuration
    └── Secret source file ──→ /run/secrets/... ──→ read by the application at run time
```

This experiment taught me to tell "configuration" and "secrets" apart: both need to be passed into containers, but they should not travel the same way. `.env` makes configuration easier to switch between environments, while secrets keep passwords appearing, as far as possible, only as files in the containers that actually need them.
