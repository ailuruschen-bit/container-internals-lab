# Docker イメージのビルド実践（4）：Java とマルチステージビルド

[English](../12-docker-image-build-java-multistage.md) | [中文](../zh/12-docker-image-build-java-multistage.md) | **日本語**

今回の実験では Java アプリケーションのイメージのビルドに取り組んだ。まず 1 つのイメージの中でコンパイルと実行を行い、次にマルチステージビルドでビルド環境と実行環境を分け、さらに `ENTRYPOINT` による引数の受け渡しを観察し、最後に同じ方法を最小構成の Spring Boot プロジェクトに適用した。

今回の最もわかりやすい変化は、Dockerfile が出来上がったプログラムをコピーするだけでなく、本格的なビルド作業を担うようになったことである。

## 1. シングルステージビルド：イメージの中で Java をコンパイルする

最初のバージョンでは JDK を含むベースイメージを使い、ソースのコンパイル、JAR のパッケージ化、プログラムの実行を同じビルドステージで行う。

Dockerfile は次のように簡略化できる。

```dockerfile
FROM eclipse-temurin:21-jdk

WORKDIR /app

COPY Hello.java .

RUN javac Hello.java && \
    jar --create \
        --file app.jar \
        --main-class Hello \
        Hello.class

CMD ["java", "-jar", "app.jar"]
```

ここで初めて `RUN` を実際に使った。

### `RUN` はビルド時に実行される

```dockerfile
RUN javac Hello.java && \
    jar --create --file app.jar --main-class Hello Hello.class
```

ビルダーはビルドの過程で `javac` と `jar` を実行する。

```text
Hello.java
    │ javac
    ▼
Hello.class
    │ jar
    ▼
app.jar
```

コマンドの実行が終わると、生成された `Hello.class` と `app.jar` は、このビルドステージのファイルシステムの結果の一部になる。

これは以前学んだ `CMD` とは異なる。

```text
RUN  ──→ docker build 時に実行され、イメージの内容を生成する
CMD  ──→ docker run 時に実行され、コンテナのプロセスを起動する
```

したがって、イメージのビルド中に Java プログラムが `CMD` によって起動されることはない。後でコンテナを作成するときに初めて、次のコマンドが実行される。

```text
java -jar app.jar
```

### ビルドして実行する

```bash
docker build -t java-lab:v1 .
docker run --rm java-lab:v1
```

このバージョンはすでに正常に動作するが、最終的なイメージは依然として完全な JDK のイメージをベースにしている。JDK にはコンパイラやその他の開発ツールが含まれているが、コンパイル済みの JAR を実行するだけのコンテナでは、通常それらのビルドツールを残しておく必要はない。

手元の実験では、`java-lab:v1` のサイズはおよそ次のとおりだった。

```text
746 MB
```

ここから次の問いが生まれる。ビルド時には JDK を使いつつ、最終的なイメージには Java プログラムの実行に必要な環境だけを残すことはできないだろうか。

## 2. マルチステージビルド：コンパイルと実行を分ける

マルチステージビルドでは、1 つの Dockerfile に複数の `FROM` を書ける。`FROM` のたびに新しいビルドステージが始まる。

今回の実験は次のように書ける。

```dockerfile
FROM eclipse-temurin:21-jdk AS builder

WORKDIR /app
COPY Hello.java .

RUN javac Hello.java && \
    jar --create \
        --file app.jar \
        --main-class Hello \
        Hello.class


FROM eclipse-temurin:21-jre

WORKDIR /app

COPY --from=builder /app/app.jar .

CMD ["java", "-jar", "app.jar"]
```

この Dockerfile には 2 つのステージがある。

```text
builder ステージ               最終の実行ステージ
JDK                            JRE
  │                              │
  ├── COPY Hello.java            │
  ├── javac                      │
  └── jar → app.jar ─────────────┤ COPY --from=builder
                                 │
                                 └── CMD java -jar app.jar
```

### `FROM ... AS` が名前を付けるのはビルドステージ

```dockerfile
FROM eclipse-temurin:21-jdk AS builder
```

`builder` はこのビルドステージの名前であり、特定のイメージレイヤーの別名ではない。

