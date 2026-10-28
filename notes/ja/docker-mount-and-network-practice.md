# Docker 基本操作の実践：マウントとネットワークをカーネルの仕組みに対応づける

[English](../docker-mount-and-network-practice.md) | [中文](../zh/docker-mount-and-network-practice.md) | **日本語**

これまで Namespace、Cgroup、OverlayFS、コンテナネットワークを学んできたが、触れてきたのは主に抽象的な仕組みだった。今回の実験の目的は Docker のコマンドを暗記することではなく、それらの仕組みを、実際に観察できるコンテナの振る舞いと対応づけることにある。

実験環境は **macOS + OrbStack** である。Docker Engine は実際には OrbStack が提供する Linux 環境の中で動いているため、ネイティブな Linux 上の Docker と比べて、ファイル共有やポート転送が 1 層多く挟まる経路がある。

## 1. まず実行環境を把握する

現在の環境は、次のように簡略化できる。

```text
macOS
  └── Docker CLI
        └── Docker Context: orbstack
              └── macOS に転送された Docker ソケット
                    └── OrbStack VM 内の Docker Engine
                          └── Linux コンテナ
```

Docker CLI はクライアントにすぎず、Docker Context はクライアントがどの Engine に接続すべきかを保持している。また、OrbStack の Linux マシンと Docker コンテナは別種の実行環境であり、日常的なコンテナ操作のために Linux マシンへ先に入る必要はない。

環境の確認には、次のコマンドを使った。

```bash
orb start
docker context ls
docker --context orbstack info
docker version
```

以前、次のようなエラーに遭遇した。

```text
failed to connect to the docker API
... docker.sock: no such file or directory
```

これは Docker コマンドが存在しないという意味ではなく、CLI が選んだソケットが使えないという意味である。OrbStack の場合、よくある原因は OrbStack が起動していないか、現在の Context が誤った場所を指していることである。

## 2. `run` と `exec` からコンテナのプロセスを理解する

Docker のコマンドの多くは、「管理対象 + 操作」という形で読める。

```bash
docker network create lab-net
docker network inspect lab-net
docker volume ls
```

よく使われる省略形もある。たとえば `docker ps` は `docker container ls` と同等である。

コンテナを作成することと、既存のコンテナに入ることは別の操作である。

```bash
docker run -dit --name process-lab alpine sh
docker exec -it process-lab sh
docker stop process-lab
docker rm process-lab
```

- `run` は新しいコンテナの実行環境を作成し、そのメインプロセスを起動する。
- `exec` は、実行中のコンテナ環境の中で追加のプロセスを起動する。
- `exec` で起動したシェルを終了しても、コンテナのメインプロセスは終了しない。
- メインプロセスが終了すると、通常はコンテナも停止する。

ここでの `alpine` は軽量な Linux ユーザー空間のイメージであり、`sh` はコンテナ起動後に実行されるプログラムである。イメージにはプログラム、共有ライブラリ、設定ファイルが含まれるが、独自の Linux カーネルは含まれない。コンテナは引き続き OrbStack の Linux 環境のカーネルを使う。

今回の実験でよく使ったオプションは次のとおり。

| オプション | 由来 | 効果 |
|---|---|---|
| `-i` | interactive | 標準入力を開いたままにする |
| `-t` | tty | 疑似端末を割り当てる |
| `-d` | detach | バックグラウンドで実行する |
| `--rm` | remove | コンテナ終了時に自動で削除する |
| `--name` | name | コンテナ名を指定する |
| `-u` | user | 実行ユーザーを指定する |
| `-p` | publish | コンテナのポートを公開する |
| `--mount` | mount | ファイルシステムのマウントを設定する |
| `--network` | network | Docker ネットワークを指定する |

短いオプションは `-it` や `-dit` のように組み合わせられる。`i` は Interactive、`t` は TTY、`d` は Detach、`p` は Publish を表す。

## 3. バインドマウント：コンテナと macOS でファイルを共有する

まず macOS 上にディレクトリとファイルを用意した。

```bash
mkdir -p "$HOME/docker-fs-lab"
echo "Hello from macOS" > "$HOME/docker-fs-lab/test.txt"
```

次に、そのディレクトリをコンテナの `/data` にマウントする。

```bash
docker run --rm -it \
  --name mount-lab \
  --mount type=bind,source="$HOME/docker-fs-lab",target=/data \
  alpine sh
```

`--mount` の 3 つのフィールドの意味は次のとおり。

