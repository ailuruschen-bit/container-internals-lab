# Docker イメージのビルド実践（1）：Hello World

[English](../docker-image-build-hello-world.md) | [中文](../zh/docker-image-build-hello-world.md) | **日本語**

前回はローカルのイメージを確認・削除する方法を学んだ。今日は初めて Dockerfile を使って自分のイメージをビルドした。

今回のプログラムはシェルの 2 行だけだが、実験の主眼はプログラムそのものではない。Dockerfile がどのようにイメージになり、そのイメージがどのように実行中のコンテナになるのかを観察することにある。

## 1. 最小限のプロジェクトを用意する

実験環境は引き続き macOS + OrbStack で、ベースイメージには `alpine:latest` を使う。

プロジェクトにはファイルが 2 つあるだけである。

```text
docker-image-lab/
└── 01-hello/
    ├── Dockerfile
    └── hello.sh
```

`hello.sh`：

```sh
#!/bin/sh
echo "Hello from my first Docker Image!"
pwd
```

`Dockerfile`：

```dockerfile
FROM alpine:latest

WORKDIR /app

COPY hello.sh .

CMD ["sh", "hello.sh"]
```

この Dockerfile から最終的に生まれるビルド結果は 2 種類ある。1 つはコンテナから見えるべきファイル、もう 1 つはコンテナ起動時に使われるデフォルトの設定である。

## 2. 4 つの Dockerfile 命令がそれぞれ行うこと

### `FROM`：ビルドの出発点を選ぶ

```dockerfile
FROM alpine:latest
```

`FROM` は、新しいイメージをどのベースイメージをもとにビルドするかを指定する。ここでは Alpine の既存のファイルシステムレイヤーと関連する設定を再利用する。ローカルにそのイメージがなければ、ビルダーはレジストリからプルできる。

`hello-lab` をビルドしても、元の Alpine イメージは変更されない。新しく加えた内容は、新しいイメージに属するビルド結果になる。

### `WORKDIR`：デフォルトの作業ディレクトリを設定する

```dockerfile
WORKDIR /app
```

`WORKDIR` は、以降の関連する命令が使う作業ディレクトリに影響し、コンテナ起動時のデフォルトの作業ディレクトリにもなる。`/app` が存在しなければビルド中に作成されるため、先に次のように書いておく必要はない。

```dockerfile
RUN mkdir -p /app
```

### `COPY`：ファイルをイメージに入れる

```dockerfile
COPY hello.sh .
```

`COPY` の基本的な構文は次のとおりである。

```dockerfile
COPY <コピー元のパス> <コピー先のパス>
```

コピー元はビルドコンテキストから取られ、コピー先はビルド中のイメージのファイルシステムに属する。ここでのコピー先 `.` は相対パスであり、直前に設定した `WORKDIR /app` を基準に解決されるので、結果は次のようになる。

```text
/app/hello.sh
```

このファイルの内容はすでにイメージに取り込まれており、コンテナの実行時に macOS 上の元の `hello.sh` を参照し続けるわけではない。したがって `COPY` はバインドマウントとはまったく異なる。

- `COPY` は、ビルド時にファイルの内容をイメージに取り込む。
- バインドマウントは、実行時に外部のパスをコンテナにマウントする。

### `CMD`：デフォルトの起動コマンドを記録する

```dockerfile
CMD ["sh", "hello.sh"]
```

`CMD` は、イメージのビルド時に `hello.sh` を実行するわけではない。デフォルトのコマンドをイメージの設定（Image Config）に書き込み、後でコンテナを作成するときに使われる。

```text
docker build  ──→  Cmd の設定を保存する
docker run    ──→  Cmd を使ってプロセスを起動する
```

ここでは `sh hello.sh` を明示的に呼び出しているので、シェルがスクリプトの内容を読み込む。そのため、`hello.sh` 自体に実行権限は必要ない。スクリプトを直接実行する形に変えるなら、正しい実行権限とインタープリターの設定も必要になる。

## 3. Exec 形式と Shell 形式

Dockerfile のコマンドには、よく使われる 2 つの形式がある。

Exec 形式は JSON 配列を使う。

```dockerfile
CMD ["sh", "hello.sh"]
```

配列の最初の要素が実行するプログラムで、それ以降はそのプログラムに渡す引数である。この形式では、デフォルトでコマンド全体を別のシェルで解釈し直すことはなく、シェルの変数展開も自動では行われない。コンテナのメインプロセスを起動する方法としては、こちらのほうが適している。