1 つのステージの中でも `COPY` や `RUN` などの複数の手順によって複数のレイヤーが生まれうる。`AS builder` はそのビルド環境全体に名前を付け、後続のステージから参照しやすくするものである。

名前を付けなくても番号でステージを参照できるが、名前なら Dockerfile に新しいステージを挿入しても変わらず、意味もより明確になる。

## 3. `COPY --from` で必要なビルド成果物だけを取り出す

2 つ目のステージでは次を実行する。

```dockerfile
COPY --from=builder /app/app.jar .
```

ここには 2 つの異なるファイルシステムのビューが関わっている。

- `/app/app.jar` は `builder` ステージ内のコピー元のファイルである。
- コピー先の `.` は現在の JRE 実行ステージに属し、現在の `WORKDIR /app` を基準に解決される。

結果は次のようになる。

```text
builder:/app/app.jar
          │
          │ COPY --from=builder
          ▼
final:/app/app.jar
```

コンパイルステージにある `Hello.java`、`Hello.class`、`javac` やその他の JDK のツールが、この `COPY` によって一緒に最終イメージへ入ることはない。明示的にコピーした `app.jar` だけが実行ステージに持ち込まれる。

通常の `COPY` との違いは次のとおりである。

```text
COPY source target
    source はビルドコンテキストから取られる

COPY --from=builder source target
    source は別のビルドステージのファイルシステムから取られる
```

## 4. 最終イメージに実際に残るもの

Dockerfile 全体をデフォルトでビルドすると、最後のステージが最終的なイメージとして出力される。

ここではステージとレイヤーを区別する必要があり、「最後のレイヤーだけが残る」と単純に言うことはできない。

```text
最終イメージ
    ├── 最後のステージのベースイメージのレイヤー
    ├── WORKDIR などの実行設定
    ├── COPY で取り込んだ app.jar のレイヤー
    └── CMD などのイメージ設定
```

最終的な実行イメージにも、最後のステージ自身が必要とする複数のレイヤーが含まれている。最終イメージに入らないのは、前の `builder` ステージのファイルシステム全体とビルドツールである。

つまりマルチステージビルドは、それまでの内容をすべてまとめてから JDK を削除するのではなく、より軽いランタイムのベースイメージから改めて始め、必要な成果物だけをコピーしてくるものである。

```text
シングルステージ
JDK + ソース + class + JAR + ビルドツール

マルチステージの最終結果
JRE + JAR
```

手元の実験では、2 つのイメージのサイズは次のとおりだった。

```text
java-lab:v1    約 746 MB
java-lab:v2    約 475 MB
```

サイズが減ったのは JAR が小さくなったからではなく、最終イメージが JRE をベースにするようになり、完全な JDK のビルド環境を持たなくなったからである。

## 5. 前のステージはイメージに入らないが、キャッシュは残るのか

前の `builder` ステージ全体が最終イメージのファイルシステムになることはないが、毎回のビルドで必ずゼロからコンパイルし直さなければならないわけではない。

BuildKit は、前のステージの各手順のビルド結果をキャッシュに保存できる。Dockerfile、ソースコード、関連する入力が変わっていなければ、次にビルドしたときにも次の表示が見られることがある。

```text
CACHED
```

したがって、次の 2 つは同時に成り立つ。

```text
builder ステージは最終の実行イメージに入らない
                ＋
builder ステージのビルド結果はキャッシュとして再利用できる
```

ただし、キャッシュは最終イメージに恒久的に付属するものではない。現在のビルダーのキャッシュ管理の範囲に属しており、手動での削除、容量の回収、ビルダーの切り替え、マシンの変更などによって失われることがある。別のマシンや CI 環境で再利用したい場合は、ビルドキャッシュのインポート、エクスポート、共有を別途行う必要がある。

したがって、より正確には「前のステージのキャッシュは決して失われない」ではなく、次のように言うべきである。

> 前のステージは最終イメージに入らないが、関連するビルドキャッシュが利用できる限り、後続のビルドの高速化に役立ち続ける。

## 6. `history` で最終イメージを確認する

マルチステージ版をビルドする。

```bash
docker build \
  --progress=plain \
  -t java-lab:v2 .
```

続いて、最終イメージの履歴を確認する。

