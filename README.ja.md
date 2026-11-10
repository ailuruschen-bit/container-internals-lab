# 技術学習ノート

[English](README.md) | [中文](README.zh.md) | **日本語**

学習の過程で本当に心を動かされた小さな疑問や小さな実験、そしてその時点での理解を記録している。

一度に完全な知識体系を築くことは目指していない。各ノートでは、そのとき腑に落ちた 1 つの問いだけを丁寧に説明し、理解が深まるにつれて修正していく。

## ノート

- 01. [環境変数は誰のものか](notes/ja/01-environment-variables-belong-to-processes.md)
- 02. [OS はどのように CPU を「取り戻す」のか](notes/ja/02-how-the-kernel-takes-back-the-cpu.md)
- 03. [Linux のファイルシステム、マウント、Mount Namespace について学んだこと](notes/ja/03-where-is-container-root-filesystem.md)
- 04. [Namespace は見え方を隔離し、Cgroup は使用量を制限する](notes/ja/04-namespace-views-and-cgroup-limits.md)
- 05. [Linux コンテナネットワークの仕組み：Network Namespace から NAT まで](notes/ja/05-linux-container-networking.md)
- 06. [Docker 基本操作の実践：マウントとネットワークをカーネルの仕組みに対応づける](notes/ja/06-docker-mount-and-network-practice.md)
- 07. [Docker イメージの基本操作：まずはイメージを確認する](notes/ja/07-docker-image-basics.md)
- 08. [なぜコンテナ化が必要なのか](notes/ja/08-why-containerize-applications.md)
- 09. [Docker イメージのビルド実践（1）：Hello World](notes/ja/09-docker-image-build-hello-world.md)
- 10. [Docker イメージのビルド実践（2）：Nginx のトップページをカスタマイズする](notes/ja/10-docker-image-build-nginx.md)
- 11. [Docker イメージのビルド実践（3）：キャッシュ、ARG/ENV、.dockerignore](notes/ja/11-docker-image-build-cache-args-ignore.md)
- 12. [Docker イメージのビルド実践（4）：Java とマルチステージビルド](notes/ja/12-docker-image-build-java-multistage.md)
- 13. [Docker イメージの実践（5）：バージョンの置き換え、ロールバック、レジストリ](notes/ja/13-docker-image-version-rollback-registry.md)
- 14. [Docker Compose 入門：`docker run` から複数サービスの構成へ](notes/ja/14-docker-compose-basics.md)
- 15. [Docker の `USER`：アプリケーションを非 root で実行する](notes/ja/15-docker-user-best-practices.md)

英語版と日本語版は、ノートの追加に合わせて順次補充していく。

## 実験ファイル

ノートで使った Dockerfile、ソースコード、Compose ファイルは [`labs/`](labs/) にまとめており、各手順を説明する英語のコメントを付けている。

## 過去のコンテンツ

以前に章立てに沿って整理した、コンテナの低レイヤーに関する記事と実験は、ブランチ `archive/structured-container-course-2026-10-18` にすべて保存している。
