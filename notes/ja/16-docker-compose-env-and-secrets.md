# Docker Compose の設定実践：`.env` から Secrets まで

[English](../16-docker-compose-env-and-secrets.md) | [中文](../zh/16-docker-compose-env-and-secrets.md) | **日本語**

前回の Compose のノートはすでに長くなっていたので、環境設定と機密情報は別のノートに分けて記録することにした。今回の実験で解決したい問題は具体的である。データベース名とユーザー名は通常の設定としてコンテナに渡してよいが、パスワードを Compose の設定やコンテナの環境変数にそのまま展開すべきではない。

## 1. `.env` が解決するのは変数の置換

Compose のプロジェクトディレクトリに `.env` を用意する。

```dotenv
DB_NAME=labdb
DB_USERNAME=labuser
```

`compose.yaml` では、`${変数}` でこれらの値を参照できる。

```yaml
services:
  db:
    image: postgres:17-alpine
    environment:
      POSTGRES_USER: ${DB_USERNAME}
      POSTGRES_DB: ${DB_NAME}
```

Compose はコマンドの実行時にデフォルトの `.env` を読み込み、まず変数の置換を済ませてからコンテナを作成する。置換後の結果は次のコマンドで確認できる。

```bash
docker compose config
docker compose config --environment
```

パスワードも `${DB_PASSWORD}` と書いた場合、`docker compose config` の結果にパスワードがそのまま現れる。それを `environment` に渡せば、パスワードはコンテナの環境変数にもなる。つまり `.env` は環境ごとの差異の管理には向いているが、機密性の高い値の受け渡し方は何も変えていない。

置換用の変数をほかの場所から読み込む必要がある場合は、次のようにする。

```bash
docker compose --env-file ./config/.env.dev up -d
```

一方、サービスの下に書く `env_file` は別の用途を持つ。指定したファイルの変数をまとめてコンテナに渡すものであり、`compose.yaml` 全体の `${変数}` に置換用の値を提供するためのものではない。

## 2. `.env.example` は構造だけを示す

実際の `.env` は `.gitignore` に追加し、リポジトリには本物の機密性の高い値を含まないテンプレートだけを残す。

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

新しい環境では、テンプレートをコピーして自分の設定を記入すればよい。

```bash
cp .env.example .env
```

これでローカルの設定を誤ってコミットするのを防げるが、`.gitignore` はファイルを暗号化するわけではない。一度コミットしてしまったパスワードは、後から削除しても安全な状態には戻らないので、速やかに変更する必要がある。

## 3. Compose Secrets の基本モデル

Compose の Secret の使い方は 2 段階に分かれる。

1. トップレベルの `secrets` で、Secret の名前と内容の出どころを宣言する。
2. 具体的なサービスで、アクセス権を明示的に与える。

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

起動すると、Compose は元のファイルを読み取り専用のファイルとして、権限を与えられたコンテナにマウントする。

```text
ホストの ./secrets/db_password.txt
              │
              ▼
コンテナの /run/secrets/db_password
```

トップレベルで宣言しても、すべてのサービスが自動的に Secret を得るわけではない。`secrets` を書いたサービスだけが、対応するマウントを得る。

また、Secret が自動的に環境変数になることもない。コンテナ内のプログラムがそのファイルを読み込むか、プログラム自身が「ファイルから設定を読み込む」機能を備えている必要がある。

## 4. PostgreSQL：`_FILE` でパスワードを読み込む

PostgreSQL の公式イメージの起動スクリプトは `POSTGRES_PASSWORD_FILE` に対応している。そのため、Secret のファイルがどこにあるかを伝えるだけでよく、読み込み用のスクリプトを自分で書く必要はない。

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

ここでの環境変数が保持しているのはファイルのパスだけであり、本当のパスワードはマウントされたファイルの中にある。

```text
POSTGRES_PASSWORD_FILE=/run/secrets/db_password
                                      │
                                      ▼
                         PostgreSQL の起動スクリプトが読み込む
```

