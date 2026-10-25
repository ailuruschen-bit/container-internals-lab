# 環境変数は誰のものか

[中文](../environment-variables-belong-to-processes.md) | [English](../en/environment-variables-belong-to-processes.md) | **日本語**

シェルでは `NAME=Tom` と書くことも、`export NAME=Tom` と書くこともできる。同じ内容を `.bashrc` や `/etc/environment` に書くこともできる。どれも「変数を設定している」ように見えるが、変数を誰が保持するのか、子プロセスに引き継がれるのか、いつ消えるのかはそれぞれ異なる。

## シェル変数と環境変数

まず Bash で通常の変数を設定してみる。

```bash
NAME=Tom
echo "$NAME"
bash -c 'echo "child: ${NAME:-<empty>}"'
```

現在のシェルでは `NAME` を参照できるが、新たに起動した Bash は `<empty>` を出力する。この時点の `NAME` は、現在のシェルが管理しているだけの変数にすぎない。

`export` で環境変数としてマークしてから、もう一度試す。

```bash
export NAME=Tom
bash -c 'echo "child: $NAME"'
```

今度は子プロセスが `child: Tom` を出力する。両者の本質的な違いは次のとおりである。

- シェル変数は、現在のシェルの内部状態にのみ存在する。
- エクスポートされた環境変数は、シェルが外部コマンドを起動する際に子プロセスへ渡される。
- 子プロセスが受け取るのは環境のコピーである。子プロセスが自身の変数を変更しても、親プロセスには反映されない。

1 つのコマンドだけで変数を使いたい場合は、次のように書ける。

```bash
APP_ENV=test bash -c 'echo "$APP_ENV"'
```

この代入はそのコマンドの環境にだけ渡され、現在のシェルには残らない。

## 環境変数はシェル固有の機能ではない

環境変数の実体は**プロセスの環境データ**である。Linux が新しいプログラムを起動するとき、呼び出し元は `execve()` の `envp` 引数で `KEY=VALUE` 形式の文字列の並びを渡し、カーネルがそれを新しいプログラムに引き渡す。シェルは代入、`export`、変数展開といった便利な構文を提供し、コマンドの起動時に渡す環境を組み立てているにすぎない。

したがって、シェルを介さずにプロセスの環境を設定することもできる。Java、Python、systemd、コンテナランタイムには、それぞれ独自の設定方法がある。逆に、シェルの配列や関数といった内部データは、通常は環境変数として子プロセスに渡せない。

```text
現在のシェル
    │ コマンドを起動し、エクスポート済みの変数をコピー
    ▼
子プロセス
    │ さらにコマンドを起動
    ▼
孫プロセス
```

## 一時的な設定と永続的な設定

ターミナルで `export JAVA_HOME=...` を実行して変更されるのは、現在のシェルプロセスである。そのシェルを閉じれば、変数も消える。

いわゆる「永続的な設定」とは、カーネルが変数を恒久的に保存していることではなく、変数を設定するテキストをディスク上に残しておくことである。以降、ログインセッションやシェルが作成されるたびに、関係するプログラムが設定ファイルを読み直し、新しいプロセスの環境をあらためて設定する。

主なファイルと、Bash における典型的な読み込み条件は次のとおり。

| ファイル | 適用範囲 | 典型的な読み込み条件 |
|---|---|---|
| `~/.bashrc` | 現在のユーザー | 対話型・非ログインの Bash |
| `~/.bash_profile` | 現在のユーザー | ログイン Bash。通常はこの中から `~/.bashrc` を明示的に読み込む |
| `~/.profile` | 現在のユーザー | ログインシェル共通の設定。Bash は `~/.bash_profile` と `~/.bash_login` がどちらも存在しない場合にのみ読み込む |
| `/etc/profile` | 全ユーザー | ログインシェルのシステム全体の初期化 |
| `/etc/bash.bashrc` または `/etc/bashrc` | 全ユーザー | 対話型 Bash。ファイル名と読み込み方法はディストリビューションによって異なる |
| `/etc/environment` | システムのログイン環境 | 通常は PAM などのログイン関連コンポーネントが読み込む。Bash スクリプトではない |

