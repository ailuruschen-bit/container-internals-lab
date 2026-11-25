# Docker Compose の状態の観察、再起動、リソース制限

[English](../17-docker-compose-state-and-resource-limits.md) | [中文](../zh/17-docker-compose-state-and-resource-limits.md) | **日本語**

Compose の基本的な設定を理解した後、別の問題に目を向けるようになった。コンテナを作成した後、それらが正常に動いているかをどう観察するのか、そしてホストのリソースの使用をどう制限するのか、である。

## 1. プロジェクトからコンテナへと状態を観察する

現在の Docker Engine で実行中の Compose プロジェクトを確認する。

```bash
docker compose ls
```

今回の実験では、次のように表示された。

```text
NAME         STATUS       CONFIG FILES
05-compose   running(2)   .../05-compose/docker-compose.yaml
```

`running(2)` は、この Compose プロジェクトで現在 2 つのコンテナが実行中であることを表している。これはプロジェクト単位の概要であり、各コンテナ内のサービスが実際に使える状態かどうかまではわからない。

続いて、実際のコンテナを確認する。

```bash
docker ps
```

```text
05-compose-app-1   Up 8 days
05-compose-db-1    Up 8 days (healthy)
```

ここでは、Compose のサービスが最終的に作成したコンテナと、その稼働時間や健全性の状態を確認できる。

各コマンドの観察範囲は異なる。

```text
docker compose ls      現在の Engine にある Compose プロジェクト
docker compose ps      現在の Compose プロジェクトのコンテナ
docker ps              現在の Engine で実行中のすべてのコンテナ
docker ps -a           現在の Engine にある、停止したものも含むすべてのコンテナ
```

## 2. `restart`：コンテナが終了した後の基本的な復旧

Compose では、サービスに再起動ポリシーを設定できる。

```yaml
services:
  app:
    image: spring-lab:dev
    restart: unless-stopped
```

よく使われるポリシーは次のとおりである。

| 設定 | 意味 |
|---|---|
| `no` | デフォルト値。自動では再起動しない |
| `on-failure` | メインプロセスが 0 以外の状態で終了したときに再起動する |
| `always` | コンテナが停止した後、再起動を試み続ける |
| `unless-stopped` | 自動で再起動するが、明示的に停止または削除された後は起動し直さない |

現在最もよく使っているのは `unless-stopped` である。長期間動かし続ける必要がありつつ、手動で停止した意図は尊重したいサービスに向いている。

ただし、`restart` が見ているのは**コンテナがすでに停止したかどうか**である。コンテナのメインプロセスがまだ動いている間は、内部のサービスが正常に応答できなくなっていても、それだけで再起動ポリシーが発動することはない。

## 3. `healthcheck`：内部のサービスが使えるかを確認する

`healthcheck` を使うと、コンテナの中でチェックのコマンドを定期的に実行できる。

```yaml
services:
  db:
    image: postgres:17-alpine
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U $$POSTGRES_USER -d $$POSTGRES_DB"]
      interval: 5s
      timeout: 5s
      retries: 10
```

Docker はチェックの結果に基づいて、コンテナの健全性の状態を管理する。

```text
starting ──→ healthy
     │
     └────→ unhealthy
```

チェックのコマンドは対象のコンテナの中で実行されるので、イメージには `pg_isready`、`curl`、あるいは呼び出すその他のプログラムが含まれていなければならない。

依存する側では、次のように使う。

```yaml
services:
  app:
    depends_on:
      db:
        condition: service_healthy
```

Compose は初回の起動時に、`db` がヘルスチェックに合格するのを待ってから `app` を起動する。

次の違いは特に区別しておく必要がある。

```text
restart                         コンテナが終了した後に再起動するかどうか
healthcheck                     実行中のコンテナが健全かどうか
depends_on + service_healthy    依存する側が初回の起動前に健全になるのを待つかどうか
```

