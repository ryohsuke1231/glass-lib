# glass-lib 設計書

- 版: **v0.11（拡張のシェーダの改良: 連続曲率の角・なめらかな影・分散の色収差・背景色のハイライト）**
- 日付: 2026-10-06
- v0.11 の変更（ユーザーの指示「liquid-glass のシェーダの改良を取り込む」、2026-10-06。拡張の `cf39b6d`・`1fac2c7`）:
  - 参照シェーダを拡張の `1fac2c7` に更新した（`shaders/reference/README.md`）。間の `5062870` はコメントの整理と未使用の uniform の削除だけ
  - 🔒 **角は連続曲率（スーパー楕円）**。角の中点は円の角と同じ所に置いたまま、曲がり始めを角から最大 1.6 倍の半径の所にする（`corner_smoothing` 0.6）。辺と曲率が連続するので、リムと屈折が角の付け根で折れない。§10.3.3
  - 🔒 **影は erfc 2 項と (1 − t²)³ の窓**（直線の本影と多項式の半影の継ぎ目が、影の縁の折れ目と段に見えていた）。8 bit の段は半 LSB のノイズで散らす。§10.3.3
  - 🔒 **色収差は分散の形**: `chroma-strength` は屈折の変位に対する割合（0〜1、単位なし）。青が緑よりその割合だけ多く、赤が少なく曲がる（以前は px）。どの材質も 0 のまま。§10.3.3・§11.1
  - 🔒 **ハイライトは背景の色相で光る**: 白を足して灰色に褪せる代わりに、Oklab の明度はそのままで、背景の色相（彩度 1.25 倍）を戻す。glass-lib では iOS 27 の縁の光の山にも同じことをする（拡張には無い）。§10.3.3
  - 角の滑らかさとハイライトの色は、拡張では設定（`glass-corner-smoothing`・`glass-backdrop-highlights`）だが、glass-lib では**設定にせず既定値で固定**した（光学の設定はこれ以上増やさない、§11.1）
- v0.10.1 の変更（ユーザーの確認、2026-09-28。memo 追記20）:
  - 🔒 **材質の体を v0.9 に戻した**（ユーザー:「霜を 0.73 で重ね、その下から弱くぼかした中身が透けて見える」見た目は気に入らない、もとに戻す）。REGULAR・THICK・MENU は plain の体、ぼかし 2・12・8、REGULAR のティントは白 0.12。
    ダークも同じ霜（0.85）だったので戻した。縁（iOS 27）、ダークの光の向き、REGULAR の影（0.015 / ダークなし）はそのまま。霜の仕組み（`surfaces` の `ios27`、シェーダとレンダラの経路）は残してあり、どの材質も使っていない（§10.3.2・§11.2）
  - 🔒 **グループへ戻る幽霊は、隣に重なった分だけ縮み、着いたら消える**（ユーザー: Controls の「⋯」で閉じると、集まったあと 0.5 秒ほど 1.3 倍の大きさで固まり、そのあと普通の大きさに戻る）。
    原因: 幽霊は隣と同じ矩形に重なったまま、ゼリーが止まるまで（約 0.5〜0.9 秒）描かれていた。重なった 2 つの形の smooth union は、各辺を `spacing` の 1/4 ずつ膨らませる（44px のボタンに幽霊 2 つで 56px）。止まった所で幽霊が消え、急に元の大きさに戻っていた。§6.8
  - 🔒 **タップでは板は滑るだけ**（ユーザー: ドラッグ中のばねはよいが、クリックで選んだときに行き過ぎたり震えたりすると気がそれる）。タップとアプリの `set_active` は `glass_jelly_glide`（臨界減衰・伸びなし）、ドラッグとその後に収まる動きは今までどおり `glass_jelly_plate`。§6.6・§6.9
  - 🔒 **開いたばかりのメニューは、窓のノードを待つ**（ユーザー: 開くとき一瞬黒いものが出る）。窓が毎フレーム描き直すとき（動く背景）、ポップオーバーの最初のフレームに窓のノードが無く、ふつうの見た目（テーマの暗い面、メニューの大きさ）に落ちていた。250ms までは何も描かずに次のフレームを待つ。§6.7、memo 地雷51
  - デモ: Controls・Tabs・Lab でも任意の画像を開ける（ユーザーの指示。Controls・Lab は背景の Photo の模様に、Tabs は写真のタブに 1 枚で。グリッドへ戻るボタン付き）。§14
  - 🔒 **カプセルに 1 つだけのメニューボタンは、抜けるときカプセルごと消える**（ユーザー: 天気で、開いている間 右端に幅 3px ほどのガラスが残る）。カプセルの余白（2px × 2）が残っていた。ボタンが縮むのと同じ割合でカプセルを透明にする。§6.7
- v0.10 の変更（ユーザーの指示「liquid_glass_widgets の README の B をすべて取り込む。ソースと比べて、シェーダ・既定の色（ティント・強さ・質感）もあちらに合わせる」、2026-09-28。比較の記録は `docs/memo.md` 追記19）:
  - 🔒 **材質の体と縁を iOS 27 に**（ユーザーの選択: 全部・板の材質に）。liquid_glass_widgets が SwiftUI の `glassEffect(.regular)` と並べて測った値（`LiquidGlassSettings.ios27Light` / `ios27Dark`）を移した。
    ~~REGULAR・THICK・MENU の体は「霜（中身を 14pt ぼかした雲）を、`blur-radius` でぼかした中身の写しの上に 0.73（ダーク 0.85）で重ね、ティント、彩度」。ライトは #F8F8F8 0.53・彩度 2.1、ダークは白 0.12・彩度 1.4。~~ → v0.10.1 で v0.9 の体に戻した
    縁（全材質）は「外側の中身の 0.5pt の線を光の軸に沿う所で 0.314 暗く、光の軸の両端に光の山」。CLEAR・PROMINENT は体をそのままに縁だけ。§10.3.2・§11.2
  - 🔒 **材質の値をライト/ダークで持つ**（`AdwStyleManager:dark`）。ダークでは光が下から（`light-angle-deg` 270）、REGULAR の影なし（§11.2）
  - レンズは変えていない: あちらの近軸レンズ（thickness 32 @3x、n 1.24）は縁で約 20pt・帯 約 11pt で、v0.9 の `thin`（macOS 27 の実測）と同じだった
  - 🔒 **ゼリー（§6.9）を共通の部品に**（`glass-jelly.c`）。トグルの板のタップも、モーフィング（`morph-id`・グループへの出入り）も、スイッチ・スライダーのつまみも同じばね。
    `morph-id` の入れ替えは、相手のガラスをしずくとして残して融合させる（liquid_glass_widgets の morph engine の涙形）。§6.8
  - 🔒 **Materialize**: 相手のいないパネルは、1.15 倍から縮みながら 250ms で現れ（中身は遅れて、ぼかしが晴れる）、350ms で膨らみながら消える（SwiftUI の `.materialize`。§6.8）
  - 🔒 **マジックレンズ**: トグルグループ・タブバーの板の下だけ、文字とアイコンをレンズの色（タブバーはアクセント）で描き、押している間は 1.15 倍に拡大する（§6.6）
  - 🔒 **メニューがボタンから生える**（ユーザーの選択: ポップアップを重ねる・ボタンがカプセルから抜ける）。§6.7
  - 🔒 **動きを減らす**（GTK 4.22 の `gtk-interface-reduced-motion`）: 伸び・行き過ぎ・移動をやめ、フェードだけにする（§13）
- v0.9（2026-09-27）: macOS 27 の実測に合わせたレンズと縁。既存拡張のシェーダと同時に改訂
- v0.9 の変更（ユーザーの指示「実機のスクリーンショットを解析して、屈折と縁を本物に近づける」、2026-09-27。解析の記録は `docs/memo.md` 追記16）:
  - 🔒 レンズの帯（ドームが立ち上がる幅）を角の半径から切り離し、定数 `EDGE_LENS_BAND`（22 論理 px）にした。帯より小さいガラスはレンズ全体を相似に縮める（§10.1・§10.3）。
    macOS では、半径 48pt の丸いボタンと角の半径 27pt のウィジェットで、縁からの深さごとの変位が同じだった（大きさにも角の半径にもよらない）
  - 🔒 内側の影（AO）を、リムの光が当たらない向きにだけ付ける（macOS の縁の 1px は、光の軸を向く所で明るく、沿う所で暗い）（§10.3）
  - 既定値（§11.1、拡張の gschema と同じ）と材質の縁の値（§11.2）を実測に合わせた。`crisp-soft` は `apple-s`（CLEAR は `apple-l`）に置き換えた。
    ガラス越しの背景の彩度を 1.5 倍にした（レンダラの定数。§11.2）
  - 参照シェーダは拡張の新しい版に更新し、ゴールデンは 250 件（帯より小さい形を追加）で 0/255（§10.5）
  - 続けて（ユーザーの指示、同日）: 材質 `PROMINENT`（確定のボタン。§11.2）、`GlassLens` と `set_lens()`（§6.1・§11.2）、`GlassToggleGroup` の板のドラッグ（ばねで吊るす。§6.6）。
    デモ: 写真を開く（Photos）、ダイアログはふつうの `AdwDialog` にして Close ボタンだけをガラス（`PROMINENT`、鮮やかな青 0.92・ぼかし 10）に、
    天気の空とギャラリーの背景を cairo から描画ノードに（CPU で毎回ラスタライズしていた。§14.1、memo 追記17）
- v0.8.1（2026-09-27）までの版: v0.8（v1 の仕上げ: ティント・パネルごとのパラメータ・フォールバックの Adaptive・API ドキュメント・Flatpak・天気アプリ。ロードマップの残りを破棄した版。2026-09-26）
- v0.8.1 の変更（公開前のレビューでの指摘、2026-09-27）:
  - パラメータのキーの定数 `GLASS_PARAM_BLUR_RADIUS` など（`Glass.PARAM_BLUR_RADIUS`）。`spec/params.json` から公開ヘッダ `glass-params.h` を生成する（§6.1・§11.1）
  - `GlassSwitch`・`GlassMenuButton` を `GtkActionable` に（§6.7）。`GlassToggleGroup` に削除・全消去・項目ごとのツールチップ（§6.6）
- v0.7 からの変更（ユーザーの指示、2026-09-26）:
  - 既定値: アプリ内の縁の値（`crisp-soft`）の `max_z` 50 → 35、`profile_shape_n` 7 → 2.4、`rim_directional_power` 1.6 を追加（拡張の 2.7 から。§11.2）
  - ティント（色・強さ）を調整できる設定にした: 強さは光学パラメータのキー `tint-strength`、色は `GlassContext:tint-color`（§11.1・§11.2）。デモの Lab に入れた
  - パネルごとのパラメータ（`glass_panel_set_param()` など）。値の決まり方を「パネル → 全体 → 部品自身 → 材質 → 既定」にした（§11.2）
  - フォールバック（CSS）の Adaptive を実装した（§12.1）
  - 既存拡張の設定に追従する機能（v0.7 までの §11.3）を破棄した
  - モーフィング: 始まりの形を最初のフレームで読む・現れている途中のパネルからは出てこない（§6.8）。アプリが `css-classes` を置き換えてもライブラリのクラスが戻る（§6.4）
  - API ドキュメント（gi-docgen）、`examples/`、Flatpak（2 つのデモ）、README（§15）。天気アプリ Glass Weather（§14.1）
  - ロードマップ（§17）: dGPU の確認は行わない、HiDPI は利用者のフィードバックに任せる。残りは破棄（§17.1 に一覧）
  - 2026-09-27 の追加（ユーザーの指示・指摘）: 天気アプリを macOS の天気アプリのレイアウトに（サイドバー・右上の検索・1/3 と 2/3。§14.1）。
    メニューのちらつきと文字のずれ・メニューの位置（§6.7）。ヘッダーにガラスの部品を置けるように（§6.6）。Flatpak はホストの `flatpak` でインストールする（§15.3）
- v0.6 からの変更（ユーザーの提案、2026-09-26）: §19 の候補だった `GlassTabBar`・`GlassSearchEntry`・形のモーフィング（`GlassPanel:morph-id`）・材質 `MENU`・角ごとの半径を v1 に入れた（§6.8）。
  Python（PyGObject）からの利用を確かめ、テスト（`tests/test-python.py`）にした（§16）
- 状態: v1 の範囲を実装済み（2026-09-26）。Phase 0（S1・S5）、Phase 2（ライブラリ本体・ウィンドウ部品群・§6.7・§6.8 の部品）、Phase 3（デモ 2 つ）、Phase 4 の API ドキュメント・Flatpak・README
- v0.5 からの変更（ユーザーの決定・指摘、2026-09-26）: 窓そのものが透けるガラス（Tier 2）と `ext-background-effect-v1` を glass-lib の計画から外した（§2.2）。
  `max_z` を 14 → 50、`sheen_intensity` を 0.32 → 0.08 に（3 つの材質の既定。§11.2）。ガラスの上のセグメント・ボタンの列・メニューボタンのボタンを枠なしにした（休止中に背景が付かない。§6.7、memo 地雷27）
- v0.4 からの変更（ユーザーの指摘、2026-09-25）: 材質の影の強さを 0.07 に（§11.2）。スイッチ・スライダーのつまみの既定のぼかしを 0 に（パネル自身の既定値、§11.2）。
  融合で、触れ合う形がくびれずに 1 つのカプセルになるよう「橋」を足した（§6.7）。入れ子のビューとダイアログのガラスが、下だけが変わったときに古いままになる不具合を直した（§6.7、memo 地雷24）
- v0.3 からの変更: v2 の候補だった「ガラスの上のガラス（層）」「押したときの膨らみ」「ガラスの融合（`GlassGroup`）」「スイッチ・スライダー」「ポップオーバー・メニュー・ダイアログ」を v1 に移した（ユーザーの提案、2026-09-25。§6.7）。
  取り込みはビューごとに 1 回の描画にまとめ、専用の GL レンダラで行う（§8.2）。サイドバーの CSS クラスを `.glass-sidebar` に変えた（memo 地雷17）
- v0.2 からの変更: ウィンドウ部品群（§6.6）を v2 から v1 に移した（ユーザーの決定、2026-09-25）。アプリ内の縁の値とぼかし半径を確定（§11.2、C6）
- 前版: [`archive/design-v0.1.md`](archive/design-v0.1.md)（調査の全記録。Tier 2 やウィジェット群の検討はそちらに残してある）

凡例: ✅ = 確認済み（実測または一次情報）/ ⚠️ = スパイクで検証する / 💡 = 判断・提案 / 🔒 = 決定事項（変更するときはこの文書を改訂する）

---

## 目次