`_FILE` は一部のイメージが採用している取り決めであり、Docker がすべての環境変数に自動で提供している機能ではない。ほかのイメージを使うときは、まずこの書き方に対応しているかを確認する必要がある。

さらに注意すべき点として、`POSTGRES_PASSWORD_FILE` が関わるのは主に空のデータディレクトリの初回の初期化である。名前付きボリュームにすでにデータベースが存在する場合、Secret のファイルを変更するだけでは、既存のデータベースユーザーのパスワードは自動的には変わらない。

## 5. Spring Boot：Config Tree で Secret を読み込む

自分の Spring Boot アプリケーションには `SPRING_DATASOURCE_PASSWORD_FILE` がないが、Spring Boot はディレクトリを Config Tree としてインポートする機能に対応している。

Config Tree は、ディレクトリ内のファイル名を設定項目の名前として、ファイルの内容を設定値として扱う。そのため、Secret の `target` を使って、コンテナ内でのファイル名を変えればよい。

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

マウントの結果は次のとおりである。

```text
/run/secrets/spring.datasource.password
              │                    │
              │                    └── ファイルの内容が設定値になる
              └── ファイル名が spring.datasource.password になる
```

Spring Boot は `/run/secrets/` をインポートすると、次の環境変数と同等の設定値を得られる。しかも、パスワードそのものを環境変数に入れる必要はない。

```text
SPRING_DATASOURCE_PASSWORD=<パスワード>
```

同じ `db_password` の Secret を、サービスごとに異なるファイル名で使うことができる。PostgreSQL はデフォルトの `db_password` のままにし、Spring Boot は `target` によって、Config Tree が認識できる設定キーに名前を変えている。

## 6. 組み合わせた Compose の設定

今回の実験のうち、設定に関わる部分は次のようにまとめられる。

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

対応関係は次のとおりである。

```text
.env
  ├── DB_NAME      ──→ Compose が置換 ──→ コンテナの環境変数
  └── DB_USERNAME  ──→ Compose が置換 ──→ コンテナの環境変数

db_password.txt
  └── Compose Secret
          ├── db  /run/secrets/db_password
          └── app /run/secrets/spring.datasource.password
```

## 7. Secrets のセキュリティ上の境界

Secrets を使うと、`docker compose config` を実行しても見えるのは Secret のファイルの出どころとマウントの関係だけで、ファイルの内容が設定に展開されることはない。パスワードがコンテナの環境変数の一覧に現れることもない。`${DB_PASSWORD}` を直接使う場合と比べて、設定の出力、環境変数の確認、デバッグログなどで意図せず露出しにくくなる。

ただし、通常のローカルの Compose では、`file` が指す元のファイルは依然としてホスト上に平文で存在している。そのファイルを読める人、Docker Engine を操作できる人、権限を与えられたコンテナに入れる人は、パスワードを読み取れる可能性がある。そのため、さらに次のことが必要である。

- 本物の Secret のファイルを `.gitignore` に追加し、`.example` のテンプレートだけをコミットする。
- ホスト上のファイルの権限と、Docker の管理権限を制限する。
- Secret を Dockerfile、イメージのレイヤー、アプリケーションのログに書き込まない。

Secrets が改善するのは機密情報をコンテナに渡す方法であり、鍵の保管、暗号化、ローテーションまで備えた完全な仕組みと同じではない。

## 8. 現時点での理解

```text
通常の環境設定
    └── .env ──→ ${変数} の置換 ──→ Compose の設定またはコンテナの環境変数

機密性の高い設定
    └── Secret の元ファイル ──→ /run/secrets/... ──→ アプリケーションが実行時に読み込む
```

今回の実験で、「設定」と「秘密」を区別できるようになった。どちらもコンテナに渡す必要があるが、まったく同じ方法で渡すべきではない。`.env` は環境に応じて設定を切り替えやすくし、Secrets はパスワードができるだけ、本当にそれを必要とするコンテナの中にだけ、ファイルとして現れるようにする。
