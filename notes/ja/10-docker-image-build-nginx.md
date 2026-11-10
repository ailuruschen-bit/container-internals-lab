# Docker イメージのビルド実践（2）：Nginx のトップページをカスタマイズする

[English](../10-docker-image-build-nginx.md) | [中文](../zh/10-docker-image-build-nginx.md) | **日本語**

前回は `alpine` から出発して、最小限の Hello World イメージをビルドした。今回は汎用の Linux ベースイメージにソフトウェアをインストールするところから始めるのではなく、`nginx:alpine` を直接ベースにして、自分用の Nginx Web イメージをビルドする。

今回の Dockerfile は命令が 2 つしかないが、2 つの点をよりはっきり理解できた。子イメージがベースイメージの実行設定をどのように継承するのか、そして `COPY` がベースイメージを変更せずに最終的なファイルシステムのビューをどう変えるのか、である。

## 1. プロジェクトを用意する

プロジェクトのディレクトリはごく単純でよい。

```text
docker-image-lab/
└── 02-nginx/
    ├── Dockerfile
    └── index.html
```

`index.html` は公開するカスタムページで、たとえば次のようなものである。

```html
<!doctype html>
<html lang="zh-CN">
  <head>
    <meta charset="UTF-8">
    <title>My Nginx Image</title>
  </head>
  <body>
    <h1>Hello from my own Nginx image!</h1>
  </body>
</html>
```

Dockerfile：

```dockerfile
FROM nginx:alpine

COPY index.html /usr/share/nginx/html/index.html
```

前回と比べると、今回は `WORKDIR` も `CMD` も自分で書いていない。ベースイメージに Nginx のプログラム、起動スクリプト、デフォルトの設定、デフォルトの Web ルートがすでに用意されているので、自分が本当に関心のあるページの内容だけを差し替えればよい。

## 2. なぜ `CMD` を書かなくてよいのか

`FROM nginx:alpine` は、ベースイメージのファイルシステムレイヤーを取り込むだけでなく、そのイメージの設定（Image Config）も出発点として引き継ぐ。

手元にある現在の `nginx:alpine` の主な設定は次のとおりである。

```text
Entrypoint: ["/docker-entrypoint.sh"]
Cmd:        ["nginx", "-g", "daemon off;"]
Ports:      80/tcp
```

新しい Dockerfile では `ENTRYPOINT` も `CMD` も設定し直していないので、ビルドされた `my-nginx:v1` はこれらの設定をそのまま使い続ける。

コンテナが最終的に起動するときは、ひとまず両者が次のように組み合わされると理解できる。

```text
/docker-entrypoint.sh nginx -g "daemon off;"
└──── ENTRYPOINT ────┘ └──────── CMD ────────┘
```

エントリーポイントのスクリプトは、まず公式の Nginx イメージが提供する初期化処理を行い、それから `CMD` で指定された Nginx のコマンドを実行する。`daemon off;` によって Nginx はフォアグラウンドで動き続ける。Nginx のプロセスが終了しない限り、コンテナも実行中の状態を保つ。

したがって、「`CMD` を継承したから Nginx が自動的に起動する」という理解の方向は正しい。ただし、より正確には**継承された `ENTRYPOINT` と `CMD` が一緒になって、コンテナのデフォルトの起動動作を構成している**と言うべきである。

### 80 番ポートはどこから来るのか

イメージの設定には、次の項目も見られる。

```json
{"80/tcp":{}}
```

これはベースイメージのポート宣言に由来し、このイメージがコンテナの `80/tcp` でサービスを提供する想定であることを示している。しかし、この宣言自体が Nginx に待ち受けを開始させるわけでも、コンテナのポートを自動的にホストへ公開するわけでもない。

- Nginx が 80 番ポートで待ち受けるかどうかは、Nginx の設定で決まる。
- イメージの `ExposedPorts` は、想定されるポートに関するメタデータである。
- ホストからあるポートでアクセスできるかどうかは、コンテナの実行時に `-p` などの設定を使うかどうかにもよる。

## 3. `COPY` でデフォルトのトップページをカスタマイズする

公式の Nginx イメージのデフォルトの静的ファイルのディレクトリは次のとおりである。

```text
/usr/share/nginx/html
```

ここにはすでにデフォルトの `index.html` がある。今回のビルドでは次を実行する。

```dockerfile
COPY index.html /usr/share/nginx/html/index.html
```

コピー元のファイルは今回のビルドコンテキストから取られ、コピー先は新しいイメージのファイルシステムにおけるデフォルトのトップページの位置である。

コンテナから最終的に見えるディレクトリのビューで言えば、カスタムページがベースイメージのデフォルトページを置き換えている。しかしイメージのレイヤー構造から見ると、Docker は `nginx:alpine` の既存のレイヤーをさかのぼって変更しているわけではない。

```text
my-nginx:v1
    ├── 新しいレイヤー：カスタムの index.html
    └── ベースレイヤー：nginx:alpine
                    └── 元のデフォルトの index.html
```

上位のレイヤーが同じパスに新しいファイルを提供しているので、`/usr/share/nginx/html/index.html` を読むとカスタムの内容が見える。ベースイメージの古いファイルは元の読み取り専用レイヤーに残ったままで、最終的な統合ビューの中で上位のファイルに覆い隠されているだけである。

これはつまり、`my-nginx:v1` をビルドしてもローカルの `nginx:alpine` は変わらないということでもある。今後、元のイメージでコンテナを起動すれば、そのイメージ本来のデフォルトページが表示される。