1. [決定事項](#1-決定事項)
2. [質問への回答: 拡張機能の「アプリウィンドウ」のガラスとの違い](#2-質問への回答-拡張機能のアプリウィンドウのガラスとの違い)
3. [v1 の範囲](#3-v1-の範囲)
4. [前提となる事実](#4-前提となる事実)
5. [アーキテクチャ](#5-アーキテクチャ)
6. [公開 API（v1）](#6-公開-apiv1)
7. [GlassView の詳細](#7-glassview-の詳細)
8. [Full レンダラの詳細](#8-full-レンダラの詳細)
9. [フォールバック（CSS）](#9-フォールバックcss)
10. [シェーダ Core の切り出し](#10-シェーダ-core-の切り出し)
11. [パラメータとマテリアル](#11-パラメータとマテリアル)
12. [Adaptive（前景色の自動切り替え）](#12-adaptive前景色の自動切り替え)
13. [アクセシビリティ](#13-アクセシビリティ)
14. [デモアプリ Glass Gallery](#14-デモアプリ-glass-gallery)
15. [リポジトリ構成・ビルド・開発環境](#15-リポジトリ構成ビルド開発環境)
16. [テスト](#16-テスト)
17. [ロードマップ](#17-ロードマップ)
18. [リスク](#18-リスク)
19. [v2 以降の候補（保留したもの）](#19-v2-以降の候補保留したもの)
20. [残っている確認事項](#20-残っている確認事項)
- [付録 A: 計測値と確認結果](#付録-a-計測値と確認結果)
- [付録 B: 既存拡張から引き継ぐ方針](#付録-b-既存拡張から引き継ぐ方針)

---

## 1. 決定事項

| # | 項目 | 決定 | 設計への影響 |
|---|---|---|---|
| 1 | 名前 | 🔒 **glass-lib** | リポジトリ `~/Projects/GitHub/glass-lib`。GI 名前空間 **`Glass`**（`gi://Glass?version=1`）。C の接頭辞 `Glass` / `glass_`。pkg-config 名 `glass-lib-1`、共有ライブラリ `libglass-lib-1.so`、ヘッダ `<glass.h>` |
| 2 | デスクトップガラスのサービスの置き場所 | 🔒 既存の拡張機能の中 | ただし **v1 ではデスクトップガラスを作らない**（§2・§3）。将来やるときは既存拡張のモジュールとして作る |
| 3 | 対象の GNOME | 🔒 GNOME 50 が主、51 に追随 | GTK ≥ 4.22、libadwaita ≥ 1.9。Flatpak なら `org.gnome.Platform//50` |
| 4 | ライブラリの言語 | 🔒 C（GObject）＋ GObject Introspection | meson。GIR・typelib を生成し、GJS・Python から使えるようにする |
| 5 | デモアプリの言語 | 🔒 TypeScript → GJS | 既存の拡張機能と同じ道具（`tsc`、`@girs` の型） |
| 6 | ライセンス | 🔒 MIT | `LICENSE` は既存リポジトリと同じ文面 |
| 7 | Mutter を改造する路線 | 🔒 やらない | Wayland プロトコルの草案も v1 では作らない |
| 8 | 対象のツールキット | 🔒 GTK4 / libadwaita のみ | Qt・Electron・Flutter は対象外 |
| 9 | 部品の範囲 | 🔒 **ただのガラス（アイコン・テキストなどを載せられるもの）＋ウィンドウ部品群**（ツールバー・ヘッダーバー・サイドバー・セグメント・ボタン。§6.6）**＋§6.7 の部品**（ボタンの列、スイッチ、スライダー、融合するグループ、ポップオーバー・メニュー、ダイアログ） | ウィンドウ部品群は 2026-09-25 のユーザーの決定で、§6.7 は同日のユーザーの提案で v2 から前倒し。タブバー・検索欄・モーフィングも 2026-09-26 に v1 に入れた（§6.8） |
| 10 | アプリへの適用のしかた | 🔒 **アプリ内の中身に対するガラスだけ**（macOS Tahoe と同じ） | Tier 2（窓そのものが透けるガラス）は **glass-lib では作らない**（§2。ユーザーの決定、2026-09-26） |

---

## 2. 質問への回答: 拡張機能の「アプリウィンドウ」のガラスとの違い

### 2.1 何が同じで、何が違うか

v0.1 の「デスクトップが透けるガラス（Tier 2）」は、既存の拡張機能の **Application Windows**（アプリウィンドウのガラス）と**描画の技術はほぼ同じ**です。
どちらも、窓の背後にあるもの（壁紙・他の窓）を取り込んで、窓の下にガラスを描きます。違いは「**どこを、誰が決めるか**」と「**文字がどうなるか**」です。

| | 既存: Application Windows | v0.1 の Tier 2（アプリ協調版） |
|---|---|---|
| ガラスになる範囲 | **窓全体**（1 つの角丸矩形） | アプリが指定した**領域だけ**（例: サイドバーだけ） |
| 誰が決めるか | ユーザー（ホワイトリスト / ブラックリスト） | アプリ（`GlassPane` を置いた場所） |
| 窓の中身 | 拡張機能が**窓の中身ごと不透明度を下げる** → 文字やアイコンも半透明になる | アプリがその領域の**背景だけ**を透明に描く → 文字やアイコンは不透明のまま |
| 形・材質 | 窓の矩形と共通の設定 | 領域ごとの形（角の半径）と材質 |
| アプリへの情報 | なし | 背景の明るさ（前景色の切り替え用）をアプリに返す |
| アプリの対応 | 不要（どのアプリにも効く） | glass-lib を使ったアプリだけ |

### 2.2 glass-lib で Tier 2 を作らない理由

🔒 **Tier 2 は glass-lib の計画から外す**（ユーザーの決定、2026-09-26。v0.5 までは v2 の候補だった）。窓そのものを透かすのは既存の拡張機能の Application Windows の仕事で、glass-lib はアプリ内の中身に対するガラスだけを作る。理由は次のとおり。

1. **macOS Tahoe のアプリも、ガラスを窓そのものではなく、アプリ内の中身に対して掛けている**（ユーザーの観察）。
   これは Apple の設計（ガラスはコンテンツの上に浮くコントロール層）とも一致する。サイドバーも「窓の中身の上に浮くガラスの板」として扱われている。
2. 「窓を透かしたい」という需要には、**既存の拡張機能の Application Windows がすでに応えている**。
3. Tier 2 は最も壊れやすい部分（アプリと拡張機能の通信、commit との同期、窓の同定、背後の取り込み）で、v1 の価値に対してコストが大きい。

### 2.3 将来のための確認結果（S4、完了）

窓の同定に使う情報が取れることは確認できた（付録 A.1）。

- `org.gnome.Settings`（GtkApplicationWindow）→ unique name `:1.57`、窓のパス `/org/gnome/Settings/window/1` ✅
- `kitty`（GTK ではない）→ 何も取れない（想定どおり）

（記録として残す。Tier 2 は計画から外したので、この結果を使う予定はない。v0.1 の §8 の設計も記録として残してある。）

---

## 3. v1 の範囲

### 3.1 作るもの

| 部品 | 内容 |
|---|---|
| `GlassView` | 中身（content）の上にガラスの層を持つコンテナ。ガラスの本体はこれが描く |
| `GlassPanel` | ガラスの板。子を 1 つ持ち、アイコン・テキスト・ボタンなどを載せられる。カプセル型または角丸矩形 |
| ウィンドウ部品群（§6.6） | `GlassToolbarView`（スクロール端の効果つき）、`GlassHeaderBar`、`GlassSplitView`（浮くサイドバー）、`GlassToggleGroup`（セグメント）、`GlassButton` |
| 層・押下・融合と専用部品（§6.7） | ガラスの上のガラス（層）、押したときの膨らみ（`interactive`）、`GlassGroup`（近いガラスが融合する）、`GlassButtonGroup`、`GlassSwitch`、`GlassSlider`、`GlassPopover`・`GlassMenuButton`（メニュー）、`GlassDialog` |
| タブバー・検索欄・モーフィングほか（§6.8） | `GlassTabBar`、`GlassSearchEntry`、形のモーフィング（`morph-id`、グループの出入り）、材質 `MENU`、角ごとの半径 |
| `GlassContext` | ライブラリ全体の設定（レンダラの選択、透明度を下げる、光学パラメータ、ティントの色） |
| Full レンダラ | 既存の `glass.frag` と同じ見た目（屈折・色収差・リム・影・AO）をアプリ内で描く |
| フォールバック | GL が使えないとき、または `GlassView` の外に置かれた `GlassPanel` を、CSS の `backdrop-filter` ですりガラスとして描く |
| Adaptive | ガラスの下の明るさから、載せた文字・アイコンの色（明/暗）を自動で切り替える |
| CSS テーマ | ガラスの上に載せた libadwaita の部品（ラベル、アイコン、flat ボタン）を馴染ませる |
| デモアプリ | Glass Gallery（§14） |
| シェーダ Core | `glass.frag` を分解した単一ソース。ゴールデン画像で見た目の一致を保証 |

### 3.2 作らないもの（v1）

- 別のビューのガラス同士の屈折（層は 1 つのビューの中だけ。§6.7）
- 中身の自動の余白（パネルに隠れる分は、v1 ではアプリが余白を付ける。§7.1）

いずれも v2 以降の候補として §19 に理由と入口を残したが、2026-09-26 に破棄した（§17.1）。
タブバー・検索欄・モーフィング・材質 `MENU`・角ごとの半径は、2026-09-26 のユーザーの提案で v1 に入れた（§6.8）。
デスクトップが透けるガラス（Tier 2）と D-Bus サービス、GNOME 51 の `ext-background-effect-v1` 対応は、v2 でも作らない（§2.2）。**v1 の設計は、これらを後から足しても作り直しにならないようにする**（形状は配列で持つ、材質は enum で持つ、など）。

---

## 4. 前提となる事実

v0.1 の調査のうち、v1 に効くものだけを再掲する（詳細と出典は v0.1 の §1・付録）。

| 事実 | 確認 | v1 への影響 |
|---|---|---|
| GTK 4.22 は独自のシェーダ（`GskGLShaderNode`）を描けない（Vulkan・GL とも `The renderer does not support gl shaders`） | ✅ 実測 | GSK のノードの中では屈折を描けない → 自前の GL で描く |
| GTK 4.22 の CSS `backdrop-filter`（copy/paste ノード）は動く | ✅ 実測 | フォールバックに使う |
| 屈折に使える displacement ノードは GTK の内部にあるが非公開（4.24 でも） | ✅ | 上流への提案は v2 以降 |
| `gsk_renderer_render_texture()` は GPU 上の `GdkDmabufTexture` を返す（0.24〜0.26ms） | ✅ 実測 | ガラスの下の中身を 1 枚の画像として得られる |
| dmabuf の fd を取り出す公開 API は無い | ✅ | 自前の GL へは CPU 経由で渡す（600x120 で 0.27ms） |
| `GdkGLTextureBuilder` で自前の GL テクスチャを GTK に渡せる | ✅ | 出力はゼロコピー。GtkGLArea と同じ経路 |
| 既定のレンダラは Vulkan、アプリの GL コンテキストは GLES 3.2 | ✅ 実測 | シェーダは GLES 3.0 と GL 3.3 core の両方で動くように書く |
| `glass.frag` が読むのはぼかし済みのレイヤだけ | ✅ コード | 中身は縮小して取り込めばよい（転送量が 1/4） |
| `glass.frag` の `gradientStep()` と変位の上限 `max_disp_px` は **FBO の大きさに依存**している | ✅ コード | 小さなパネルでは見た目が変わってしまう → 明示的な値にする（§10.2） |
| `glass.frag` の `pointer_x/y`・`mouse_radius`・`bg_glow_intensity` は宣言だけで未使用 | ✅ コード | Core には持ち込まない |

---

## 5. アーキテクチャ

### 5.1 構成

```
┌─────────────── アプリ（GJS/TypeScript, Python, C, Vala …） ───────────────┐
│                                                                            │
│   GlassView ─┬─ content（スクロールする写真・リスト・地図 など）            │
│              └─ overlay 子（GlassPanel を含む任意のウィジェット）            │
│                    GlassPanel ── child（アイコン・ラベル・flat ボタン）      │
│                                                                            │
│   libglass-lib-1（C/GObject, GI 名前空間 "Glass"）                          │
│     ├─ GlassContext … レンダラの選択・透明度を下げる・光学パラメータ         │
│     ├─ Full レンダラ（非公開）                                              │
│     │    render_texture(中身を縮小) → CPU → GL にアップロード                │
│     │    → ぼかし（既存拡張と同じ式）→ Glass Core → GdkGLTexture            │
│     ├─ フォールバック（CSS backdrop-filter）                                 │
│     ├─ Adaptive（取り込んだ画素から明/暗を判定 → CSS クラス）                │
│     └─ CSS テーマ（GResource）                                              │
└────────────────────────────────────────────────────────────────────────────┘
        shaders/core（単一ソース。将来は既存拡張もここから生成できる）
```

コンポジタ（Mutter・GNOME Shell）には一切依存しない。**どの Wayland コンポジタ上でも、X11 上でも同じように動く。**

### 5.2 描画の順序（1 フレーム）

```
GlassView の snapshot
 1. backdrop-color の単色 ＋ content の描画ノード（content_node）   … 画面にも出す
 2. GlassPanel ごとに（木の順序＝重なりの順序で）
      a. 形状と材質を集める（位置は snapshot の中で compute_bounds で読む）
      b. キャッシュを確認（§7.5）
      c. 必要なら: 取り込み → ぼかし → Glass Core → テクスチャ
      d. テクスチャノードを追加（パネルの矩形＋影の余白）
 3. overlay 子を通常どおり描く（GlassPanel の前景＝子のアイコンやラベル）
```

ガラスの本体を `GlassPanel` 自身ではなく `GlassView` が描く理由:
GTK はウィジェットごとに描画ノードをキャッシュする。中身がスクロールしても `GlassPanel` の snapshot は呼ばれない（キャッシュが使われる）ので、パネルが自分で描くと**ガラスが古いまま残る**。
一方 `GlassView` は、子孫のどれかが再描画されるたびに snapshot される。だからビューが描けば、**中身とガラスは必ず同じフレームで一致する**。

### 5.3 不変条件

1. **中身とガラスは同じフレームで一致させる**（1 フレーム遅れを作らない。既存拡張で最も時間を使った問題）。
2. **幾何は snapshot の中で読む**（allocation 確定後の値。既存拡張の memo 地雷18 と同じ理由）。
3. **前景（パネルの子）は取り込みに入れない**。Adaptive のフィードバックが構造的に起きない。
4. **パスの依存は DAG**。同じテクスチャを読みながら書くこと（ピンポン）はしない。
5. **snapshot の中ではスタイル（CSS クラス）を変えない**。Adaptive の結果は次のフレームの前に反映する（§12.5）。
6. **既存拡張の見た目を黙って変えない**。Core への切り出しはゴールデン画像で一致を保証する（付録 B）。
7. どこで失敗しても**文字が読めること**を優先する（GL の失敗 → フォールバック → ハイコントラストなら不透明）。

---

## 6. 公開 API（v1）

### 6.1 型と関数

```c
/* 初期化: gtk_init / adw_init の後に 1 回。型の登録と CSS の読み込み */
void            glass_init                      (void);

/* ── GlassContext（ディスプレイごとの単一インスタンス） ── */
GlassContext   *glass_context_get_default       (void);
void            glass_context_set_renderer      (GlassContext *self, GlassRendererMode mode);
GlassRendererMode glass_context_get_renderer    (GlassContext *self);
void            glass_context_set_reduce_transparency (GlassContext *self, gboolean reduce);
gboolean        glass_context_get_reduce_transparency (GlassContext *self);
/* 光学パラメータ（上級者・デモの Lab 用）。キーは §11.1 の表。範囲外は clamp して警告。
 * キーごとに定数がある: GLASS_PARAM_BLUR_RADIUS = "blur-radius"（glass-params.h、生成。v0.8.1） */
gboolean        glass_context_set_param         (GlassContext *self, const char *key, double value);
double          glass_context_get_param         (GlassContext *self, const char *key);
gboolean        glass_context_is_param_set      (GlassContext *self, const char *key);
double          glass_context_get_effective_param (GlassContext *self, GlassMaterial material, const char *key); /* その材質で実際に使われる値 */
void            glass_context_reset_param       (GlassContext *self, const char *key);
/* 測ったレンズ（§11.2）を全体に: max-z・profile-shape-n・displacement-scale の 3 つをまとめて設定（v0.9） */
void            glass_context_set_lens          (GlassContext *self, GlassLens lens);
const char * const *glass_context_list_params   (GlassContext *self);
gboolean        glass_context_get_param_range   (GlassContext *self, const char *key, double *min, double *max, double *def);
/* ティントの色（全体）。NULL = 材質の色。alpha は使わない（強さはキー tint-strength）。§11.2 */
void            glass_context_set_tint_color    (GlassContext *self, const GdkRGBA *color);
gboolean        glass_context_get_tint_color    (GlassContext *self, GdkRGBA *color);

/* ── GlassView ── */
GtkWidget      *glass_view_new                  (void);
void            glass_view_set_content          (GlassView *self, GtkWidget *content);
GtkWidget      *glass_view_get_content          (GlassView *self);
void            glass_view_add_overlay          (GlassView *self, GtkWidget *widget);
void            glass_view_remove_overlay       (GlassView *self, GtkWidget *widget);
void            glass_view_set_backdrop_color   (GlassView *self, const GdkRGBA *color); /* NULL = 自動 */
GlassRendererMode glass_view_get_active_renderer (GlassView *self);  /* 実際に使われているもの */

/* ── GlassPanel ── */
GtkWidget      *glass_panel_new                 (void);
void            glass_panel_set_child           (GlassPanel *self, GtkWidget *child);
void            glass_panel_set_material        (GlassPanel *self, GlassMaterial material);
void            glass_panel_set_corner_radius   (GlassPanel *self, double radius);  /* -1 = カプセル */
void            glass_panel_set_corner_radii    (GlassPanel *self, double top_left, double top_right,
                                                 double bottom_right, double bottom_left);  /* 負 = corner-radius（§6.8） */
void            glass_panel_set_morph_id        (GlassPanel *self, const char *morph_id);    /* §6.8 */
void            glass_panel_set_tint            (GlassPanel *self, const GdkRGBA *tint); /* alpha = 強さ。NULL = 全体か材質のもの */
/* このパネルだけの光学パラメータ（§11.2）。全体（context）の値より優先 */
gboolean        glass_panel_set_param           (GlassPanel *self, const char *key, double value);
double          glass_panel_get_param           (GlassPanel *self, const char *key);  /* 未設定なら NaN */
gboolean        glass_panel_is_param_set        (GlassPanel *self, const char *key);
void            glass_panel_reset_param         (GlassPanel *self, const char *key);
double          glass_panel_get_effective_param (GlassPanel *self, const char *key);  /* 実際に使われる値 */
void            glass_panel_set_lens            (GlassPanel *self, GlassLens lens);   /* 3 つのパネルの値をまとめて（v0.9） */
void            glass_panel_set_has_shadow      (GlassPanel *self, gboolean has_shadow);
void            glass_panel_set_adaptive        (GlassPanel *self, GlassAdaptiveMode mode);
GlassAppearance glass_panel_get_appearance      (GlassPanel *self);  /* 読み取り専用（notify あり） */
```

### 6.2 列挙型

| 型 | 値 | 意味 |
|---|---|---|
| `GlassRendererMode` | `AUTO` / `FULL` / `FALLBACK` | AUTO = GL が使えれば FULL |
| `GlassMaterial` | `REGULAR` / `CLEAR` / `THICK` / `MENU` / `PROMINENT` | §11.2。`THICK` は大きな板（サイドバー）用、`MENU` はポップオーバー・メニュー用（§6.8）、`PROMINENT` は確定のボタン用（v0.9） |
| `GlassLens` | `THIN` / `THICK` | 測ったレンズ（§11.2。v0.9）。`THIN` = macOS の Dock（`CLEAR` 以外の材質の既定）、`THICK` = ウィジェット・丸いメディアボタン（`CLEAR` の既定） |
| `GlassEdgeStyle` | `NONE` / `SOFT` / `HARD` | スクロール端の効果（§6.6） |
| `GlassAdaptiveMode` | `AUTO` / `PREFER_LIGHT` / `PREFER_DARK` / `OFF` | 判定が曖昧なときにどちらへ寄せるか。OFF = 切り替えない（テーマの色のまま） |
| `GlassAppearance` | `UNKNOWN` / `LIGHT` / `DARK` | LIGHT = ガラスの下が明るい → 前景は暗い色 |

`PREFER_LIGHT` / `PREFER_DARK` は `GlassAppearance` の向き（ガラスの見た目）で名付けている。
既存拡張の `adaptive-text-preference` は**文字の色**で名付けている（`'light'` = 明るい文字 = DARK の見た目）ので、逆になることに注意する（§12.4）。

### 6.3 プロパティ

| 型 | プロパティ | 型・既定値 |
|---|---|---|
| `GlassView` | `content` | GtkWidget |
| | `backdrop-color` | GdkRGBA（NULL = ライト/ダークに応じた窓の背景色） |
| `GlassPanel` | `child` | GtkWidget |
| | `material` | `GlassMaterial`、`REGULAR` |
| | `corner-radius` | double、`-1`（カプセル） |
| | `top-left-radius`・`top-right-radius`・`bottom-right-radius`・`bottom-left-radius` | double、`-1`（`corner-radius` に従う。§6.8） |
| | `morph-id` | string、NULL（§6.8） |
| | `tint` | GdkRGBA（NULL = 全体の `tint-color` か材質の色と、`tint-strength`） |
| | `has-shadow` | boolean、TRUE |
| | `adaptive` | `GlassAdaptiveMode`、`AUTO` |
| | `appearance` | `GlassAppearance`（読み取り専用） |
| `GlassContext` | `renderer` | `GlassRendererMode`、`AUTO`（環境変数 `GLASS_RENDERER=full\|fallback` で上書き） |
| | `reduce-transparency` | boolean、FALSE |
| | `tint-color` | GdkRGBA、NULL（材質の色。§11.2） |

### 6.4 CSS

| 種類 | 名前 | 用途 |
|---|---|---|
| CSS ノード名 | `glassview`、`glasspanel` | |
| クラス（ライブラリが付ける） | `.glass-light` / `.glass-dark` | Adaptive の結果（§12） |
| クラス（ライブラリが付ける） | `.glass-fallback` | CSS で描いているとき（§9） |
| CSS 変数 | `--glass-fg-color`、`--glass-dim-fg-color`、`--glass-hover-color`、`--glass-active-color` | ガラスの上の前景色 |

CSS はライブラリの GResource から `GTK_STYLE_PROVIDER_PRIORITY_SETTINGS`（400）で読み込む。
libadwaita（THEME、200）より上、アプリ（APPLICATION、600）より下なので、アプリはいつでも上書きできる。

- 🔒 **アプリがクラスを丸ごと置き換えても、ライブラリのクラスは戻る**（v0.8）: GJS・Python のコンストラクタで `css_classes: [...]` を渡すと、`css-classes` が置き換わり、
  パネルが付けた形のクラス（`glass-radius-*`）・モード・明暗のクラスと、部品のクラス（`.glass-button` など）が消える。形のクラスが消えると、子が既定の `border-radius: 9999px` で楕円に切り抜かれる（地雷14 と同じ見た目。天気アプリで見つかった）。
  `GlassPanel` は `notify::css-classes` で自分のクラスを付け直す（アプリのクラスは残す）。ライブラリ自身のクラスの変更の途中では付け直さない（付け直すと古いクラスが残る。memo 地雷33）。
- 🔒 **ガラスの上のボタンの中身に前景色を直接付ける**（v0.8）: テーマが USER 優先度でボタン自身の `color` を決めると、パネルの `color` の継承が届かず、明るいガラスの上で白い文字・アイコンになった（地雷30 と同じ仕組み）。
  テーマはボタンの中身（`button > *`）には色を付けていないので、`.glass-light`・`.glass-dark` のパネルの中のボタンの中身に `color: var(--glass-fg-color)` を付ける（変数は最も近いパネルのものが継承されるので、入れ子でも正しい）。memo 地雷32。

### 6.5 使用例

TypeScript（GJS）:

```ts
import Adw from 'gi://Adw?version=1';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

Glass.init();

const grid = new Gtk.GridView({ /* 写真のグリッド */ });
const scrolled = new Gtk.ScrolledWindow({ child: grid });

const view = new Glass.View({ content: scrolled });

// 上に浮くタイトルのカプセル
const title = new Glass.Panel({ halign: Gtk.Align.CENTER, valign: Gtk.Align.START, margin_top: 12 });
title.set_child(new Gtk.Label({ label: 'Photos', css_classes: ['heading'] }));
view.add_overlay(title);

// 下に浮くツールバー（アイコンのボタンを並べる）
const box = new Gtk.Box({ spacing: 4 });
for (const icon of ['go-previous-symbolic', 'starred-symbolic', 'user-trash-symbolic'])
  box.append(new Gtk.Button({ icon_name: icon, css_classes: ['flat', 'circular'] }));
const toolbar = new Glass.Panel({ child: box, halign: Gtk.Align.CENTER, valign: Gtk.Align.END, margin_bottom: 16 });
view.add_overlay(toolbar);
```

Python:

```python
import gi
gi.require_version('Glass', '1')
from gi.repository import Glass, Gtk

Glass.init()
panel = Glass.Panel(material=Glass.Material.CLEAR, corner_radius=16)
panel.set_child(Gtk.Label(label='Hello'))
panel.connect('notify::appearance', lambda p, _: print(p.get_appearance()))
```

C:

```c
GtkWidget *panel = glass_panel_new ();
glass_panel_set_child (GLASS_PANEL (panel), gtk_label_new ("Hello"));
glass_view_add_overlay (GLASS_VIEW (view), panel);
```

### 6.6 ウィンドウ部品群（v1。2026-09-25 に v2 から前倒し）

macOS Tahoe では、ツールバー・サイドバー・セグメントが**何もしなくても**ガラスになる。
libadwaita のアプリはすでに `AdwToolbarView`・`AdwHeaderBar`・`AdwOverlaySplitView`・`AdwToggleGroup` で窓を組んでいるので、**同じ使い方の Glass 版**を用意する。
部品はすべて `GlassView`・`GlassPanel` の上に組み立てる（描画の仕組みは増やさない）。

| 部品 | libadwaita の対応 | 振る舞い |
|---|---|---|
| `GlassToolbarView` | `AdwToolbarView` | 中身がバーの下まで伸びる（常に）。上下のバーは中身の上に浮く。**スクロール端の効果**（`top-edge-style`・`bottom-edge-style`）: `SOFT` = バーの下の帯で中身をぼかしながら薄めて、背景の色へ溶かす（既定）。`HARD` = 同じ帯を均一にぼかして区切り線を引く。`NONE` = なし。バーの高さは `top-bar-height`・`bottom-bar-height`（読み取り専用）で出すので、アプリはスクロールする中身の先頭にその分の余白を付ける（libadwaita の `extend-content-to-top-edge` と同じ扱い） |
| `GlassHeaderBar` | `AdwHeaderBar` | `pack_start`・`pack_end` の部品は、それぞれ**1 つのガラスのカプセル**にまとめて浮かせる。それ自体がガラスの部品（`GlassPanel`・`GlassButton`・`GlassGroup`）は、カプセルに入れず（ガラスの上のガラスにしない）カプセルの内側の隣に置く（v0.8。高さはカプセルにそろえる）。タイトルはガラスなし（スクロール端の効果の上に載る）。窓のボタン（閉じる等）も小さなカプセルに入れる（どんな中身の上でも見えるように）。空の所はドラッグで窓を動かせる（`GtkWindowHandle`） |
| `GlassSplitView` | `AdwOverlaySplitView` | サイドバーは**窓の中に浮く角丸の板**（材質 `THICK`、窓の縁から 8px 内側、半径 14px）。中身はサイドバーの下まで伸び、サイドバーが覆う幅は `content-inset`（読み取り専用）で出す。`show-sidebar` でスライドして出し入れする |
| `GlassToggleGroup` | `AdwToggleGroup` | カプセルの中に並んだトグル。選ばれたものの下を丸い板がスライドする。**板はガラスの上のガラス**（§6.7 の層。押している間は膨らむ）。トグルは `GtkToggleButton` ではなく `GtkButton`＋`.active` クラス（テーマの `button:checked` の塗りが板を覆うため。memo 地雷12）。項目は添字で扱う: `append`・`remove`（選ばれていた項目なら、その位置に来た項目か、末尾なら 1 つ前が選ばれる）・`remove_all`・`set_tooltip`（v0.8.1）。
🔒 **板はドラッグでも動かせる**（v0.9。ユーザーの指示、2026-09-27）: 選ばれた項目の上で押して 6px 動かすと板が指に付いてくる（それまではふつうのタップ。捕捉フェーズの `GtkGestureDrag` が、動いた時点で押下を取る）。
ドラッグ中は指に最も近い項目だけを選ばれた色（`.active` のクラスだけ）にし、`notify` は出さない。離すとその項目が選ばれ（`notify::active`）、板はそこに収まる。
ドラッグ中の板は、押したときの膨らみの 2.5 倍に膨らむ（外のカプセルからはみ出してよい。離すと戻る。v0.9.1、ユーザーの指示、2026-09-28）。
板は指にばねで吊るす: 左右の端がそれぞれのばね（ω 48（v0.9.1 で 36 から硬く）、減衰比 0.45、進む側を 35% 硬く・後ろを 35% 柔らかく）で追うので、動いている間は伸び（幅 0.6〜1.7 倍。動くのは並びの向きだけで、高さと縦の位置は変えない）、急に止めると前の端が行き過ぎてから収まる。端の先ではゴムのように抵抗する。物理は 2ms の刻み、フレームクロックで駆動。
🔒 v0.10: このばねを §6.9 のゼリーとして共通にし、**タップ（とアプリの `set_active`）でも板はゼリーで動く**（280ms の時間で決まるスライドはやめた）。並びが右から左でも、端の抵抗は正しい側に効く。
🔒 v0.10.1: **タップ（とアプリの `set_active`）では、板は `glass_jelly_glide` で滑るだけ**（臨界減衰・伸びなし。0.15 秒で 9 割、行き過ぎない）。弾むのはドラッグ（とドラッグを離して収まるまで）だけ。
ユーザー: クリックで選んだときに移動方向に行き過ぎたり震えたりすると、気がそれてよくない（指で動かしていないのに動きが目を引く）。タップで滑っている途中からドラッグすると、その速さのままドラッグのばねに替わる。
🔒 **マジックレンズ**（v0.10）: 板の下だけ、トグルの中身（文字・アイコン・タブの箱。ボタンの背景は含めない）をもう一度、板の形で切り抜いて描く。色は描かない子ノード `lens` の `color`（既定は群の色のまま、タブバーはアクセント `--accent-color`）、押している間は板の中心で 1.15 倍（liquid_glass_widgets のタブバーの値）。
板が項目をまたいでいる間は、覆われた部分だけが色を変える（クラスで一度に切り替えない）。群の snapshot の中で、マスクのノード（`GskMaskNode`）で描く: 取り込みには入らず（§5.3-3）、スタイルも変えない（§5.3-5）。タブバーの選ばれた項目のアクセント色（v0.8 の `.active > box`）はこれに置き換えた |
| `GlassButton` | `GtkButton` ＋ `.circular` / `.pill` | それ自体がガラスのボタン（アイコンか文字）。押している間はガラスが少し明るくなる。`GtkActionable` |

- 部品の中のボタンは自動で `flat` の見た目になる（ガラスの上に libadwaita の塗りの背景を重ねない）。前景色は Adaptive に従う。
- **ガラスの上のガラスは層として描く**（v0.4。§6.7）: `GlassPanel` の子孫にある `GlassPanel`（例: サイドバーの中のヘッダーバーのカプセル）は、下のガラスを背景にしたガラスになる。
  `GlassPanel` の中に置いた `GlassView` は背景色を塗らない（下のガラスを隠さないため）。
- スクロール端の効果は `GlassView` が描く（中身の直後、ガラスの前）。ガラスの取り込みもこの効果を含む（ガラスの後ろに見えているものと同じ）。
  SOFT: バーの高さ＋最大 32px の帯で、中身を GSK の blur（10px）でぼかし、背景色を 55% 重ね、バーの半分まで不透明・帯の終わりで透明になるマスクをかける。HARD: バーの高さの帯を blur 16px・背景色 72% で覆い、前景色 15% の 1px の線を引く。
- 実装済み（2026-09-25）。API は `lib/glass-toolbar-view.h` などの各ヘッダ。CSS: ヘッダーバーのカプセルは `.header-capsule`（窓のボタンは `.window-controls` も）、サイドバーは `.glass-sidebar`（`.sidebar` はテーマが不透明な背景を塗るため使わない。memo 地雷17）、セグメントは `.toggle-group`（板の色は子ノード `pill` の `color`）、ボタンは `.glass-button`。

### 6.7 層・押下・融合と専用部品（v1。2026-09-25 に v2 から前倒し）

ユーザーの提案（2026-09-25）で、v2 の候補だったものを v1 に入れた。描画の仕組みは `GlassView` の 1 回の snapshot のまま増やさない（ポップオーバーとダイアログは、`GlassView` の外で同じレンダラを使う）。

- **ガラスの上のガラス（層）**: `GlassPanel` の中にある `GlassPanel` は、ビューが**後の層**として描く（層 = ビューとの間にある `GlassPanel` の数）。
  層 1 以上の背景は GSK で取り込み直さず、**GL で合成する**: 層 0 の取り込みの生の画像に、それより下の層の出力テクスチャを事前乗算の OVER で重ねてからぼかす（`glass_renderer_compose`）。
  層 0 の祖先の取り込み矩形を、子孫の分まで広げておく。パスの依存は DAG のまま（下の層の出力を読むだけ）。
  `GlassPanel` の中に置いた `GlassView`（サイドバーの中のツールバーなど）は、祖先のパネルの出力を自分の座標に移して下に敷く。
  そのパネルの出力が変わると、中のビューを**同じフレームで**描き直させる（GTK は中のビューのノードをキャッシュしているので、下だけが変わると古いガラスが残る。memo 地雷24）。
  前景（子のウィジェット）は取り込みにも合成にも入れない（§5.3 のまま）。
- **押したときの膨らみ（`GlassPanel:interactive`）**: 押している間、ガラスと子が中心を基準に少し大きくなり（既定で最大 +6px・+16%）、色が少し明るくなる。
  `AdwSpringAnimation`（押す: 速く固く。離す: 少し行き過ぎて戻る）。押下は捕捉フェーズの `GtkEventControllerLegacy` で見る（イベントは消費しない）。
  `GlassButton`・`GlassButtonGroup` は既定で有効。
- **融合（`GlassGroup`）**: 子孫の `GlassPanel` をひとつのガラスとして描く。形は角丸矩形（最大 8 個）の smooth union（多項式の smin、幅 `spacing`、既定 16px）。
  離れていれば別々、近づくと液滴のようにつながる。Core には `glass_shape_count > 0` のときだけ通る経路として足した（0 なら参照の経路のまま。ゴールデン 0/255 を維持）。
  smin だけでは、触れ合った形のつなぎ目に必ずくびれが残る（丸いボタンを並べると数珠のように波打つ。ユーザーの指摘）。そこで、隙間が `spacing` の半分（smin がつながり始める距離）より近い 2 つの形の間に**橋**を足す:
  一方の形をもう一方まで動かした軌跡（並んでいれば 2 つの凸包）を、隙間に応じて内側に縮めたもの。隙間 0 では凸包そのもの（丸いボタンの列が 1 つのカプセル）、
  離れるほど細い首になり、smin の首と同じ距離で切れる。どの組に橋を架けるかは CPU で決める（最大 12 本）。
- **`GlassButtonGroup`**: ボタンを 1 つのカプセルに並べる（ヘッダーバーのカプセルもこれ）。各ボタンを丸いピルで切り抜いて描くので、
  テーマの角の半径が USER 優先度で上書きされても、ホバーの形が半分のカプセルにならない（memo 地雷19）。
- **`GlassSwitch`・`GlassSlider`**: トラック（レール）は CSS の色で描き、つまみを `GlassPanel` にする。普段のつまみは白い板（ティントが濃い）で、
  つかんでいる間はティントが薄くなってレンズになる（Apple のスイッチ・スライダーと同じ）。つまみがトラックの外で屈折できるよう、内部の `GlassView` をトラックより一回り大きく置く
  （その背景色は取り込みにだけ入れる）。アクセシビリティの役割は `SWITCH`・`SLIDER`。
  どちらも `GtkActionable`（v0.8.1）。スイッチは `GtkSwitch` と同じく真偽値の状態を持つアクションに従う: GTK にはアクションの状態を見る公開 API が無いので、
  隠した `GtkSwitch` を子に持ち、その `active` と相互に写す（アクションの扱いは GTK のものをそのまま使う）。
  🔒 つまみのぼかしは既定で 0（レンズは曇らせない。ユーザーの決定、2026-09-25）。パネル自身の既定値として持つ（§11.2）ので、アプリが `blur-radius` を明示すればつまみにも効く。
- **`GlassPopover`・`GlassMenuButton`**: ポップオーバーは別のサーフェス（xdg_popup）なので `GlassView` の中に入らない。
  **親の窓の、今のフレームのノード**を `GtkWidgetPaintable` で借りて取り込み、自分のサーフェスの位置（`gdk_popup_get_position_x/y` と `gtk_native_get_surface_transform`）に合わせて THICK のガラスを描く。
  親のサーフェスの `render` シグナルで描き直す。`glass_popover_new_from_model()` はメニュー（項目・区切り・サブメニュー）を組み立てる。
  - 🔒 **窓のノードが無いフレームでは、最後のノードを使う**（v0.8。ユーザーの指摘: ホバーやサブメニューで、黒い見た目とガラスの見た目が高速に入れ替わった）。
    項目の `gtk_widget_queue_draw()` はポップオーバーを越えて窓まで遡り、窓の描画ノードを捨てる（ポップオーバーの親は窓の中のボタン）。窓が描き直す前にポップオーバーが描かれると、ガラスの背景が無く、ふつうのポップオーバーの見た目に落ちていた。memo 地雷36。
  - ガラスの上の中身は、`contents` の**内容の箱**を原点に置く（`gtk_widget_snapshot_child()` の子の位置はそこから。`compute_bounds` は枠の箱なので、パディングと枠の分だけ左上にずれていた。memo 地雷37）。
  - 🔒 **開いたばかりで窓のノードがまだ無いときは、何も描かずに待つ**（v0.10.1。ユーザーの指摘: メニューを開くとき一瞬黒いものが出る）。最後のノードは閉じるときに捨てるので、開いた最初のフレームには上の対策が効かない。
    窓が毎フレーム描き直すとき（天気の空、ギャラリーの動く背景）は、ポップオーバーの最初のフレームで窓のノードが捨てられていることがあり（ヘッドレスの sway で 10 回中 4 回）、ふつうの見た目（テーマの暗い面をメニューの大きさで）が 1 フレーム出ていた。
    開いてから 250ms までは何も描かず、tick で次のフレームに描き直す（生えるガラスはまだ始まっていないので、ボタンが見えているだけ）。それを過ぎてもノードが無ければ、ふつうの見た目にする（文字が読めること）。memo 地雷51。
  - 窓の外にはみ出した部分の背景は、ティントの色（不透明）で埋める（見えないものは屈折させない。黒くなっていた）。
  - `GlassMenuButton` は `GtkActionable`（v0.8.1。中のボタンに渡す）: アクションが無効ならボタンは押せず、押すとアクションを起動してからメニューを開く。
    アクションを中の部品に渡す部品（`GlassButton` も）は、自分にもアクションの muxer を先に作っておく（無いと、窓に入る前に付けたアクション名が窓のアクションに届かない。memo 地雷44）。
  - `GlassMenuButton` のメニューはボタンの下（10px 空けて）に開き、端をボタンの端にそろえる: メニューの幅が収まる側へ開く（両方に収まるなら窓の端から遠い方、どちらにも収まらなければボタンの中央。v0.8。ユーザーの指示）。
  - 🔒 **メニューはボタンから生える**（v0.10。ユーザーの選択、2026-09-28）: `GlassMenuButton` は、ポップアップのサーフェスをボタンの上まで伸ばして開く（`contents` の上の余白 = ボタンの高さ＋10px、オフセット = −ボタンの高さ。中身の位置は今までどおりボタンの 10px 下）。
    ポップオーバーのガラスは最初のフレームでボタンの形（丸いピル）にあり、§6.9 のゼリーでメニューの形へ動く。ボタンの位置には元のガラスがしずくとして残り、行程の 40% で縮んで消える（その間は融合、幅 24px。liquid_glass_widgets の morph engine）。項目は行程の 30% から現れ、ガラスの形で切り抜く。
    項目を選んだとき・`glass_menu_button_popdown()` では、逆にボタンの位置へ吸い込まれてから閉じる（`glass_popover_dismiss()`）。GTK が閉じるとき（外のクリック、Esc）はすぐに消える（閉じたサーフェスには描けない）。
    GTK がポップアップをボタンの反対側に反転させたとき（下に余地がない）は、ボタンがサーフェスの外なので、生えずにただ開く。
  - 🔒 **カプセルから抜ける**（ユーザーの選択）: ボタンがカプセル（`GlassButtonGroup`、ヘッダーのカプセルも）の中にあれば、メニューが開いている間はボタンの幅を 0 まで縮めて消す（ばね、行き過ぎなし）。カプセルのガラスは毎フレームの割り当てに追従するので、ボタンの分だけ閉じる。閉じるとボタンが戻り、カプセルも元の幅に戻る。
    ボタンが縮む間もポップアップが動かないよう、開いた時点のボタンの位置（窓の座標）を覚え、割り当てのたびに `gtk_popover_set_pointing_to()` をそこへ合わせ直す。
    🔒 v0.10.1: カプセルにほかに見えている項目が無いとき（天気の見出しの右端）は、ボタンが縮むのと同じ割合でカプセルを透明にし、ボタンと一緒に消す。幅 0 のボタンでもカプセルの余白（2px × 2）が残り、ボタンの高さの細いガラスになっていた（ユーザーの指摘）。
  - 窓とポップアップは別のサーフェスで、同じフレームにはそろわない。生えるときは、ポップアップの最初のフレームがボタンと同じ形でボタンを覆うので、1 フレームずれても二重になるだけで、ガラスの無いフレームは無い。
- **`GlassDialog`**: `AdwDialog` はダイアログのホストが窓の中身の上に描くので、`GlassView` の中に入らない。
  中身の下に、**ホストの中身（ダイアログ以外の子）の今のフレームのノード**から THICK のガラスを描く。
  中身だけが変わるフレーム（動く背景）でも GTK はダイアログのノードを使い回すので、窓が描かれるフレームごとに、フレームクロックの `before-paint` で描き直させる（同じフレーム。memo 地雷24）。
  後ろが変わっていなければ取り込みとパスはキャッシュに当たる。何も変わらない間はフレーム自体が来ないので、描き直しが続くことはない。
- ポップオーバーとダイアログは、GL が使えないとき・ハイコントラストのとき、不透明の背景（`--popover-bg-color`・`--dialog-bg-color`）になる。
- 🔒 **別の `GlassView` のガラスは見えるが、屈折の層にはならない**: 層は 1 つのビューの中だけ。入れ子のビュー（ページのツールバー）の出力は、
  外のビュー（スプリットビューのサイドバー）の取り込みにはふつうの画像として入る（そのため外のビューの取り込みは内のビューのガラスの完成を待つ。§8.8）。
- 🔒 **ガラスの上のボタンは枠なし**: `GlassToggleGroup` の項目と `GlassMenuButton` のボタンは枠なし（`has-frame` が FALSE）で作り、`GlassButtonGroup`（ヘッダーのカプセルも）は入れられたボタンの枠を外す（取り除くと戻す。suggested・destructive・`.opaque` はそのまま）。
  テーマが USER 優先度で枠のあるボタンに背景を塗ると、ライブラリの CSS では消せず、選ばれていない項目にホバーしていなくても薄いピルが出るため（ユーザーの指摘、memo 地雷27）。
- CSS: ボタンの列は `.button-group`（中の行は `box.pills`）、スイッチ・スライダーのつまみは `.switch-knob`・`.slider-knob`（色はトラック `track`・レール `rail`・`fill` の `color`）、
  ポップオーバーは `popover.glass`（メニューのページは `.glass-menu`）、ダイアログは `dialog.glass`（ガラスの色は `glassdialogsurface > backdrop` の `color`）。

### 6.8 タブバー・検索欄・モーフィング・材質 MENU・角ごとの半径（v1。2026-09-26 に v2 から前倒し）

ユーザーの提案（2026-09-26）で、§19 の候補だったものを v1 に入れた。部品はどれも §6.7 までの仕組み（`GlassView` の 1 回の snapshot、層、融合）の上に作り、描画の仕組みは増やさない。
意味は Apple の Liquid Glass（iOS 26・macOS Tahoe）の同名の部品に合わせる。

- **角ごとの半径**: `GlassPanel` のプロパティ `top-left-radius`・`top-right-radius`・`bottom-right-radius`・`bottom-left-radius`（double、既定 -1 = `corner-radius` に従う）と、
  まとめて設定する `glass_panel_set_corner_radii()`。各角の実際の半径は「その角の値 → `corner-radius` → カプセル（短い辺の半分）」で、どれも短い辺の半分までに切り詰める。
  - Core: `glass_corner_mode` が 1 のときだけ、輪郭の SDF（`sdRoundRect` と方向）の半径を、画素のある象限の角の値にする（iq の `sdRoundBox` と同じ選び方）。
    縁のレンズの帯の幅（高さの正規化・`bevelPx`・平らな内側の判定）は、**4 つの角の最大**を `corner_radius` として渡して全体で共通にする。象限ごとに帯の幅を変えると、辺の中央で高さが食い違う（継ぎ目が出る）ため。
    `glass_corner_mode` が 0（既定、参照と同じ）なら計算は今のまま（ゴールデン 0/255 を維持）。
  - `GlassGroup` の中（融合）では角ごとの半径は使わない（最大の半径の角丸矩形として融合する）。
  - CSS: フォールバックと子の切り抜きは、角ごとの `border-radius` のクラスを実行時に作って付ける（今の `glass-radius-*` と同じ方式）。
  - 使いどころ: 画面の下に付くシート（`GlassDialog` が libadwaita の bottom sheet になったとき: 上の角 15px、下の角 0。`dialog.bottom-sheet` クラスで判別）、窓の縁に沿う板。
- **材質 `MENU`**（`GlassMaterial` に追加）: ポップオーバーとメニュー用。ぼかし 8px、ティントはテーマのポップオーバーの背景色 0.45（`THICK` の 12px・0.55 より透ける）、影 24px・0.07、Adaptive なし（テーマの色）。
  `GlassPopover`（メニューも）はこれを使う。ダイアログは `THICK` のまま。値は初期案（Lab で調整できる）。
- **`GlassTabBar`**: iOS のタブバー（libadwaita の `AdwViewSwitcherBar` に当たる）。`AdwViewStack`（`stack` プロパティ）のページごとに、アイコンと題名を縦に並べた項目を 1 つのガラスのカプセルに並べる。
  選ばれたページの項目の下を、ガラスの上のガラスの板がスライドする（`GlassToggleGroup` と同じ仕組みを内部で使う）。選ばれた項目のアイコンと題名はアクセントの色
  （色は項目の中の箱に付ける。テーマが USER 優先度でボタンの文字色を決めていると、ボタンに付けた色は負けるため。memo 地雷30）。
  `AdwViewStackPage` の `visible`・`title`・`icon-name`・`needs-attention`（点を付ける）に追従する。項目は枠なしのボタン（地雷27）。
- **`GlassSearchEntry`**: ガラスのカプセルの検索欄（`GlassPanel` の派生、`REGULAR`）。虫眼鏡のアイコン、文字（`GtkText`）、消去のボタン（文字があるときだけ）。
  `GtkSearchEntry` は使わない: テーマが USER 優先度で `entry` に背景と角の半径を塗るので、ガラスの上に四角い箱が出る（地雷27 と同じ理由）。`GtkText` は背景を持たない。
  `GtkEditable` を実装する（`GtkText` に委譲）。シグナルは `GtkSearchEntry` と同じ `search-changed`（`search-delay` ms 後、既定 150）・`activate`・`stop-search`（Esc）。
  `placeholder-text`、`key-capture-widget`（その部品で打った文字をここに回す）。アクセシビリティの役割は `SEARCH_BOX`。
- **モーフィング**（`GlassPanel:morph-id`、文字列）: ガラスの形が別の形へ変わっていく（Apple の `glassEffectID`）。
  - **同じ `morph-id` のパネルの入れ替え**: パネルが表示されたとき、同じビューの中に同じ `morph-id` のパネルがあり、それが隠れた（または隠れるところ）なら、新しいパネルのガラスは**古いパネルが最後に描かれた形から**自分の形へ、ばねのアニメーションで変わる。
    中身（子）は途中から現れる（不透明度 0 → 1）。古いパネルのガラスはそれ以上描かない（新しいパネルのガラスになった）。例: 検索のボタンが検索欄に変わる。
  - **グループへの出入り**（`GlassGroup` の中、`morph-id` の一致がないとき）: 表示されたパネルは、グループの並びで最も近い（同じ近さなら前の）見えているパネルから**しずくが分かれるように**出てくる。
    同時に現れたパネル（現れている途中のもの）からは出てこない（どれも元からあったガラスから出る。v0.8）。
    出てくる元が見えているパネルなら、その位置は**最初のフレームの snapshot の中で読む**（§5.3-2。v0.8）: 表示と同じフレームで中央寄せの列が伸びて元のパネルが動くので、表示した時点の位置から始めると、元のパネルから離れた所に現れた（デモの Controls で見つかった）。
    隠れたパネルのガラスは、しばらく中身なしで描き続け（幽霊）、隣のパネルに**吸い込まれるように**縮んで消える。融合（smooth union と橋）がそのまま効くので、液体のようにつながって分かれる。
    幽霊のアニメーションはビューに付ける（libadwaita は map されていない部品のアニメーションを飛ばすため。memo 地雷29）。
  - グループの外で `morph-id` の一致がなければ、今までどおり（すぐ現れて、すぐ消える）。
  - ~~形は矩形と角の半径を補間する（ばね: 減衰 0.8・剛性 300）。幽霊は 260ms で縮む~~ → 🔒 v0.10: **形は §6.9 のゼリーで動く**（辺ごとのばね。進む向きに伸び、着くと前の端が行き過ぎてから収まる）。角の半径と中身の不透明度は、ゼリーが来た割合で補間する。
    幽霊も同じゼリーで隣のパネルへ吸い込まれる。
    🔒 v0.10.1: 幽霊は**隣に重なった分だけ縮む**（その割合で中心へ縮め、角の半径も同じ割合。重なりは幽霊の矩形のうち隣の矩形に入っている面積の割合で、行き過ぎて戻っても縮んだまま）。残りが 5% 以下になったら消える（隣に着いた時点。44px の 2 つのボタン分の行程で約 0.18 秒）。
    重なったまま描くと、smooth union が各辺を `spacing` の 1/4 ずつ膨らませ、ゼリーが止まるまでその大きさで固まって見えた（2 つなら 44px → 56px）。割合は幽霊の面積で測る（ばねで伸びた幽霊は隣より長く、尾がまだ外にある）。
  - 🔒 **`morph-id` の入れ替えは涙形**（v0.10）: グループの外では、相手のガラスが元の位置にしずくとして残り、行程の 40% で中心へ縮んで消える。その間、新しいガラスと 2 つの形の smooth union（幅 24px、§6.7 の融合と同じ経路）として描くので、ちぎれるときに首ができる（liquid_glass_widgets の morph engine: 元のしずくが最初の 40% で縮む）。
  - 🔒 **Materialize**（v0.10）: 相手のいないパネル（`morph-id` の一致もグループの隣もない）は、SwiftUI の `glassEffectTransition(.materialize)` のように現れる。250ms、ガラスは 0〜92% で一定の速さに現れ（屈折・ティント・霜・縁・影がその割合で強くなり、出力も同じ割合で不透明に）、大きさは 1.15 倍から 1 倍へ。
    中身は 35% から現れ、ぼかし 8px から晴れる。値は liquid_glass_widgets がネイティブの 120fps の映像から測ったもの。ビューが画面に出たのと同じ時（ビューがまだ描いていない）に現れたパネルは、ビューと一緒に出たものとして、何もしない。
    消えるとき（相手がいない）は、ガラスだけが 350ms で 1.15 倍に膨らみながら薄れる（幽霊。中身は隠れた時点で消えている）。
  - 🔒 **動きを減らす**（v0.10。§13）: `gtk-enable-animations` が偽なら何もアニメーションしない（今までどおり）。`gtk-interface-reduced-motion` が `REDUCE`（GTK 4.22）なら、移動と伸び（ゼリー・涙形・膨らみ）をやめ、Materialize は大きさもぼかしも変えないフェードに、`morph-id` の入れ替えもフェードにする。
  - 補間はビューの snapshot の中で、そのフレームの割り当て（§5.3-2）とアニメーションの値から計算するので、中身とガラスは同じフレームで一致する（押したときの膨らみと同じ扱い）。
  - 大きさの変化（ボタンの列が伸びるなど）は、ガラスが毎フレームの割り当てに追従するので、`GtkStack` の `interpolate-size` や `GtkRevealer` で割り当てをなめらかにすればそのまま形が変わる（別の仕組みは要らない）。
- CSS: タブバーは `glasstabbar`（中のカプセルは `.tab-bar`、項目は `button.tab`）、検索欄は `glasspanel.search-entry`（文字は `text`）。

### 6.9 ゼリー（動くガラスの共通のばね。v0.10）

🔒 動くガラスはすべて同じ物理で動く（`lib/glass-jelly.c`。ユーザーの提案 B5、2026-09-28）。矩形の 4 辺が、それぞれの目標（mark）へ自分のばねで向かう。進む向き（目標の中心の速さの tanh を、立ち上がり 30ms・減衰 120ms で平滑）の側の辺を `lead` だけ硬く、後ろを `lead` だけ柔らかくするので、動いている間は伸び、止まると前の辺が行き過ぎてから収まる。
進む向きの長さは目標の `min_stretch`〜`max_stretch` 倍に留める（1 枚のガラスで、2 本のばらばらな辺にしない）。物理は 2ms の刻み。

| 種類 | ω（rad/s） | 減衰比 | lead | 伸び | 使うもの |
|---|---|---|---|---|---|
| `glass_jelly_plate` | 48 | 0.45 | 0.35 | 0.6〜1.7 | トグル・タブの板のドラッグ（v0.9.1 でユーザーが決めた値）、スイッチ・スライダーのつまみ（縦には伸びない） |
| `glass_jelly_glide` | 26 | 1 | 0 | 0.6〜1.8 | タップ・`set_active` で動く板（v0.10.1。行き過ぎず伸びない。0.15 秒で 9 割、v0.9 の 280ms のスライドと同じくらい） |
| `glass_jelly_travel` | 16 | 0.62 | 0.35 | 0.6〜1.8 | モーフィング、グループへの出入り、ボタンから生えるメニュー。liquid_glass_widgets の morph のばね（剛性 120・減衰 16 → ω 11・0.73）を基に、辺の遅れの分だけ少し硬く |

- ゼリーはビュー（ポップオーバーは自分）の snapshot の中で、そのフレームの時刻とそのフレームの割り当て（§5.3-2）を目標に進める。tick は描き直しを頼むだけ。だから中身とガラスは同じフレームのまま。
- つまみ（`glass_panel_set_follows()`）は、ガラスが自分の位置からばねで吊られる: スライドや指で動くと伸び、止まると少し行き過ぎる。
- 動きを減らす（§13）ではばねを通さず、すぐ目標に置く（`glass_jelly_snap()`）。

---

## 7. GlassView の詳細

### 7.1 レイアウト

`GtkOverlay` と同じ規則にする（GTK の開発者が迷わないように）。

- `content` はビュー全体に広がる。ビューの自然な大きさは `content` の大きさ。
- overlay 子は、自分の `measure` の結果と `halign` / `valign` / `margin-*` で配置する。
- パネルの下に中身が隠れる分の余白（例: 最初の写真が上のパネルに隠れない）は、v1 ではアプリが中身に `margin` を付けて調整する。
  自動の「中身の余白」は v2 の候補だったが破棄した（§17.1）。部品が覆う量は `top-bar-height`・`content-inset` などで出す。

### 7.2 パネルの登録と、どこに置かれたかの判定

1. `GlassPanel` は `root` されたときに `gtk_widget_get_ancestor(self, GLASS_TYPE_VIEW)` で最も近いビューを探す。
2. 見つかったビューの **overlay 側の子孫**なら、ビューに登録する（ガラスはビューが描く）。
3. ビューが無い、または**ビューの content 側の子孫**なら、フォールバック（CSS）で自分で描く。
   Apple の HIG も、コンテンツ層の中にガラスを置くことを勧めていない。
4. `unroot` で登録を外す。
5. `GlassView` とパネルの間にあるコンテナは**背景を透明にしておく**こと（背景を塗るとガラスが隠れる）。文書と CSS の既定で示す。

### 7.3 背景色（backdrop-color）

中身が透明な部分（背景を塗らない `GtkScrolledWindow` など）は、取り込むと透明になり、ガラスが黒ずむ。
GTK は窓の CSS の背景を別の段で描くので、ビューからはノードとして取り出せない。

- 💡 ビューは CSS の背景を使わず（`glassview { background: none; }`）、`backdrop-color` の単色を content の下に自分で描く。
  **画面に出すものと取り込むものが同じ**になる。
- 既定（NULL）は窓の背景色。🔒 **CSS の `var(--window-bg-color)` を実行時に解決する**: ビューの内部に描かない子のノード（CSS 名 `backdrop`）を持ち、その `color` を `var(--window-bg-color)` にして `gtk_widget_get_color()` で読む。
  テーマ（ライト/ダーク、アクセント、Ubuntu の Yaru の変種）が変わると `css-changed` で追従する。値を固定で持たない（C1）。
- `GlassPanel` の中にあるビュー（入れ子）は、既定で背景色を塗らない（§6.6）。
- ふつうのダイアログ・ポップオーバーの中のビューは、その背景色（`--dialog-bg-color`・`--popover-bg-color`）を既定にする（v0.9。CSS の `dialog:not(.glass) glassview > backdrop` など）。
  デモのダイアログ（中身はふつう、Close ボタンだけがガラス）で、窓の背景色の四角が出ないように。

### 7.4 snapshot の手順（擬似コード）

```c
static void
glass_view_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  GlassView *self = GLASS_VIEW (widget);
  float scale = gdk_surface_get_scale (gtk_native_get_surface (gtk_widget_get_native (widget)));

  /* 1. 背景色 + 中身 */
  GtkSnapshot *tmp = gtk_snapshot_new ();
  gtk_snapshot_append_color (tmp, &self->backdrop_color, &view_bounds);
  gtk_widget_snapshot_child (widget, self->content, tmp);
  GskRenderNode *backdrop = gtk_snapshot_free_to_node (tmp);
  gtk_snapshot_append_node (snapshot, backdrop);

  /* 2. ガラスの本体（登録順 = 木の順序） */
  for (GlassPanel *panel in self->panels) {
    if (!panel_is_visible (panel)) continue;
    graphene_rect_t bounds;
    gtk_widget_compute_bounds (GTK_WIDGET (panel), widget, &bounds);   /* 今フレームの値 */
    GlassShape shape = glass_panel_build_shape (panel, &bounds, scale);
    GdkTexture *tex = glass_renderer_render_panel (self->renderer, backdrop, &shape,
                                                   glass_panel_get_material_state (panel),
                                                   &out_rect, &adaptive_stats);
    if (tex) {
      gtk_snapshot_append_texture (snapshot, tex, &out_rect);   /* パネル + 影の余白 */
      glass_panel_queue_adaptive (panel, &adaptive_stats);      /* 反映は次のフレームの前（§12.5） */
    }
  }

  /* 3. overlay 子（パネルの前景を含む） */
  for (child in self->overlays)
    gtk_widget_snapshot_child (widget, child, snapshot);

  gsk_render_node_unref (backdrop);
}
```

### 7.5 キャッシュ（何もしないで済むフレームを増やす）

パネルごとに 2 段のキャッシュを持つ。

| 段 | キー | 当たれば省略できるもの |
|---|---|---|
| 取り込み＋ぼかし | backdrop ノードの同一性、取り込み矩形、スケール、ぼかしのパラメータ | `render_texture`・読み出し・アップロード・ぼかし |
| ガラスパス | 上のキー、形状、材質、光学パラメータ、見た目（明/暗） | Glass Core の 1 パス（前回のテクスチャをそのまま追加する） |

- **ノードの同一性**: `gtk_widget_snapshot_child` は子のキャッシュ済みノードを translate で包むので、包むノードは毎回新しくなる。
  そこで transform ノードを剥がして、**子のノードのポインタ＋変換の値**で比べる。一致しなければ変わったとみなす（安全側）。
- 中身が静止していてパネルの文字だけ変わったフレームは、**GPU の仕事がゼロ**になる。
- スクロール中は毎フレーム取り込みが走る（想定どおり。予算は §8.8）。

### 7.6 v1 の制約

- パネル同士が重なっても、互いを屈折させない（どちらの背景も content だけ）。重ねないことを推奨する。
- パネルの子が画面外にはみ出しても、ガラスはパネルの矩形だけ。
- ビューの中にビューを入れ子にするのは可（内側のガラスは外側の取り込みに入る。依存は DAG のまま）。

---

## 8. Full レンダラの詳細

### 8.1 GL コンテキスト

- `GtkNative`（窓）ごとに 1 つの `GdkGLContext` を `gdk_surface_create_gl_context()` で作り、ビュー同士で共有する。
- **GtkGLArea と同じ方式**: snapshot の中で `gdk_gl_context_make_current()` → 描画 → `GdkGLTextureBuilder` でテクスチャにして追加する。GtkGLArea が snapshot の中で同じことをしているので、この部分の安全性は GTK 自身が示している。
- API: GLES 3.0 以上、または GL 3.3 core 以上。シェーダは読み込み時に `#version 300 es` か `#version 330 core` の行を付けて、両方に対応する（§10.4）。
- `unrealize` で GL の資源をすべて破棄する。

### 8.2 取り込み（中身を 1 枚の画像にする）

```
取り込み矩形 C = パネルの矩形 + ぼかしの余白（3σ）+ レンズの余白、をビューの範囲で切り詰めたもの
レンズの余白 = max(0, EDGE_LENS_REACH − min(パネルの幅, 高さ))   （EDGE_LENS_REACH = 96 論理 px）
縮小率 k   = 1 / blur-downscale（既定 1/2。既存拡張の glass-blur-downscale と同じ）
node       = transform( scale(S·k) · translate(−C.x, −C.y) ) { clip(C) { backdrop } }
tex        = gsk_renderer_render_texture (renderer, node, C を S·k 倍した矩形)
```

- `S` は `gdk_surface_get_scale()`（分数スケールに対応）。
- 🔒 **レンズの余白**: レンズは縁から内側へ最大 `EDGE_LENS_REACH` 先を読む。パネルがそれより薄いと反対側の縁の先に出るので、その分も取り込む。
  足りないと、縁の帯が取り込みの縁で固定された色の筋になる（`docs/memo.md` 地雷10）。
- 🔒 **隣り合うパネルは 1 回の取り込みを共有する**（2026-09-25、memo 地雷16）。取り込み 1 回のコストはほぼ固定（`render_texture` 0.5〜0.8ms）なので、
  同じぼかしで、和集合の面積が 2 つの和の 1.35 倍以下になるパネルの取り込み矩形をまとめる（ヘッダーのカプセルの行は 1 回、上と下のバーは別）。ガラスのパスはパネルごと。
- 🔒 **1 つのビューの取り込みは 1 回の描画にまとめる**（アトラス。2026-09-25、memo 追記7）: 古くなった取り込み矩形をすべて縦に積んだ 1 枚（間 2px）を
  1 回の `render_texture` で描いて読み出し、矩形ごとに分ける。画素が前回と同じなら（memcmp）ぼかしとパスを飛ばす（下の中身のノードが変わっても見た目が同じとき）。
- 🔒 **取り込みは専用の GL レンダラで描く**（`gsk_gl_renderer_new()` を窓のディスプレイで realize したもの。窓のレンダラが GL ならそれ）。
  `render_texture` の固定費が、窓の Vulkan レンダラの 0.25〜0.5ms から約 0.15ms に下がる。戻り値はどちらも dmabuf なので読み出しは残る。
  `GLASS_DEBUG=window-capture` で窓のレンダラに戻せる。
  ✅ **snapshot の中で `render_texture` を呼んでよい**（S1 で確認。Vulkan・GL の両レンダラ、Wayland・X11）。
- ⚠️ `render_texture` は GPU の完了を待つ（GSK がフレームの片付けでフェンスを待つ）。GPU は投入順に実行するので、**先に積まれた自前のパスの分も待つ**。
  GPU の仕事を減らすことは、そのまま CPU 時間を減らすことになる（§8.8）。
- 🔒 **GTK の描画 API（`render_texture`・`GdkTextureDownloader`）を呼んだ後は、自前の GL を続ける前に自分のコンテキストを current にし直す。**
  GTK の GL レンダラは自分のコンテキストを current にしたまま戻る。FBO と VAO はコンテキスト間で共有されないので、別の framebuffer に描いてしまう（`docs/memo.md` 地雷1）。
- 🔒 `GdkGLTexture` の release コールバックでは GL を呼ばない（`docs/memo.md` 地雷2）。

### 8.3 読み出しとアップロード

- `GdkTextureDownloader`、形式 `GDK_MEMORY_R8G8B8A8_PREMULTIPLIED`、色空間は sRGB を明示する（`gdk_texture_downloader_set_color_state`）。
- **読み出した画素から Adaptive の統計も取る**（§12。追加の読み出しは不要）。
- `glTexSubImage2D` で使い回しのテクスチャにアップロードする（大きさが変わったときだけ作り直す。行の幅は `GL_UNPACK_ROW_LENGTH`）。

### 8.4 ぼかし

**既存拡張と同じ式を C に移植する**（`liquidEffect.ts` の `_setGaussianBlurRadius()` と `_computeGaussianKernel()`）。

- `sigma_texel = min(radius / blur-downscale, 15)`。カーネルの形は `max(sigma_texel, 1.0)` で決める（小さい半径でも縮小のエイリアスを吸収する）。
- 片側のタップ数 `max(2, ceil(4σ))`、線形サンプリングでペアにまとめる。
- 水平 → 垂直の 2 パス。**出力先はパスごとに別のテクスチャ**（ピンポンしない。memo 5.3）。
- テクスチャの端は `GL_CLAMP_TO_EDGE`。ビューの端に接するパネルでも、透明が滲み込まない（GSK の blur ノードでは端が透明として扱われるので、ぼかしは GSK ではなく自前で行う）。
- Dual Kawase（`blur-method = 1`）は v1 では移植しない（既定は Gaussian）。必要になれば追加する。
- 🔒 **霜の雲**（v0.10。§10.3.2。v0.10.1 からどの材質も使っていない）: iOS 27 の体の材質では、取り込みごとに 2 つめのぼかし（σ 14 論理 px。テクセルでは上限 15 なので、3x では少し弱い）を同じ取り込みから作り、ガラスのパスが 2 つめのテクスチャとして読む。
  輝度の重みがあるとき（白を黒の `weight` 倍に数える）は、前のパスで `(rgb·w, w)`（w は最大 1 に正規化）を作ってからぼかし、縦のパスの最後で割り戻す（`unweight`）。どれも別のテクスチャに書く（DAG のまま）。

### 8.5 ガラスパス（Glass Core）

- 出力矩形 O = パネルの矩形 ＋ 影の余白（`shadow-radius`）。デバイス px の格子にそろえる（再サンプリングでぼやけないように）。
- フルスクリーンの四角形を 1 枚描き、Core の `glass_shade()` を呼ぶ（§10）。
- 入力:
  - ぼかし済みテクスチャ（取り込み矩形 C の座標系）と、その実際のテクセル数（既存の `blur_tex_w/h` と同じく、拡大時の滑らかな補間に使う）
  - 形状（矩形・半径。デバイス px）
  - 材質と光学パラメータ（論理 px の値を `S` 倍）
  - 見た目（Adaptive の結果）
- 🔒 **スーパーサンプリング（RGSS 4 サンプル、既定 ON）**: Core の `glass_shade(uv)` を 1 画素につき 4 回、1/4 画素ずつずらして呼び、平均する（出力は事前乗算なので単純平均でよい）。
  リムのフレネル項（幅 1px ほど）とレンズの畳み込みは画素より細かく変化するので、1 サンプルだと小さなカプセルの丸い端で段差が出る（`docs/memo.md` 地雷4・追記2）。
  各サンプルは Core の式そのものなので、見た目の設計は変わらない。コストは S1 で GPU busy% +1〜2 ポイント（スクロール中のみ）。
  品質を上げる機能なので既定 ON。比較用に個別の設定で OFF にできる（方針 1 は「品質を落とす最適化」の話なので当たらない）。
- 🔒 **測ったレンズのフットプリント（`lens_footprint_px`、既定 ON）**: スーパーサンプリングだけでは、縁の 1〜3px 内側の「崖」
  （1 画素が元の約 50px を掃く所）と、縁ちょうどの屈折の不連続で、屈折した細かい模様がジャギーになる（ユーザーの指摘、`docs/memo.md` 地雷9・追記5）。
  各サンプルが自分のフットプリントをレンズに通して追い、着地点の折れ線に沿って平均する。縁をまたぐ部分は屈折しない背景と割合で混ぜる。
  渡す値はサンプル間隔の半分（4 サンプルなら 0.25、1 サンプルなら 0.5。デバイス px なので `S` 倍しない）。
  256 サンプルの正解に対する最大誤差は 39〜63 → 14〜26（/255）。コストは GPU busy% +1 ポイント（スクロール中、パネル 2 枚）。
  縁の帯のサンプル数を増やす方法（16〜32 サンプル）は、誤差が同程度以上で +3〜7 ポイントだったので採らない。

### 8.6 出力テクスチャのプール

- パネルごとに最大 4 枚。`gdk_gl_texture_builder_build()` の破棄通知で、GTK が使い終わったテクスチャをプールに戻す。
- `GLsync` を `gdk_gl_texture_builder_set_sync()` で付ける（GTK 側が描画完了を待てるように）。
  パスごとには `glFlush` せず、ビュー（またはポップオーバー・ダイアログ）の描画の終わりに 1 回だけ flush する（`glass_renderer_flush`）。
  GTK はこのフェンスを別のコンテキストで待つので、flush は省けない。
- 全部が使用中なら、そのフレームは前回のテクスチャを再利用し（書き換え中のテクスチャを GTK に渡さない）、**次のフレームでもう一度描く**（v0.8。頼まないと古いガラスが残った。memo 地雷42）。プールは 4 枚（v0.8。3 枚から）

### 8.7 失敗時の扱い

| 失敗 | 扱い |
|---|---|
| GL コンテキストが作れない / realize に失敗 | その窓のビューはすべて FALLBACK。1 回だけ警告 |
| シェーダのコンパイル・リンクに失敗 | 同上。ログ（`G_MESSAGES_DEBUG=glass`）にコンパイラの出力を出す |
| `render_texture` が NULL / 例外 | そのフレームは前回のテクスチャ。3 フレーム続いたら FALLBACK |
| GL のエラー（`glGetError`、デバッグ時のみ確認） | ログ |

### 8.8 性能の予算と計測

- 目標（Radeon 780M、1x）: スクロール中の追加 CPU 時間が **1 窓あたり 3ms/frame 以下**。静止中はほぼ 0（キャッシュが当たる）。
- 見積り: 1200x240 の帯を半分（600x120）で読み出すと約 0.3ms（付録 A.2）。
- **計測点**（`GLASS_DEBUG=hud` でビューの隅に表示。デモの Lab からも切り替え可能）:
  取り込み / 読み出し / アップロード / ぼかし / ガラスパス の各 ms、キャッシュの当たり率、テクスチャのサイズ。
- 実測（2026-09-25、Glass Gallery の Photos を自動スクロール。ビュー 2 つ = ページのツールバーとスプリットビュー。取り込み 2 回/フレーム）:
  60fps、ガラスの CPU **3.0〜3.1ms/フレーム**。経過: 3.3ms（v0.3）→ アトラスと専用 GL レンダラで 2.8ms → §6.7 の部品（層・板・つまみ）で 3.3ms → 色収差の経路を 1 本にまとめて 3.0〜3.1ms。
  - 内訳（1 フレーム）: ページの取り込み 約 1.0ms（うちスクロール端の効果の GSK の blur が 約 0.35ms）と読み出し 0.2ms、
    サイドバーの取り込み 約 1.15ms（**うち約 0.6ms はページのガラスのパスが GPU で終わるのを待つ時間**）と読み出し 0.17ms、アップロード・ぼかし・パスの発行 約 0.3ms。
  - GPU（`GLASS_DEBUG=gpu-time`。パスごとの時間を 2 秒ごとにログへ）: ページのカプセル 1 枚 約 0.16ms × 7、サイドバーのパス 約 1.4ms。
    780M は軽負荷の間 800MHz のまま。小さなパスでも時間の大半は縁のレンズの帯（スーパーサンプリング × 測ったフットプリントの経路）。
  - 残りの原因は構造にある: サイドバーの取り込み矩形（ぼかしの余白）が、ページのヘッダーのカプセル（ガラスと前景）に重なる（Photos のヘッダーの先頭のカプセルはサイドバーのすぐ右）。
    そのため外のビューの取り込みは内のビューのガラスの完成を待ち、読み出しが CPU を通るので、その待ちが CPU 時間になる。
    サイドバーの取り込みを省くと（計測用）1.8ms/フレーム。
  - 案（保留の後、2026-09-26 に破棄。§17.1。ユーザーの判断、2026-09-25: 3ms を厳密に守る必要はなく、性能の問題も起きていないので今は現状のまま）: 内のビューの 1 回の描画で外のビューの取り込みも描き、内のビューのガラスと前景は GL で重ねる（§6.7 の層の合成をビューをまたいで行う）。
    外のビューは自分のノードの木で「内のビューの背景がそのまま見えている」ことを確かめてから使い、違えば今どおり自分で取り込む。見積り 2.2ms 前後（memo 追記7）。
- dGPU（PCIe）では読み出しが iGPU より遅い見込み。縮小（1/2、設定で 1/4）とキャッシュで吸収する。
  1/4 は見た目が落ちるので既定にしない（既存拡張の方針 6 と同じ）。

---

## 9. フォールバック（CSS）

GL が使えないとき、`GlassView` の外（または content 側）に置かれたパネル、`GLASS_RENDERER=fallback` のときは、パネル自身が CSS で描く。

```css
glasspanel.glass-fallback {
  background-color: alpha(white, 0.18);          /* 材質のティント */
  backdrop-filter: blur(12px) saturate(1.6);     /* GTK 4.22 の copy/paste ノード */
  box-shadow: inset 0 1px alpha(white, 0.45),    /* リムの近似 */
              inset 0 -1px alpha(black, 0.08),
              0 6px 18px alpha(black, 0.18);     /* 影 */
}
```

- 屈折はない（すりガラス）。材質ごとに値を変える。
- Adaptive はフォールバックでも動かす（§12.1 のフォールバック用の取り込み。✅ v0.8 で実装）。
- ハイコントラストのときは Full でもこの CSS に切り替え、背景を不透明にする（§13）。

---

## 10. シェーダ Core の切り出し

✅ **S5 で実装・検証済み（2026-09-24）。** 参照シェーダと 200 ケースで**ビット単位で一致**（差 0/255）。経緯は `docs/memo.md` 追記3。

### 10.1 構成（実装）

```
shaders/
├── reference/glass.frag          拡張の出荷版の無改変コピー（編集しない。README に出どころとハッシュ）
├── core/                         単一ソース
│   ├── glass_core.glsl           下の 5 つを順に #include する入口
│   ├── glass_params.glsl         uniform と定数（EDGE_LENS_FALLOFF / EDGE_LENS_REACH / EDGE_LENS_BAND、測ったフットプリントのタップ数）
│   ├── glass_shape.glsl          角丸矩形の SDF・超楕円の高さ・法線
│   ├── glass_surface.glsl        SCB・ディザ
│   ├── glass_optics.glsl         屈折・端の減衰・ぼかし済み背景の読み出し（フットプリントのタップ、測ったフットプリントの折れ線）
│   └── glass_shade.glsl          vec4 glass_shade(vec2 uv): 本体（早期リターン・影・レンズ・照明・合成）
├── targets/
│   ├── gl/glass.frag             glass-lib 用: GLASS_SAMPLE を定義し、Core ＋ スーパーサンプリングの main()
│   │                             （比較用に、縁の帯だけ N サンプルにする u_rim_samples もある。既定 0 = 使わない）
│   ├── gl/quad.vert              頂点バッファなしの全面の四角形（行 0 = 画像の上）
│   └── compat/                   参照シェーダを GL で動かす包み（テストと S1 の --reference 用）
│       ├── reference.frag        prelude ＋ reference/glass.frag ＋ postlude を #include
│       ├── cogl_prelude.glsl     Cogl の名前の代わり、main() の改名
│       └── cogl_postlude.glsl    改名した main() を呼ぶ main()（スーパーサンプリング付き）
└── meson.build                   #include の展開（tools/glsl-include.py）と glslangValidator の検査
```

- `#include` はビルド時に展開する。生成物は `build/shaders/glass-core.frag`・`glass-reference.frag`・`quad.vert` で、GResource に入れる。
- **式とその順序は参照から変えていない。** コメントは式の根拠だけを残し、拡張固有の経緯は省いた。
- Core の入口は `vec4 glass_shade(vec2 uv)`（事前乗算の色を返す）。背景の読み出しは、ターゲットが定義する `GLASS_SAMPLE(uv)` を通す。
- 将来、拡張機能も Core を使えるように、Cogl 用のターゲット（`GLASS_SAMPLE` = `texture2D(cogl_sampler1, uv)`）を足せば済む形にしてある（v1 では作らない）。

### 10.2 隠れた「FBO の大きさ」への依存を明示する（重要）

参照では、次の 2 つの値が `resolution`（FBO の大きさ）から決まっていた。
既存拡張のドック・メニューなどはモニタ全面の FBO なので、見た目は次の値で承認されている。

| 参照の式 | 全面 FBO（1920x1080）での値 | 小さなパネル（例 300x60）でそのまま使った場合 |
|---|---|---|
| `gradientStep = clamp(min(res) / 560, 0.45, 1.20)` | **1.20** | 0.45 → 法線の平滑化が弱まり、縁の見た目が変わる |
| `max_disp_px = 0.30 * min(res)` | **324px**（実質的に効かない） | 18px → 屈折が大きく弱まる |

→ Core では uniform にした。

| uniform | 意味 | glass-lib が渡す値 | 参照と同じにするには |
|---|---|---|---|
| `gradient_step` | 法線の有限差分の幅（px） | `1.2 × S` | 元の式で計算した値 |
| `max_displacement_px` | 変位の上限（px） | `324 × S` | `0.30 × min(res)` |
| `lens_px_scale` | `EDGE_LENS_REACH`（96px）・`EDGE_LENS_BAND`（22px）とフットプリントの上限（64px）の倍率 | `S`（HiDPI で論理サイズを保つ） | `1` |
| `edge_damping` | 面の端の 3% で屈折を弱める（参照の `stabilizedUV`） | `0`（出力はガラス＋余白だけ） | `1` |
| `lens_footprint_px` | レンズのフットプリントを測る幅（±px）。0 = 参照の解析的な見積もりと 10 タップ | サンプル間隔の半分（§8.5） | `0` |

- RGSS のタップの間隔（0.75〜2.5px）は画面の画素の話なので、スケールしない。
- `S` は `gdk_surface_get_scale()`。glass-lib の px 単位の値（半径・変位・色収差など）もすべて `S` 倍して渡す。

### 10.3 Core に入れたもの・入れなかったもの

| 入れた | 入れなかった |
|---|---|
| 形状・屈折・トーン・照明・影・AO・ディザ、2 つの早期リターン | `dock_*`・`isDock`・`padding` → 正確な矩形の `glass_rect` で受ける |
| `edge_taps_enabled`・`early_exit_enabled`（A/B 用。既定 1） | `panel_bg_*`・`panel_rect_*`（Quick Settings 専用の塗り） |
| `debug_view`（1・2 は参照と同じ、3 = レンズの変位の可視化） | `multi_region_mode` と領域の配列（v2 で融合と一緒に作り直す） |
|  | 未使用の `pointer_x/y`・`mouse_radius`・`bg_glow_intensity`・`intensity`・`blur_strength`・`cogl_sampler0` |

形状を配列で受ける形（v2 の融合用）は、融合を作るときに入れる。v1 は 1 パス 1 形状。

#### 10.3.1 レンズの帯と縁の影（v0.9、2026-09-27。拡張のシェーダと同時に変えた）

macOS 27 の実機のスクリーンショット（5K、同じ範囲をガラスあり／なしで撮った 9 組）から、縁からの深さ `u` ごとの変位 `D(u)` を相互相関で測った（`docs/memo.md` 追記16）。

- 🔒 **レンズの帯は定数 `EDGE_LENS_BAND`（22 論理 px × `lens_px_scale`）。** 以前は角の半径の幅でドームを立ち上げていた（`normalizedDepth()` と `bevelPx` が `corner_radius`）ので、角の半径の設定ごとに別のレンズになっていた。
  macOS は半径 48pt の丸いボタンと角の半径 27pt のウィジェットで `D(u)` が同じで、帯は大きさにも角の半径にもよらない。
  形の小さい方の半分（融合では混ぜた半径）が帯より小さいときは、帯・ドームの高さ・変位を同じ比で縮める（`lensBandFor()`・`lensScaleFor()`）。レンズの形は変わらず、小さなピルで反対側まで読みにいかない。
  帯の中のレンズの形は、これまでどおり `displacement_scale`・`max_z`・`profile_shape_n` が決める（`EDGE_LENS_FALLOFF` 2.4 はそのまま）。設定は増やしていない。
- 🔒 **内側の影（AO）は、リムの光が当たらない向きにだけ付く**（`ao × (1 − lightMask)`）。macOS の縁の最も外側の 1px は、光の軸（縦）を向く所で明るく、沿う所で背景の約 0.4 倍に暗い。
  `surface_light_enabled = 0`（拡張のアプリウィンドウ）では光が無いので、これまでどおり一周に付く。

#### 10.3.2 iOS 27 の材質（v0.10、2026-09-28。glass-lib だけ。参照・拡張は変えない）

`shaders/core/glass_material.glsl`。liquid_glass_widgets（Flutter）の `liquid_glass_render.frag` の iOS 27 の項（2026-09、まだリリース前。SwiftUI の `glassEffect(.regular)` と同じ画面で測った値）を、glass-lib の単位（論理 px × `lens_px_scale`。あちらは 3x の物理 px なので 1/3）に移した。
どれも uniform が 0 なら通らず、0 のとき `glass_shade()` は参照と同じ式（ゴールデン 250 件 0/255 のまま）。値は材質の固定値で、設定（`set_param` のキー）ではない（§11.2）。
v0.10.1 から、体（`glass_body_mode`・`frost_opacity`・`frost_clamp`）はどの材質も使っていない（ユーザーが霜の見た目を好まなかった。§11.2）。式と、ゴールデンのハーネスの検算は残してある。縁の項は全材質が使う。

| uniform | 意味 |
|---|---|
| `glass_body_mode` | 1 = iOS 27 の体: 霜（`GLASS_SAMPLE_FROST` = 同じ格子で 14pt ぼかした雲。レンズを通さず、その画素の位置で読む）を、屈折した写し（`GLASS_SAMPLE`）の上に `frost_opacity` で重ねる。写しは雲から `frost_clamp` 以上は離れない（正: それより暗くならない、負: 明るくならない）。次にティント（無彩色は混ぜる、有彩色は背景の輝度を保って色相を寄せる）、彩度（Rec. 709。背景の輝度で ±20%: 黒の上 1.2 倍、白の上 0.8 倍。測った値の前提）。0 = 参照の体（SCB、ティント） |
| `frost_opacity`・`frost_clamp` | 上のとおり |
| `rim_shade`・`rim_shade_ends` | 縁の 0.5pt の線: 深さ 0.25〜0.67 px（論理）の帯。色は縁の 0.83px 外の中身（屈折なし・ティントなし・彩度 1、縁に沿って 0.5px ずつ 3 タップ）で、そこから 0.314 × `rim_shade` × mix(`rim_shade_ends`, 1, (1−along²)^1.75) を引く（混ぜずに引くので、どの背景からも一定の段差）。along = 外向きの方向と光の進む向きの内積 |
| `rim_light` | 光の軸の両端の光の山: 進む先（ライトでは下）が 1、手前が 0.85（along³）。縁の線のすぐ内側の芯（減衰 0.5pt）と数 pt の裾（2.7pt）、体が明るいほど弱く（(1−輝度)^0.3。黒の上で +50/255、白に近い所で +27/255）、加える |
| `edge_absorption` | ベベル（深さ 32/3 px の四分円）を通る光の吸収: 縁で最大 `edge_absorption` × 1.4（光の手前）〜0.6（先） |

- 光の向きは `light_angle_deg` から: 90°（ライト）で上から、下の縁が光の山の強い側。ダークの材質は 270°（下から。あちらのダークの値）。
- 平らな内側の早期リターンは、縁の項が届く深さ（32/3 px）より内側だけにする。
- 霜の雲はレンダラが取り込みごとに作る（§8.4）。輝度の重み（白を黒の何倍に数えるか: 雲は `frost-weight`、写しは `blur-weight`）は、ぼかしの前のパスでアルファに入れ（RGBA8 に収まるよう最大 1 に正規化）、縦のぼかしの最後に割り戻す。
- 検査: ゴールデンのハーネスが、平らな黒と白の上で式どおりの値（中央 133・251/255、左端の線、下の縁の山）になることを確かめる（§10.5）。

#### 10.3.3 連続曲率の角・影・色収差・ハイライト（v0.11、2026-10-06。拡張のシェーダと同じ式）

拡張の `cf39b6d`（影）と `1fac2c7`（角・色収差・ハイライト）を Core に移した。参照も同じ版なので、ゴールデンで式の一致を確かめている（§10.5）。

- 🔒 **角**（`glass_shape.glsl`: `cornerShape()`・`sdRoundRect()`・`sdRoundRectDir()`）: 角は (半径, 指数 n) のスーパー楕円。半径は `r × k`（k = 1 + `corner_smoothing`、ただし形の小さい方の半分 / r を超えない）、n は「角の中点が円の角と同じ」条件 √2·k·(1 − 2^(−1/n)) = √2 − 1 から決める。
  スーパー楕円のノルムは距離ではないので、勾配の長さで割って、リム・ベベル・輪郭のぼかしの幅を一周同じにする。
  余裕のない角（ピルの端）は k が 1 に落ちて円になる。そのときは参照と同じ円の式を通す（`log2`・`pow` を省く。融合の部品はほとんどがピル）。
  - `corner_smoothing` はレンダラが 0.6（拡張の既定）で渡す。設定ではない。拡張の `corner_smoothing_enabled`（アプリの窓の角に合わせて円にする）は glass-lib に当たるものが無いので、uniform は積だけを受ける。
  - 角ごとの半径（`glass_corner_mode` 1）も、融合（`fusedSD()` の部品と橋）も同じ角を使う。CSS（フォールバック、子の `border-radius`）は円のまま（角の中点は同じ。ガラスの方が辺の付け根の近くで少し内側に入る）。
- 🔒 **影**: 0.18·erfc(2.619 t) + 1.17·erfc(0.895 t)（縁で 1.35 = 以前の本影 0.80 ＋ 半影 0.55 と同じ）に (1 − t²)³ を掛ける（t = d / 半径）。半径で値・傾き・曲率が 0 になるので、以前の `boundsMask` とハードカットは要らない。0 でない所だけ半 LSB のノイズを足す。
- 🔒 **色収差**: `chromaVec = disp × clamp(chroma_strength, 0, 1)`。赤は `refractedUv − chromaVec`、青は `+ chromaVec`（青がよく曲がる）。屈折が無い所では色が分かれない。
  青は屈折の (1 + chroma) 倍まで遠くを読むので、薄いパネルの取り込みの余白（`LENS_REACH`）も同じ倍率にした（`glass_capture_rect_for_panel()`）。
- 🔒 **ハイライトの色**（`glass_surface.glsl`: `backdropHighlight()`）: スクリーン合成した結果の Oklab の明度はそのまま、色度を下の色の 1.25 倍に置き換える（光の 1/4 までで入り切る）。色域を外れたら、暗くせずに無彩色の側へ寄せる。
  `highlight_backdrop_color` = 1 をレンダラが渡す（設定ではない）。glass-lib の材質は macOS のリム・スペキュラ・シーンが 0 なので、効くのは主に iOS 27 の縁の光の山（`ios27RimLight()`。白を足した後に同じ変換をする。glass-lib だけ）。

### 10.4 GLES 3.0 と GL 3.3 の両対応

- Core は GLSL 1.30 以降の書き方（`texture`、`in`/`out`）を使わず、ターゲットに任せる。精度は GLES で `precision highp float;`（ローダが付ける）。
- `meson test` で `glslangValidator` が Core・参照・quad を GLES 3.00 と GL 3.30 core の両方で検査する（6 件）。

### 10.5 ゴールデンテスト（`tests/golden/glass-golden.c`）

- Mesa の surfaceless EGL で窓なしの GLES 3.0 コンテキストを作り、参照（`compat/reference.frag`）と Core（`gl/glass.frag`）を同じ入力で描いて、画素を比べる。使えない環境では skip（77）。
- 場面: 11 通りの設定（拡張の値・アプリ内の値・色収差なし・早期リターンなし・デバッグ 1/2・スーパーサンプリング・2 倍スケール・小さい面・円の角・面の端）× 5 形状（カプセル・パネル・大・正方形・ピル）× 5 背景（縞・市松・グラデーション・同心円・ノイズ）= 275 ケース。
  v0.11 から、「円の角」以外は連続曲率の角（0.6）と背景色のハイライト、色収差は 0.15（屈折の割合）。
  ピル（高さ 24）は `EDGE_LENS_BAND` より薄く、レンズを縮める経路を通る（v0.9 で追加）。光学の値は拡張の既定（v0.9）。シーンだけは式を比べるために 0.08 にしてある。
- 受け入れ条件: 全画素で差が 1/255 以下。**結果は全ケースで 1/255 以内**（v0.10 までは 0/255。v0.11 で円の角を円の式で計算する近道などの丸めの差が出た）。
  角を一時的に円に戻すと 150 ケースが失敗する（v0.11 で確認）。
- 失敗したケースは `--write DIR` で参照・Core・差（32 倍）の PNG を保存できる。
- 空振りしていないことも確かめた。`EDGE_LENS_FALLOFF` を 2.4 → 2.5 に変えると 141 ケースが失敗する。

---

## 11. パラメータとマテリアル

### 11.1 光学パラメータ（全体の設定）

既定値は**既存拡張の gschema の既定値と同じ**（2026-09-27、v0.9 で両方を macOS 27 の実測に合わせて改訂。memo 追記16）。キー名も拡張機能と同じ意味・同じ単位にする。

| キー（`glass_context_set_param`） | 既定 | 単位 | 拡張機能のキー |
|---|---|---|---|
| `max-z` | 88.0 | px | `glass-max-z` |
| `displacement-scale` | 10.5 | px | `glass-displacement-scale` |
| `edge-smoothing` | 0.5 | px | `glass-edge-smoothing` |
| `profile-shape-n` | 3.6 | – | `glass-profile-shape-n` |
| `ior` | 2.40 | – | `glass-ior` |
| `chroma-strength` | 0.0 | 屈折の割合（0〜1。v0.11 から。以前は px） | `glass-chroma-strength` |
| `specular-intensity` | 0.0 | – | `glass-specular-intensity` |
| `shininess` | 42.0 | – | `glass-shininess` |
| `rim-width` | 2.3 | px | `glass-rim-width` |
| `rim-intensity` | 0.5 | – | `glass-rim-intensity` |
| `rim-directional-power` | 1.9 | – | `glass-rim-directional-power` |
| `rim-power` | 3.0 | – | `glass-rim-power` |
| `rim-light-color-intensity` | 1.0 | – | `glass-rim-light-color-intensity` |
| `sheen-intensity` | 0.0 | – | `glass-sheen-intensity` |
| `light-angle-deg` | 90.0 | deg | `glass-light-angle-deg` |
| `shadow-radius` | 50.0 | px | `shadow-radius` |
| `shadow-intensity` | 0.22 | – | `shadow-intensity` |
| `ao-intensity` | 0.65 | – | `glass-ao-intensity` |
| `ao-radius` | 1.0 | px | `glass-ao-radius` |
| `blur-radius` | 2.0 | px | `dock-blur-radius`（材質ごとの値を使う。§11.2） |
| `tint-strength` | 0.12 | – | `dock-tint-strength`（v0.8。材質ごとの値を使う。§11.2） |
| `blur-downscale` | 2 | 2 / 4 | `glass-blur-downscale` |

- px の値は**論理 px**。描画時に `S`（サーフェスのスケール）倍する。
- 影（`shadow-radius`・`shadow-intensity`）の表の値は拡張と同じだが、**アプリ内では材質ごとの値を使う**（§11.2）。拡張の値は壁紙の上に浮くドック・メニュー向けで、明るい窓の中の小さな部品には強すぎる（ユーザーの指摘、`docs/memo.md` 地雷5）。
- v0.9 の値の出どころ（memo 追記16）: レンズ（`max-z`・`displacement-scale`・`profile-shape-n`）は macOS の Dock とミニプレーヤーの `D(u)` に合わせた（rms 0.24pt）。
  リム・内側の影・輪郭のぼかしは、縁の画素に合わせた（光は縦 = 90°）。色収差 0 と `sheen` 0 は、macOS のガラスに色のにじみも面の光も無いため。影は Spotlight のパネルの横の減衰に合わせた。
- 範囲（min/max）は拡張機能の設定画面（`prefs.js`）と同じにする（実装時に写す）。範囲外は clamp して `g_warning`。
- **光学の設定はこれ以上増やさない**（既存拡張の方針 7）。`EDGE_LENS_FALLOFF`・`EDGE_LENS_REACH` は定数のまま。
  `tint-strength`（v0.8）は新しい光学の設定ではなく、材質が持っていたティントの強さを調整できるキーにしたもの（ユーザーの指示、2026-09-26）。
- 定義は `spec/params.json` に 1 か所で書き、C のヘッダ（と API ドキュメントの表、`tools/gen-params-doc.py`）をビルド時に生成する。
  公開ヘッダ `glass-params.h`（`tools/gen-params-header.py`）はキーごとの文字列の定数（`GLASS_PARAM_BLUR_RADIUS`、GI では `Glass.PARAM_BLUR_RADIUS`）で、キーの打ち間違いをコンパイル時（バインディングでは属性のエラー）に見つけるためのもの。
  内部の列挙（配列の添字）は `GLASS_PARAM_ID_BLUR_RADIUS` と名付けて区別する（v0.8.1）。

### 11.2 材質（v1 は 5 種類。`PROMINENT` は v0.9）

> 🔒 **v0.10 で体と縁を iOS 27 にした**（ユーザーの選択: 全部・板の材質、2026-09-28）。**v0.10.1 で体は v0.9 に戻した**（ユーザー: 霜の見た目が気に入らない、2026-09-28）。下の表と箇条のうち、ぼかし・ティント・彩度は v0.9 の値のままで、影と縁は次の表（`spec/params.json`。API ドキュメントの Parameters のページにも生成される）。
>
> | 材質 | 体（`surfaces`） | 縁（`outlines`） | ぼかし | ティント | 影 ライト / ダーク | 彩度 |
> |---|---|---|---|---|---|---|
> | `REGULAR` | plain | ios27 | 2 | 白 0.12 | 16px・0.015 / なし | 1.5 |
> | `CLEAR` | plain | ios27 | 1.5（縮小なし） | 白 0.04 | 16px・0.07 | 1.5 |
> | `THICK` | plain | ios27 | 12 | 窓の背景色 0.55 | 24px・0.07 | 1.5 |
> | `MENU` | plain | ios27 | 8 | ポップオーバーの背景色 0.45 | 24px・0.07 | 1.5 |
> | `PROMINENT` | plain | ios27 | 10 | アクセント 0.92 | 16px・0.07 | 1.5 |
>
> - 縁（`ios27-s`・`ios27-l` の縁の値）: macOS の縁（`rim-intensity`・`ao-intensity`）は 0 にし、iOS 27 の縁に置き換えた。レンズ（`thin`・`thick`）と `edge-smoothing` 0.4 はそのまま。
> - ダークの値（`values_dark`）: 光は下から（`light-angle-deg` 270、全材質）、REGULAR の影なし。値の決まり方（パネル → 全体 → 部品 → 材質 → 既定）の「材質」の段が、今の外観の値になる。
> - ~~REGULAR・THICK・MENU の体は iOS 27 の霜（14pt の雲を、0.6 でぼかした写しの上に 0.73 / ダーク 0.85。ティント #F8F8F8 0.53 / 白 0.12、彩度 2.1 / 1.4）~~ → v0.10.1 で v0.9 の体に戻した。`surfaces` の `ios27` と、その経路（霜の雲・輝度の重み。§8.4・§10.3.2）は残してあり、どの材質も使っていない。
> - つまみと板の plain の体（`glass_panel_set_plain_body()`）: 材質が霜の体に戻っても、つまみとトグルの板（下のガラスの上のレンズで、自分のティントを持つ）は plain のまま。今はどの材質も plain なので効果はない。
> - 取り込みの余白は、ぼかし（霜があればその大きい方）の 3σ。取り込みの共有（§8.2）は、ぼかしの組（半径・重み・霜）が同じものの間だけ。

| 材質 | 用途 | ぼかし半径 | ティント | 影（半径・強さ） | 取り込みの縮小 | 備考 |
|---|---|---|---|---|---|---|
| `REGULAR` | ツールバー、タイトル、ボタンの台 | 2px | 白 0.12 | 16px・0.07 | `blur-downscale` | ティントは既存拡張のドックの既定値と同じ。ぼかし（拡張のドックは 5）と影の半径は S1 でユーザーと比べて決めた。影の強さ 0.07 もユーザーの決定（2026-09-25） |
| `CLEAR` | 写真・動画の上 | 1.5px | 白 0.04 | 16px・0.07 | 1（縮小しない） | ぼかしが弱いと縮小が見えるため等倍。~~Adaptive が mixed のときは暗幕（黒 0.25）を足す~~（未実装のまま破棄。§17.1） |
| `THICK` | サイドバー（大きな板） | 12px | 窓の背景色 0.55（ライト/ダークに追従） | 24px・0.07 | `blur-downscale` | 大きな板は下の中身が場所ごとに違うので、前景色を切り替えず（Adaptive なし、テーマの色のまま）、濃いティントで読めるようにする |
| `MENU` | ポップオーバー・メニュー（§6.8） | 8px | ポップオーバーの背景色 0.45（テーマに追従） | 24px・0.07 | `blur-downscale` | `THICK` より透ける。前景色はテーマのまま（Adaptive なし）。値は初期案（2026-09-26） |
| `PROMINENT` | 確定のボタン（Done・Send・Close。SwiftUI の `.glassProminent`） | 10px | テーマのアクセント色（`--accent-bg-color`） 0.92 | 16px・0.07 | `blur-downscale` | v0.9（ユーザーの指示、2026-09-27）。前景はアクセントの前景色（`.glass-prominent`、Adaptive なし）。パネル自身の `tint` で色を変えられる（`Context:tint-color` はアクセントを置き換えない: アプリの見た目ではなく「確定」の印なので）。アクセントはパネルの描かない子ノード `accent` の CSS の色で読む |

ティントの列の数（0.12 など）は `tint-strength` の材質の値、色は材質の色（v0.8。`spec/params.json` では `values` の `tint-strength` と `tint` の rgb に分けた）。

- 🔒 **縁の値（アプリ内用）**（v0.9 で改訂、2026-09-27。memo 追記16）: 材質は縁に効く値（`edge_smoothing`・`rim_width`・`rim_power`・`rim_intensity`・`rim_directional_power`・`ao_radius`・`ao_intensity`・`chroma_strength`・`sheen_intensity`・`profile_shape_n`・`displacement_scale`・`max_z`）を材質自身の値として持つ。
  値は macOS 27 の実測から作ったプリセット（`spec/params.json` の `edge_presets`）:
  - `apple-s`（`REGULAR`・`THICK`・`MENU`）: Dock とミニプレーヤーの薄いレンズ。`max_z` 88、`profile_shape_n` 3.6、`displacement_scale` 10.5（縁で約 20pt、12pt の深さでほぼ 0）
  - `apple-l`（`CLEAR`）: 丸い動画ボタンとデスクトップのウィジェットの厚いレンズ。`max_z` 100、`profile_shape_n` 1.35、`displacement_scale` 26（縁で約 40pt、20pt の深さでほぼ 0）。天気アプリのカードがウィジェットに当たる
  - 共通: `edge_smoothing` 0.4、`rim_width` 2.3、`rim_power` 3、`rim_intensity` 0.5、`rim_directional_power` 1.9、`ao_radius` 0.6、`ao_intensity` 0.65、`chroma_strength` 0、`sheen_intensity` 0
  それまでの `crisp-soft`（S1 でユーザーが選んだ値。2026-09-25/26: `edge_smoothing` 0.75、`rim_width` 2、`rim_power` 9、`ao` 3px・0.10、`chroma_strength` 0.8、`profile_shape_n` 2.4、`max_z` 35、`displacement_scale` 45、`rim_directional_power` 1.6、`sheen` 0.08）は、縁で 65pt 動く強いレンズだった（macOS の Dock の約 3 倍）。
  縁の**線**（輪郭のぼかし・リム・内側の影）が細いという方向は同じで、レンズは弱く広くなった。設定の種類は増やしていない（値の選び方だけ）。
  スーパーサンプリング 4x と測ったフットプリントは既定 ON（§8.5）。
  §11.1 の全体の値（拡張と同じ）は、比較と大きなガラス用に残す（v0.9 からは `apple-s` と同じレンズ）。
- 🔒 **レンズの選択**（v0.9。ユーザーの選択、2026-09-27）: `apple-s`・`apple-l` のレンズの部分（`max-z`・`profile-shape-n`・`displacement-scale`）は `spec/params.json` の `lenses` に `thin`・`thick` として 1 か所で書き、
  列挙型 `GlassLens`（`THIN`・`THICK`）と `glass_panel_set_lens()`・`glass_context_set_lens()` で開発者が選べる（3 つの値をまとめてパネル／全体の値として設定するだけで、状態は持たない。個々の値は `set_param` で上書き、`reset_param` で戻る）。
  値だけを大文字の定数で出す案より、1 つ書き忘れて中途半端なレンズになることがない。新しい光学の設定ではない（既存の 3 つのキーの値の組）。
- 彩度: macOS はガラス越しの背景の彩度を上げている（測った 8 組すべてで 1.5〜1.9 倍）。レンダラは `saturation` を 1.5 で渡す（`GLASS_BACKDROP_SATURATION`。設定ではない。拡張も面ごとの彩度の既定を 1.5 にした）。v0.10 からは材質の体（`surfaces`）の値: plain は 1.5、ios27 はライト 2.1・ダーク 1.4。
- 見た目（明/暗）でティントを変えるか（Apple は変える）は、**v1 では変えない**（既存拡張と同じ: 白いティント固定、前景色だけ切り替える）。
  v0.7 までの「Lab での A/B（C3）」は破棄した。代わりにティントの色と強さを Lab で調整できるようにした（下）。
- 🔒 **ティント**（v0.8。ユーザーの指示、2026-09-26）: 強さは光学パラメータのキー `tint-strength`（0〜1。材質の値は上の表）、色は次のうち最初にあるもの。
  1. パネルの `tint`（強さも alpha で決める。スイッチ・スライダーのつまみ、トグルの板など、部品が自分で決めるティントもこれ）
  2. `GlassContext:tint-color`（全体。alpha は使わない）
  3. 材質の色（白、または `THICK`・`MENU` はテーマの窓・ポップオーバーの背景色）
  Lab の「Tint」で色と強さを変えると、自分のティントを持たないすべてのガラスに効く。透明度を下げる設定では強さを 0.6 以上にする（§13）。
- 🔒 **値の決まり方**（v0.8 で改訂）: 各キーの値は次の順に最初にあるもの。
  1. `glass_panel_set_param()` でそのパネルに設定した値（アプリが 1 枚だけ変える。例: 大きなカードだけレンズを強く）
  2. `glass_context_set_param()` で全体に設定した値（Lab）
  3. 部品自身の既定値（非公開。v1 ではスイッチ・スライダーのつまみの `blur-radius` 0 だけ。§6.7）
  4. 材質の値
  5. §11.1 の既定値（拡張と同じ）
  つまり Lab の値はすべての材質に効き（パネルに設定した値は除く）、`reset_param()` で次の段の値に戻る。
  パネルごとの値は設定の種類を増やすものではない（同じキーの効く範囲だけ。ユーザーの選択、2026-09-26）。
  ぼかし半径も同じ仕組みのキー `blur-radius` で扱う（既存拡張も要素ごとのぼかし半径を設定に持っているので、新しい光学の設定ではない）。
- アプリがパネルに設定できるのは `tint`・`corner-radius`（と角ごとの半径）・`has-shadow`・`material` と、上のパネルごとのパラメータ。

### 11.3 ~~既存拡張の設定に追従する~~（破棄）

❌ **破棄した**（ユーザーの決定、2026-09-26）。v0.7 までは v1.x の任意の機能として、サンドボックス外のアプリが既存拡張の設定（`/org/gnome/shell/extensions/liquid-glass/` の `glass-*`）を読んで見た目を合わせる案（`GlassContext:follow-shell-settings`）があった。
アプリ内のガラスは材質の値（§11.2）で調整し、拡張の値とは独立に決める。`spec/params.json` の `extension_key` は、値の出どころの記録として残す。

---

## 12. Adaptive（前景色の自動切り替え）

### 12.1 入力

- Full: §8.3 で読み出した取り込み画素（縮小済み・ぼかし前）を使う。
- フォールバック（✅ v0.8 で実装。ユーザーの指示「CSS の色自動切り替え」、2026-09-26）: ビューが自分の backdrop（背景色＋中身＋スクロール端の効果）を小さく `render_texture` して読み出す。
  - snapshot の中では、Adaptive のパネルの矩形（`compute_bounds`、§5.3-2）と backdrop のノードを覚えるだけ。描画と読み出しは snapshot の外（タイマー）で、窓のレンダラで行う。
  - 全パネルの矩形の和を 1/4（長い辺が 256px を超えるなら それ以下）で 1 回だけ描き、パネルごとに §12.2 の統計を取る。セルの最小はフォールバックの CSS のぼかし（12px）、ティントは CSS の白 0.18 で計算する。
  - 中身（ノードの同一性・位置）とパネルの矩形が前回と同じなら何もしない。変わっても最大 10 回/秒。
  - パネルの中のビュー（自分の backdrop が下のガラスで、読めない）・つまみのビュー・ハイコントラストでは測らない（テーマの色）。ビューの外やビューの中身の側に置いたパネルも測らない（テーマの色のまま。§7.2）。
  - 反映の仕方（EMA・反転の保持・次のフレームの前にクラスを付け替える）は Full と同じ（§12.3・§12.5）。テスト: `/widgets/fallback-adaptive`（白 → LIGHT、黒 → DARK）。
- **パネルの前景は取り込みに入っていない**ので、自分の文字色の変化が次の判定に影響しない。
  既存拡張の `SWITCH_SETTLE_MS`（自分の色の変化を測らないための待ち時間）は不要になる。

### 12.2 統計

1. パネルの矩形（影の余白は含めない）を、縦横比に合わせたグリッド（最大 16x4、最小 4x2）に分ける。
   セルの大きさはぼかし半径以上にする → **セルの平均 ≒ ぼかした後の値**になる（ぼかしを CPU でやり直さなくてよい）。
2. セルの平均色に SCB（brightness / contrast / saturation）を掛ける（ガラスのベースと同じ）。
3. sRGB → リニア → 相対輝度 `Y = 0.2126R + 0.7152G + 0.0722B`（既存の `_luminanceFromRgb` と同じ）。
4. `P10`・`P50`・`P90` を出す。

### 12.3 判定（既存の `contrastSampler.ts` の定数を移植）

- 候補: 明るい前景（`#f2f2f2`）と暗い前景（`#1a1a1a`）（既存の `lightTextColor` / `darkTextColor`）。
- スコア（WCAG のコントラスト比）:
  明るい前景は背景の明るい側で最悪になるので `CR(#f2f2f2, P90)`、暗い前景は暗い側で最悪になるので `CR(P10, #1a1a1a)`。
- 切り替え: 今と逆の側のスコアが **1.2 倍**（`SWITCH_ADVANTAGE`）を超えたときだけ反転する。
  `PREFER_*` のときは、好みの側へは 1.02 倍、逆へは 1.6 倍（既存と同じ非対称のしきい値）。
  両者の比が 1.15 未満（`AMBIGUOUS_RATIO`）のときは好みの側に決める。
- 時間方向: `P10/P50/P90` に **dt に依存する EMA**（τ = 150ms、α = 1 − exp(−dt/τ)）。反転した後 400ms は再反転しない。
- `adaptive = OFF` のときは判定しない（`appearance = UNKNOWN`、テーマの色のまま）。

### 12.4 既存拡張との対応

| glass-lib | 既存拡張 `adaptive-text-preference` |
|---|---|
| `AUTO` | `'auto'` |
| `PREFER_LIGHT`（ガラスが明るい見た目 = 暗い文字） | `'dark'`（暗い文字を好む） |
| `PREFER_DARK`（ガラスが暗い見た目 = 明るい文字） | `'light'`（明るい文字を好む） |

`spec/adaptive-vectors.json` に入力（セルの輝度の列・前の状態・経過時間）と期待される出力の組を置き、C の単体テストで確認する。
既存拡張の TS 実装も同じベクタで確認できるようにしておく（将来、拡張機能が Core と判定ロジックを共有するとき用）。

### 12.5 反映（snapshot の中でスタイルを変えない）

1. snapshot の中では結果を保存するだけ。
2. 見た目が変わったら、パネルの `gtk_widget_add_tick_callback()`（または idle）で**次のフレームの前**に
   `.glass-light` / `.glass-dark` を付け替え、`notify::appearance` を出す。
3. CSS 変数 `--glass-fg-color` などがクラスに応じて切り替わり、子のラベル・シンボリックアイコンの色が変わる。
4. 色の遷移は CSS の `transition: color 200ms`（動きを減らす設定のときは 0）。

前景色の反映が 1 フレーム遅れるが、200ms の遷移の中なので見えない。**ガラス自体（中身との一致）は同じフレームのまま**。

---

## 13. アクセシビリティ

| 状況 | 取得元 | 振る舞い |
|---|---|---|
| ハイコントラスト | `AdwStyleManager:high-contrast` | フォールバックの CSS に切り替え、背景を不透明（ティントの色を不透明に）、1px の枠線 |
| 透明度を下げる | `GlassContext:reduce-transparency`（GNOME には標準の設定が無い） | 屈折なし、ぼかし最大、ティントを濃く |
| アニメーションを切る | `gtk-enable-animations` | 前景色の遷移を 0ms に。モーフィング・Materialize・メニューの生え方・カプセルの出入り・ゼリーをしない（すぐ最後の形に） |
| 動きを減らす（v0.10） | `gtk-interface-reduced-motion`（GTK 4.22。GNOME 50 の `org.gnome.desktop.a11y.interface reduced-motion`） | 移動・伸び・行き過ぎ・膨らみをやめる: ゼリーはばねを通さず目標に（§6.9）、ドラッグ中の板の 2.5 倍の膨らみなし、`morph-id` の入れ替えと Materialize は大きさもぼかしも変えないフェードに、メニューはボタンから生えずに開く。フェードは残す（フェードは動きではない。liquid_glass_widgets と同じ扱い） |
| 支援技術 | `GlassPanel` の a11y ロールは `GTK_ACCESSIBLE_ROLE_GROUP` | 子の a11y はそのまま |
| フォーカスリング | CSS | ガラスの上でも見えるよう `--glass-fg-color` に追従させる |

---

## 14. デモアプリ Glass Gallery

- アプリ ID: `io.github.ryohsuke1231.GlassGallery`
- 言語: TypeScript → GJS。libadwaita ＋ `Glass`。

| ページ | 内容 | 確かめること |
|---|---|---|
| **Photos** | 写真のグリッドが、上のタイトルのパネルと下のツールバーのパネル（アイコンのボタン）の下を流れる | 屈折、**同じフレームでの一致**、前景色の自動切り替え、キャッシュ |
| **Playground** | ドラッグ・リサイズできるパネルを、切り替えられる背景（縞・市松・グラデーション・文字・写真・動くグラデーション）の上で動かす。材質・角の半径・ティント・影 | 形と材質の見え方、縁の屈折、端（ビューの端に接したとき） |
| **Controls** | §6.7 の部品（スイッチ・スライダー・融合するボタン・ボタンの列・メニュー・ダイアログ）を、切り替えられる背景の上に置く。融合するボタンは「⏮+⏵+⋯ − ☆ − 🗑」（+ はくっついて 1 つのカプセル、− は離れている）: 「⋯」で ☆ と 🗑 がカプセルからしずくのように出てきて、戻るときは吸い込まれる（v0.8。ユーザーの指示で、一律の隙間のスライダーは外した。`GlassGroup:spacing` は API に残す） | 層（ガラスの上のガラス）、融合の形とモーフィング、つまみのレンズ |
| **Tabs** | `AdwViewStack` の 3 ページ（写真・文章・色）の上に浮くタブバー。右下の検索ボタンが検索欄に変わる（`morph-id`）。検索は文章のページを絞り込む。1 つのタブに注意の点。開いた画像は写真のタブに 1 枚で出る（v0.10.1） | タブバーの板、検索欄、モーフィング |
| **Lab** | 光学パラメータ（§11.1）のスライダー、ティント（色と強さ。v0.8）、Full / フォールバックの切り替え、透明度を下げる、HUD（§8.8）。値は `GlassContext` に設定するので、アプリのすべてのガラスに効く | 画質と性能の比較、既定値を決める |

- 動く背景（Moving）は、Playground・Controls・Lab で**再生／一時停止と速度**（0.25〜4 倍、対数の目盛り）を変えられる。操作部品はガラスのボタンと `GlassSlider`（Lab ではインスペクタの中）。
  既定値を探すときに、背景の動きを止めて見比べたり、ゆっくり動かして縁の追従を見たりするため（ユーザーの提案、2026-09-25）。

- 写真は同梱しない（ライセンスのため）。実行時に `/usr/share/backgrounds` の画像を読み（Flatpak ではホストのものを `/run/host` から。`--filesystem=host-os:ro`）、加えて「フォルダを開く」（ファイル選択のポータル）と、コードで生成するテスト模様を使う。
- 開発中は `meson devenv` でビルドしたライブラリ（typelib）を使って `gjs -m` で起動する。Flatpak は §15.3。

### 14.1 ショーケース用デモ: 天気アプリ Glass Weather（✅ v0.8 で実装）

Glass Gallery は**検証用のハーネス**。開発者を惹きつけるための「見せる」デモは別に作る。ユーザーの提案で**天気アプリ**にする（2026-09-24）。

**実装（2026-09-26）**: `demo/src/weather/`（`main.ts`・`window.ts`・`api.ts`・`sky.ts`・`conditions.ts`）。アプリ ID `io.github.ryohsuke1231.GlassWeather`、起動は `meson devenv -C build -w . gjs -m demo/dist/weather/main.js`。

- レイアウトは macOS の天気アプリをまねる（v0.8。ユーザーの指示、2026-09-27）:
  - `GlassSplitView` の開閉できるサイドバー（Glass Gallery と同じ）: 一番上に開いた検索欄、その下に場所の一覧（名前・その場所の現在時刻・少し大きめの気温。ホバーで × が出て消せる）。検索中は一覧の代わりに候補を出す。
  - 右上に閉じた検索ボタン（ガラスのボタン。ヘッダーの ⋯ のカプセルの左）。押すとガラスが検索欄に変わり（`morph-id`）、候補はその下のガラスのポップオーバー（`MENU`）に出る。
  - 中身: 現在の天気（空に直接白い文字）→ 1 時間ごと（48 時間分。幅いっぱい。収まらないときは横のスクロールバー）→ その下の左 1/3 に 10 日、右 2/3 に 2×2 の 4 枚（体感と湿度のゲージ・風の方位盤・1 時間ごとの降水量の棒（横にスクロール）・日の出から日の入りの弧と今の太陽）。幅が 720px 未満（小さな窓、サイドバーで狭いとき）は 10 日の下に 4 枚。
  - カードは `CLEAR` のガラス（ユーザーの指示）。文字の色は空の明暗で切り替わる（昼は暗い文字、夜は明るい文字）。
- 構成: `GlassSplitView` の中身が `GlassToolbarView`（ヘッダーが浮く）、その中に `GlassView`（中身 = 空、overlay = スクロールするカード）。
  ビューを 2 段にするのは、1 つのビューではガラスの本体をすべて描いてから前景を描くので、ヘッダーの下を通るカードの文字がヘッダーのガラスの上に出てしまうため（§7.6。外のビューなら、カードとその文字をふつうの画像として屈折する）。
  1/3・2/3 の並びは小さな自前のレイアウト（`ThirdsLayout`）: 最小の幅は縦に積んだときのもの。`GtkGrid` の同じ幅の 3 列だと、窓の最小の幅が約 1060px になり、狭くできなかった。`AdwBreakpointBin` はスクロールする中身を窓の高さで切ってしまうので使わない（memo 追記12）。
- 空はコードで描く（晴れ・曇り・霧・雨・雪・雷 × 昼・夜。雲が流れ、雨・雪が降る。「動きを減らす」では止まる）。画像のライセンスの問題が無い。
  🔒 **描画ノード（グラデーション・色）で描く。cairo は使わない**（v0.9。memo 追記17）: cairo のノードは描かれるたびに CPU でラスタライズされ（同じノードでも GSK はキャッシュしない）、
  窓の描画とガラスの取り込み（`render_texture`）のたびに 1400×900 で 16〜24ms かかっていた（動く空は毎フレーム、しかも 1 フレームに複数回）。描画ノードなら GPU が描く。
  Glass Gallery の背景（Canvas）も同じ理由で描画ノードにした。**ガラスの下で毎フレーム変わる中身を cairo で描かない**ことは、glass-lib を使うアプリへの助言でもある（取り込みのたびに描き直される）。
- °C / °F は `GlassToggleGroup`。場所・単位は `~/.config/glass-weather/state.json`（初回は東京・ロンドン・ニューヨーク）。
- メニューの「Preview Sky」で空を切り替えて、ガラスと文字の色の見え方をどの空でも確かめられる（天気は作り物になり、そう表示する）。
- データは Open-Meteo（下の表）。予報は 15 分キャッシュ（`~/.cache/glass-weather/`）。通信できないときは古いキャッシュ、無ければサンプルのデータを、そう表示して出す。出典「Weather data by Open-Meteo.com」をデータの横に表示（2026-09-26 に規約を再確認: 非営利なら無料、CC BY 4.0、出典のリンクが要る）。
- 環境変数: `GLASS_WEATHER_SKY=clear-day|rain-night|…`（空のプレビュー）、`GLASS_WEATHER_OFFLINE=1`（通信しない）。
- やらなかったこと: 現在地（GeoClue のポータル）。都市の検索で足りるため。

**天気アプリが向いている理由**

- ガラスの見せ場が自然に揃う。空の上に浮く現在の天気のカード、横にスクロールする時間ごとの予報の帯、週間予報のリストがあり、中身がガラスの下を流れる。
- 背景（空）を**コードで生成**できる。晴れ・曇り・雨・夜のグラデーションと動く雲や雨粒を描けば、画像のライセンス問題が無い。動く背景の上のガラスは屈折がよく見える。
- 昼は明るく夜は暗い背景になるので、**前景色の自動切り替え（Adaptive）**を自然に見せられる。
- v1 の範囲（ただのガラスのパネル）だけで作れる。

**データ（無料で使える公開 API。2026-09-24 に規約を確認）**

| API | キー | 条件 | ライセンス・表示 |
|---|---|---|---|
| [Open-Meteo](https://open-meteo.com/)（予報・ジオコーディング） | 不要 | **非営利に限る**（有料プラン・広告のないアプリは可）。上限 600 回/分・5,000 回/時・10,000 回/日 | データは CC BY 4.0。アプリ内に出典の表示が要る |
| [MET Norway Locationforecast](https://api.met.no/)（予備） | 不要 | アプリ名と連絡先を入れた **User-Agent が必須**。ローカルにキャッシュし、`If-Modified-Since` と `Expires` を守る。座標は小数 4 桁まで | CC BY 4.0（出典・ライセンスへのリンク・改変の有無の表示） |

- 既定は Open-Meteo（キー不要・扱いが簡単）。規約は実装のときに再確認する（⚠️ 変わっている可能性がある）。
- 位置: 都市名の検索（Open-Meteo のジオコーディング）を基本とし、現在地は位置情報のポータル（GeoClue）でユーザーが許可したときだけ使う。
- アイコン: Adwaita のシンボリックアイコン（`weather-clear-symbolic` など）を使う（同梱不要）。
- 通信: libsoup 3（GJS から `Soup 3.0`）。取得結果をキャッシュし、起動のたびに API を叩かない。
- 言語: Glass Gallery と同じ TypeScript → GJS。Flatpak で配布（`--share=network`）。

---

## 15. リポジトリ構成・ビルド・開発環境

### 15.1 構成（v0.8 時点）

```
glass-lib/
├── README.md  LICENSE（MIT）  .gitignore
├── meson.build  meson_options.txt   オプション: introspection, tests, examples, documentation, demos, spikes
├── docs/
│   ├── design.md              ← 本書
│   ├── memo.md                地雷の記録（既存リポジトリと同じ運用）
│   ├── reference/             API ドキュメント（gi-docgen）の設定と、ガイドのページ（英語）
│   ├── screenshots/           README の画像（写真を含まない画面だけ）
│   └── archive/design-v0.1.md
├── spec/
│   ├── params.json            光学パラメータと材質の定義（C ヘッダと API ドキュメントの表を生成）
│   └── adaptive-vectors.json  Adaptive のテストベクタ
├── shaders/                   §10.1
├── lib/
│   ├── glass.h  glass-version.h.in  glass-main.c  glass-context.[ch]  glass-view.[ch]  glass-panel.[ch] …（公開の部品ごと）
│   ├── render/  glass-renderer.[ch]  glass-gl.[ch]   （非公開）
│   ├── adaptive/  glass-adaptive.[ch]   （非公開）
│   └── style/  glass.css
├── demo/                      TypeScript: src/main.ts + src/pages/（Glass Gallery）、src/weather/（Glass Weather）、data/（デスクトップ・metainfo・アイコン）、meson.build（-Ddemos= でインストール）
├── examples/                  最小の完全なアプリ（C・Python・GJS。C は -Dexamples でビルドして確かめる）
├── build-aux/flatpak/         2 つのデモの Flatpak マニフェスト（§15.3）
├── tests/                     C の単体テスト、ウィジェット、Python、golden/（Core と参照の比較。§10.5）
├── spikes/                    Phase 0 の試作（s1-full-renderer）
└── tools/                     glsl-include.py（#include の展開）、gen-params.py・gen-params-doc.py（params.json から生成）
```

### 15.2 開発環境（このマシン）

```bash
# ライブラリのビルドに要るもの（2026-09-24 に全部導入済み: gtk4 4.22.4, libadwaita-1 1.9.1, epoxy 1.5.10, GI 1.86.0）
sudo apt install libgtk-4-dev libadwaita-1-dev libepoxy-dev   # gcc, meson, ninja, gobject-introspection は元から導入済み

# ビルド
meson setup build && meson compile -C build
meson test -C build

# デモ（Node 22 は導入済み）
cd demo && npm install && npm run build
meson devenv -C build -w . gjs -m demo/dist/main.js           # Glass Gallery
meson devenv -C build -w . gjs -m demo/dist/weather/main.js   # Glass Weather

# 例
build/examples/hello-glass
meson devenv -C build -w . python3 examples/hello-glass.py
meson devenv -C build -w . gjs -m examples/hello-glass.js
```

- デモの型: `@girs/gtk-4.0`・`@girs/adw-1`・`@girs/soup-3.0` と、ビルドした `Glass-1.gir` から `@ts-for-gir/cli` で生成した型。
- 依存: gtk4 ≥ 4.22、libadwaita-1 ≥ 1.9、epoxy。

### 15.3 Flatpak（v0.8）

- マニフェストは `build-aux/flatpak/io.github.ryohsuke1231.GlassGallery.json` と `…GlassWeather.json`。ランタイムは `org.gnome.Platform//50`、SDK 拡張は `node22`・`typescript`（25.08）。
- 1 つのモジュールで、リポジトリ（`dir` の source）を meson でビルドする（`-Dtests=false -Dspikes=false -Dexamples=false -Ddemos=gallery` など）。
  デモの TypeScript は `meson install` のときに `demo/install-js.sh` が `tsc --noCheck` で JavaScript にする（Flatpak のビルドはネットワークを使えず、`npm install` で型のパッケージを入れられないため。型の検査は開発の `npm run build` で行う）。
- 権限: 両方 `--share=ipc --socket=wayland --socket=fallback-x11 --device=dri`。Gallery はホストの壁紙を読むため `--filesystem=host-os:ro`、Weather は `--share=network`。
- このマシンで両方をビルドし、インストールして起動した（2026-09-26。GL のガラスが描かれ、天気のデータも取れた）。
  `org.flatpak.Builder` の Flatpak で作るときは、ユーザーのインストールを見せるために `--env=FLATPAK_USER_DIR=$HOME/.local/share/flatpak`、FUSE が使えないので `--disable-rofiles-fuse` が要った（README）。
- 🔒 **インストールはホストの `flatpak` で行う**（2026-09-27。ユーザーの指摘: rofi の drun から起動できなかった）。flatpak はインストールのときにデスクトップファイルの `Exec` を「インストールした `flatpak` のパス」で書き換える。
  サンドボックスの中の flatpak-builder から `--install` すると `/app/bin/flatpak run …` になり、ホストには無い。`--repo=repo` でリポジトリに出し、`flatpak build-bundle` と `flatpak install --bundle`（ホスト）で入れる（memo 地雷38）。

### 15.4 API ドキュメント（v0.8）

- gi-docgen（`-Ddocumentation=true`）。設定は `docs/reference/glass.toml.in`、ガイドのページ（英語）は同じディレクトリ: Getting Started・How the Glass Works・Tuning・Parameters and Materials（`spec/params.json` から生成）・Styling・Debugging。
- `--fatal-warnings` でビルドする。公開 API の説明が欠けていると失敗する（`gi-docgen check` で 0 件にした）。
- このマシンには gi-docgen が無いので、確かめるときはスクラッチの venv に入れて、別のビルドディレクトリで行った（`build/` を gi-docgen に依存させない）。

### 15.5 Git の運用

- `main` はリリース用、作業は `dev` ブランチ（既存リポジトリと同じ）。
- 最初のコミットで `dev` を作る。

---

## 16. テスト

| 対象 | 方法 | 合格条件 |
|---|---|---|
| シェーダ Core | ゴールデンテスト（§10.5、`meson test`） | 参照 `glass.frag` と 1/255 以内（実績 1/255。v0.10 までは 0/255）。iOS 27 の材質（§10.3.2）は、平らな黒と白の上で式どおり（中央の色 ±2/255、左端の線、下の縁の山） |
| シェーダの構文 | `glslangValidator`（GLES 3.0 / GL 3.3） | エラーなし |
| ぼかし | カーネルの係数を既存拡張の TS 実装と比較 | 係数が一致 |
| Adaptive | `adaptive-vectors.json` を C で再生（`tests/test-adaptive.c`） | 全ベクタで一致 ✅ |
| パラメータ | clamp・既定値・`list_params`・材質の解決・ティントの色と強さの解決（`tests/test-params.c`） | 表（§11.1・§11.2）と一致 ✅ |
| ウィジェット | 登録と解除、content 側・入れ子・レンダラ設定でのフォールバック、部品の報告値、ヘッダーのカプセルの表示（見える子がいる間だけ）、ボタンの列、スイッチ・スライダーの値と役割、グループの登録、メニューの組み立て、ダイアログの中身、ガラスの上のボタンが枠なしであること、下だけが変わったときの入れ子のビュー・ダイアログの描き直し、角ごとの半径、モーフィング（グループへの出入りと `morph-id` の入れ替え）、タブバー（スタックと両方向に連動）、検索欄（`GtkEditable`・`search-changed`）、フォールバックの Adaptive（白 → LIGHT・黒 → DARK、クラスは常に 1 つ）、パネルごとのパラメータとティントの優先順位、`css-classes` を置き換えたときにライブラリのクラスが戻ること、ゼリー（伸びる・行き過ぎる・収まる）、動きを減らす設定の読み取り（`tests/test-widgets.c`、30 件。ディスプレイが要る。描画を確かめるものは、窓にフレームが来ないと落ちる: memo 地雷49） | 警告なし ✅ |
| Python | PyGObject から公開 API を一通り使う: out 引数（`get_param_range`・`get_tint`・`get_corner_radii`・`get_tint_color`）、プロパティ、シグナル、インタフェース（`GtkEditable`）、パネルごとのパラメータ、Python でのサブクラス化、全部品を 1 つの窓に置いて連動を確かめる（`tests/test-python.py`。PyGObject とディスプレイが要る） | 失敗なし ✅ |
| 例・ドキュメント | C の例は `-Dexamples`（既定 ON）でビルドする。API ドキュメントは gi-docgen の `--fatal-warnings` | ビルドが通る ✅ |
| 性能 | デモの HUD、GPU busy%（既存の計測方法） | §8.8 の予算内 |
| 手動 | ライト/ダーク、ハイコントラスト、Vulkan / GL（`GSK_RENDERER=gl`）、X11、Flatpak（2026-09-26 に 2 つのデモで確認）。HiDPI（2x・分数スケール）は利用者のフィードバックに任せる（§17） | 文字が常に読める・ずれがない |

---

## 17. ロードマップ

規模は S（小）/ M（中）/ L（大）の相対値。**各スパイクの結果は `docs/memo.md` に記録し、この文書に反映する。**

### Phase 0: スパイク

| ID | 内容 | Go の基準 | 規模 |
|---|---|---|---|
| S1 | **Full レンダラの最小試作**（`spikes/s1-full-renderer`）: スクロールする中身の上にパネル 2 枚。取り込み → 読み出し → GL のぼかし → 元の `glass.frag` を互換プレリュードで動かす → `GdkGLTexture` | (1) 中身とガラスのずれが 0 フレーム、(2) 780M・1x でスクロール中 60fps、(3) 追加の CPU ≤ 3ms/frame、(4) Vulkan と GL の両レンダラで動く、(5) snapshot 中の `render_texture` が安全（または専用レンダラで回避できる） | ✅ **Go**（2026-09-24）。(2)〜(5) は満たした。パネル 2 枚で平均 1.4〜1.5 ms/frame、最大 3.5 ms。(1) は構造上保証済みで、ユーザーの目視でも問題なし（リサイズも正常）。ユーザーの指摘で影を弱め、縁のジャギーをスーパーサンプリングで直した（`docs/memo.md` 追記2）。詳細は試作の README と `docs/memo.md` 追記1 |
| S5 | **Core の切り出しとハーネス**（§10） | 元と 1 LSB 以内 | ✅ **Go**（2026-09-24）。200 ケースで差 0/255。S1 も Core を既定にした |
| ~~S4~~ | 窓の同定（Tier 2 用） | ✅ 完了（§2.3） | – |

S2（GSK だけで描く Lite）、S3（拡張機能での blit）、S6（GNOME 51）は、v1 の範囲外になったので行わない。

### Phase 1: 土台（M）— 完了

- ✅ meson の骨組み、`spec/params.json` とヘッダの生成、シェーダ Core、ハーネスとゴールデン画像（Phase 0 と Phase 2 の中で作った）
- ❌ CI（GitHub Actions）は破棄（§17.1）

### Phase 2: ライブラリ本体（L）— 実装済み（2026-09-25。memo 追記6）

- ✅ `glass_init`、`GlassContext`、`GlassView`、`GlassPanel`、Full レンダラ、フォールバック、Adaptive、CSS、GIR/typelib
- ✅ ウィンドウ部品群（§6.6。2026-09-25 のユーザーの決定で v2 から前倒し）
- ✅ §6.7 の部品（層・押下・融合・ボタンの列・スイッチ・スライダー・ポップオーバー／メニュー・ダイアログ。2026-09-25 のユーザーの提案で v2 から前倒し）
- ✅ スクロール中の CPU: 3.3 → 3.0〜3.1ms/フレーム（§8.8）。予算の 3ms をわずかに超えるが、ユーザーの判断で現状のままとした（2026-09-25）。残りの案（ビューをまたぐ取り込みの共有）は破棄（§17.1）
- ✅ §6.8 の部品（タブバー・検索欄・モーフィング・材質 `MENU`・角ごとの半径。2026-09-26 のユーザーの提案で v2 から前倒し）
- ✅ Python からの利用の確認（2026-09-26。`tests/test-python.py`。cairo で描く `GtkDrawingArea` をアプリが使うには、別に `python3-gi-cairo` が要る。glass-lib の問題ではない）
- ✅ フォールバックの Adaptive（§12.1。2026-09-26）
- ✅ ティントの調整（`tint-strength`・`tint-color`）、パネルごとのパラメータ（§11.2。2026-09-26）
- ➖ HiDPI（2x・分数スケール）: 自分では確かめない。利用者のフィードバックに任せる（ユーザーの決定、2026-09-26）。値はすべて論理 px で持ち、描くときに `gdk_surface_get_scale()` 倍にしている（§10.2）
- ❌ dGPU（外付け GPU など）での確認: 破棄（ハードウェアが無く実験が難しい。ユーザーの決定、2026-09-26）

### Phase 3: デモ（M）— 完了（2026-09-26）

- ✅ Glass Gallery: Photos / Playground / Controls（§6.7 の部品。融合のデモは v0.8 で「+」と「−」の並びに）/ Tabs（§6.8 の部品）/ Lab（ティントも）
- ✅ Glass Weather（§14.1）
- ✅ 材質の値（C2）: ユーザーが Lab で調整して既定値を決める運用にした（v0.8 の `max_z`・`profile_shape_n`・`rim_directional_power` はその結果）

### Phase 4: 仕上げ（M）— 完了（2026-09-26）

- ✅ API ドキュメント（gi-docgen。§15.4）、`examples/`（C・Python・GJS）
- ✅ Flatpak（2 つのデモ。§15.3）
- ✅ README（スクリーンショット付き）

### 17.1 破棄したもの（ユーザーの指示「ロードマップに残っているものは破棄」、2026-09-26）

| 破棄したもの | 元の場所 |
|---|---|
| CI（GitHub Actions: ビルド・単体テスト・シェーダ検査） | Phase 1 |
| GNOME 51 のランタイムでの CI | Phase 4、§18 |
| dGPU（外付け GPU など）での確認 | Phase 2、§8.8、§18 |
| 性能の調整（スクロール中 3.0〜3.1ms/フレームのまま） | Phase 4 |
| アクセシビリティの手動の確認（ハイコントラスト・透明度を下げる・動きを減らす） | Phase 4、§16 |
| 「見た目に応じたティント」（Apple 流）の A/B（C3） | Phase 3、§11.2、§20。ティントを Lab で調整できるようにしたことで代える |
| 既存拡張の設定への追従（`follow-shell-settings`） | v1.x、§11.3（ユーザーの明示の指示） |
| `CLEAR` の材質で、明暗が混ざるときに暗幕を足す | §11.2（未実装のまま） |
| 中身の自動の余白 | §19 |
| 既存拡張が Core を使う（シェーダの一本化） | §19 |
| 上流 GTK への提案（displacement ノードの公開、dmabuf の取り出し） | §19、§18 |
| ビューをまたぐ取り込みの共有（スクロール中 3.0 → 約 2.2ms/フレームの案） | §19、§8.8 |
| 天気アプリの現在地（GeoClue のポータル） | §14.1（都市の検索で足りる） |

HiDPI の確認は破棄ではなく、利用者のフィードバックで対応する。

---

## 18. リスク

| リスク | 影響 | 対策 |
|---|---|---|
| snapshot 中の `render_texture` が GTK と衝突する | Full が使えない | ✅ S1 で衝突しないことを確認。ただし GL レンダラでは current のコンテキストが変わる（§8.2、地雷1） |
| dGPU で読み出しが遅い | スクロール中にカクつく | 縮小（1/2、設定で 1/4）とキャッシュ。確認は行わない（§17.1）。問題が報告されたら `blur-downscale` とキャッシュで対応する |
| パネルを重ねたときに互いを屈折しない | 見た目の違和感 | ✅ 同じビューの中は層として屈折する（§6.7）。別のビューの間はふつうの画像として見える |
| content の背景が透明だとガラスが黒ずむ | 見た目 | `backdrop-color`（§7.3） |
| ビューとパネルの間のコンテナに背景がある | ガラスが隠れる | 文書と CSS の既定、デバッグ時の警告 |
| GTK の将来の変更（4.24 以降） | 動作の変化 | 公開 API だけを使う（GNOME 51 の CI は破棄。§17.1） |
| HiDPI（2x・分数スケール）で見た目がずれる | 縁・屈折の見え方 | 値は論理 px で持ち、描くときにスケール倍にする（§10.2）。確認は利用者のフィードバックに任せる（§17） |
| 名前「Liquid Glass」への言及 | 商標 | ライブラリ名は glass-lib。README に Apple との無関係を明記 |

---

## 19. v2 以降の候補（保留したもの）

2026-09-26 のユーザーの指示で、残っていた候補はすべて破棄した（§17.1）。記録として残す。

| 候補 | 入口（v1 の何を拡張するか） |
|---|---|
| ~~ガラスの融合・押したときの膨らみ・ガラスの上のガラス・Switch・Slider・SegmentedControl~~ | ✅ v0.4 で v1 に入れた（§6.7） |
| ~~モーフィング、TabBar、SearchEntry、MENU 材質、角ごとの半径~~ | ✅ v0.7 で v1 に入れた（§6.8。ユーザーの提案、2026-09-26） |
| ~~中身の自動の余白~~ | ❌ 破棄。パネルに隠れる分の余白をビューが中身に伝える案 |
| ~~Tier 2（窓が透けるガラス）・GNOME 51 の `ext-background-effect-v1`~~ | ❌ 計画から外した（§2.2。ユーザーの決定、2026-09-26） |
| ~~既存拡張の設定への追従~~ | ❌ 破棄（§11.3） |
| ~~既存拡張が Core を使う（シェーダの一本化）~~ | ❌ 破棄。§10.2 の明示化で、拡張機能は今と同じ値を渡せば一致する（入口は残っている） |
| ~~上流 GTK への提案（displacement ノードの公開、dmabuf の取り出し）~~ | ❌ 破棄 |
| ~~ビューをまたぐ取り込みの共有（スクロール中の CPU を 3.0 → 約 2.2ms/フレームに）~~ | ❌ 破棄（§8.8 の案） |

### 19.1 macOS のようなウィンドウ部品群を既定で提供するか（2026-09-24 のユーザーの問い）

> **2026-09-25 更新**: ユーザーの決定で、優先度 1〜3（ツールバー・スクロール端・サイドバー・セグメント・ボタン）を v1 に前倒しした（§6.6）。
> 以下の表の 4・5（ダイアログ・ポップオーバー・スイッチ・スライダー）は v2 のまま。
> **2026-09-25 更新（2）**: ユーザーの提案で 4・5 も v1 に入れた（§6.7）。ポップオーバーは「親の窓のノードを借りる」方式で描けた。

**結論（2026-09-24 時点）: 提供するべき。ただし v2 以降で、libadwaita の部品の「置き換え版」として作る。**
macOS Tahoe では、ツールバー・サイドバー・ポップオーバー・メニュー・セグメントが**何もしなくても**ガラスになる。
アプリの開発者に一番効くのは「部品を差し替えるだけで窓全体が Liquid Glass らしくなる」こと。
libadwaita のアプリはすでに `AdwToolbarView`・`AdwHeaderBar`・`AdwNavigationSplitView` などで窓を組んでいるので、同じ使い方の Glass 版を用意すれば移行は数行で済む。
v1 の範囲（ただのガラス）は変えない（決定事項 9）。以下は v1 の後に作る順の計画。

| macOS Tahoe の部品 | libadwaita の対応 | glass-lib の案（仮称） | 技術的な注意 | 優先度 |
|---|---|---|---|---|
| ツールバー（窓の上） | `AdwToolbarView` ＋ `AdwHeaderBar` | `GlassToolbarView`: 中身がバーの下まで伸び、ボタンはガラスのカプセルにまとまって浮く | v1 の `GlassView` の延長。窓のボタン（閉じる等）との並び、ドラッグ領域 | 1 |
| スクロール端の効果（soft / hard） | なし（上のバーの影だけ） | `GlassToolbarView` の機能 | ぼかし＋マスクで中身の端を薄める。hard（列ヘッダ等）は均一の帯 | 1 |
| サイドバー | `AdwNavigationSplitView` / `AdwOverlaySplitView` | `GlassSidebar`: 窓の中に浮く角丸の板（大きい要素用の材質） | 大きい要素は明暗を**切り替えず**濃さを連続的に変える（§12・v0.1 §9.4）。角ごとの半径 | 2 |
| セグメント | `AdwToggleGroup` | `GlassToggleGroup`: 選択がガラスの滴として移動 | 融合（smooth union）と純レンズの層が要る | 3 |
| ボタン（.glass / .glassProminent） | `GtkButton` ＋ CSS | ボタンのスタイルクラス | 押下の反応（interactive） | 3 |
| ダイアログ・シート | `AdwDialog`（窓の中に浮くとき） | Glass 版の背景 | 窓の中に描かれるので、アプリ内ガラスで描ける | 4 |
| ポップオーバー・コンテキストメニュー | `GtkPopover` / `GtkPopoverMenu` | `GlassPopover` | **別の Wayland サーフェス（xdg_popup）**なので、今の方式（同じ窓の中身を取り込む）がそのままでは使えない。親の `GlassView` から背景を借りる方式を検討。当面は不透明な MENU 材質 | 5（難） |
| スイッチ・スライダー（操作中だけレンズ） | `GtkSwitch` / `GtkScale` | 専用部品 | 純レンズの層 | 5 |

参考資料: `docs/research/macos-tahoe-liquid-glass-widgets.md`（ユーザーが ChatGPT Deep Research で作成した調査。推測を含むので、そのまま仕様にはしない）。

---

## 20. 残っている確認事項

| # | 内容 | 決め方 |
|---|---|---|
| ~~C1~~ | `backdrop-color` の既定値 | ✅ 固定値にせず、実行時に CSS の `var(--window-bg-color)` を解決して使う（このマシンの libadwaita 1.9 ではライト `#fafafb`・ダーク `#222226`。Ubuntu の Yaru の色の変種でも正しくなる）。§7.3 |
| ~~C2~~ | 材質 `REGULAR` / `CLEAR` の値 | ✅ ユーザーが Lab で調整して決める運用にした。v0.8 で `max_z` 35・`profile_shape_n` 2.4・`rim_directional_power` 1.6（§11.2） |
| ~~C3~~ | 見た目に応じたティント（Apple 流）を既定にするか | ❌ 破棄（§17.1）。ティントの色と強さを Lab で調整できるようにした（§11.2） |
| ~~C4~~ | snapshot 中の `render_texture` の安全性 | ✅ S1 で解決（安全。専用レンダラは不要） |
| ~~C5~~ | 既存拡張の `prefs.js` の各パラメータの範囲 | ✅ `spec/params.json` に写した |
| ~~C6~~ | アプリ内の縁の値とぼかし半径 | ✅ ユーザーが決定（2026-09-25）: `crisp-soft`・ぼかし 2px・測ったフットプリント・スーパーサンプリング 4x |

---

## 付録 A: 計測値と確認結果

### A.1 窓の同定（S4、2026-09-24、ユーザー実行）

```
GNOME Shell-Message: 05:58:01.652: [lg-dbusid] windows
kitty |  |  |  |  | wayland
org.gnome.Settings | org.gnome.Settings | :1.57 | /org/gnome/Settings/window/1 |  | wayland
```

- GtkApplicationWindow（設定）は unique name と窓のパスが取れる。GTK 以外（kitty）は取れない。
- `get_tag()` はどちらも空（GTK 4.22 は xdg-toplevel-tag を設定しない）。

### A.2 GTK の描画（2026-09-23、このマシン）

| 計測 | 結果 |
|---|---|
| 既定のレンダラ | GskVulkanRenderer |
| `render_texture` の戻り値 | Vulkan・GL とも `GdkDmabufTexture` |
| `render_texture`（単色 512x256、20 回平均） | Vulkan 0.26ms / GL 0.24ms |
| `render_texture`（色ノード 60 個、Python でのノード生成込み） | 600x120: 1.30ms / 1200x240: 1.31ms / 1920x1080: 1.70ms |
| CPU への読み出し | 600x120: 0.27ms / 1200x240: 0.84ms / 1920x1080: 3.75ms |
| `GskGLShaderNode` | 両レンダラで非対応（出力はピンク） |
| CSS `backdrop-filter` | 動作 |
| アプリの GL コンテキスト | GLES 3.2 |

---

## 付録 B: 既存拡張から引き継ぐ方針

既存リポジトリの `memo.md` の「ユーザーの方針」のうち、glass-lib にも当てはまるもの。

1. 品質を落とす最適化は、**個別の設定**としてだけ提供する（プリセットにまとめない）。
2. `glass.frag` の平坦部を 1 タップにする最適化はしない。`blur-downscale = 4` は既定にしない。
3. **光学の設定はこれ以上増やさない。** `EDGE_LENS_FALLOFF`・`EDGE_LENS_REACH` は定数。
4. 設定は意味のある値だけを選べる形にする。
5. 着手前に memo とコードのコメントを読み、地雷を踏もうとしていないか確認する。ただしコメントを鵜呑みにせず深く推論する。
6. 気がかりがあれば押し通さずに止まって相談する。返答は日本語。