実行中にコンテナが `unhealthy` になっても、Docker が健全性の状態だけを理由に自動で再起動することはない。`depends_on` も、そのタイミングで起動のオーケストレーションをやり直すことはない。このような実行中の障害からどう復旧するかは、後の本格的なプロジェクトの実験で、具体的な場面と合わせて検討する。

## 4. Cgroup で CPU とメモリを制限する

コンテナにはデフォルトで CPU とメモリの上限がなく、ホスト上のほかのプロセスとリソースを大きく奪い合う可能性がある。Compose はリソースの制約を Docker に渡すことができ、最終的には Linux の Cgroup が制限を行う。

```yaml
services:
  app:
    cpus: 1.0
    mem_limit: 512m

  db:
    cpus: 0.5
    mem_limit: 256m
```

`cpus` は、使用を許可する CPU の計算量の割り当てを表す。

```text
cpus: 0.5   最大でおよそ論理 CPU 0.5 個分の計算時間
cpus: 1.0   最大でおよそ論理 CPU 1 個分の計算時間
```

これが制限するのは一定期間内に得られる CPU 時間の合計であり、コンテナを特定の CPU コアに固定するものではない。特定のコアに割り当てるには、別の設定である `cpuset` を使う。

`mem_limit` は、コンテナが割り当てられるメモリの上限である。ハードリミットを超えると、メモリの割り当てに失敗したり、プロセスが OOM の仕組みによって強制終了されたりすることがある。それによってコンテナが終了した場合に初めて、前述の再起動ポリシーがその後の復旧に関わる可能性がある。

リソース制限の目的はホストを守ることだけではなく、デプロイ前にアプリケーションに必要なリソースを明確にすることでもある。制限値が低すぎれば、それ自体が不安定さを生むので、実際の監視結果を見ながら少しずつ調整していく必要がある。

## 5. `docker stats` でリソースの使用状況を観察する

実行中のすべてのコンテナを継続的に観察する。

```bash
docker stats
```

特定のコンテナだけを観察する。

```bash
docker stats 05-compose-app-1
```

このコマンドはデフォルトで表示を更新し続けるので、`Ctrl+C` で終了する。現在の値を 1 回だけ取得したい場合は、次のようにする。

```bash
docker stats 05-compose-app-1 --no-stream
```

主なフィールドは次のとおりである。

| フィールド | 意味 |
|---|---|
| `CPU %` | 現在の CPU 使用率 |
| `MEM USAGE / LIMIT` | 現在のメモリ使用量と上限 |
| `MEM %` | 上限に対するメモリ使用量の割合 |
| `NET I/O` | ネットワークの送受信量 |
| `BLOCK I/O` | ブロックデバイスの読み書き量 |
| `PIDS` | コンテナ内のプロセスとスレッドの数 |

今回の実験では、`mem_limit` を設定する前は、2 つのコンテナのメモリ上限がどちらも Docker の Linux 環境で使えるメモリ量に近い値として表示されていた。制限付きでコンテナを作り直した後に初めて、`MEM USAGE / LIMIT` の LIMIT が Compose で指定した値に変わった。

`compose.yaml` を変更した後は、次を実行すればよい。

```bash
docker compose up -d
docker stats --no-stream
```

Compose は設定の変更に応じて関係するコンテナを作り直し、名前付きボリュームのデータはそのまま残る。

## 6. 現時点での理解

```text
Compose プロジェクトの状態
    └── docker compose ls

コンテナの実行状態
    ├── docker compose ps / docker ps
    ├── restart がプロセスの終了に対応する
    └── healthcheck がサービスの健全性を表す

リソースの境界
    ├── cpus / mem_limit が Cgroup の制約として書き込まれる
    └── docker stats で実際の使用量を観察する
```

`restart`、`healthcheck`、リソース制限は、3 種類の異なる問題を解決している。コンテナが終了した後の復旧、サービスが使えるかどうかの観察、そしてホストのリソースの保護である。これらを組み合わせることで最も基本的な状態管理ができるが、まだ完全な障害検知と自動復旧の仕組みとは言えない。