```bash
docker image history java-lab:v2
```

手元の結果の上部には、主に次の内容が表示された。

```text
CMD ["java" "-jar" "app.jar"]
COPY /app/app.jar .
WORKDIR /app
```

その下にあるのは最終の JRE ベースイメージの履歴であり、JDK のコンパイルステージで `javac` や `jar` が生成したファイルのレイヤーではない。

これは、最終イメージがたしかに最後の実行ステージから作られ、`builder` ステージが出力した `app.jar` だけを受け取っていることを示している。

最終的な起動コマンドも確認できる。

```bash
docker image inspect java-lab:v2 \
  -f '{{json .Config.Cmd}}'
```

出力：

```json
["java","-jar","app.jar"]
```

コンテナを実行する。

```bash
docker run --rm java-lab:v2
```

コンテナはビルド済みの JAR を実行するだけでよく、起動時にソースを再コンパイルする必要はない。

## 7. 現時点での理解

今では、Java アプリケーションのイメージのビルドを 2 つの役割として捉えている。

```text
Build Stage
    ├── JDK が必要
    ├── ソースをコンパイルする
    └── JAR を生成する
             │
             │ ビルド成果物だけを渡す
             ▼
Runtime Stage
    ├── JRE だけが必要
    ├── JAR を保持する
    └── Java のプロセスを起動する
```

シングルステージの Dockerfile は短く書けるが、「プログラムを生成する」ためのツールと「プログラムを実行する」ためのツールを、すべて同じ最終イメージに入れてしまう。

マルチステージビルドでは、ステージごとに異なるベースイメージを使える。ビルドステージには完全なツールチェーンを備えた JDK を選び、実行ステージにはプログラムの実行に必要なものだけを満たす JRE を選び、`COPY --from` で成果物を明示的に受け渡す。

その価値はイメージが小さくなることだけではない。最終的な実行環境が本当に必要なものに近づき、ソースコード、コンパイラ、ビルド時にしか使わないファイルを含まなくなることで、ビルドの責務と実行の責務がよりはっきり分かれる。

これを土台にして、Java の起動コマンドを `CMD` から `ENTRYPOINT` に移し、実際の Spring Boot プロジェクトのビルドにも取り組んだ。

## 8. `ENTRYPOINT` で Java の起動プログラムを固定する

前のバージョンでは次のように書いていた。

```dockerfile
CMD ["java", "-jar", "app.jar"]
```

イメージ名の後に引数を続けて渡すと、それらの引数は `CMD` 全体を置き換える。

```bash
docker run --rm java-lab:v2 hello
```

このとき Docker は、`hello` を `app.jar` に渡すのではなく、コンテナで実行するプログラムとして扱おうとする。

Java プログラム自体を固定しつつ引数は変えられるようにするには、Dockerfile を次のように変更する。

```dockerfile
ENTRYPOINT ["java", "-jar", "app.jar"]
CMD ["default"]
```

両方を Exec 形式で書いた場合、デフォルトの起動コマンドは次のように理解できる。

```text
ENTRYPOINT                         CMD
["java", "-jar", "app.jar"]  +  ["default"]
                    │
                    ▼
java -jar app.jar default
```

`ENTRYPOINT` は実行するプログラムを固定し、ここでの `CMD` はデフォルトの引数を提供する。

### デフォルトの引数を使う

```bash
docker run --rm java-lab:v3
```

手元の実験での出力：

```text
Java application started!
Argument: default
```

### `docker run` で新しい引数を渡す

```bash
docker run --rm java-lab:v3 hello
```

出力は次のように変わる。

```text
Java application started!
Argument: hello
```

イメージ名の後の `hello` が `CMD ["default"]` を置き換え、さらに引数として元の Exec 形式の `ENTRYPOINT` に追加される。

```text
java -jar app.jar hello
```

したがって、より正確には「`docker run` の後ろの内容は常に Java に渡される」ではなく、次のように言うべきである。

> イメージに Exec 形式の `ENTRYPOINT` がある場合、実行コマンドで指定した引数は `CMD` を置き換え、引数として `ENTRYPOINT` に渡される。

## 9. `--entrypoint` でデフォルトのエントリーポイントを上書きする

デフォルトのプログラムそのものも置き換えたい場合は、次のようにする。