Shell 形式は 1 つのコマンド文字列である。

```dockerfile
CMD sh hello.sh
```

Linux コンテナでは、通常次のような形で実行される。

```text
/bin/sh -c "sh hello.sh"
```

Shell 形式ではシェルの変数展開やパイプなどの構文を使えるが、追加のシェルプロセスが、シグナルの受信や転送に影響することもある。

現時点での簡単な区別は次のとおりである。

```text
Exec Form   プログラムと引数を明示的に指定する
Shell Form  コマンド文字列をまずシェルに解釈させる
```

## 4. ビルドコンテキストとは何か

プロジェクトのディレクトリに移動し、イメージをビルドする。

```bash
cd "$HOME/docker-image-lab/01-hello"
docker build -t hello-lab:v1 .
```

コマンド末尾の `.` は、カレントディレクトリが今回のビルドのビルドコンテキストであることを表す。

```text
docker build -t hello-lab:v1 .
                                 ↑
                          Build Context
```

通常の `COPY` や `ADD` などの命令が必要とするローカルファイルを、ビルダーが読み込めるのはビルドコンテキストの中からだけである。`COPY hello.sh .` がコピー元のファイルを見つけられるのも、`hello.sh` が現在のコンテキストの中にあるからである。

ビルドコンテキストはイメージのファイルシステムと同じではなく、コンテキスト内のファイルがすべて自動的に最終的なイメージに入るわけでもない。ファイルは、対応するビルド命令によって処理されて初めてビルド結果の一部になる。

実際のプロジェクトでは、ログ、ビルド成果物、バージョン管理のディレクトリ、機密ファイルなどを不要なビルド入力に含めないようにすべきであり、`.dockerignore` で除外できる。今回のディレクトリにはファイルが 2 つしかないので、今のところ複雑な設定は必要ない。

ビルドコマンドのほかの部分の意味は次のとおりである。

- `build`：Dockerfile に従ってビルドを実行する。
- `-t hello-lab:v1`：結果にイメージ名とタグを付ける。
- `.`：ビルドコンテキストを指定する。これにより、ビルダーはデフォルトでカレントディレクトリの `Dockerfile` を探す。

ここにも混同しやすい点がある。`docker build` の `-t` は `--tag` だが、`docker run` の `-t` は `--tty` である。同じ短いオプションでも、サブコマンドによって意味が異なることがある。

## 5. Build から Run へ

全体の流れは次のように表せる。

```text
Dockerfile + hello.sh
          │
          ▼
     docker build
          │
          ▼
 Image: hello-lab:v1
          │
          ▼
      docker run
          │
          ▼
  Container Process
```

Build の段階では、ファイルシステムの内容を生成し、イメージの実行設定を保存する。Run の段階では、イメージをもとにコンテナの書き込み可能層、ネットワーク、プロセス環境を作成し、最後に起動コマンドを実行する。

イメージのデフォルトの `CMD` を使って起動する。

```bash
docker run --rm hello-lab:v1
```

期待される出力：

```text
Hello from my first Docker Image!
/app
```

これで次の 3 点を同時に確認できる。

- `hello.sh` がイメージにコピーされている。
- コンテナ起動時に `CMD` が実行された。
- `WORKDIR` で設定した `/app` がデフォルトの作業ディレクトリになった。

`--rm` は、コンテナが停止した後にそのコンテナを自動で削除するという意味であり、コンテナの作成に使ったイメージは削除されない。

## 6. イメージのデフォルトの `CMD` を上書きする

イメージ名の後に別のコマンドを指定する。

```bash
docker run --rm hello-lab:v1 echo "CMD overridden"
```

出力は次のようになる。

```text
CMD overridden
```

`docker run` の基本的な構文は次のとおりである。

```text
docker run [OPTIONS] IMAGE [COMMAND] [ARG...]
```

今回のイメージでは `ENTRYPOINT` を設定していないので、イメージ名の後の `COMMAND` が元の `CMD` を置き換える。

同じように、デフォルトのコマンドを対話型のシェルに置き換えることもできる。

```bash
docker run --rm -it hello-lab:v1 sh
```

中に入ったら、イメージの内容を手作業で確認する。