## 4. カスタムイメージをビルドする

プロジェクトのディレクトリに移動する。

```bash
cd "$HOME/docker-image-lab/02-nginx"
```

イメージをビルドする。

```bash
docker build -t my-nginx:v1 .
```

ここでも、カレントディレクトリをビルドコンテキストとして使う。ビルダーはその中の Dockerfile を読み込み、`COPY` によって `index.html` を新しいイメージに追加する。

ビルドが完了したら、ローカルのイメージを確認する。

```bash
docker image ls my-nginx
```

この時点で `my-nginx:v1` は新しいイメージであり、`nginx:alpine` は独立したベースイメージとしてローカルに残っている。

## 5. `inspect` で設定の継承を確認する

イメージの完全な情報を確認する。

```bash
docker image inspect my-nginx:v1
```

継承された `CMD` だけを確認する。

```bash
docker image inspect my-nginx:v1 \
  -f '{{json .Config.Cmd}}'
```

今回の実験では、次の結果が得られた。

```json
["nginx","-g","daemon off;"]
```

ここでのオプションは `--format` の短縮形である `-f` であり、`if` ではない。

`ENTRYPOINT` だけを確認する。

```bash
docker image inspect my-nginx:v1 \
  -f '{{json .Config.Entrypoint}}'
```

出力：

```json
["/docker-entrypoint.sh"]
```

宣言されたポートを確認する。

```bash
docker image inspect my-nginx:v1 \
  -f '{{json .Config.ExposedPorts}}'
```

出力：

```json
{"80/tcp":{}}
```

これらの結果からわかるのは、子イメージはページのファイルを 1 つ追加しただけだが、ベースイメージがあらかじめ用意した実行設定は引き続き存在している、ということである。

子の Dockerfile に新しい `CMD` を書けば、それはベースイメージから継承した `CMD` を置き換える。2 つのデフォルトのコマンドが同時に残るわけではない。

## 6. `history` でビルド結果を観察する

```bash
docker image history my-nginx:v1
```

出力の一番上には、次のような記録が見られる。

```text
COPY index.html /usr/share/nginx/html/index.html
```

その下には `nginx:alpine` から引き継いだビルド履歴があり、Nginx のインストール、環境変数の設定、ポートの宣言、`ENTRYPOINT` や `CMD` の設定などの操作が含まれている。

これは Dockerfile の継承関係とちょうど対応している。

```text
my-nginx:v1 の COPY
          ↓
nginx:alpine の既存の履歴
          ↓
さらに下層の Alpine イメージの内容
```

`docker image history` は、イメージがおおよそどのようなビルド手順で作られ、各手順でどれだけのサイズが増えたかを見るのに向いている。ただし、元の Dockerfile の完全なバックアップではなく、出力されるコマンドもビルダーによって変換されたり切り詰められたりしている可能性がある。

作成時のコマンドをより完全な形で見たい場合は、次のようにする。

```bash
docker image history --no-trunc my-nginx:v1
```

## 7. コンテナを起動してページを確認する

コンテナの 80 番ポートを、手元のマシンの 8080 番に公開する。

```bash
docker run -d --rm \
  --name my-nginx \
  -p 127.0.0.1:8080:80 \
  my-nginx:v1
```

ページにアクセスする。

```bash
curl http://localhost:8080
```

カスタムの `index.html` の内容が返ってくれば、次のことが確認できる。

- Nginx が継承した起動設定で正常に動いている。
- Nginx がコンテナ内で 80 番ポートを待ち受けている。
- カスタムのトップページが新しいイメージのファイルシステムに入っている。
- ホストの 8080 番からコンテナの 80 番へのポート公開が機能している。

コンテナに直接入って、最終的なファイルを確認することもできる。

```bash
docker exec my-nginx \
  cat /usr/share/nginx/html/index.html
```

実験が終わったら、コンテナを停止する。

```bash
docker stop my-nginx
```

起動時に `--rm` を指定しているので、コンテナは停止後に自動で削除される。ビルドした `my-nginx:v1` のイメージは残る。

## 8. 今日得られた理解

今回の実験における継承とカスタマイズの関係は、次のようにまとめられる。

```text
nginx:alpine
    ├── Nginx のプログラムとデフォルトの設定
    ├── デフォルトの静的ファイル
    ├── ENTRYPOINT
    ├── CMD
    └── ExposedPorts
            │
            │ FROM
            ▼
my-nginx:v1
    ├── ベースイメージのファイルと設定を継承する
    └── カスタムの index.html を COPY で新しいレイヤーに追加する
```

これによって、「あるイメージをベースにする」ということを、より具体的に理解できた。それはインストール済みのプログラムを再利用するだけでなく、イメージが提供するデフォルトの実行設定を継承することでもある。それらのデフォルト値が要件に合っていれば、子の Dockerfile で改めて宣言する必要はない。

同時に、いわゆる「ベースイメージのファイルを置き換える」とは、通常は元の読み取り専用レイヤーを変更することではなく、新しいレイヤーの同じパスに新しい内容を書き込み、それを最終的なビューで見えるファイルにすることである。

最初に Alpine からシェルのプログラムをビルドしたときと比べると、今回は実際の使い方により近い。汎用的な設定がすでに済んでいるアプリケーションのイメージを選び、自分のコンテンツだけをその上に重ねて、繰り返し実行できるカスタムイメージを得る、という形である。
