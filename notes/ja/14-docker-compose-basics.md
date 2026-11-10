# Docker Compose 入門：`docker run` から複数サービスの構成へ

[English](../14-docker-compose-basics.md) | [中文](../zh/14-docker-compose-basics.md) | **日本語**

今日から Docker Compose を学び始めた。今のところ終えたのは最も基本的な設定の実験だけである。このノートではまず、`compose.yaml`、サービス、ポートと環境変数、そしてサービスの起動の依存関係とヘルスチェックについての理解を記録しておく。

Compose の最も直接的な価値は、これまで複数の `docker run` コマンドに散らばっていたコンテナの設定を、繰り返し実行できる 1 つの YAML ファイルにまとめて保存できることである。

## 1. `compose.yaml` は何を記述しているのか

コンテナを単独で実行するなら、次のように書くかもしれない。

```bash
docker run -d \
  --name spring-app \
  -p 127.0.0.1:8081:8080 \
  -e SPRING_PROFILES_ACTIVE=dev \
  spring-lab:v2
```

Compose を使えば、同じ情報を次のように書ける。

```yaml
services:
  app:
    image: spring-lab:v2
    ports:
      - "127.0.0.1:8081:8080"
    environment:
      SPRING_PROFILES_ACTIVE: dev
```

対応関係は次のとおりである。

| `docker run` | Compose のサービス設定 |
|---|---|
| `spring-lab:v2` | `image: spring-lab:v2` |
| `--name` | 通常は Compose がプロジェクト名とサービス名から管理する |
| `-p`、`--publish` | `ports` |
| `-e`、`--env` | `environment` |

Compose ファイルが記述しているのは、一群のサービスをどのような設定で動かしたいかである。次を実行すると、

```bash
docker compose up -d
```

Compose はカレントディレクトリの `compose.yaml` を読み込み、必要なネットワークとコンテナを作成して、サービスをバックグラウンドで起動する。

## 2. サービスとコンテナの関係

`services` の下にある名前は、それぞれ 1 つのサービスの定義を表している。

```yaml
services:
  app:
    image: spring-lab:v2

  cache:
    image: redis:7-alpine
```

ここでの `app` と `cache` はサービス名であり、イメージ名ではないし、手で指定しなければならないコンテナ名でもない。

今のところ、次のように理解している。

```text
Service
    ├── どのイメージを使うか、またはどうビルドするか
    ├── コンテナの実行環境をどう設定するか
    └── ほかのサービスとどのような関係にあるか
              │
              ▼
       Compose がコンテナを作成する
```

デフォルトでは、1 つのサービスから 1 つのコンテナが作成される。Compose はレプリカの数を調整することもできるが、サービスは設定と管理の単位であり、コンテナは実際に動いているインスタンスなので、両者は完全には同じものではない。

## 3. Compose は事前のイメージのビルドを必須としない

次のように書いた場合、

```yaml
services:
  app:
    image: spring-lab:v2
```

Compose はローカルにある既存のイメージを使う。ローカルにない場合は、通常 pull のポリシーに従ってレジストリから取得しようとする。

一方で、Compose ではビルドの方法を直接宣言することもできる。

```yaml
services:
  app:
    build:
      context: ../04-java/springboot
      dockerfile: Dockerfile
    image: spring-lab:dev
```

ここでの `build` はソースからビルドするよう Compose に指示し、`image` はそのビルド結果にイメージ名とタグを設定する。

### `context` と `dockerfile`

`context` は、`docker build` の最後に指定するビルドコンテキストに対応する。

```text
compose.yaml があるディレクトリ
    │
    └── ../04-java/springboot
            ├── Dockerfile
            ├── pom.xml
            └── src/
```

相対的なコンテキストのパスは、Compose のプロジェクトディレクトリから解決される。今回の `../04-java/springboot` は Spring Boot のプロジェクトディレクトリを表しており、Dockerfile の `COPY` も、このコンテキストでアクセスできるファイルしか使えない。

`dockerfile` はビルドに使うファイルを指定するもので、相対パスはビルドコンテキストを基準に解決される。ファイル名がコンテキストのルートにある標準の `Dockerfile` であれば、この項目は省略できる。`context` を明示的に設定しない場合も、デフォルトでプロジェクトディレクトリ `.` が使われる。

したがって、この 2 つの項目は常に両方とも書かなければならないわけではない。完全な書き方をする利点は、ビルドの入力と Dockerfile の場所がより明確になることである。