```sh
pwd
ls -l /app
cat /app/hello.sh
sh /app/hello.sh
```

このとき `hello.sh` は自動では実行されない。`sh` がすでにデフォルトの `CMD` を置き換えているからである。今後 `ENTRYPOINT` を学んだら、それと `CMD`、実行時に指定するコマンドとの組み合わせ方をあらためて理解する必要がある。

## 7. タグはイメージを参照する名前にすぎない

今回のビルドで得られたイメージの参照名は次のとおりである。

```text
hello-lab:v1
```

これは単純に次のように分解できる。

```text
repository : tag
hello-lab  : v1
```

タグを省略すると、Docker はデフォルトで `latest` を使おうとする。

```bash
docker run hello-lab
```

これは実際には `hello-lab:latest` を探す。現在あるのは `hello-lab:v1` だけなので、ローカルに `v1` があるからといって自動的にそれが見つかるわけではない。

既存のイメージに、別のタグを追加することができる。

```bash
docker image tag hello-lab:v1 hello-lab:latest
docker image ls
```

その後は、次のコマンドで `latest` が見つかるようになる。

```bash
docker run --rm hello-lab
```

この操作はイメージを再ビルドするわけでも、イメージのファイルシステムを丸ごとコピーするわけでもなく、参照名を 1 つ追加するだけである。同じイメージに複数のタグを付けることができ、`latest` もただのタグにすぎないため、それが最も新しくビルドされた内容を指している保証はない。

## 8. ビルド結果を確認する

ここで、前回学んだ `inspect` を自分でビルドしたイメージに使ってみる。

```bash
docker image inspect hello-lab:v1
```

イメージの設定だけを確認する。

```bash
docker image inspect hello-lab:v1 \
  -f '{{json .Config}}'
```

作業ディレクトリとデフォルトのコマンドを個別に確認する。

```bash
docker image inspect hello-lab:v1 \
  -f '{{.Config.WorkingDir}}'

docker image inspect hello-lab:v1 \
  -f '{{json .Config.Cmd}}'
```

今回の Dockerfile の命令とイメージの結果との対応は次のとおりである。

| Dockerfile の命令 | ビルド結果 |
|---|---|
| `FROM` | ベースイメージのファイルシステムレイヤーと設定を出発点にする |
| `WORKDIR` | デフォルトの作業ディレクトリを設定し、必要ならディレクトリを作成する |
| `COPY` | `hello.sh` をイメージのファイルシステムに追加する |
| `CMD` | デフォルトの起動コマンドをイメージの設定に書き込む |

したがって、このイメージはひとまず次のように理解できる。

```text
Docker Image
   ├── Filesystem Layers
   │      └── /app/hello.sh
   └── Image Config
          ├── WorkingDir: /app
          └── Cmd: ["sh", "hello.sh"]
```

## 9. 今日得られた理解

Dockerfile はイメージのビルド手順書であり、イメージはビルドが完了した成果物であり、コンテナはイメージから作成された実行中のインスタンスである。

```text
Dockerfile
    │ docker build
    ▼
Docker Image
    ├── ファイルシステムの内容
    └── デフォルトの実行設定
    │ docker run
    ▼
Container
    ├── イメージをもとに実行環境を作成する
    ├── 独自の書き込み可能層を追加する
    └── 実際のプロセスを起動する
```

1 つのイメージから複数のコンテナを繰り返し作成でき、コンテナは起動時にイメージのデフォルト設定の一部を上書きすることもできる。

ビルドが成功すれば、元の Dockerfile や `hello.sh` を残していなくても、ローカルに生成されたイメージは引き続き実行できる。コンテナが実際に依存しているのはビルドの成果物であり、元のプロジェクトディレクトリを再び読み込むわけではないからである。

最後に重要な気づきがある。Dockerfile を、上から順に実行されるシェルスクリプトだと単純に考えてはいけない。命令によって生み出される結果は異なる。

```text
COPY  ビルド時にファイルの内容を処理する
CMD   ビルド時にデフォルトの実行設定を記録する
RUN   ビルド時にコマンドを実行する（今後の実験で扱う）
```

今日完了したのは、イメージのビルドの最小限のサイクルである。次は Dockerfile を使ってカスタムの Nginx Web イメージをビルドし、`RUN`、ビルドキャッシュ、そして静的ファイルがどのようにイメージに入るのかを引き続き観察する予定である。
