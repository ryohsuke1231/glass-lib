# glass-lib 開発ガイドライン

## 開発対象
- GTK4 / libadwaita アプリの中身の上に、Liquid Glass 風の屈折するガラスを載せる **C (GObject) ライブラリ**と、そのデモアプリ **Glass Gallery**。
- 対象環境: GNOME 50（主）/ GNOME 51（追随）、GTK ≥ 4.22、libadwaita ≥ 1.9、Wayland（X11 でも動くこと）。
- ライブラリは meson でビルドし、GObject Introspection（名前空間 `Glass`、`gi://Glass?version=1`）で GJS・Python から使えるようにする。
- デモは TypeScript → GJS。
- **設計の正は `docs/design.md`。** 作業の前に関係する節を読むこと。設計と違うことをする必要が出たら、実装より先に相談し、合意したら `docs/design.md` を改訂する。
- 基本的には `dev` ブランチで作業する。コード編集などを行う際は、`dev` ブランチにいることを確認すること。`main` はリリース用。

## ディレクトリ
- `lib/`: ライブラリ本体（C）。`render/` と `adaptive/` は非公開。
- `shaders/`: シェーダ。`core/` が単一ソースで、`targets/` がターゲットごとのラッパ。
  - `shaders/reference/glass.frag` は既存拡張の出荷版の**無改変コピー**。編集しない（更新手順は同じディレクトリの README）。
- `spec/`: パラメータ定義（`params.json`）と Adaptive のテストベクタ。
- `demo/`: TypeScript のデモ 2 つ。Glass Gallery（`src/main.ts`・`src/pages/`）と天気アプリ Glass Weather（`src/weather/`）。`data/` はデスクトップファイル・metainfo・アイコン。
- `examples/`: C・Python・GJS の最小の例。`build-aux/flatpak/`: デモの Flatpak マニフェスト。`docs/reference/`: API ドキュメント（gi-docgen）の設定とページ。
- `tests/`、`tools/`: 単体テスト、ゴールデン画像のハーネス、計測スクリプト。
- `spikes/`: Phase 0 の試作（`s1-full-renderer` など）。ライブラリ本体ではない。結果は各 README と `docs/memo.md` に残す。
- `docs/`: 設計書・地雷の記録（`memo.md`）・過去の版（`archive/`）。

（まだ無いディレクトリもある。全体の予定は設計書 §15.1。）

## コマンド実行ルール
- meson のコマンドはリポジトリのルートで実行する（例: `meson setup build`、`meson compile -C build`、`meson test -C build`）。
- **C・GLSL・`meson.build`・`spec/` を変更したら、`meson compile -C build` を実行してビルドエラーが出ないか確認する。** テストがある部分を触ったら `meson test -C build` も実行する。
- シェーダを変更したら、`meson test -C build` で `glslangValidator` の検査（GLES 3.0 / GL 3.3）が通ることを確認する。
- **シェーダ Core（`shaders/core/`）を変更したら、`glass-core-golden`（参照 `glass.frag` との比較）が通ることを確認する。** 意図して見た目を変えるときは、先にユーザーに相談する（参照との差が出るのは当然なので、その差を説明できること）。
- `npm` のコマンドは `demo/` の中で実行する（例: `cd demo && npm run build`）。TS ファイルを修正・編集したら `npm run build` を実行してビルドエラーを確認する。
- デモの起動は `meson devenv -C build -w . gjs -m demo/dist/main.js`（ビルドしたライブラリの typelib を使うため。`-w .` がないと build/ の中で実行されてパスが合わない）。天気アプリは `demo/dist/weather/main.js`。
- Flatpak と API ドキュメントの作り方は設計書 §15.3・§15.4（gi-docgen はこのマシンに無い）。
- **見た目の確認のためにアプリを開いたり、スクリーンショットを撮ったりしなくてよい。** 見た目はユーザーが確かめる。Claude はビルド・`meson test`・デモの型検査までを行い、何を見てほしいかを報告する（ユーザーの環境は sway で、確認用の窓が作業中のワークスペースを乱すため）。
- デモの `npm run build` は、先にビルドしたライブラリの `build/lib/Glass-1.gir` から型（`demo/types/`）を生成する。ライブラリの公開 API を変えたら `meson compile -C build` の後に `npm run build`。