ログインシェルとして起動した Bash は、`~/.bash_profile`、`~/.bash_login`、`~/.profile` の順に探し、最初に見つかった 1 つだけを読み込む。そのため、`~/.bash_profile` に次の記述を加えるのが一般的である。

```bash
if [ -f "$HOME/.bashrc" ]; then
    . "$HOME/.bashrc"
fi
```

`/etc/environment` はシェルスクリプトとして書くべきではない。通常は単純なキーと値のペアだけを記述し、`export`、コマンド置換、複雑な変数展開は使わない。

```text
JAVA_HOME=/usr/lib/jvm/java-21
APP_ENV=prod
```

読み込みの流れは、ディストリビューション、ログイン方法、サービスマネージャーによって異なることがある。たとえば systemd のサービスは、あるユーザーが `.bashrc` に変数を書いたからといって、その変数を自動的に受け取るわけではない。あるファイルが効いているかどうかを判断するには、まず対象のプロセスを誰が起動し、その起動過程でどの設定が読み込まれるのかを確認する必要がある。

## 本番用スクリプト：共通設定の再利用と環境の選択

次のディレクトリ構成では、共通の変数と環境ごとの差分を分けている。

```text
demo/
├── config/
│   ├── common.env
│   ├── dev.env
│   ├── test.env
│   └── prod.env
└── run.sh
```

`config/common.env`：

```bash
export APP_NAME=my-service
export LOG_LEVEL=info
```

`config/dev.env`：

```bash
export API_BASE_URL=http://localhost:8080
```

`config/test.env` と `config/prod.env` は同じ変数名を使い、それぞれの環境の URL を設定する。`run.sh` は第 1 引数に応じて設定を選択する。

```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
APP_ENV=${1:-dev}

case "$APP_ENV" in
    dev|test|prod) ;;
    *)
        echo "usage: $0 {dev|test|prod}" >&2
        exit 2
        ;;
esac

source "$SCRIPT_DIR/config/common.env"
source "$SCRIPT_DIR/config/$APP_ENV.env"
export APP_ENV

bash -c 'printf "app=%s env=%s api=%s log=%s\n" \
    "$APP_NAME" "$APP_ENV" "$API_BASE_URL" "$LOG_LEVEL"'
```

`demo` ディレクトリで実行する。

```bash
chmod +x run.sh
./run.sh dev
./run.sh test
```

ここで使っている `source`（`.` とも書ける）は、ファイルを**現在のシェル**で実行する。そのため `run.sh` は、読み込んだ変数をその後で使える。一方、`./config/dev.env` を直接実行すると別のプロセスが作られるため、`run.sh` の変数を変更することはできない。

`source` 自体は変数を自動的にエクスポートしない。上の設定ファイルでは明示的に `export` を使っているので、`bash -c` のような子プロセスがそれらを継承できる。既存の設定ファイルに `KEY=value` しか書かれていない場合は、読み込みの間だけ自動エクスポートを有効にすればよい。

```bash
set -a
source "$SCRIPT_DIR/config/common.env"
set +a
```

`source` で読み込んだファイルは、現在のスクリプトの権限で任意のシェルコマンドを実行できる。信頼できるファイルだけを読み込むべきである。パスワードやトークンなどの機密情報は Git にコミットせず、コマンドライン引数で渡すことも極力避ける。本番環境では、アクセス権限を適切に管理した設定ファイルや、専用のシークレット管理サービスを優先して使うべきである。

## まとめ

通常のシェル変数は現在のシェルに属し、エクスポートされた環境変数は子プロセスにコピーされる。設定ファイルによって変数を「永続化」できるのは、新しいセッションの起動時にそれが読み直されるからであり、Linux が恒久的なグローバル環境を保持しているからではない。実際のスクリプトでは、まず設定を誰が読み込むのかを確認し、そのうえで `source`、`export`、明確な環境選択ルールを用いて変数のスコープを制御する。
