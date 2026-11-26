# Docker のトラブルシューティング実践（3）：Spring Boot がデータベースのホスト名を解決できない

[English](../20-docker-troubleshooting-db-name-resolution.md) | [中文](../zh/20-docker-troubleshooting-db-name-resolution.md) | **日本語**

Spring Boot が PostgreSQL に接続できないとき、私はやはり普段 Java アプリケーションを調べるときと同じように、まず例外の連鎖を読む。`UnknownHostException: db` が見えたら、次にアプリケーションが動いているネットワーク環境から、`db` を名前解決できるかどうかを確認する。

## 1. ログから解決に失敗したホスト名を見つける

```bash
docker logs --tail=100 <container>
docker compose logs --tail=100 app
```

まず直近の 100 行を確認し、`Caused by` をたどって根本の原因を探す。たとえば JDBC URL が `jdbc:postgresql://db:5432/labdb` で、ログに `UnknownHostException: db` が出ていれば、まず `db` の名前解決を調べる。ログが長く、例外の連鎖が一部しか表示されていない場合は、`--tail` の値を大きくすればよい。

## 2. アプリケーションのコンテナから名前解決を試す

`app` がまだ動いていれば、元のコンテナで直接 `docker compose exec app getent hosts db` を実行できる。すでに終了している場合は、Compose ファイルがあるディレクトリで、同じサービス定義から一時的なコンテナを起動する。

```bash
docker compose run --rm --entrypoint sh app
getent hosts db
exit
```

`run` は `app` の設定に基づいて新しいコンテナを作成し、`--entrypoint sh` でシェルに入り、終了すると `--rm` によって自動で削除される。元のコンテナは残るので、引き続きログを調べられる。`db` がすでに動いている場合は、デバッグ時に `--no-deps` を付けて、依存するサービスの起動を省略できる。

ここで 1 つ Linux のコマンドを覚えておく。`getent hosts db` である。これはコンテナ内のシステムの設定に従ってホスト名を調べるもので、結果は DNS から来ることも、`/etc/hosts` から来ることもある。使い方でのサブコマンドは複数形の `hosts` である。まずイメージに `getent` があることを確認してから、結果をもとに判断する。

## 3. 結果に応じてさらに範囲を絞る

今回の実験の一時的なコンテナでは、`getent hosts db` は IP アドレスを返し、存在しない名前を調べたときはレコードが返らなかった。

- `db` を解決できない場合は、まず `docker compose ps -a` でデータベースのコンテナの状態を確認し、JDBC URL のホスト名が Compose のサービス名と一致しているか、`app` と `db` が同じネットワークを共有しているかを確認する。
- アドレスを解決できる場合は、続いて PostgreSQL の `5432` 番ポートと認証を確認する。元の Java のプロセスがまだ `UnknownHostException` を報告している場合は、それが実際に使っているホスト名も確認する必要がある。一時的なコンテナでの結果は、そのときのシステムの名前解決の状況を表しているにすぎない。

今回の実験で使うべきなのはサービス名の `db` である。コンテナを作り直すと IP アドレスは変わる可能性があるので、JDBC URL を修正するときも引き続きサービス名を使えばよい。
