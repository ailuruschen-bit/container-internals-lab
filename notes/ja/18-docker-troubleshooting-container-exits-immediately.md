# Docker のトラブルシューティング実践

[English](../18-docker-troubleshooting-container-exits-immediately.md) | [中文](../zh/18-docker-troubleshooting-container-exits-immediately.md) | **日本語**

コンテナで障害が起きたら、まず `ps` で稼働状態を確認し、ログと `inspect` を見る。ここまではどのケースでも同じで、その先は症状に合わせて調べる。

## 状態とログを確認する

```bash
docker ps -a
docker logs --tail=100 <container>
docker compose logs --tail=100 <service>
```

Compose ファイルのあるディレクトリでは、`docker compose ps -a` で現在のプロジェクトのコンテナを確認できる。通常の Docker コマンドにはコンテナ名か ID を渡す。`docker compose logs`、`exec`、`top` にはサービス名を渡す。

## 1. コンテナが起動直後に終了する

コンテナはメインプロセスとともに終了する。`Exited (0)` は単発処理の正常終了でも表示される。予期しない終了では、`.State` の `ExitCode`、`Error`、開始・終了時刻を見て、`.Config.Entrypoint` と `.Config.Cmd` で起動設定を確認する。アプリがログを出す前に起動に失敗すると、`Error` にだけ手がかりが残ることもある。

```bash
docker inspect -f '{{json .State}}' <container>
docker inspect -f '{{json .Config.Entrypoint}}' <container>
docker inspect -f '{{json .Config.Cmd}}' <container>
```

フィールドの位置が分からなければ、まず `docker inspect <container>` の全体を見る。出力は JSON 配列なので、`jq` の `.[0]` で先頭のコンテナを選べる。

## 2. コンテナは稼働中だがサービスが応答しない

```bash
docker logs -f --tail=20 <container>
docker top <container>
docker exec -it <container> sh
```

コンテナが `Up` なら、直近のログを追いながら `top` でサービスのプロセスが残っているか確認する。ファイルや権限、待ち受けポートを調べるときは `exec` で入り、イメージに入っている `ps` などを使う。プロセスが残っていれば、コンテナ内から待ち受けポートへの接続を試す。

修正を確認したらプロジェクト側に反映する。`compose.yaml` を変えた場合は `docker compose up -d app` で適用する。`docker compose restart app` は既存コンテナの設定を引き継ぐ。イメージの再ビルドが必要なら `docker compose up -d --build app` を使い、失敗していたリクエストを再確認する。

## 3. Spring Boot がデータベースのホスト名を解決できない

Spring Boot から PostgreSQL に接続できないときは、ログの `Caused by` をたどる。`UnknownHostException: db` なら、アプリコンテナで `getent hosts db` を実行する。終了済みなら、同じサービス設定で一時的なシェルを立ち上げる。

```bash
docker compose exec app getent hosts db
```

```bash
docker compose run --rm --entrypoint sh app
getent hosts db
exit
```

`getent hosts` はシステムの設定に従って名前を引く。結果が `/etc/hosts` に由来することもある。`db` を引けなければ JDBC URL、データベースコンテナの状態、共有ネットワークを確認する。IP が返るなら、データベースのポートと認証を調べる。

実験では、一時コンテナで `db` の IP を取得でき、存在しない名前には結果が返らなかった。

## 4. ヘルスチェックが失敗する

```bash
docker inspect -f '{{json .State.Health}}' <container>
docker inspect <container> | jq '.[0].State.Health'
docker inspect <container> | jq '.[0].State.Health.Log[-1]'
```

`Up (unhealthy)` なら `.State.Health` を確認する。`FailingStreak` は連続失敗回数、`Log[-1]` は直近のチェック結果。まず `ExitCode` と `Output` を見て、手元で再実行するときは `.Config.Healthcheck` で実際のコマンドを確認する。

実験ではファイルがない状態でチェックを失敗させ、`Output` に `ready file missing` が表示された。

## 5. メモリ不足でコンテナが終了する

突然 `Exited (137)` になり、アプリのログに目立った異常がなければ OOM を疑う。`137` は通常 `SIGKILL` に対応し、強制終了でも同じ値になる。`OOMKilled: true` は Docker が OOM による終了を記録したことを示す。このとき `Error` が空の場合もある。

```bash
docker inspect <container> | jq '.[0].State | {Status, ExitCode, OOMKilled, Error}'
docker inspect <container> | jq '.[0].HostConfig.Memory'
```

メモリ上限が `32 MiB` の一時コンテナで、`ExitCode: 137` と `OOMKilled: true` を確認した。

`.HostConfig.Memory` の単位はバイトで、`0` はコンテナ個別の上限が未設定であることを示す。Compose の `mem_limit` と照らし合わせる。再現できるなら、コンテナの稼働中に `docker stats <container>` の `MEM USAGE / LIMIT` を見て、急増か継続的な増加かを確認する。