- `type=bind`：バインドマウントを使う。
- `source`：マウント元。ここでは macOS 上のディレクトリ。
- `target`：コンテナ内でのアクセスパス。

コンテナ内でファイルを読み、別のファイルを書き込む。

```bash
cat /data/test.txt
echo "Hello from container" > /data/container.txt
```

コンテナを終了した後も、macOS 側で新しいファイルを確認できる。

```bash
cat "$HOME/docker-fs-lab/container.txt"
```

この実験からわかるのは、`/data` はコンテナ内にコピーされたデータではなく、コンテナが共有リソースにアクセスするためのパスの入口だということである。OrbStack では、macOS のファイルがまずファイル共有の仕組みを通じて Linux 環境に提供され、それがさらにコンテナにマウントされる。

### ボリュームと tmpfs

バインドマウントのほかに、Docker ではさらに 2 種類のマウントがよく使われる。

| 種類 | データの出どころ | 適した用途 |
|---|---|---|
| `bind` | 指定したホストのパス | 開発用ディレクトリ、設定ファイル |
| `volume` | Docker が管理するストレージ | データベース、永続データ |
| `tmpfs` | メモリ上の一時ファイルシステム | キャッシュ、一時データ |

名前付きボリューム：

```bash
docker run --rm -it \
  --mount type=volume,source=mydata,target=/data \
  alpine sh

docker volume ls
docker volume inspect mydata
```

名前付きボリュームは、それを使っていたコンテナを削除しても残る。バインドマウントと違い、実際の保存場所は Docker が管理する。

tmpfs：

```bash
docker run --rm -it \
  --mount type=tmpfs,target=/data \
  alpine sh
```

tmpfs には `source` が不要であり、コンテナが停止すると、その中のデータは通常の永続データとしては残らない。

### コンテナのルートファイルシステムを観察する

コンテナ内で次を実行した。

```bash
mount | grep ' on / '
mount | grep ' on /proc '
```

私の実験では、次のような結果が得られた。

```text
overlay on / type overlay (...)
proc on /proc type proc (...)
```

これは、コンテナの `/` が OverlayFS によって構成され、`/proc` は別途マウントされた procfs であることを示している。OverlayFS は通常、`lowerdir` でベース層を提供し、`upperdir` に現在のコンテナの変更を保存し、`workdir` を内部処理に使う。

したがって、コンテナから見えるルートディレクトリは専用の物理ディスクではなく、複数のファイルシステムとマウント関係が組み合わさってできたビューである。実際にどのストレージドライバーやスナップショットの仕組みが使われるかは Docker の環境次第なので、一度の実験で得た `overlay` という出力を、あらゆる環境で決まった結果だと考えてはいけない。

## 4. カスタムブリッジ：コンテナ用の仮想 LAN を作る

Docker はデフォルトでいくつかのネットワークを提供している。

```bash
docker network ls
```

| ネットワーク | ドライバー | 役割 |
|---|---|---|
| `bridge` | bridge | デフォルトの仮想ブリッジネットワーク |
| `host` | host | Docker ホスト環境のネットワーク名前空間を共有する |
| `none` | null | 通常の外部ネットワーク接続を構成しない |

ネイティブな Linux では、`host` は Docker ホストのネットワークを指す。OrbStack はこの意味づけの上に macOS との統合を加えており、ホストネットワーキングを使うサービスにも Mac の `localhost` からアクセスできるようにしている。そのため、今回の実験で見えた現象を、そのまますべての Docker 環境に当てはめることはできない。

カスタムのブリッジネットワークを作成した。

```bash
docker network create lab-net
docker network inspect lab-net
```

実験で得られた情報には、次のものが含まれていた。

```text
Name: lab-net
Driver: bridge
Subnet: 192.168.97.0/24
Gateway: 192.168.97.1
```

ここでの IPAM は IP Address Management の略である。Docker がサブネットを管理し、ネットワークに接続されたコンテナに内部 IP アドレスを割り当てる。

2 つのコンテナを作成し、このネットワークに直接接続する。

```bash
docker run -dit --name net-a --network lab-net alpine sh
docker run -dit --name net-b --network lab-net alpine sh
```

下層の構造は、次のように簡略化できる。

```text
Linux Kernel
      │
   Bridge
    /    \
  veth  veth
   │      │
 net-a  net-b
 NetNS  NetNS
```

作成時に `lab-net` に接続しなかったコンテナも、後から接続できる。

```bash
docker run -dit --name net-c alpine sh
docker network connect lab-net net-c
docker network disconnect lab-net net-c
```