```bash
docker run --rm \
  --entrypoint java \
  java-lab:v3 \
  -version
```

今回コンテナが実行するのは次のコマンドである。

```text
java -version
```

次のコマンドではない。

```text
java -jar app.jar default
```

`--entrypoint java` は、今回作成するコンテナについてイメージの `ENTRYPOINT` を上書きする。このオプションを使うと、イメージに元からある `CMD` は自動的には追加されなくなり、イメージ名の後の `-version` が新しいエントリーポイントの引数になる。

ここでの「上書き」と「CMD の除外」は、どちらも今回のコンテナの設定の中で起きていることであり、元の `java-lab:v3` イメージを変更するわけではない。次にこのイメージを通常どおり実行すれば、元の `ENTRYPOINT` と `CMD` が使われる。

イメージに保存されている 2 つの設定を確認するには、次を実行する。

```bash
docker image inspect java-lab:v3 \
  -f 'Entrypoint={{json .Config.Entrypoint}} Cmd={{json .Config.Cmd}}'
```

手元での結果は次のとおりである。

```text
Entrypoint=["java","-jar","app.jar"] Cmd=["default"]
```

### `CMD` と `ENTRYPOINT` についての現時点での理解

| 設定 | 今回の実験での役割 |
|---|---|
| `ENTRYPOINT` | コンテナが主に実行する Java プログラムを固定する |
| `CMD` | エントリーポイントのプログラムに、置き換え可能なデフォルトの引数を提供する |
| イメージ名の後の引数 | `CMD` を置き換え、デフォルトの `ENTRYPOINT` に渡される |
| `--entrypoint` | 今回のコンテナについてデフォルトのエントリーポイントを置き換え、元の `CMD` は自動的には使われなくなる |

この組み合わせは、「プログラムは固定で、引数がよく変わる」イメージに向いている。すべてのイメージで `ENTRYPOINT` と `CMD` を両方使わなければならないという意味ではなく、利用者にプログラムを上書きさせたいのか、引数だけを上書きさせたいのかに応じて決める必要がある。

## 10. マルチステージビルドを Spring Boot に適用する

通常の Java の JAR での実験を終えた後、最小構成の Spring Boot プロジェクトもビルドした。

Spring Boot プロジェクトでは `javac` や `jar` を直接呼び出すのではなく、Maven が依存関係を解決し、ソースをコンパイルし、ビルドのライフサイクルを実行して実行可能 JAR を生成する。そのため、ビルドステージには Maven と JDK を含むベースイメージを選び、実行ステージは引き続き JRE だけを使う。

プロジェクトの構成は次のように簡略化できる。

```text
spring-lab/
├── Dockerfile
├── pom.xml
└── src/
    └── main/
        ├── java/
        └── resources/
```

対応する Dockerfile はおおよそ次のとおりである。

```dockerfile
FROM maven:3.9-eclipse-temurin-21 AS build

WORKDIR /build

COPY pom.xml .
RUN mvn dependency:go-offline

COPY src ./src
RUN mvn clean package -DskipTests


FROM eclipse-temurin:21-jre

WORKDIR /app

COPY --from=build /build/target/*.jar app.jar

EXPOSE 8080

ENTRYPOINT ["java", "-jar", "app.jar"]
```

これも、すでに理解している 2 つのステージと同じ構造である。

```text
Maven + JDK Build Stage
    ├── pom.xml を読み込む
    ├── 依存関係をダウンロードする
    ├── Spring Boot プロジェクトをコンパイルする
    └── target/*.jar
                 │
                 │ COPY --from=build
                 ▼
JRE Runtime Stage
    ├── app.jar
    └── java -jar app.jar
```

## 11. この Spring Boot プロジェクトで Maven が担うもの

今回の Spring Boot プロジェクトは Maven を採用しているので、ビルドの過程は Maven に大きく依存している。しかし、それは Spring Boot が Maven でしか使えないという意味ではない。Spring Boot は Gradle にも対応している。より正確には、**`pom.xml` で管理されているこのプロジェクトでは、依存関係の解決とビルドの流れを Maven が動かしている**、と言うべきである。