### `image` でビルド結果に名前を付ける

`build` と `image` は、同じサービスの下にある 2 つの設定である。

```yaml
build:
  context: ../04-java/springboot
  dockerfile: Dockerfile
image: spring-lab:dev
```

Compose がこのビルドを実行すると、生成されたイメージには次の名前が付く。

```text
spring-lab:dev
```

`image` を書かなくても Compose はビルドでき、通常はプロジェクト名とサービス名を組み合わせた名前を使う。しかし、それは自分で明確に決めたリリース用の名前ではなく、Compose で push するときにも、どのレジストリのリポジトリに push すべきかを決められない。そのため、ビルド結果を配布する必要がある場合は、`image` を明示的に設定すべきである。

単独でビルドすることもできる。

```bash
docker compose build app
```

起動前にビルドを実行するよう Compose に指示することもできる。

```bash
docker compose up --build -d
```

`build` と `image` の両方がある場合、先に pull を試みるかどうかは、`pull_policy` や使うコマンドにも左右される。今回の実験では `--build` で起動前のビルドを明示的に指示しており、「`build` を書けば毎回必ず自動で再ビルドされる」ことを決まったルールとして扱わないようにしている。

したがって、「Compose はビルド済みのイメージを使わなければならない」というのは正確ではない。より完全には、次のように言える。

> どのサービスも最終的にはイメージを必要とするが、そのイメージはあらかじめ存在していても、レジストリから pull しても、`build` の設定に基づいて Compose が生成してもよい。

## 4. `ports`：`docker run -p` に対応する

```yaml
services:
  app:
    image: spring-lab:v2
    ports:
      - "127.0.0.1:8081:8080"
```

3 つの部分の意味は `docker run -p` と同じである。

```text
ホストのアドレス : ホストのポート : コンテナのポート
127.0.0.1        :      8081      :       8080
```

`ports` はリストなので、1 つのサービスで複数のポートマッピングを宣言できる。短い構文を引用符付きの文字列として書くと、コロンや特殊な形式を含む値が YAML によって別の型として解釈されるのを防げる。

ポートの公開は、主に Compose のネットワークの外からサービスにアクセスするために使う。同じ Compose プロジェクト内のサービス同士が通信するときは、通常は相手のサービス名とコンテナのポートを直接使い、ホストに公開したポートを経由する必要はない。

## 5. `environment`：`docker run -e` に対応する

マップ形式：

```yaml
services:
  app:
    image: spring-lab:v2
    environment:
      SPRING_PROFILES_ACTIVE: dev
      LOG_LEVEL: INFO
```

リスト形式も使える。

```yaml
environment:
  - SPRING_PROFILES_ACTIVE=dev
  - LOG_LEVEL=INFO
```

これらの変数は Compose が作成するコンテナに入り、イメージにある同名のデフォルトの `ENV` を上書きする。イメージをさかのぼって変更することはない。

パスワードやトークンなどの機密情報を Compose ファイルに直接書いてコミットするのは適切ではない。今後、`.env`、`env_file`、Secret などの設定方法と、それぞれが解決する問題についても学ぶ必要がある。

## 6. `volumes`：ストレージを宣言してサービスにマウントする

今回は PostgreSQL のサービスに名前付きボリュームを追加した。

```yaml
services:
  db:
    image: postgres:17-alpine
    volumes:
      - pgdata:/var/lib/postgresql/data

volumes:
  pgdata:
```

ここには `volumes` が 2 か所に現れており、それぞれ役割が異なる。

### トップレベルの `volumes` は名前付きボリュームを宣言する

```yaml
volumes:
  pgdata:
```

これは、現在のプロジェクトに `pgdata` という論理名の名前付きボリュームがあることを Compose に伝えている。設定が空であれば、Docker Engine のデフォルトのボリュームドライバーが使われる。

ファイルの中で宣言しただけでは、実際のリソースはすぐには作成されない。`docker compose up` を実行したときに、Compose が必要に応じて作成し、すでに存在していればそのまま再利用する。

Compose はデフォルトで、実際のリソース名にプロジェクト名を付け加える。今回は次のコマンドで、

```bash
docker volume ls
```

次の名前を確認できた。

```text
05-compose_pgdata
```

`pgdata` は Compose ファイル内の論理名で、`05-compose_pgdata` は Docker Engine で実際に見えるボリューム名である。必要なら、次のコマンドも実行できる。

```bash
docker volume inspect 05-compose_pgdata
```

### サービス内の `volumes` はマウントの関係を指定する