ここであえて `net-c` を使ったのは、`net-a` はすでに `--network lab-net` で接続済みであり、「初めてネットワークに接続する」例として使えないからである。1 つのコンテナを複数の Docker ネットワークに同時に接続することもできる。

`net-a` から `net-b` との通信を確認する。

```bash
docker exec -it net-a sh
ping -c 3 net-b
```

`net-b` という名前をそのまま使えるのは、ユーザー定義のブリッジネットワークがコンテナ名の名前解決を提供しているからである。データ自体は、veth ペアと Linux ブリッジを通って実際に転送される。

## 5. コンテナの内側からネットワーク設定を観察する

Alpine に必要なツールがない場合は、先にインストールしておく。

```bash
apk add --no-cache iproute2 iputils curl netcat-openbsd
```

そのうえで、インターフェース、ルート、ネイバー、DNS をそれぞれ確認する。

```bash
ip -4 addr show eth0
ip route
ip neigh
cat /etc/resolv.conf
```

重要なのは出力を覚えることではなく、それらの関係を読み取ることである。

- `eth0` は、コンテナの Network Namespace 内にある仮想ネットワークインターフェースである。
- 同じサブネット内の宛先とは直接通信でき、それ以外の宛先は通常デフォルトゲートウェイに渡される。
- ネイバーテーブルは、同一リンク上の IP アドレスと MAC アドレスの対応を記録している。
- カスタムブリッジネットワークの `/etc/resolv.conf` には、Docker の組み込み DNS が使う `127.0.0.11` がよく見られる。

## 6. ソケットでコンテナ間の通信を確認する

`net-b` で TCP ポートを待ち受ける。

```bash
docker exec -it net-b sh
apk add --no-cache netcat-openbsd
nc -l 9000
```

`net-a` から接続する。

```bash
docker exec -it net-a sh
apk add --no-cache netcat-openbsd
nc net-b 9000
```

接続が確立すると、双方から直接テキストを送れる。通信経路は次のとおりである。

```text
net-a: Socket
   ↓
eth0 → veth Pair → Linux Bridge → veth Pair → eth0
                                              ↓
                                        net-b:9000
```

2 つのコンテナはすでに同じ仮想ネットワーク上にあるため、この実験ではホストのポートを公開する必要がない。

2 つのコンテナにそれぞれ自分の `9000` 番ポートを待ち受けさせることもできる。ポートはそれぞれの Network Namespace に属しているので、番号が同じでも衝突しない。

## 7. NAT：コンテナからインターネットへアクセスする

まず `net-a` で疎通を確認する。

```bash
ping -c 3 google.com
ip -4 addr show eth0
ip route
```

通常のブリッジネットワークでは、コンテナの IP アドレス、デフォルトゲートウェイ、転送、アウトバウンドのアドレス変換を、Docker があらかじめ設定していることが多い。そのため、プライベート IP を使うコンテナでもインターネットにアクセスできる。

インターネット側から見える送信元アドレスを、コンテナと macOS のそれぞれから問い合わせた。

```bash
curl -4 --noproxy '*' https://api.ipify.org
echo
```

`-4` は IPv4 の使用を強制し、`--noproxy '*'` は curl の設定にある HTTP プロキシを使わないようにする。

典型的なアウトバウンドの経路は、次のように表せる。

```text
コンテナのプライベート IP
      ↓
veth Pair
      ↓
Linux Bridge
      ↓
Routing / SNAT
      ↓
外部ネットワーク
```

SNAT は送信元アドレスを書き換え、必要に応じて送信元ポートも書き換える。conntrack は接続と変換の状態を記録し、戻りのパケットが元のコンテナに届くようにする。

ただし、現在の環境には Docker の Linux ネットワーク、OrbStack、macOS、家庭のルーターなど、複数のネットワーク層が同時に存在しうる。パブリック IP の問い合わせで確認できるのは、最終的に外部へ見えているアドレスだけであり、その結果だけから各層でどのような NAT ルールが使われたかを断定することはできない。

## 8. ポートマッピング：macOS からコンテナにアクセスする

Nginx コンテナを作成し、サービスをローカルのループバックアドレスにだけ公開した。

```bash
docker run -d --rm \
  --name web-lab \
  --network lab-net \
  -p 127.0.0.1:8080:80 \
  nginx:alpine
```

`127.0.0.1:8080:80` の意味は次のとおり。

| 部分 | 意味 |
|---|---|
| `127.0.0.1` | ホスト環境のループバックアドレスにのみバインドする |
| `8080` | 公開するポート |
| `80` | コンテナ内のサービスのポート |