Maven は `pom.xml` を読み込んだ後、まずそのプロジェクトで実際に有効になるビルドモデルを組み立てる。その主な出どころは次のとおりである。

- プロジェクト自身が宣言している座標、プロパティ、ビルド設定。
- `<parent>` によって継承する親 POM の設定。
- `<dependencyManagement>` が提供する依存関係のバージョン管理。
- `<dependencies>` で宣言した直接の依存関係と、その推移的な依存関係。
- `<build>` で設定した、あるいは継承によって得た Maven プラグイン。

プロジェクトが `spring-boot-starter-parent` を継承していれば、Spring Boot が用意した一連のデフォルト設定を得られる。たとえば、管理された主要な依存関係のバージョン、コンパイルの設定、一部のプラグインの設定などである。ここでの「継承」は Java のクラス継承ではなく、Maven が POM を解決する際に親子の設定を合成して Effective POM を作ることを指す。

`<dependencies>` で宣言するものも、一律に「モジュール」と呼ぶのは適切ではない。Maven が扱うのはアーティファクトであり、よくある形式は JAR である。直接の依存関係がさらに別のアーティファクトを参照していることもあり、Maven は推移的な依存関係、バージョン管理、スコープなどのルールに従って、最終的に使う依存関係の集合を計算する。

解決の際、Maven は通常まずローカルリポジトリを確認し、ローカルにない POM、JAR、プラグインを、設定されたリモートリポジトリからダウンロードする。デフォルトのローカルリポジトリの場所は次のとおりである。

```text
~/.m2/repository
```

つまり Maven は「ライブラリをダウンロードする」だけではなく、POM を解釈し、依存関係を解決し、ライフサイクルとプラグインを実行して、最終的にビルド成果物を生成する役割も担っている。

Spring Boot プロジェクトでは、`mvn package` は `package` フェーズまでに必要な Maven のライフサイクルの手順を実行する。Spring Boot Maven Plugin を設定していれば、さらに通常の JAR を、次のコマンドで起動できる実行可能 JAR へと再構成する。

```text
java -jar app.jar
```

## 12. Maven のビルドステージの 2 つの書き方

この実験を終えて、ビルドステージには少なくとも 2 つの一般的な書き方があることに気づいた。どちらも最終的には Maven でプロジェクトをビルドするが、違いは Maven をベースイメージが提供するのか、プロジェクト自身の Wrapper が起動するのかにある。

### 方法 1：Maven を含むベースイメージを使う

今回の実験では次を採用している。

```dockerfile
FROM maven:3.9-eclipse-temurin-21 AS build
```

このベースイメージには Maven と JDK がすでに用意されているので、そのまま次を実行できる。

```dockerfile
RUN mvn dependency:go-offline
RUN mvn clean package -DskipTests
```

ここで呼び出しているのは、ベースイメージにインストールされたグローバルな `mvn` である。この方法の利点は Dockerfile が単純で、Wrapper を起動する必要がないことであり、Maven のバージョンは選んだ Maven イメージのタグによって決まる。

この方法でも、ベースイメージのバージョンは明示すべきである。変化し続ける大まかなタグを使っていると、別の時期に再ビルドしたときに、実際の Maven や JDK のバージョンが変わっている可能性がある。

### 方法 2：JDK のイメージで Maven Wrapper を使う

Maven Wrapper を有効にしたプロジェクトには、通常次のファイルが含まれている。

```text
spring-lab/
├── .mvn/
│   └── wrapper/
│       └── maven-wrapper.properties
├── mvnw
├── mvnw.cmd
├── pom.xml
└── src/
```

それぞれの役割は次のとおり。

- `mvnw` は Linux と macOS で使う起動スクリプトである。
- `mvnw.cmd` は Windows で使う起動スクリプトである。
- `.mvn/wrapper/maven-wrapper.properties` は、Wrapper が使う Maven のディストリビューションやダウンロード先などの設定を記録している。
- Wrapper の種類によっては、`maven-wrapper.jar` やダウンローダーのソースコードが含まれることもある。

Wrapper は、Maven のインストールパッケージ一式をそのままプロジェクトにコミットするものではない。`./mvnw` を実行すると、プロジェクト内の Wrapper の設定に従って指定されたバージョンを探し、ローカルになければその Maven ディストリビューションをダウンロードして使う。