```yaml
services:
  db:
    volumes:
      - pgdata:/var/lib/postgresql/data
```

短い構文は、コロンで次のように区切られる。

```text
ボリュームの論理名 : コンテナ内のマウント先のパス
pgdata             : /var/lib/postgresql/data
```

トップレベルの宣言は使える名前付きボリュームを定義するだけで、どのサービスにも自動的に使用を許可するわけではない。サービス内の設定によって初めて、`pgdata` が PostgreSQL が実際にデータベースのファイルを保存する場所にマウントされる。

### なぜコンテナを削除してもデータが残るのか

データベースのファイルは `db` コンテナの書き込み可能層だけに書き込まれるのではなく、独立したボリュームに書き込まれる。

```text
05-compose-db-1
    │ /var/lib/postgresql/data
    ▼
05-compose_pgdata
```

`docker compose down` を実行すると、コンテナとデフォルトのネットワークは削除されるが、名前付きボリュームはデフォルトで残る。今回の実験でも、Compose のコンテナはすでに停止・削除されていたが、`05-compose_pgdata` は `docker volume ls` で引き続き確認できた。

次に `docker compose up` を実行すると `db` コンテナが作り直され、元のボリュームがマウントされるので、データベースは以前に書き込んだデータを引き続き読み込める。

これがデータの永続化の基本的な意味である。データのライフサイクルが、特定の 1 つのコンテナに完全には縛られなくなる。

ただし、ボリュームによる永続化は、データベースのバックアップ、ホストをまたいだ複製、バージョンアップが済んだことを意味するわけではない。次のコマンドを実行すると、Compose が管理するボリュームまで積極的に削除される。

```bash
docker compose down -v
```

そこにはデータベースのデータが含まれている可能性があるので、`-v` を通常の片付け用のオプションとして気軽に使ってはいけない。

## 7. Compose はデフォルトでサービスのネットワークを作る

複数のサービスがある場合、Compose はデフォルトで現在のプロジェクト用のネットワークを作成し、ほかのネットワークを指定していないサービスをそこに接続する。

同じネットワーク内のコンテナは、サービス名で互いの名前を解決できる。

```text
app  ──→  cache:6379
```

ここでは、作成のたびに動的に割り当てられるコンテナの IP を覚えるのではなく、`cache` という安定したサービス名を使うべきである。

これこそ、Compose が単に「複数の run コマンドを YAML に書いたもの」にとどまらない点である。Compose は、コンテナ間のネットワークとライフサイクルをプロジェクト単位で管理している。

### Compose が作るのも Docker のリソースである

Compose はサービスとプロジェクトという管理の抽象化を提供しているが、最終的なリソースは同じ Docker Engine が作成・管理している。そのため、次のコマンドで、

```bash
docker ps
```

サービスに対応する実際のコンテナを確認できる。

```text
05-compose-app-1
05-compose-db-1
```

`docker compose ps` は現在の Compose プロジェクトだけを表示し、`docker ps` は現在の Docker Engine で実行中のすべてのコンテナを表示する。

デフォルトのネットワークも、通常の Docker コマンドで確認できる。

```text
docker network ls
    └── 05-compose_default
```

ここでの `05-compose` はプロジェクト名、`default` は Compose が自動的に作成したデフォルトのブリッジネットワークである。接続の詳細を確認したいときは、`docker network inspect 05-compose_default` を使える。

```text
Compose のサービス定義      ──→  Docker のコンテナ
Compose のデフォルトネットワーク ──→  Docker のブリッジネットワーク
```

1 つのサービスは複数のレプリカを持てるので、サービスとコンテナはやはり完全に同じものではない。実際の各レプリカこそが、Docker Engine から見えるコンテナである。

## 8. サービス名だけを書いた `depends_on` は何を保証するのか

短い書き方：

```yaml
services:
  app:
    image: spring-lab:v2
    depends_on:
      - cache

  cache:
    image: redis:7-alpine
```

Compose はまず `cache` を作成・起動してから `app` を起動し、終了するときは依存関係の逆の順序で処理する。

しかし、「コンテナが起動した」ことは「中のアプリケーションがリクエストを受け付ける準備ができた」ことと同じではない。たとえば、データベースのプロセスはすでに動いていても、まだ初期化の途中かもしれない。短い書き方の `depends_on` は、それが本当に使える状態になるまで待つことはしない。

したがって、`depends_on` が解決するのは依存関係の順序であり、アプリケーションが実際に準備できた状態かどうかを自動的に理解してくれるわけではない。