## ファイル読み取りの許可
- `docs/memo.md`: 踏んだ地雷や罠、教訓の記録。読み取りも書き込みも自由。新しい罠が原因まで確定したら追記する（書き方は既存リポジトリの `memo.md` に倣う）。
- ローカルにある GTK・libadwaita・GLib・Mesa などのヘッダやソース（`/usr/include/gtk-4.0` など）: バグ原因の特定などで必要ならば、許可を取らずに自由に読んでよい。
- 既存リポジトリ `~/Projects/GitHub/liquid-glass`（GNOME Shell 拡張）: `shaders/glass.frag`、`memo.md`、`performance-plan.md`、`src/liquidEffect.ts`、`src/contrastSampler.ts` などは**参照は自由**。**変更はしない**（別リポジトリ・別の作業）。変更が必要になったら相談する。

## タスクの進め方
1. 原因の分析を行う。
2. 原因が確定した場合: コードを直接編集・修正し、そのままビルド（上の「コマンド実行ルール」）を実行して確認する。
3. 原因が仮説段階の場合: 検証用のスクリプト・小さな試作を作るか、検証用のログを仕込む。
4. 状況が明確でない場合: ユーザーに質問する。

## 設計上の不変条件（破る前に必ず相談する）
- **中身とガラスは同じフレームで一致させる。** 1 フレーム遅れの経路を作らない。
- **ガラスの本体は `GlassView` の snapshot が描く。** `GlassPanel` 自身には描かせない（GTK の描画ノードのキャッシュで古くなるため）。
- **幾何は snapshot の中で読む**（`gtk_widget_compute_bounds` など）。それより前の段階で読んだ位置は使わない。
- **snapshot の中で CSS クラスやスタイルを変えない。** Adaptive の結果は次のフレームの前に反映する。
- **パネルの前景（子のウィジェット）は取り込みに入れない。**
- **パスの依存は DAG にする。** 同じテクスチャを読みながら書くこと（ピンポン）はしない。
- **既存拡張の見た目を黙って変えない。** シェーダ Core は、元の `glass.frag` とゴールデン画像で 1 LSB 以内に一致させる。
- **FBO の大きさに依存していた値（`gradientStep`・`max_disp_px`）は明示的な uniform で渡す**（設計書 §10.2）。
- **GTK の公開 API だけを使う。** 非公開シンボル、非推奨で動かない `GskGLShaderNode` は使わない。
- どこで失敗しても文字が読めること（GL の失敗 → CSS のフォールバック → ハイコントラストなら不透明）。

## ユーザーの方針（既存拡張から引き継ぐ）
- 品質を落とす最適化は**個別の設定**としてだけ提供する。プリセット（高品質／バランス …）にまとめない。
- **光学の設定はこれ以上増やさない。** `EDGE_LENS_FALLOFF`・`EDGE_LENS_REACH` は定数のまま。`blur-downscale = 4` は既定にしない。
- 設定は意味のある値だけを選べる形にする。
- 着手前に `docs/memo.md` とコードのコメントを読み、地雷を踏もうとしていないか確認する。ただし、コメントを鵜呑みにせず深く推論する。
- 気がかりがあれば、押し通さずに止まって相談する。返答は日本語。

## デバッグ
- アプリ側の調査: GTK Inspector（`GTK_DEBUG=interactive`）、`G_MESSAGES_DEBUG=glass`、レンダラの切り替え（`GSK_RENDERER=vulkan|gl`、`GLASS_RENDERER=full|fallback`）、計測の表示（`GLASS_DEBUG=hud`）、パスごとの GPU 時間（`GLASS_DEBUG=gpu-time`、ログに 2 秒ごと）。
- 性能は「スクロール中」と「静止中」を分けて測る。GPU busy% の測り方と注意点は、既存リポジトリの `memo.md` 0.5 節に従う。
- 実験用の Python（PyGObject）スクリプトは、プロジェクトの外（スクラッチ用のディレクトリ）に置く。

## Looking Glass スクリプト作成ルール（GNOME Shell 側を調べるときだけ）
- `Clutter` や `Cogl` などを明示的にインポートしない。`imports.gi` でインポートもしない。`Clutter` のように、最初からそのまま使う。
- トップレベルのベタ書きスタイルで記述すること。
- **結果は必ず最後に `log()` で出力すること。** 末尾に `<変数名>;` と書くだけでは不可
  （Looking Glass の結果欄には出ても journal には残らず、あとから追えないため）。
  複数行にまとめるときは `log([...].join('\n'))` のように 1 回の `log()` にする。
- **必ず「1行版」も併せて作成すること。** 改行なしでそのまま Looking Glass の
  入力欄に貼れる形。コメント・整形・余分な変数は削り、複数文は `;` で繋ぐ。
  複数行版（読む用）と 1行版（貼る用）の両方を必ず提示する。ファイルとして保存する必要はなく、チャット内で示すこと。

## その他
- ユーザーが示した観察を、その時点でうまく説明することができなくても、それをユーザーの視覚的な誤解として扱わない。