マルチステージの Dockerfile では、通常の JDK イメージを使う形に変えられる。

```dockerfile
FROM eclipse-temurin:21-jdk AS build

WORKDIR /build

COPY .mvn .mvn
COPY --chmod=0755 mvnw .
COPY pom.xml .

RUN ./mvnw dependency:go-offline

COPY src ./src
RUN ./mvnw clean package -DskipTests


FROM eclipse-temurin:21-jre

WORKDIR /app
COPY --from=build /build/target/*.jar app.jar

EXPOSE 8080
ENTRYPOINT ["java", "-jar", "app.jar"]
```

ここではグローバルな `mvn` は実行せず、常にプロジェクトが提供する `./mvnw` を実行している。こうすれば、開発マシン、CI、Docker のビルドステージのいずれも、同じ Wrapper の設定に従って指定されたバージョンの Maven を使える。

`mvnw` には実行権限が必要である。この例では `COPY --chmod=0755` でコピー時に権限を設定しているが、リポジトリ側で実行ビットを正しく保存しておく方法もある。

Wrapper の初回実行では、通常 Maven ディストリビューションをダウンロードするためのネットワーク接続も必要になる。Wrapper の種類によって、Wrapper の JAR、Java のダウンローダー、あるいは `curl` や `wget` などのツールに依存することがあるため、どんな軽量 JDK イメージでも起動に必要な条件がそろっているとは限らない。ベースイメージを選んだら、Wrapper が正常に起動するかを実際に確認する必要がある。

### Maven ディストリビューションとプロジェクトの依存関係は別のキャッシュディレクトリ

Wrapper がダウンロードした Maven ディストリビューション自体は、デフォルトで次の場所に保存される。

```text
~/.m2/wrapper/dists
```

Maven が解決したプロジェクトの依存関係とプラグインは、デフォルトで次の場所に保存される。

```text
~/.m2/repository
```

Docker のビルドステージがデフォルトで root ユーザーとして実行される場合、`~` は通常 `/root` なので、対応するパスは次のようになる。

```text
/root/.m2/wrapper/dists
/root/.m2/repository
```

ベースイメージがビルド用のユーザーを切り替えていたり、Maven のパラメーターでローカルリポジトリの場所を変更していたりすれば、実際のパスも変わる。したがって `~/.m2/repository` はデフォルトの取り決めであって、変更できない固定のアドレスではない。

ベースイメージのグローバルな Maven を使う場合も、Maven Wrapper を使う場合も、依存関係の解決後は Maven のローカルリポジトリの仕組みを使う。主な違いは「Maven 自体を誰が決め、誰が提供するか」にある。

| ビルド方法 | Maven の出どころ | Maven のバージョンを主に決めるもの |
|---|---|---|
| Maven のベースイメージ | イメージにインストール済み | ベースイメージのタグ |
| Maven Wrapper | Wrapper がプロジェクトの設定に従って取得 | `.mvn/wrapper` の設定 |

どちらの方法もビルドステージでのみ使う。最終ステージでは JAR だけをコピーするので、Maven、Wrapper、`~/.m2`、プロジェクトのソースコードが最終的な JRE のイメージに入ることはない。

## 13. なぜ先に `pom.xml` をコピーし、その後でソースをコピーするのか

ビルドステージでは、最初から `COPY . .` を実行するのではなく、まず依存関係の定義ファイルを処理している。

```dockerfile
COPY pom.xml .
RUN mvn dependency:go-offline
```

その後で、頻繁に変更するソースコードをコピーする。

```dockerfile
COPY src ./src
RUN mvn clean package -DskipTests
```

これは前回のビルドキャッシュの実験と直接対応している。

`pom.xml` が変わらずに Java のソースだけが変わった場合、依存関係をダウンロードするビルド手順はキャッシュにヒットする可能性があり、ソースを再コピーしてパッケージ化する手順だけを実行し直せばよい。最初にプロジェクト全体をコピーしていると、ソースが少しでも変われば前の `COPY` の結果が変わり、その後の依存関係のダウンロード手順もキャッシュを失いやすくなる。

2 つの順序は次のように比較できる。