macOS からアクセスし、公開の対応関係を確認する。

```bash
curl http://localhost:8080
docker port web-lab
```

次のような出力が得られる。

```text
80/tcp -> 127.0.0.1:8080
```

一方、同じ `lab-net` にいる `net-a` は、コンテナ名と内部ポートで直接アクセスできる。

```bash
docker exec net-a wget -qO- http://web-lab:80
```

2 つの経路は同じではない。

| アクセス元 | リクエスト先 | 今回の `-p` に依存するか |
|---|---|---|
| macOS | `localhost:8080` | する |
| `net-a` | `web-lab:80` | しない |

通常の Linux 上の Docker ホストでは、インバウンドのポートマッピングは DNAT などの仕組みによって実現されることが多い。OrbStack では、さらに macOS と Linux の仮想環境との間のポート転送も処理する必要がある。

最後に、アクセスログを確認して実験用コンテナを停止する。

```bash
docker logs web-lab
docker stop web-lab
```

作成時に `--rm` を指定しているので、コンテナは停止後に自動で削除される。

## 9. 目的からコマンドを逆算する

一連の実験を経て、私はまず「どの種類のリソースを変えたいのか」を問い、それから具体的なオプションを探すようにしている。

| 目的 | Docker の機能 | よく使うコマンド |
|---|---|---|
| ホストのディレクトリにアクセスする | バインドマウント | `docker run --mount` |
| アプリケーションのデータを永続化する | ボリューム | `docker volume`、`--mount` |
| 仮想 LAN を作る | ブリッジネットワーク | `docker network create` |
| コンテナをネットワークに接続する | ネットワークへの接続 | `docker run --network`、`docker network connect` |
| ネットワーク設定を確認する | Inspect | `docker network inspect` |
| コンテナからインターネットにアクセスする | ルーティング + SNAT | 通常のブリッジネットワークが自動で設定することが多い |
| 外部からコンテナのサービスにアクセスする | ポートマッピング | `docker run -p` |
| コンテナとメインプロセスを作成する | Run | `docker run` |
| 既存のコンテナでプログラムを実行する | Exec | `docker exec` |
| 状態と出力を確認する | 観察 | `docker ps`、`docker logs`、`docker inspect` |

すべての実験が終わったら、今回作成したコンテナとネットワークを片付ける。

```bash
docker stop net-a net-b net-c
docker rm net-a net-b net-c
docker network rm lab-net
```

名前付きボリューム `mydata` は、コンテナとは独立して残るように設計されている。中のデータが不要だと確認できてから `docker volume rm mydata` で削除すべきであり、ボリュームの削除をコンテナ片付けの既定の手順にするべきではない。

## 10. 最終的に得られた理解

Docker はファイルシステムや TCP/IP を独自に実装し直しているのではなく、Linux に既存の仕組みを組み合わせて、管理しやすいコンテナ環境を作っている。

```text
Linux Kernel
     │
     ├── Namespace：PID、マウント、ネットワークなどのリソースのビューを隔離する
     ├── Cgroup：プロセスグループのリソース使用を制限・計測・スケジューリングする
     ├── ファイルシステム：OverlayFS、バインドマウント、ボリューム、tmpfs
     └── ネットワーク：veth、ブリッジ、ルーティング、NAT、ポートマッピング
```

今回の実践で、抽象的な概念を具体的な振る舞いに対応づけることができた。

- バインドマウントは、コンテナがデータをコピーしなくても、新しいパスを通じて共有ファイルにアクセスできることを示している。
- ルートディレクトリと `/proc` のマウント結果は、コンテナのディレクトリツリーが異なるファイルシステムの組み合わせでできていることを示している。
- ping と Netcat は、隔離された Network Namespace 同士を仮想デバイスでつなぎ直せることを示している。
- インターネットへのアクセスは、プライベート IP でもルーティングとアドレス変換によって外部ネットワークに到達できることを示している。
- ポートマッピングは、コンテナが外部から直接到達できるアドレスを持たなくても、外部から開始された接続を受け付けられることを示している。

最終的に私がたどり着いた学び方は、**まずコンテナにどのような環境を持たせたいのかを明確にし、Docker での設定方法を探し、コンテナの内外から結果を観察し、最後にその現象を下層の仕組みに還元する**ことである。

Docker のコマンドをすべて暗記する必要はない。自分がファイルシステム、ネットワーク、プロセスのどれを設定しているのかを把握し、具体的なオプションは `--help` で調べるほうが、丸暗記よりもたいてい確実である。