## 9. `healthcheck`：何をもって健全とするかを定義する

正しい設定名は `healthcheck` であり、`health_check` ではない。

```yaml
services:
  cache:
    image: redis:7-alpine
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 5
      start_period: 10s
```

これは、Docker が `cache` コンテナの中で次のコマンドを定期的に実行することを表している。

```text
redis-cli ping
```

コマンドが成功すると、コンテナは `healthy` の状態になれる。各フィールドの役割は次のとおり。

- `test`：実際に実行するチェックのコマンド。
- `interval`：チェックとチェックの間隔。
- `timeout`：1 回のチェックに許される最大時間。
- `retries`：何回連続で失敗したら不健全と判定するか。
- `start_period`：アプリケーションの起動のために確保する時間。この間の失敗はすぐには失敗回数に数えられない。

チェックのコマンドは対象のコンテナの中で実行されるので、イメージには `redis-cli`、`curl`、あるいはチェックに必要なその他のプログラムが含まれていなければならない。ホストにあるコマンドがインストールされていても、コンテナ内で使えるとは限らない。

ヘルスチェックは健全性の状態を生み出すだけで、コンテナが `unhealthy` になったからといってアプリケーションを自動的に修復するわけではない。再起動、置き換え、アラートをどうするかは、その後の実行やオーケストレーションの仕組み次第である。

## 10. 依存関係に健全な状態を待たせる

`app` が `cache` の本当に健全な状態を待つようにするには、`depends_on` の長い書き方を使う必要がある。

```yaml
services:
  app:
    image: spring-lab:v2
    ports:
      - "127.0.0.1:8081:8080"
    environment:
      SPRING_PROFILES_ACTIVE: compose
    depends_on:
      cache:
        condition: service_healthy

  cache:
    image: redis:7-alpine
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 5s
      timeout: 3s
      retries: 5
      start_period: 10s
```

起動の順序は次のようになる。

```text
cache を作成する
    ↓
cache を起動する
    ↓
healthcheck が成功する
    ↓
app を作成して起動する
```

この例は、Compose の依存関係とヘルスチェックの仕組みを説明するためのものにすぎない。現在の `spring-lab:v2` が実際に Redis を使っているかどうかは、Spring Boot プロジェクト自体の依存関係と設定によって決まる。Compose で `depends_on` を宣言しただけで、アプリケーションが自動的に Redis の機能を得るわけではない。

## 11. 最も基本的な Compose の操作

`compose.yaml` があるディレクトリで次を実行する。

```bash
docker compose config
```

まず最終的な設定を解析して出力する。起動前に YAML や設定のマージの問題を見つけるのに向いている。

サービスを作成して起動する。

```bash
docker compose up -d
```

状態とログを確認する。

```bash
docker compose ps
docker compose logs -f
```

現在のプロジェクトが作成したコンテナとデフォルトのネットワークを停止・削除する。

```bash
docker compose down
```

`down` はデフォルトでは、イメージも名前付きボリュームも削除しない。`docker compose down -v` は Compose が管理するボリュームまで削除し、そこにはデータベースなどの永続データが含まれている可能性があるため、日常的なリスクのない片付けコマンドとして扱ってはいけない。

## 12. 現時点での理解

今のところ、Compose を次のように整理して理解している。

```text
サービスのビルドと実行の設定
    ├── build / image
    ├── ports / environment
    └── volumes のマウント

複数サービスの関係
    ├── デフォルトのネットワークと名前解決
    ├── depends_on
    └── healthcheck

独立したリソース
    ├── Image
    ├── Network
    └── Named Volume
```

`compose.yaml` はシェルのコマンドを一語一語 YAML に翻訳したものではなく、一群のサービスに望む実行状態を宣言するものである。Compose はその設定に基づいてイメージ、ネットワーク、コンテナを作成し、それらの起動と停止をまとめて管理する。

ただし、Compose が現在主に解決しているのは、1 つの Docker 環境における複数コンテナのアプリケーションの定義とライフサイクルの管理である。ある程度のオーケストレーションの機能は備えているが、複数のサーバーにまたがってスケジューリング、自己修復、ローリングリリースを行うクラスターのオーケストレーションシステムと同一視することはできない。

これはまだ Compose の学習の入り口にすぎない。今後は `.env`、設定の上書き、再作成の振る舞い、そして完全なアプリケーションスタックの実際の構成方法についても理解を深めていく必要がある。
