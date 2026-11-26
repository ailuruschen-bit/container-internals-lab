# Docker のトラブルシューティング実践（1）：起動直後にコンテナが停止する

[English](../18-docker-troubleshooting-container-exits-immediately.md) | [中文](../zh/18-docker-troubleshooting-container-exits-immediately.md) | **日本語**

今回から、Docker でよくあるトラブルシューティングの場面を整理し始めた。最初の現象は、`docker run` や `docker compose up` を実行した後、コンテナが起動した直後に停止してしまうというものである。

調査するときは、Dockerfile の修正やイメージの再ビルドを急がず、「状態、ログ、詳細情報」の順に証拠を集める。

この現象を安定して再現するために、今回はエラーを出力して `1` を返す一時的なコンテナを使う。

```bash
docker run --name note-exit-lab \
  alpine sh -c 'echo "application startup failed" >&2; exit 1'
```

ここで `--rm` を使っていないのは、終了したコンテナを残しておかないと、後でその状態や詳細情報を確認できないからである。実験が終わったら、`docker rm note-exit-lab` で片付ける。

## 1. まずコンテナの実際の状態を確認する

`docker ps` はデフォルトで実行中のコンテナしか表示しない。終了したインスタンスを見るには、次を使う必要がある。

```bash
docker ps -a
```

実験では、次のように表示された。

```text
CONTAINER ID   IMAGE    STATUS       NAMES
b70b6baf23b2   alpine   Exited (1)   note-exit-lab
```

ここでまず 3 つの有用な情報が得られる。

- 後続のコマンドで使う、コンテナの ID または名前。
- 現在の状態が、再起動を繰り返している状態やまだ実行中の状態ではなく、たしかに `Exited` であること。
- 括弧内の `1` が、メインプロセスが最後に終了したときの終了コード（Exit Code）であること。

コンテナが停止したからといって、Docker 自体に障害が起きたとは限らない。コンテナのライフサイクルはメインプロセスに結びついているので、メインプロセスが正常に処理を終えた場合も停止する。そのときの終了コードは通常 `0` である。したがって、本当に判断すべきなのは、なぜ終了したのか、そしてその終了が想定どおりなのかどうかである。

## 2. メインプロセスが残したログを確認する

ID または名前がわかったら、次にログを確認する。

```bash
docker logs <container>
```

`<container>` にはコンテナ名と ID のどちらを指定してもよい。Compose で起動したコンテナであれば、Compose ファイルがあるディレクトリで `docker compose logs <service>` を使うこともできる。ここで指定するのは `services:` の下のサービス名で、たとえば `app` である。また、`docker compose ps -a` を使えば、状態の一覧を現在のプロジェクトに絞り込める。

例：

```bash
docker logs b70b6baf23b2
```

今回の実験では、次の結果が得られた。

```text
application startup failed
```

ログが多い場合は、まず末尾の部分から確認すればよい。

```bash
docker logs --tail 100 <container>
```

`docker logs` が主に取得するのは、コンテナが標準出力と標準エラー出力に書き込んだ内容である。アプリケーションがログを内部のファイルにしか書き込んでいない場合や、ログを出力する前に失敗した場合は、ここが空になることもある。したがって、「ログがない」ことは「エラーがない」ことを意味しない。

## 3. まず `inspect` の全体を見て、それからフィールドを絞り込む

`inspect` のフィールドは多く、最初からすべてのパスを覚える必要はない。より実用的なのは、まず `docker ps -a` でコンテナ ID を取得し、完全な JSON を確認する方法である。

```bash
docker inspect <container>
```

出力の最も外側は配列で、その中にコンテナの設定、実行状態、マウント、ネットワークなどの詳細情報が含まれている。まず全体の構造からフィールドの位置を確認し、それから `-f` で必要な部分を取り出す。

今回の場面で最初に確認する価値が最も高いのは `.State` オブジェクトである。

```bash
docker inspect -f '{{json .State}}' <container>
```

実験の出力は次のようになる。

```json
{"Status":"exited","Running":false,"OOMKilled":false,"ExitCode":1,"Error":"","StartedAt":"...","FinishedAt":"..."}
```

`json` を使うと、入れ子になった `.State` オブジェクトが正しい JSON として出力され、テンプレートのデフォルトの形式よりも読みやすくなる。

## 4. `.State` で重点的に見るもの

| フィールド | 役割 |
|---|---|
| `Status` | `running`、`exited`、`restarting` など、現在どの状態にあるか |
| `ExitCode` | メインプロセスが最後に終了したときに返した状態コード |
| `Error` | Docker のランタイムがコンテナの起動時に記録したエラー |
| `OOMKilled` | プロセスがメモリ不足によって OOM の仕組みで強制終了されたかどうか |
| `Restarting` | コンテナが再起動ポリシーに従って再起動を繰り返しているかどうか |
| `StartedAt` | 直近の起動時刻 |
| `FinishedAt` | 直近の停止時刻 |

さらに 1 つのフィールドに絞り込みたい場合は、直接アクセスすることもできる。

```bash
docker inspect -f '{{.State.ExitCode}}' <container>
docker inspect -f '{{.State.OOMKilled}}' <container>
docker inspect -f '{{.State.Error}}' <container>
```

## 5. 終了コードが示すのは方向性だけ

よく見られるいくつかの値は、範囲を絞り込むのに役立つ。

| 終了コード | よくある意味 |
|---|---|
| `0` | メインプロセスが正常に終了した。障害とは限らない |
| `1` | アプリケーションが返した一般的なエラー。ログと合わせて判断する必要がある |
| `126` | コマンドは見つかったが実行できなかった。たとえば権限の問題 |
| `127` | 実行すべきコマンドが見つからなかった |
| `137` | プロセスが `SIGKILL` を受け取った。OOM の可能性も、人為的な強制終了の可能性もある |
| `143` | プロセスが `SIGTERM` を受け取った。通常の停止処理でよく見られる |

終了コードだけで完全な診断はできない。たとえば `137` が見えても、それだけでメモリ不足と断定することはできず、さらに次の確認が必要である。

```bash
docker inspect -f '{{.State.OOMKilled}}' <container>
```

## 6. 状態だけでは原因を特定できないとき

ログと `.State` から「起動に失敗した」ことしかわからず、その理由が示されていない場合は、コンテナが実際に使った起動設定をさらに確認できる。

```bash
docker inspect -f '{{json .Config.Entrypoint}}' <container>
docker inspect -f '{{json .Config.Cmd}}' <container>
```

これにより、実際のエントリーポイントのプログラムとデフォルトの引数を確認でき、コマンドの書き間違い、ファイルが存在しない、起動引数が想定と違うといった問題を調べられる。

## 7. 現時点での調査の順序

```text
docker ps -a
    │
    ├── コンテナの状態、ID、終了コードを確認する
    ▼
docker logs <container>
    │
    ├── アプリケーションが標準出力と標準エラー出力に書いた内容を確認する
    ▼
docker inspect <container>
    │
    ├── まず JSON の全体を見る
    ▼
docker inspect -f '{{json .State}}' <container>
    │
    └── 結果に応じて ExitCode、OOMKilled、Error、または起動設定を確認する
```

この流れの要点は、`inspect` のすべてのフィールドを暗記することではなく、まず完全な JSON で全体の地図を作り、それから `-f` で範囲を一歩ずつ絞り込むことにある。「起動直後に停止する」場面では、`.State` が最初の切り口として最も適している。