```text
キャッシュを再利用しにくい      キャッシュを再利用しやすい

COPY . .                       COPY pom.xml .
RUN mvn package                RUN mvn dependency:go-offline
                               COPY src ./src
                               RUN mvn package
```

`dependency:go-offline` は、プロジェクトの依存関係と関連するプラグインを事前に解決してダウンロードし、それらをこのビルド手順のキャッシュ結果に含める。これは最終的に実行するコンテナがオフラインでなければならないという意味ではなく、完全な Maven 環境を最終的な JRE のイメージにコピーするわけでもない。

`pom.xml` が変われば依存関係の入力そのものが変わっているので、依存関係のダウンロードを再実行するのは妥当なキャッシュの無効化である。

## 14. Spring Boot の最終イメージに残るもの

2 つ目の `FROM` で、まったく新しい実行ステージが始まる。

```dockerfile
FROM eclipse-temurin:21-jre
```

最終イメージに、Maven、プロジェクトのソースコード、ビルドステージのローカル依存リポジトリがまとめて含まれることはない。ここでは前のステージからパッケージ化された成果物だけをコピーしている。

```dockerfile
COPY --from=build /build/target/*.jar app.jar
```

手元の `spring-lab:v1` の最終的な履歴の上部には、主に次の内容が含まれていた。

```text
COPY /build/target/*.jar app.jar
EXPOSE 8080
ENTRYPOINT ["java" "-jar" "app.jar"]
```

最終イメージにコピーされた JAR は約 23 MB で、`spring-lab:v1` のイメージ全体は約 519 MB だった。イメージのサイズの大部分は JRE のベースイメージによるもので、Maven のビルドステージによるものではない。

`EXPOSE 8080` は、このイメージがコンテナの `8080/tcp` でサービスを提供する想定であることを示しているだけで、ポートを自動的にホストへ公開するわけではない。実行時には、やはり明示的に設定する必要がある。

```bash
docker run -d \
  --name springboot1 \
  -p 127.0.0.1:8081:8080 \
  spring-lab:v1
```

ここでは macOS の `8081` をコンテナの `8080` に公開している。ログを確認する。

```bash
docker logs springboot1
```

手元のログでは、組み込みの Tomcat がコンテナの 8080 番ポートで起動したことが確認できた。ルートパスにアクセスすると、

```bash
curl -i http://localhost:8081
```

現在の最小構成のプロジェクトは HTTP 404 を返した。これはコンテナや Tomcat の起動に失敗したことを示しているのではなく、アプリケーションがまだ `/` に対応する Controller を定義していないだけである。ポートの疎通と Spring Boot の起動そのものは確認できている。

## 15. 今回の Java ビルド実験で得られた全体像

最初のシングルステージの Java のビルドから、Spring Boot の Maven マルチステージビルドまで、今回の実験では 4 つの役割を段階的に切り分けてきた。

```text
Build Tool
    Maven / javac / jar
            │
            ▼
Build Artifact
    実行可能 JAR
            │
            │ COPY --from
            ▼
Runtime Environment
    JRE
            │
            ▼
Container Process
    ENTRYPOINT + CMD または実行時の引数
```

Java アプリケーションのコンテナ化についての今の理解は、「Java プロジェクトのディレクトリをイメージにコピーして起動する」ことではなく、次のとおりである。

1. Maven + JDK のベースイメージ、または JDK + Maven Wrapper のビルドステージで、ビルド済みの JAR を生成する。
2. JAR だけを、より軽量な実行ステージに渡す。
3. `ENTRYPOINT` で Java の起動プログラムを固定し、必要に応じて `CMD` でデフォルトの引数を提供する。
4. Dockerfile の順序を工夫し、Maven の依存関係のダウンロードでできるだけビルドキャッシュを再利用する。
5. 実行時のポート公開と外部の設定によって、同じイメージを異なる環境で使う。

マルチステージビルドが解決するのはビルド環境と実行環境の分離であり、`ENTRYPOINT` と `CMD` が解決するのはコンテナのメインプログラムとデフォルトの引数の組み合わせであり、Maven の依存関係の手順の順序は繰り返しのビルドの効率に影響する。これらを組み合わせて初めて、比較的完成された Java アプリケーションのイメージのビルド過程になる。
