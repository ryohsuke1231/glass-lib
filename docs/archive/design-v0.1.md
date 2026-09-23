> **アーカイブ（v0.1、2026-09-23）**: 範囲と決定事項は v0.2 で変わりました。現行の設計は [`../design.md`](../design.md) です。この版では仮の名前空間 `Lg` を使っています。

# Liquid Glass アプリケーション基盤 — システム設計書

- 版: v0.1（設計検討版・実装未着手）
- 日付: 2026-09-23
- 対象: GNOME 50（主対象）/ GNOME 51（追随）、Wayland、GTK 4.22+ / libadwaita 1.9+
- 元資料: `~/Downloads/Liquid Glass System Design Specification.md`（以下「元仕様」）
- 既存資産: `~/Projects/GitHub/liquid-glass`（GNOME Shell 拡張。`shaders/glass.frag`、`memo.md`、`performance-plan.md`）

凡例: ✅ = このマシンまたは一次情報で確認済み / ⚠️ = 要検証（スパイクで確かめる）/ 💡 = 提案・判断

---

## 目次

0. [要約](#0-要約)
1. [前提と事実確認](#1-前提と事実確認)
2. [元仕様のレビュー](#2-元仕様のレビュー誤り改善点)
3. [Liquid Glass を分解する](#3-liquid-glass-を分解する設計の出発点)
4. [全体アーキテクチャ](#4-全体アーキテクチャ)
5. [シェーダ設計（Glass Core）](#5-シェーダ設計glass-core)
6. [マテリアルモデル](#6-マテリアルモデル)
7. [Tier 1: アプリ内ガラス](#7-tier-1-アプリ内ガラスlibliquidglass-のレンダラ)
8. [Tier 2: デスクトップガラス](#8-tier-2-デスクトップガラスコンポジタ側)
9. [Adaptive Appearance](#9-adaptive-appearance)
10. [ウィジェットとデザインシステム（Apple 風ウィジェットの質問への回答）](#10-ウィジェットとデザインシステム)
11. [公開 API](#11-公開-apic--gobject-introspection)
12. [デモアプリ](#12-デモアプリ)
13. [リポジトリ・ビルド・配布・ライセンス](#13-リポジトリビルド配布ライセンス)
14. [テスト戦略](#14-テスト戦略)
15. [ロードマップ](#15-ロードマップ)
16. [リスクと対策](#16-リスクと対策)
17. [未決事項（ユーザーへの質問）](#17-未決事項ユーザーへの質問)
- [付録 A: 検証用 Looking Glass スクリプト](#付録-a-検証用-looking-glass-スクリプト)
- [付録 B: 計測値](#付録-b-このマシンでの計測値2026-09-23)
- [付録 C: 参考資料](#付録-c-参考資料)

---

## 0. 要約

### 0.1 一言で

**ガラスを 2 つの場所で描く。** アプリの中身の上に浮くガラス（ツールバー、タブバー、ボタン）はアプリ側のライブラリが描き、
デスクトップ（壁紙や他の窓）が透けるガラスはコンポジタ側（GNOME Shell 拡張）が描く。
シェーダ（`glass.frag` を分解した Glass Core）とマテリアル定義は 1 つのソースを両方で共有する。

### 0.2 元仕様からの主な変更

| # | 元仕様 | 本設計 | 理由（詳細は §2） |
|---|---|---|---|
| 1 | ガラスはすべてコンポジタ（Mutter）が描く | **2 階層**: アプリ内 = ライブラリ、デスクトップ = コンポジタ | Apple の Liquid Glass の主用途は「アプリの中身の上に浮くコントロール層」。1 枚のバッファからは前景と背景を分離できないため、コンポジタでは描けない |
| 2 | 独自 Wayland プロトコル `zlg_liquid_glass_v1` | GNOME 50 では **D-Bus**（拡張機能に Wayland グローバルは追加できない）。GNOME 51 以降は標準の **`ext-background-effect-v1`**（ぼかしのみ）を代替手段として使う。独自プロトコルは将来の Mutter パッチ用の草案にとどめる | Mutter 50 は独自プロトコルを受け付けない。Mutter 51 が標準のぼかしプロトコルを実装した |
| 3 | Ping-Pong バッファで多段ガラス | **Ping-Pong は使わない。** コンポジタでは描画済みフレームバッファを背景として読むので自然に多段になる。アプリ内では同じグループの形状を smooth union で 1 パスに融合する | Cogl の遅延実行ではピンポンの行き来が依存関係の閉路になる（memo 地雷3） |
| 4 | 全ガラスで 1 本のブラーを共有 | 共有できるのは**同じ z 層の中だけ** | 窓ごとに背後にあるものが違う |
| 5 | 解析値（輝度・コントラスト・彩度）を固定小数点で通知 | **量子化した値だけ**を、**持ち主にだけ**、**頻度と最小サイズに上限を付けて**送る | 細かい値を送ると、小さなガラスを動かして背後の画面を再構成できる（サイドチャネル） |
| 6 | Rust 実装 + C ABI | **C (GObject) + GObject Introspection** | Python/GJS/Vala/Rust のどれからでも使えるのは GI。Rust からは GIR を生成しにくい |
| 7 | 解析入力は「Glass Base」 | さらに**見た目（light/dark）に応じて変わるティントを掛ける前**の値を解析する | 「見た目 → ティント → 輝度 → 見た目」というフィードバックループを断つ |
| 8 | ウィジェット・デモ・テスト計画なし | ガラスと一体の最小ウィジェットセット、デモアプリ、ゴールデン画像テスト、Go/No-Go 付きのスパイクを追加 | — |

### 0.3 最重要の事実（今日確認したもの）

- ✅ **GTK 4.22 はカスタムシェーダを描けない。** `GskGLShader` は Vulkan・GL の両レンダラで `The renderer does not support gl shaders` になり、ピンク色で塗られる（実測）。
- ✅ **GTK 4.22 には `backdrop-filter` と copy/paste ノードがある**（公開 API: `gtk_snapshot_push_copy` / `gtk_snapshot_append_paste`）。すりガラスまではネイティブで描ける（実測）。ただし屈折に必要な `GskDisplacementNode` は非公開のまま（GTK 4.24 でも非公開）。
- ✅ **`gsk_renderer_render_texture()` は GPU 上の dmabuf テクスチャを返す**（Vulkan・GL とも `GdkDmabufTexture`）。CPU への読み出しは 600x120 で 0.27ms、1200x240 で 0.84ms（Radeon 780M。付録 B）。
- ✅ **Mutter 50 は背景ぼかしのプロトコルを実装していない。** **Mutter 51（GNOME 51、2026-09-16 リリース）は `ext-background-effect-v1` を実装した**（MR !5071）。実装はフレームバッファを blit してぼかし、再描画クリップと遮蔽カリングを広げる方式。半径は 24px 固定で、拡張機能から差し替えられる公開 API は無い。
- ✅ GTK 4.23.3 以降（= 4.24）は `ext-background-effect` プロトコルに対応した。
- ✅ **拡張機能から窓を安全に同定できる。** GTK4 は `gtk_surface1.set_dbus_properties` で窓の D-Bus パス（`%s/window/%d`）を送っており、Mutter はそれを `MetaWindow.get_gtk_window_object_path()` / `get_gtk_unique_bus_name()` として GJS に公開している。
- ✅ `Clutter.BlitNode` / `Clutter.LayerNode.new_to_framebuffer` / `Clutter.BlurNode` は GJS から使える。拡張機能だけで「フレームバッファを blit して背景にする」方式を試せる。

---

## 1. 前提と事実確認

### 1.1 環境（このマシン）

| 項目 | 値 | 確認 |
|---|---|---|
| GNOME Shell / Mutter | 50.1 / 50.1（libmutter-18、Clutter 18） | ✅ dpkg |
| GTK / libadwaita | 4.22.4 / 1.9.1 | ✅ 実行時に取得 |
| GJS / PyGObject | 1.88.0 / 3.56.2 | ✅ |
| wayland-protocols | 1.47（staging に `ext-background-effect` あり） | ✅ |
| 既定の GSK レンダラ | **GskVulkanRenderer**（Wayland） | ✅ 実測 |
| アプリ用 GL コンテキスト | GLES 3.2（`gdk_surface_create_gl_context`） | ✅ 実測 |
| GPU | Radeon 780M（iGPU、メモリは CPU と共有） | memo 0.5 |
| ツールチェーン | gcc, meson 1.10, cargo/rustc, flatpak, glslangValidator。**libgtk-4-dev / libadwaita-1-dev / valac / blueprint-compiler は未導入** | ✅ |

### 1.2 GTK 4.22 の描画能力（アプリ内ガラスの前提）

| 機能 | 状態 | 意味 |
|---|---|---|
| `GskGLShaderNode`（カスタム GLSL） | ✅ **使えない**。両レンダラで compile が失敗し、ノードは「未対応」のピンクで塗られる | GSK のノードの中で `glass.frag` を走らせる道は閉じている |
| copy / paste ノード（4.22 で追加） | ✅ 公開 API。CSS `backdrop-filter` もこれで動く（ストライプ背景がぼける＝実測） | 背景を GSK の中で扱える。すりガラス（blur、彩度、色行列）はネイティブで描ける |
| blur / color-matrix / component-transfer / blend（screen、multiply など）/ mask / composite / isolation | ✅ 公開 | すりガラス、トーン調整、照明の合成まではネイティブで組める |
| displacement ノード（SVG `feDisplacementMap` 用） | ✅ **内部には在る**（`gskgpudisplacement` シェーダ）が **非公開**。4.24 でも非公開 | これが公開されれば、屈折も含めてネイティブに描ける → 上流への提案候補 |
| GtkSvg の `BackgroundImage` | ✅ ウィジェットの背景にはならない（実測でフィルタ出力が透明） | SVG フィルタを経由する抜け道は無い |
| `gsk_renderer_render_texture` | ✅ GPU 上に `GdkDmabufTexture` を返す。平均 0.24〜0.26ms（単色ノード） | 背景を 1 枚のテクスチャとして得られる |
| dmabuf テクスチャから fd を取り出す公開 API | ✅ **無い**（`gdk_dmabuf_texture_*` は `get_type` のみ） | 自前の GL へゼロコピーで渡せない。`GdkTextureDownloader` による CPU 経由の読み出しになる |
| `GdkGLTextureBuilder`（自前の GL テクスチャを GTK に渡す） | ✅ 公開（4.12+） | 出力側はゼロコピーで渡せる（GtkGLArea と同じ経路） |

### 1.3 Wayland / Mutter

- ✅ `ext-background-effect-v1`（staging）: `set_blur_region(wl_region)` で指定した領域の背後をぼかすよう頼むだけのプロトコル。ダブルバッファで、`wl_surface.commit` の時点で反映される。**材質パラメータは無い**（ぼかしの方式はコンポジタが決める）。1 つの `wl_surface` に 2 つ目を作るとプロトコルエラー `background_effect_exists` になる。
- ✅ Mutter 50.1 はこのプロトコルを実装していない（`libmutter-18.so` のシンボルで確認）。
- ✅ Mutter 51 の MR !5071（パッチの時点の内容）:
  - `MetaSurfaceContent` がサーフェスの中身より先に `meta_background_effect_paint_blur_region()` を呼ぶ。`clutter_blur_node_new_from_framebuffer()` が**フレームバッファを blit** してぼかす（半径 24px、彩度 1.25、ノイズ 0.015、すべて固定）。
  - `meta_stage_add_redraw_clip_filter()`（private）で**再描画クリップをサンプル範囲まで広げる**。`subtract_opaque_region()` では**遮蔽カリングからサンプル範囲を外す**。ペイントボリュームも広げる。
  - 公開 API（`src/meta/*.h`）や GI には出ていない → **拡張機能から blur をガラスに差し替えることはできない**（⚠️ マージ版で最終確認）。
- ✅ GNOME Shell 拡張は Wayland グローバルを追加できない（Mutter に公開 API が無い）。→ 拡張機能だけで独自プロトコルを受けることは不可能。

### 1.4 拡張機能から使える部品（GI、Mutter 50.1）

| 部品 | 用途 |
|---|---|
| `MetaWindow.get_gtk_unique_bus_name()` / `get_gtk_window_object_path()` / `get_gtk_application_id()` | D-Bus の送信者と窓を安全に対応付ける（§8.4） |
| `MetaWindow.get_tag()` | xdg-toplevel-tag（GTK 4.22 には設定する公開 API が無いので補助扱い） |
| `Clutter.BlitNode.new(src)` + `add_blit_rectangle` | その時点のフレームバッファを背景として取り込む（B2 方式、§8.6） |
| `Clutter.LayerNode.new_to_framebuffer` / `Clutter.PipelineNode` | 既存の LiquidEffect と同じくパスをペイントノードとして組む（memo 5.1） |
| `Clutter.BlurNode` | Mutter 内蔵のぼかしノード（比較用） |
| `MetaWindowActor::damaged` | 再描画クリップを広げる代わり（B2 の穴埋め） |

### 1.5 既存 `glass.frag` の分析

構成（1379 行）: SDF（角丸矩形）→ 高さプロファイル（超楕円 n）→ 法線（SDF 方向の中心差分）→ Snell の屈折 → 端のレンズ（`EDGE_LENS_FALLOFF 2.4`、`EDGE_LENS_REACH 96px`）→ 異方性フットプリントのタップ（10 タップ）→ 色収差（px 単位）→ SCB → 2 層のティント（base / custom）→ 内側 AO → リム・スペキュラ・シーン（スクリーン合成、色相を保つクランプ）→ ドロップシャドウ（umbra + penumbra、quintic）→ 事前乗算合成 → ディザ。

新基盤から見た性質:

1. ✅ **サンプリングはぼかし済みレイヤ（`cogl_sampler1`）だけ。** シャープなレイヤ（`cogl_sampler0`）は宣言されているが `main()` では読まれない。→ 背景は**ぼかしてから縮小した 1 枚**で足りる。アプリ内レンダラの転送量を大きく減らせる（§7.3）。
2. すべて **px 単位**（displacement・chroma・shadow・AO）。→ HiDPI や分数スケールでは、デバイス px への換算を 1 か所にまとめる。
3. **拡張専用のものが混ざっている:** `dock_*`、`isDock`、`panel_bg_*`、`panel_rect_*`、`debug_view`、`early_exit_enabled`、`edge_taps_enabled`、`multi_region_mode`。→ Core とターゲット別ラッパに分ける（§5）。
4. **マルチ領域は「一番近い 1 つを選ぶ」だけ**（`findActiveRegion`）。形状は融合しない。→ Liquid Glass の特徴である「液体のようにくっつく・分かれる」には **smooth union** が要る（§5.3）。
5. 角の半径は 1 つ（`corner_radius`）。→ 窓の角やサイドバーには**角ごとの半径**（vec4）が要る。
6. 解析的な早期リターン（外側・平坦な内側）は性能の要で、正しさの根拠がコメントに残っている。**融合形状では平坦な内側の条件が変わる**ので、条件を作り直す必要がある。
7. ユーザー方針（memo 0.3-7）: **光学の設定はこれ以上増やさない**。`EDGE_LENS_FALLOFF` / `EDGE_LENS_REACH` は定数。→ Core でも定数のまま。材質の差はプリセット（§6）で表し、新しいノブは増やさない。

### 1.6 memo.md の教訓のうち、新設計に直接効くもの

| memo | 新設計での扱い |
|---|---|
| 地雷1〜3（即時描画禁止・パスごとのパイプライン・DAG 化、ピンポン禁止） | コンポジタ側のパスはすべてペイントノードで組み、DAG を守る。元仕様の Ping-Pong は採用しない |
| 地雷15・25（hide/unmap すると damage を報告できない → opacity を使う） | サービスが作るアクターはすべて opacity で出し入れする |
| 地雷17（コストは「塗る面積」ではなく「何回ガラスを走らせるか」） | 最適化は「パス数を減らす」を最優先にする（アプリ内のグループ融合、キャッシュ） |
| 地雷18・追記26（later で読む座標は 1 フレーム古い） | 幾何は**描画時**に読む（アプリ内: snapshot 中に `compute_bounds`。コンポジタ: paint 時） |
| 地雷22・23（Clone は damage クリップを継承する） | B2（blit）方式ならクローン自体が不要になる。B1 を使う場合は既存の対策をそのまま使う |
| 地雷30（入れ子の paint は実行フェーズで走る。「今の framebuffer」で判定する） | B2 では blit 元のフレームバッファが**ステージビューのものか**を毎回確認する（オフスクリーンに描かれている間は背景が空になる。§8.6） |
| 地雷32（無反応なノブは弱い値より悪い） | アプリからの上書きは clamp しつつ、範囲を文書化して無反応を作らない |
| contrastSampler の自己フィードバック対策（`SWITCH_SETTLE_MS`） | 新設計では前景を含まない入力を解析するので、フィードバックは構造的に起きない。ヒステリシスは安定化のために残す |

---

## 2. 元仕様のレビュー（誤り・改善点）

| # | 箇所 | 指摘 | 本設計の扱い |
|---|---|---|---|
| R1 | 1.2 全体 | コンポジタ主導だけでは、**アプリ内のコンテンツの上に浮くガラス**（Liquid Glass の主用途）が描けない。コンポジタに届くのは 1 枚のバッファなので、「文字（前景）」と「その下の中身」を分けられない | Tier 1（アプリ内）と Tier 2（デスクトップ）に分ける |
| R2 | 3.1 Layer C「Glass Mask に基づき背景部分のみ切抜き」 | 1 枚のバッファから前景だけを切り抜くことはできない | コンポジタ側のガラスは**サーフェスの下**に描き、アプリはその領域を**透明**にしておく（CSS `backdrop-filter` と同じ意味） |
| R3 | 1.4 / 6 「Mutter 統合コア」 | Mutter に独自の Wayland グローバルを足すには **Mutter のフォーク**が要る。GNOME Shell 拡張ではできない。配布（システムの mutter の置き換え、Shell のバージョンとの対応）の負担が書かれていない | 主経路は D-Bus + 拡張機能。フォークは将来の選択肢として草案だけ用意（§8.9） |
| R4 | 3.2 Ping-Pong | Cogl の遅延実行では、同じ FBO を行きと帰りで使い回すと依存グラフが閉路になり、背景が消える（memo 地雷3、実際に踏んだもの）。そもそも不要でもある | コンポジタ: フレームバッファ自体が「下から積み上がった結果」なので、blit すれば多段になる（O(N)）。アプリ内: グループ内は融合して 1 パス、グループ間は z 順の DAG |
| R5 | 3.4 Shared Blur | 窓ごとに背後が違うので、全画面で 1 本のブラーは成り立たない | 共有は同じ z 層・同じグループの中だけ |
| R6 | 2.1 `array` 型で構造体を送る | Wayland では可読性・拡張性が悪く、型検査もできない。`z_order` の意味も曖昧 | 形状ごとのオブジェクト（`set_rect` / `set_radii` / `set_material` / `place_above`）にする（§8.9） |
| R7 | 2.1 `set_material(parameters: array)` | 任意パラメータの配列は検証できない | 意味のある材質（enum）＋型付きの少数の上書き（tint など）にする |
| R8 | 4.1 解析入力 | 見た目（light/dark）によってティントが変わると、解析 → ティント → 輝度 → 解析のループが残る | ティント前のベースを解析する（§9.1） |
| R9 | 4.2 「3×3 サンプリング」と「128×72 ダウンサンプル」、9 点で P10/P50/P90 | 数字が噛み合わない。9 サンプルのパーセンタイルは統計として意味が薄い | 要素ごとに縦横比に合わせたグリッド（例 16×4）を取り、そこからパーセンタイルを出す |
| R10 | 4.3 EMA | 固定の α はフレームレートによって時定数が変わる | dt に依存する α = 1 − exp(−dt/τ) にする |
| R11 | 2.1 adaptive イベント | 輝度・コントラスト・彩度を細かい精度で高頻度に送ると、小さい領域を動かして背後の他アプリの画面を読み取れる | 量子化・最小サイズ・頻度上限・可視時のみ・持ち主にだけ送る（§9.6） |
| R12 | 3.3 GPU タイムアウトの検知 | シェーダ単位で GPU タイムアウトは検知できない（GPU のリセットはドライバとコンポジタ全体の問題） | 扱うのはシェーダのコンパイル失敗と GL コンテキストの喪失だけ。そのときは Lite に落とす |
| R13 | 3.3 「Uniform の差分更新で負荷削減」 | 実測上のコストは uniform ではなくパス数と塗り面積（memo 地雷17） | 最適化の軸を「パスを走らせない（キャッシュ）」「パスを減らす（融合）」に置く |
| R14 | 5.1 Rust + C ABI | GI が無いと GJS/Python から使えない。Rust から GIR を出す標準的な道も無い | C/GObject + GIR |
| R15 | 全体 | HiDPI・分数スケール、マルチモニタ、窓のアニメーション（開閉・最小化・オフスクリーン描画中）、アクセシビリティ、入力への反応、形状のモーフィング、窓のリサイズ中の同期、Flatpak が扱われていない | 各章で扱う |
| R16 | 6.1 フェーズ | 実現性の検証（スパイク）が無いまま大きな実装に入る | Phase 0 に Go/No-Go 基準付きのスパイクを置く（§15） |

元仕様のうち**そのまま採用するもの**: クライアントに画素を渡さないこと、GLSL をクライアントから受け取らないこと、クライアントはサーフェスローカル座標だけを扱うこと、解析の頻度を描画から切り離すこと、ヒステリシスとシーケンス番号、プロトコルに対応していない環境へのフォールバック、Surface あたりの領域数の上限。

---

## 3. Liquid Glass を分解する（設計の出発点）

Apple の Liquid Glass（2025 年の WWDC で発表、iOS 26 / macOS Tahoe 26）を、実装すべき要素に分解する。
ここで扱うのは**振る舞いと原理**であり、Apple のアセット（フォント・アイコン・画像）は使わない（§10.4）。

| 要素 | 内容 | 本設計での担当 |
|---|---|---|
| **層の分離** | ガラスは「ナビゲーション／コントロール層」専用。コンテンツ層（リストの行、本文）には使わない。コンテンツはガラスの下を流れる | `LgGlassView` / `LgToolbarView` の構造そのもの（§7.1） |
| **レンズ（屈折）** | 散乱させる（すりガラス）のではなく、光を曲げて集める。縁で背景が押し縮められ、曲がる | Glass Core（既存の edge lensing） |
| **スペキュラ・リム** | 光源と動きに反応する縁の光 | Glass Core（既存） |
| **適応（Adaptivity）** | 小さい要素（タブバー、ボタン）は下の中身に応じて light/dark を**切り替える**。大きい要素（サイドバー、メニュー）は**切り替えず**、濃さを連続的に変える | Adaptive（§9）、要素の大きさで方針を変える |
| **バリアント** | Regular（既定）と Clear（ほぼ透明。可読性のために下に暗幕を敷く）。色付き（tint）は主要な操作にだけ控えめに使う | マテリアル（§6） |
| **インタラクション** | 押すと膨らみ、弾み、触れた点から光が広がる。スイッチのつまみやスライダーのつまみは、操作中だけレンズになる | `interactive` フラグ＋スプリング（§10） |
| **モーフィング／融合** | 近いガラス同士は液滴のようにつながり、分かれる。選択インジケータはガラスの滴として移動し、下のアイコンを拡大する | smooth union（§5.3）、グループ（`LgGlassGroup`）、レイヤ 1 の「純レンズ」（§7.5） |
| **同心円の角** | 内側の角の半径 = 外側の半径 − 余白 | デザイントークン（§10.3） |
| **スクロール端の効果** | バーの背景を塗る代わりに、バーの下に来た中身を端でぼかし、薄める | `LgToolbarView`（§10.2） |
| **アクセシビリティ** | 透明度を下げる → すりガラス寄り。コントラストを上げる → 白黒と枠線。動きを減らす → 弾みを抑える | §6.4 |

**ガラスの背後にあるもの**は 3 種類ある。これが設計を分ける軸になる。

1. **アプリ内の背景**: 同じ窓の中身（スクロールするリスト、写真、地図）。→ アプリ側でしか描けない（Tier 1）。
2. **デスクトップの背景**: 壁紙と他の窓。→ コンポジタでしか描けない（Tier 2。アプリは他の窓の画素を見られない）。
3. **ガラスの上のガラス**: Apple も基本的に避けている（ボタンはバーのガラスの上に「前景」として載るだけで、ガラスを重ねない）。例外は操作中の滴やつまみのレンズで、これは下のガラスと前景ごと屈折させる。→ 例外だけを「純レンズ」として扱う（§7.5）。

---

## 4. 全体アーキテクチャ

### 4.1 構成図

```
┌──────────── アプリ（GTK4 / libadwaita。言語は C, Python, GJS/TypeScript, Rust, Vala のどれでも） ────────────┐
│  LgToolbarView  LgGlassView  LgButton  LgSwitch  LgSlider  LgSegmentedControl  LgTabBar ...                 │
│  └── libliquidglass-1（C/GObject、GI 名前空間 "Lg"）                                                           │
│       ├─ Tier 1 レンダラ（アプリ内ガラス）                                                                     │
│       │    Full : GSK render_texture(blur 済み) → CPU → GLES3 Glass Core → GdkGLTexture                       │
│       │    Lite : copy/paste + blur + color-matrix + 照明テクスチャ（GSK ネイティブ、屈折なし／近似）          │
│       ├─ Adaptive（アプリ内）: 自分の背景の画素だけを解析 → CSS クラス・CSS 変数                               │
│       └─ Desktop Glass クライアント（Transport を抽象化）                                                      │
│            ├ D-Bus  io.github.ryohsuke1231.LiquidGlass1        …… GNOME 50/51 + 拡張機能（ガラス）         │
│            ├ ext-background-effect-v1                           …… GNOME 51+ / KDE（ぼかしのみ）            │
│            └ (将来) xx_lg_glass_v1                              …… パッチを当てた Mutter                   │
└─────────────────────────────────────┬──────────────────────────────────────────────────────────────────────┘
          形状・材質（D-Bus メソッド）  │  ▲ Appearance（量子化、持ち主にだけ unicast）
┌─────────────────────────────────────▼─── GNOME Shell 拡張「App Glass Service」 ──────────────────────────────┐
│  窓の同定: 送信者 == MetaWindow.gtk_unique_bus_name かつ path == gtk_window_object_path                      │
│  WindowGlassActor（MetaWindowActor の子、サーフェスより下）                                                  │
│     背景の取得: B1 = 既存のクローン方式 / B2 = フレームバッファ blit（Clutter.BlitNode）                     │
│     Glass Core（Cogl ターゲット）→ サーフェスの下に合成                                                      │
│  Adaptive Analyzer（前景なし・ティント前のベースを縮小して解析）                                            │
│  System Profile（ユーザーが調整した物理パラメータ）を D-Bus プロパティで配る                                 │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
   共有物: shaders/core（単一ソース）/ spec/material-schema.json / spec/adaptive.md + テストベクタ / D-Bus XML
```

### 4.2 コンポーネント

| ID | 名前（仮） | 言語 | 役割 |
|---|---|---|---|
| A | `shaders/core` | GLSL | `glass.frag` を分解した Glass Core と、ターゲット別ラッパ（Cogl snippet / GLES 3.0） |
| B | `libliquidglass-1` | C (GObject), meson | アプリ向けのウィジェット・レンダラ・Adaptive・Desktop Glass クライアント。GIR/typelib/VAPI を生成 |
| C | App Glass Service | TypeScript → GJS（Shell 拡張） | D-Bus サービス、窓の同定、デスクトップガラスの描画、解析 |
| D | `xx_lg_glass_v1`（草案） | XML | 将来 Mutter パッチを当てる場合の Wayland プロトコル |
| E | Glass Gallery（デモ） | TypeScript → GJS | 全機能のショーケースと、検証用のハーネス |
| F | tools | Python / C / sh | シェーダのゴールデン画像ハーネス、計測、LG スクリプト |

### 4.3 能力の検出とフォールバック

アプリ起動時に `LgContext` が次を調べ、`LgContext:capabilities` として公開する。

| 環境 | アプリ内ガラス | デスクトップガラス |
|---|---|---|
| GNOME 50 + 拡張 | Full（GL が使える場合）／ Lite | **ガラス**（D-Bus） |
| GNOME 50、拡張なし | Full ／ Lite | なし → 不透明な「窓」材質 |
| GNOME 51+、拡張なし | Full ／ Lite | **ぼかし**（`ext-background-effect-v1`） |
| GNOME 51+ + 拡張 | Full ／ Lite | **ガラス**（D-Bus。ext-bg-effect は同じサーフェスに設定しない） |
| KDE Plasma 6（ext-bg-effect 対応版） | Full ／ Lite | ぼかし |
| X11 / その他 | Full ／ Lite | なし |
| Flatpak | 上と同じ（`--talk-name=io.github.ryohsuke1231.LiquidGlass` が要る） | 上と同じ |

どの段で落ちても**文字が読めること**を最優先にする: デスクトップガラスが無ければ、透明にしていた領域を材質に応じた不透明色（Adwaita の `--sidebar-bg-color` など）で塗る。

### 4.4 不変条件（改訂版）

1. **画素はプロセスの境界を越えない。** アプリが解析するのは自分の窓の中身だけ。コンポジタからアプリへは、量子化したメタデータしか渡さない。
2. **シェーダはクライアントから受け取らない。** 材質は enum と、型付きで範囲を決めた少数の上書きだけ。
3. **幾何はクライアントのサーフェスローカル（論理 px）。** グローバル座標への変換はコンポジタが行う。
4. **前景は解析に入れない。見た目に応じたティントも入れない。**
5. **パスの依存関係は DAG。** 同じテクスチャへの書き戻し（ピンポン）はしない。
6. **幾何は描画時に読む**（1 フレーム遅れを作らない）。背景とガラスは同じフレームで一致させる。
7. **物理パラメータはシステムが決め、アプリは意図（材質）を宣言する。** 全アプリとシェル UI で見た目がそろう。

---

## 5. シェーダ設計（Glass Core）

### 5.1 ファイル構成

```
shaders/
├── core/
│   ├── lg_sdf.glsl        角丸矩形（角ごとの半径 vec4）、smooth union、解析勾配
│   ├── lg_profile.glsl    超楕円の高さプロファイル、法線
│   ├── lg_optics.glsl     Snell 屈折、edge lensing（定数 FALLOFF/REACH）、フットプリントのタップ、色収差
│   ├── lg_tone.glsl       SCB、base/custom ティント、見た目（light/dark）のティント
│   ├── lg_lighting.glsl   リム、スペキュラ、シーン、内側 AO、ポインタの光、押下の光
│   ├── lg_shadow.glsl     umbra/penumbra のドロップシャドウ
│   └── lg_output.glsl     スクリーン合成、色相を保つクランプ、事前乗算、ディザ
├── targets/
│   ├── cogl/glass.snippet.glsl      Cogl snippet 用（cogl_sampler1, cogl_tex_coord_in, float uniform）
│   ├── gles3/glass.frag             #version 300 es、UBO なし（GLES 3.0 で安全に動く uniform 配列）
│   └── gles3/lighting_only.frag     Lite 用の照明テクスチャ生成（背景に依存しない項だけ）
├── blur/  （既存の gaussian_h/v、downsample/upsample。コンポジタ側で使う）
└── tests/ （ゴールデン画像と入力）
```

- `#include` 相当は各ビルドで展開する（meson の `custom_target` と拡張機能のビルドスクリプトで、同じ小さな展開スクリプトを使う）。
- **拡張機能の `glass.frag` も最終的にはここから生成する。** 最初は既存の出力と一致することをゴールデンテストで保証し（§14.1）、見た目は変えない（ユーザー方針 7）。

### 5.2 形状モデル

```glsl
struct LgShape {        // 1 形状 = 1 つの角丸矩形
  vec4 rect;            // x, y, w, h（デバイス px、出力テクスチャの座標系）
  vec4 radii;           // tl, tr, br, bl
  vec4 params;          // x: group の k（融合の強さ, px）, y: 押下量 0..1, z: 形状ごとの base 色の強さ, w: 予約
  vec4 base_color;      // 要素自身の色（既存の region_tint_* と region_base_strength の後継）
};
#define LG_MAX_SHAPES 16  // 1 パスあたり。既存の MAX_GLASS_REGIONS と同じ
```

- **角ごとの半径**: `sdRoundRect` を象限ごとに半径を選ぶ版に拡張する（`r = p.x > 0 ? (p.y > 0 ? br : tr) : ...`）。
- 既存の単一矩形モード（`dock_*`）は「形状 1 個」の特別な場合として吸収する。

### 5.3 融合（smooth union）と勾配

- 同じグループの形状は **polynomial smooth min** で 1 つの SDF にまとめる:
  `h = clamp(0.5 + 0.5*(b - a)/k, 0, 1); d = mix(b, a, h) - k*h*(1 - h)`
  - `k` = グループの spacing（SwiftUI の `GlassEffectContainer(spacing:)` に相当）。`k = 0` なら通常の min（融合しない）。
- **勾配は解析的に混ぜる**: `∇d = mix(∇b, ∇a, h)`（各形状の勾配は既存の `sdRoundRectDir` の閉形式）。
  既存の `heightGradient` は「SDF 方向に 1 回の中心差分」なので、方向を融合後の勾配（正規化したもの）に差し替えるだけで済む。**形状 N 個 × 2 回**の SDF 評価で収まる。
- **平坦な内側の早期リターン**: 融合形状では `-d ≥ max(全形状の corner radius) + k + ...` を条件にする。正しさの根拠（1-Lipschitz）は smooth min でも成り立つ（smooth min は 1-Lipschitz を保つ）。⚠️ 数値で確認する。
- **モーフィング**: 形状の rect/radii をスプリングで補間するだけ（シェーダ側に特別な処理は要らない）。

### 5.4 光学・照明（既存から変えないもの）

- `EDGE_LENS_FALLOFF 2.4` / `EDGE_LENS_REACH 96.0` は定数のまま（memo 追記28、方針 7）。
- `displacement_scale`、`chroma_strength`、影、AO は **px 単位** のまま。ターゲットのラッパで `× device_scale` してから渡す（Core はデバイス px だけを知る）。
- 追加するのは**入力**（ポインタ位置、押下量）であって、光学のノブではない:
  - `pointer`（デバイス px）＋ `pointer_strength`: 既存の `pointer_x/y`・`mouse_radius`・`bg_glow_intensity` を整理した、ホバー時の柔らかい光。
  - `press`（形状ごと 0..1）: 押下中の照明の強さ（触れた点から広がる光）。膨らみ（拡大）は幾何（rect）で表す。

### 5.5 トーンと見た目

適用順（背面 → 前面）:

1. 屈折済みの背景（ぼかし済み）
2. SCB（brightness/contrast/saturation）
3. **ここまでが「ガラスのベース」＝ Adaptive の解析対象**（§9.1）
4. base 色（要素自身の色、形状ごと）
5. **見た目のティント**（light: 白寄り／dark: 黒寄り。材質ごとの強さ）
6. custom ティント（アプリまたはユーザーの色）
7. 内側 AO（乗算）
8. 照明（スクリーン合成）→ クランプ → 事前乗算 → 影と合成 → ディザ

### 5.6 ターゲットの差

| | Cogl（拡張機能） | GLES 3.0（アプリ Full） | GSK（アプリ Lite） |
|---|---|---|---|
| テクスチャ | `cogl_sampler1` | `sampler2D u_backdrop` | paste ノード |
| uniform | float と float 配列（既存と同じ） | float 配列（`uniform vec4 u_shapes[16*4]`） | — |
| 屈折 | ○ | ○ | ×（近似の選択肢のみ、§7.4） |
| 照明 | ○ | ○ | 照明テクスチャを `blend(screen)`、AO を `blend(multiply)` |
| 影 | ○ | ○ | 影テクスチャまたは `outset-shadow` ノード |

### 5.7 デバッグビュー

既存の `debug_view`（形状・影マスク）に加え、**変位の可視化**（RG = 変位ベクトル）と **Adaptive のセル表示**を追加する。
拡張機能の `global._lgGlass.debugView(n)` と、デモアプリの Material Lab の両方から切り替えられるようにする。

---

## 6. マテリアルモデル

### 6.1 意味で選ぶ材質（アプリが宣言するもの）

| 材質（enum） | 用途 | 性格 | Adaptive の方針 |
|---|---|---|---|
| `REGULAR` | バー、ボタン、タブバー（既定） | 中程度のぼかし＋レンズ、見た目のティントあり | 小さい要素は light/dark を切り替える |
| `CLEAR` | 写真・動画の上のコントロール | ぼかし最小、レンズ強め、透明度高め | 切り替えず、読めない背景では暗幕を足す |
| `THICK` | サイドバー、大きなパネル | ぼかし強め、ティント濃いめ、レンズは縁だけ | 切り替えず、濃さを連続的に変える |
| `MENU` | ポップオーバー、メニュー | `THICK` に近く、影あり | 同上 |
| `LENS` | 操作中のつまみ、選択の滴（純レンズ） | ぼかしなし、屈折最大、スペキュラ強め、前景なし | 対象外 |
| `WINDOW` | フォールバック用の不透明色 | 不透明 | — |

修飾子: `tint`（GdkRGBA ＋強さ）、`prominent`（アクセント色で塗る。主要な操作用）、`interactive`（押下・ホバーに反応）。

### 6.2 パラメータの階層

```
System Profile（ユーザーが調整。拡張機能の Glass ページと同じ値）
   └─ Material Preset（材質ごとの倍率・固定値。ライブラリに内蔵）
        └─ App Override（アプリが上書きできるのは tint・prominent・interactive と、少数の強さだけ。範囲は clamp）
```

- **System Profile の配り方**: Flatpak のアプリはホストの dconf を読めない。そこでサービスが D-Bus プロパティ `Profile: a{sv}` と変更シグナルで配る。サービスが無ければライブラリ内蔵の既定値を使う。
- **拡張機能の Glass ページ = システムの物理パラメータ**にすると、シェル UI とアプリの見た目がそろう（不変条件 7）。

### 6.3 スキーマ（`spec/material-schema.json`）

C（ライブラリ）と TypeScript（拡張機能）で同じ定義を使う。パラメータごとに次を持つ:
`name`、`unit`（px / ratio / deg / color）、`default`、`min`、`max`、`scales_with_device_px`（bool）、`override_allowed`（bool）。
ビルド時に C のヘッダと TS のモジュールを生成し、両者のズレをなくす。

### 6.4 アクセシビリティによる変形

| 設定 | 取得元 | 変形 |
|---|---|---|
| 透明度を下げる | `LgContext` の設定（System Profile 側。GNOME に標準のキーは無い）⚠️ | 屈折 0、ぼかし最大、ティント濃く（ほぼ不透明） |
| ハイコントラスト | `AdwStyleManager:high-contrast` | 不透明な背景＋ 1px の枠線、前景は純白・純黒 |
| 動きを減らす | `gtk-enable-animations` / GTK の reduced-motion ⚠️ | スプリングを臨界減衰に、モーフィングはクロスフェードに |

---

## 7. Tier 1: アプリ内ガラス（libliquidglass のレンダラ）

### 7.1 なぜコンテナが要るか — `LgGlassView`

GTK には「自分より前に描かれたもの全部」をテクスチャとして取り出す公開 API が無い（copy/paste は GSK の中でしか使えない）。
そこでガラスの下に来る中身を**コンテナが自分で snapshot する**:

```
LgGlassView (snapshot の順序)
  1. content 子 → content_node（通常どおり画面にも追加）
  2. ガラスの本体（z 順、グループ単位）→ テクスチャノード
  3. オーバーレイ子（ガラスウィジェットの前景: ラベル・アイコン）
  4. レイヤ 1 の「純レンズ」（前景まで含めて屈折させる滴・つまみ）
```

- `LgToolbarView`（AdwToolbarView に相当）は `LgGlassView` の上に「上下のバー」「中身がバーの下まで伸びる」「スクロール端の効果」を載せたもの。
- **ガラスウィジェット自身はガラスの本体を描かない。** 本体はビューが描く。理由: GTK はウィジェットごとに描画ノードをキャッシュするので、中身がスクロールしてもガラスウィジェットの snapshot は呼ばれない（キャッシュが使われる）。ビューは子の再描画のたびに snapshot されるので、ビューが描けば背景と常に同じフレームで一致する。
- 幾何は snapshot の中で `gtk_widget_compute_bounds(glass, view)` から読む（allocation は確定済み。地雷18 を踏まない）。
- **コンテンツ層に置かれた `LgGlass`**（ビューの外）は、Lite の `backdrop-filter` 相当に自動で落とす（Apple の HIG もコンテンツ層でのガラスを推奨していない）。

### 7.2 バックエンドの抽象化

```c
struct _LgBackdropRendererClass {
  gboolean (*prepare)   (LgBackdropRenderer *self, GtkNative *native, GError **error);
  void     (*render_group) (LgBackdropRenderer *self,
                            GtkSnapshot       *snapshot,
                            GskRenderNode     *backdrop,   /* content + 下のグループ */
                            const LgShapeList *shapes,
                            const LgMaterialState *material,
                            LgAdaptiveSink    *adaptive);  /* 解析結果の出力先 */
  void     (*unprepare) (LgBackdropRenderer *self);
};
```

| バックエンド | 屈折 | コピー | 条件 |
|---|---|---|---|
| **Full**（GL オフスクリーン） | ○（Glass Core そのもの） | 背景を 1 回 CPU 経由（縮小済みなので小さい） | GL コンテキストが作れること |
| **Lite**（GSK ネイティブ） | ×（または拡大の近似） | なし（GPU 内で完結） | 常に使える |
| **Native**（将来） | ○（displacement ノード） | なし | GTK が displacement ノードを公開したら |

`LG_RENDERER=full|lite` 環境変数と `LgContext:renderer` で切り替えられるようにする（A/B 用。既存の `global._lgGlass.*` と同じ思想）。

### 7.3 Full バックエンドの手順

1 グループ・1 フレームあたり:

1. **キャッシュキーを作る**: (backdrop ノードの同一性, グループの外接矩形, スケール, 材質の背景に関わる値)。
   ノードの同一性は、`gtk_widget_snapshot_child` が毎回作り直す transform ノードを剥がし、キャッシュされている子ノードのポインタと変換の値で判定する。
   **キーが同じなら 2〜4 を飛ばす**（既存拡張の B1「キャプチャ不変ならブラー再利用」と同じ考え方）。
2. **背景をぼかして縮小した状態でレンダリングする**:
   `render_texture(renderer, scale(s) { blur(r) { clip(bbox + blur余白) { backdrop } } }, bbox·s)`
   - ぼかしは GSK に任せる。`glass.frag` はぼかし済みの層しか読まないので、自前のぼかしパスは不要。
   - `s` は材質で決める。REGULAR/THICK は 0.5、CLEAR/LENS は 1.0（ぼかしが弱いと縮小が見える）。拡大して読むときの段差は、既存の `blur_tex_w/h`（滑らかな双一次補間）がそのまま吸収する。
   - ぼかし半径の意味（GSK の blur radius と既存拡張の Gaussian の対応）は、ゴールデン画像で較正する ⚠️。
3. **CPU に読み出す**: `GdkTextureDownloader`（`R8G8B8A8_PREMULTIPLIED`、色空間は sRGB を明示）。
   - **この画素から Adaptive の統計も取る**（追加の読み出しは不要）。
4. **GL にアップロード**: `glTexSubImage2D`（テクスチャは使い回す）。
5. **Glass Core を 1 パス実行**: 出力はグループの外接矩形＋影の余白。デバイス px に揃える。
6. **GTK に渡す**: `GdkGLTextureBuilder`（`set_sync` で GLsync を付ける）→ `gtk_snapshot_append_texture`。
   出力テクスチャは 3 枚のプールで回し、GTK が解放を通知するまで書き換えない（release の notify を使う）。
7. 形状だけ・ポインタだけが変わったフレームは 5〜6 だけを行う（背景は再利用）。

分数スケール: `gdk_surface_get_scale()`（double）を使い、外接矩形をデバイス px の格子にそろえてから `append_texture` する（再サンプリングでぼやけないように）。

**GL の置き場所**: `GdkSurface` ごとに 1 つの `GdkGLContext`（`gdk_surface_create_gl_context`、GLES 3.0 以上）を `GtkNative` に紐付けて共有する。unrealize で破棄する。コンテキストが作れない、コンパイルに失敗した、コンテキストを失った——いずれの場合も、そのネイティブだけ Lite に落とす。

**`render_texture` を snapshot 中に呼ぶことの再入** ⚠️: 窓のレンダラ（`gtk_native_get_renderer`）を snapshot 中に使うことが安全かは、スパイク S1 で確かめる。
駄目なら、背景専用の `GskRenderer` をディスプレイごとに 1 つ作る（グリフのキャッシュが二重になるだけ）。

### 7.4 Lite バックエンドのノード構成

```
copy {
  … ここまでに描いた中身 …
  rounded-clip(形状) {
    blend(screen) {
      blend(multiply) {
        color-matrix(SCB + ティント) { blur(r) { paste(bounds, depth=0) } }
        AO テクスチャ
      }
      照明テクスチャ（リム・スペキュラ・シーン）
    }
  }
}
```

- 照明・AO・影は**背景に依存しない**（形状と光の向きだけで決まる）。`lighting_only.frag`（GL）で作ってキャッシュする。GL が無いときは C で同じ式を CPU で計算する（サイズは小さく、形状が変わったときだけ）。
- **屈折の近似**（任意・品質の段階として）: 形状の中心を基準に paste を `transform(scale 1.03〜1.08)` して、縁の帯だけを段階的にずらす（区分的な射影変換の帯 × 数本）。あとからぼかすので継ぎ目は目立たない。コストと見た目を S2 で評価する。
- 融合はできない（形状ごとに別のクリップになる）。グループは形状の和集合をパス（`GskPath`）で作って `fill` ノードでマスクすれば、見た目の連結だけは表現できる。

### 7.5 グループと z 順（Ping-Pong を使わない多段）

- **グループ**（`LgGlassGroup`）: 近い形状を 1 パスに融合する単位。
- **レイヤ 0**: 前景を持つ普通のガラス（バー、ボタン）。背景 = content。
- **レイヤ 1**: 前景を持たない純レンズ（タブバーの選択の滴、スイッチ・スライダーの操作中のつまみ）。
  背景 = content ＋ レイヤ 0 の出力テクスチャ ＋ オーバーレイの前景ノード。
- 各グループの出力は別々のテクスチャ。後のグループは前のグループの出力を**読むだけ**なので、依存は常に DAG になる。
- Apple の「ボタンはバーのガラスの上に前景として載る」原則により、レイヤは実質 2 段で足りる。

### 7.6 性能の予算と見通し

- 目標（Radeon 780M、1x）: 1 窓あたりアプリ内ガラスの追加 CPU 時間 **≤ 3ms/frame**（スクロール中）、静止中はほぼ 0（キャッシュが当たる）。
- 見積り（付録 B の実測から）: 1200x240 の帯を 0.5 倍（600x120）で読み出すと約 0.3ms。`render_texture` は Python からの計測（ノードの生成込み）で約 1.3ms（上限値）。GL の 1 パスは小さい。
- 注意: dGPU（PCIe）では読み出しが iGPU より遅い。縮小（s=0.5）とキャッシュが効く前提。
- スクロール中は毎フレーム背景が変わるので、1〜3 を毎フレーム行う。**静止時・ホバーだけ**は 5〜6 だけ。

### 7.7 上流への提案（並行して進める）

1. **`GskDisplacementNode` の公開**（すでに内部にある。SVG フィルタ用）。これが公開されると、Full の画質で Lite と同じゼロコピーが実現する。変位マップは形状だけで決まるので、CPU/GL で作ってキャッシュできる。
2. `GdkDmabufTexture` から dmabuf を取り出す読み取り API（Full のゼロコピー化）。

いずれも GTK の Issue で用途（Liquid Glass 風のレンズ）を説明して提案する。採否が決まるまでは Full＋Lite で進める。

---

## 8. Tier 2: デスクトップガラス（コンポジタ側）

### 8.1 経路（GNOME のバージョン別）

| | GNOME 50（今の環境） | GNOME 51+ | 将来の選択肢 |
|---|---|---|---|
| 形状の伝達 | D-Bus | D-Bus（サービスがあれば）／ ext-bg-effect（無ければ） | 独自 Wayland プロトコル（Mutter パッチ）または上流に拡張点を提案 |
| 描画 | 拡張機能（B1 または B2） | 拡張機能（B1 または B2）／ Mutter 標準のぼかし | Mutter 内部で Glass Core |
| commit との同期 | 近似（サイズで同期、§8.5） | 同上 | 厳密（ダブルバッファ） |

### 8.2 クライアント側（ライブラリ）

- `LgApplicationWindow`（`AdwApplicationWindow` のサブクラス）の `desktop-glass` プロパティを TRUE にすると:
  1. 窓の背景を CSS で透明にする（`.lg-desktop-glass`）。中身のペインには不透明な背景を付ける（`.lg-opaque-pane`）。
  2. `LgGlassPane`（サイドバーなど）の境界を**サーフェスローカル座標**に直して送る:
     ウィジェットの窓座標 ＋ `gtk_native_get_surface_transform()`（CSD の影の余白ぶんのずれ）。
  3. 角: ペインが窓の角に接していれば、その角は窓の半径。そうでなければペイン自身の半径（同心: 窓の半径 − 余白）。
- **D-Bus の接続は必ず `g_application_get_dbus_connection()` を使う。** GTK が `gtk_shell1` で申告した unique name と、メソッド呼び出しの送信者を一致させるため。
- **GTK 4.24 以降との衝突回避**: GTK 自身が `backdrop-filter` のために ext-bg-effect を使うことがある。同じ `wl_surface` に 2 つ目を作るとプロトコルエラーになる。
  - ライブラリは ext-bg-effect を**自分でバインドしない**。GTK ≥ 4.24 では窓レベルの CSS `backdrop-filter` を使って GTK に任せる。
  - サービス（ガラス）を使うときは、窓レベルの `backdrop-filter` を外す（ぼかしとガラスの二重描画を避ける）。⚠️ GTK 4.24 が ext-bg-effect を使う条件は S6 で確認する。

### 8.3 D-Bus インターフェース

- バス名: `io.github.ryohsuke1231.LiquidGlass`
- オブジェクト: `/io/github/ryohsuke1231/LiquidGlass`

```xml
<node>
  <interface name="io.github.ryohsuke1231.LiquidGlass1">
    <property name="Version" type="u" access="read"/>
    <!-- "glass", "adaptive", "pointer-light", "morph", "blit-backdrop" など -->
    <property name="Capabilities" type="as" access="read"/>
    <!-- System Profile（§6.2）。変更は PropertiesChanged で通知 -->
    <property name="Profile" type="a{sv}" access="read"/>

    <method name="RegisterWindow">
      <arg name="window_object_path" type="o" direction="in"/>
      <arg name="options" type="a{sv}" direction="in"/>
      <arg name="handle" type="o" direction="out"/>   <!-- .../Window/<n> -->
    </method>
  </interface>

  <!-- handle のオブジェクト -->
  <interface name="io.github.ryohsuke1231.LiquidGlass1.Window">
    <!--
      regions: a(uu(dddd)(dddd)ya{sv})
        u        region_id
        u        group_id（同じグループは融合）
        (dddd)   x, y, w, h（サーフェスローカル、論理 px）
        (dddd)   radii tl, tr, br, bl
        y        anchor flags（bit0 left, bit1 right, bit2 top, bit3 bottom：どの辺に追従するか）
        a{sv}    material, tint, prominent, interactive, adaptive など（スキーマで検証）
    -->
    <method name="Commit">
      <arg name="serial" type="u" direction="in"/>
      <arg name="layout_size" type="(dd)" direction="in"/>  <!-- この配置を計算したときの窓の論理サイズ -->
      <arg name="regions" type="a(uu(dddd)(dddd)ya{sv})" direction="in"/>
    </method>
    <method name="Unregister"/>

    <!-- 持ち主にだけ unicast で送る -->
    <signal name="Appearance">
      <arg name="region_id" type="u"/>
      <arg name="serial" type="u"/>
      <arg name="scheme" type="y"/>      <!-- 0 unknown, 1 light, 2 dark, 3 mixed -->
      <arg name="level" type="y"/>       <!-- 輝度の 8 段階 -->
      <arg name="confidence" type="y"/>  <!-- 0..3 -->
    </signal>
    <signal name="Invalidated">
      <arg name="reason" type="s"/>      <!-- "window-gone", "service-restart", "limit" -->
    </signal>
  </interface>
</node>
```

- 形状のまとまり（領域・グループ・材質）は `Commit` 1 回で**丸ごと置き換える**（部分更新はしない。状態のずれをなくす）。
- 上限: 窓あたり 32 領域、1 領域の最小サイズ 16x16 論理 px、`Commit` は 1 窓あたり 120 回/秒まで（超えたら合体させ、最新だけを反映）。

### 8.4 窓の同定とセキュリティ

1. `RegisterWindow(path)` の送信者（`invocation.get_sender()`、D-Bus デーモンが保証する unique name）を取る。
2. `global.get_window_actors()` から `w.get_gtk_unique_bus_name() === sender && w.get_gtk_window_object_path() === path` の窓を探す。
   - 見つからなければ `NotFound`。窓がまだマップされていなければ、`window-created` を最大 2 秒待つ。
3. これで**アプリは自分の窓にしかガラスを付けられない**（他のアプリの窓を指定しても一致しない）。
4. 送信者が消えたら（`NameOwnerChanged`）、または窓が unmanaged になったら、登録を破棄する。
5. `GtkApplicationWindow` 以外（GtkApplication を使わない窓）は対象外。v1 はこの制約を受け入れる。

### 8.5 幾何の同期（commit と原子的でない問題への対処）

- **アンカー**: 領域は「左右上下のどの辺に追従するか」を持つ。窓のサイズが `layout_size` と違う間（リサイズ中）は、サービスが現在のバッファサイズに合わせて再配置する。
  ヘッダバー（上に追従、左右に伸びる）、サイドバー（左・上下に追従）、下のツールバーは、クライアントの応答を待たずに正しく追従する。
- **サイズで同期**: `layout_size` と実際のサーフェスサイズが一致したフレームで新しい配置を反映する（`MetaWindowActor` の `size-changed` と、サーフェスのサイズを描画時に確認）。
- 窓の中で動くだけの領域（スクロールに追従するボタンなど）は、最大 1 フレームずれうる。v1 では許容し、デスクトップガラスは**動かない大きな領域**（サイドバー、ヘッダ）向けと位置付ける。動くガラスはアプリ内ガラス（Tier 1）で描く。

### 8.6 描画: 背景の取り方 B1 / B2

| | B1: クローン（既存方式） | B2: フレームバッファ blit |
|---|---|---|
| 仕組み | 壁紙と背後の窓のクローンをオフスクリーンに描く（既存の `LiquidEffect` の「アプリウィンドウ」） | ガラスを描く時点で、描画済みのフレームバッファから領域を blit する（Mutter 51 の標準ぼかしと同じ原理） |
| コスト | 背後の窓の数に比例。入れ子は 2^N−1（memo ⑤） | ガラス 1 枚あたり blit 1 回＋ぼかし＋ガラス 1 パス。**多段は O(N) で自然に正しい** |
| 実績 | 実機で安定（地雷は踏み済み） | 拡張機能では未経験。Mutter 51・Blur My Shell が同じ原理 |
| 拡張機能での難点 | 重い | 再描画クリップの拡張と遮蔽カリングの変更は Mutter の private API（拡張機能からは触れない）→ 代わりの対策が要る（下記） |

**B2 の設計**（拡張機能内、GJS）:

```
WindowGlassEffect.vfunc_paint_node(node, paintContext):
  fb = paintContext.get_framebuffer()
  if (fb がステージビューのフレームバッファでない)   // 窓がオフスクリーンに描かれている（フェード中など）
      → 直前の有効な背景テクスチャを使う（地雷30 の教訓）
  layer = Clutter.LayerNode.new_to_framebuffer(backdropFb, copyPipeline)
  blit  = Clutter.BlitNode.new(fb); blit.add_blit_rectangle(srcX, srcY, 0, 0, w, h)   // ビュー座標 × スケール
  layer.add_child(blit); node.add_child(layer)
  → ぼかし（DAG、ピンポンなし）→ Glass Core のパイプラインノードを現在のフレームバッファへ
```

拡張機能で B2 を使うときの対策:

1. **再描画クリップ**: 背後で変化があっても、Mutter はクリップの外側のガラスを描き直さない（ぼかしと屈折は周囲を読むので古い縁が残る）。
   → 背後の窓の `MetaWindowActor::damaged`、背後の窓の移動・リサイズ・重なり順の変化、壁紙の変化を監視し、ガラスの影響範囲と交わったら**ガラスのアクター全体を `queue_redraw()`** する（damaged はステージ更新の前に来るので同じフレームに入る ⚠️ 確認）。
2. **遮蔽カリング**: 窓の不透明領域の下は背後の窓が描かれていないことがあり、その画素は古い。
   → **サンプリングを領域の内側に clamp する**（`blur_rect` = 領域そのもの）。縁のぼかしは端の値を伸ばして補う。
3. **マルチモニタ**: ビューごとに blit 元の矩形を計算する（ビューのレイアウトとスケール）。
4. **窓のアニメーション**（開閉・最小化で窓が変形しているとき）: ガラスの形状も同じ変換で動かす。変換が射影的なあいだ（3D 効果など）は B1 に落とすか、ガラスを消す。

**方針**: まず B2 のスパイク（S3）を行う。黒枠・古い縁・アニメ中の破綻が抑えられれば B2 を主経路にする。駄目なら B1（既存）を使う。
B2 が成立すると、**既存拡張の dock/menu などのガラスもクローン無しで描ける可能性がある**（副次的な大きな効果。ただし既存 UI への適用は別判断）。

### 8.7 ポップオーバー・メニュー（xdg_popup）

GTK のポップオーバーやメニューは**別の `wl_surface`（xdg_popup）**なので、デスクトップガラスで描くと、親の窓の中身と壁紙の両方がガラス越しに見える（アプリ内ガラスでは描けない）。
`LgPopover` は `desktop-glass` が使えるときはこの経路を使い、使えないときは不透明な `MENU` 材質に落とす。
⚠️ ポップアップのサーフェスにも `gtk_window_object_path` が付くとは限らない → 親の窓のハンドルに「子サーフェス」として登録する API（`RegisterPopup(parent_handle, popup_token)`）が要る。S4 で調べる。

### 8.8 既存拡張との共存

- 既存拡張の「Application Windows」（窓全体の後ろにガラス＋中身の不透明度を下げる）と、新しい領域単位のガラスが同じ窓に掛からないようにする: **サービスに登録された窓は、既存機能の対象から外す。**
- 💡 推奨: App Glass Service は**既存拡張の中のモジュール**として実装する。GPU 資源・設定（Glass ページ = System Profile）・`LiquidEffect` を共有でき、窓のアクターへのフックが二重にならない。開発中は既定 OFF の設定キーで隔離する。

### 8.9 将来: Wayland プロトコル草案 `xx_lg_glass_v1`

Mutter にパッチを当てる、または上流に拡張点を提案する場合のために、元仕様のプロトコルを直した形を残す。

```xml
<protocol name="xx_lg_glass_v1">
  <interface name="xx_lg_glass_manager_v1" version="1">
    <enum name="capability" bitfield="true">
      <entry name="glass" value="1"/> <entry name="adaptive" value="2"/> <entry name="pointer_light" value="4"/>
    </enum>
    <event name="capabilities"><arg name="flags" type="uint" enum="capability"/></event>
    <request name="get_glass_surface">
      <arg name="id" type="new_id" interface="xx_lg_glass_surface_v1"/>
      <arg name="surface" type="object" interface="wl_surface"/>
    </request>
    <request name="destroy" type="destructor"/>
  </interface>

  <interface name="xx_lg_glass_surface_v1" version="1">
    <request name="create_shape"><arg name="id" type="new_id" interface="xx_lg_glass_shape_v1"/></request>
    <request name="destroy" type="destructor"/>
  </interface>

  <!-- 以下はすべてダブルバッファ。親の wl_surface.commit で反映 -->
  <interface name="xx_lg_glass_shape_v1" version="1">
    <enum name="material">
      <entry name="regular" value="0"/> <entry name="clear" value="1"/> <entry name="thick" value="2"/>
      <entry name="menu" value="3"/> <entry name="lens" value="4"/>
    </enum>
    <request name="set_rect">
      <arg name="x" type="fixed"/><arg name="y" type="fixed"/><arg name="width" type="fixed"/><arg name="height" type="fixed"/>
    </request>
    <request name="set_radii">
      <arg name="tl" type="fixed"/><arg name="tr" type="fixed"/><arg name="br" type="fixed"/><arg name="bl" type="fixed"/>
    </request>
    <request name="set_group"><arg name="group" type="uint"/><arg name="spacing" type="fixed"/></request>
    <request name="set_material"><arg name="material" type="uint" enum="material"/><arg name="flags" type="uint"/></request>
    <request name="set_tint"><arg name="rgba" type="uint"/><arg name="strength" type="fixed"/></request>
    <request name="place_above"><arg name="sibling" type="object" interface="xx_lg_glass_shape_v1"/></request>
    <event name="appearance">
      <arg name="scheme" type="uint"/><arg name="level" type="uint"/><arg name="confidence" type="uint"/>
    </event>
    <request name="destroy" type="destructor"/>
  </interface>
</protocol>
```

- `ext-background-effect-v1` と同じサーフェスで併用したら、ガラスを優先して blur は無視する（プロトコルエラーにはしない）。
- Mutter 51 の実装（`MetaSurfaceContent` → `meta_background_effect_paint_blur_region`、再描画クリップのフィルタ）を土台にすれば、パッチは「blur の代わりに Glass Core を描く」「材質の状態を持つ」の 2 点に絞れる。
- 💡 上流への提案としては、ガラスそのものより「**シェル（JS）が背景エフェクトの描画を差し替えられる拡張点**」の方が受け入れられる見込みがある（形状は標準プロトコルで原子的に、材質は D-Bus で）。

---

## 9. Adaptive Appearance

### 9.1 入力と不変条件

- **解析対象 = ガラスのベース**（§5.5 の 3 まで。屈折・ぼかし・SCB 済み、**見た目のティント・custom ティント・照明・前景は含まない**）。
  - アプリ内（Full）: §7.3 の手順 3 で読み出した、ぼかし済みの背景の画素を使う（屈折による並べ替えは統計にほぼ影響しないので省略）。
  - アプリ内（Lite）: 要素の領域を低解像度（例 32x8）で `render_texture` → 読み出し。背景が変わったときだけ、最大 10Hz。
  - コンポジタ: B1/B2 の背景テクスチャをさらに縮小するパスを 1 本足し、16x16 程度を読み出す（同期読み出し。最大 10Hz、背景が変わったときだけ。既存の B4 と同じ条件）。
- これで**自分の色の変化が次の解析に入る**ことが構造的に起きない（既存の `SWITCH_SETTLE_MS` のような待ち時間が不要になる）。

### 9.2 統計

1. 要素ごとに縦横比に合わせたグリッド（最大 16x4、最小 4x2）の平均色を取る。
2. sRGB → リニア → 相対輝度 `Y = 0.2126R + 0.7152G + 0.0722B`（既存の `_luminanceFromRgb` と同じ）。
3. `P10`、`P50`、`P90` と、セル間の広がり `spread = P90 − P10` を出す。

### 9.3 判定

- 候補は 2 つの前景色（light: ほぼ白、dark: ほぼ黒。材質ごとに定義）。
- スコア（WCAG のコントラスト比）:
  - light の前景 → 最悪の場合は背景の明るい側: `score_L = CR(fg_light, P90)`
  - dark の前景 → 最悪の場合は背景の暗い側: `score_D = CR(P10, fg_dark)`
- 切り替え: 今と逆の側のスコアが **1.2 倍**（既存の `SWITCH_ADVANTAGE`）を超えたときだけ反転する。ユーザーの好み（`preference`）があれば、既存と同じ非対称のしきい値（1.02 / 1.6）を使う。
- 時間方向: 各統計量に **dt 依存の EMA**（τ = 150ms、α = 1 − exp(−dt/τ)）を掛ける。反転後 **400ms** は再反転しない。
- `mixed`: `spread` が大きく、どちらのスコアも 3.0 未満 → ティントを濃くする（暗幕）方向で補う。前景は切り替えない。
- `confidence` = 2 つのスコアの比を 4 段階に量子化した値。

### 9.4 要素の大きさによる方針

- 小さい要素（高さ ≤ 64 論理 px、または材質が REGULAR のボタン・バー）: light/dark を**切り替える**。
- 大きい要素（THICK/MENU、サイドバー）: 切り替えない。`P50` に応じて**ティントの濃さを連続的に**変え、前景のコントラストを保つ。

### 9.5 ウィジェットへの反映

- `LgGlass:appearance`（enum）を更新し、CSS クラス `.lg-on-light` / `.lg-on-dark` を付け替える。
- 色は **CSS 変数**（GTK 4.16 以降、libadwaita 1.6 以降で使われている）で配る:
  `--lg-fg-primary`、`--lg-fg-secondary`、`--lg-fg-tertiary`、`--lg-separator`。
- シンボリックアイコンは CSS の `color` に従うので、アイコンも自動で追従する。
- 遷移は CSS の `transition: color 200ms`（reduced-motion のときは 0）。

### 9.6 デスクトップ側のプライバシー

| 制限 | 値（初期案） | 理由 |
|---|---|---|
| 送る値 | scheme（2bit）、level（3bit）、confidence（2bit） | 細かい輝度・彩度は送らない |
| 最小領域 | 48x24 論理 px | 小さい領域を走査して画面を読む攻撃を防ぐ |
| 頻度 | 1 領域あたり 5 回/秒、値が変わったときだけ | 同上 |
| 条件 | 窓が可視で、領域が画面内にあるときだけ | 隠れた窓からの覗き見を防ぐ |
| 宛先 | 持ち主の unique name にだけ unicast | 他のアプリに漏らさない |

### 9.7 既存コードの再利用

`contrastSampler.ts` の判定ロジック（ヒステリシスの定数、好みの非対称しきい値、トリム平均）はそのまま仕様に移す。
`spec/adaptive.md` に式と定数、`spec/adaptive-vectors.json` に入力と期待される出力の組を置き、C 実装と TS 実装を同じベクタでテストする。

---

## 10. ウィジェットとデザインシステム

### 10.1 質問への回答: Apple 寄りのチェックボックスやトグルも作るべきか

**結論: 全部を作る必要はありません。「ガラスと一体になっている部品」だけを少数作り、残りは既存の libadwaita を CSS で馴染ませて組み合わせるのが最善です。**

理由:

1. **Liquid Glass らしさの大部分は、部品の形ではなく「材質」と「振る舞い」から来ます。**
   ガラス、下の中身に合わせた前景の切り替え、押したときの膨らみと光、滴の移動——これらはライブラリが部品に関係なく与えます。
2. **ただし、ガラスと動きが一体の部品は既存部品では表現できません。**
   例: スイッチのつまみは、ドラッグ中だけレンズになって下を屈折させます。タブバーの選択は、ガラスの滴として移動しながら下のアイコンを拡大します。
   これらはレンダラ（§7.5 のレイヤ 1）と連携しなければ描けないので、専用の部品が要ります。
3. **コンテンツ層（リスト、設定の行、入力欄、チェックボックス）は Apple も普通の部品を使っています。**
   ここは libadwaita の部品（`AdwPreferencesGroup`、`AdwActionRow`、`GtkCheckButton` など）を、CSS テーマ（角丸・余白・色のトークン）で馴染ませれば十分です。チェックボックスを作り直す必要はありません。
4. **デザイナーでなくても作れるように**、見た目は「規則」で決めます（§10.3）。
   角の半径は同心円の規則で計算します。寸法は 4px 刻み、動きはスプリングの 2 つの数値で決めます。アイコンは Adwaita のシンボリックアイコンを使います。
   形を描き起こす作業はほとんどありません（ガラスの形はほぼカプセルと角丸矩形だけです）。
5. **Apple の資産は使いません**（§10.4）。

### 10.2 部品の一覧（優先度順）

| 部品 | 役割 | ガラスの使い方 | 元にするもの | 優先度 |
|---|---|---|---|---|
| `LgGlassView` | 中身の上にガラスの層を持つコンテナ | レンダラの本体 | GtkOverlay の配置規則 | P0 |
| `LgGlassBin` | 子を 1 つ持つ汎用のガラス | レイヤ 0 | — | P0 |
| `LgGlassGroup` | 近いガラスを融合させる単位（spacing） | smooth union | SwiftUI の GlassEffectContainer | P0 |
| `LgToolbarView` | 上下の浮くバー、バーの下まで伸びる中身、スクロール端の効果 | レイヤ 0 | AdwToolbarView | P0 |
| `LgButton` | `glass` / `prominent` / `plain`（ガラスの上の前景） | レイヤ 0、interactive | GtkButton | P0 |
| `LgSwitch` | オン・オフ | 操作中だけ、つまみがレンズ（レイヤ 1） | GtkSwitch（a11y ロールは switch） | P1 |
| `LgSlider` | 連続値 | 操作中だけ、つまみがレンズ | GtkScale | P1 |
| `LgSegmentedControl` | 2〜5 択 | 選択の滴（レイヤ 1）が移動し、ドラッグ中は下のラベルを拡大 | AdwToggleGroup（libadwaita 1.7+） | P1 |
| `LgTabBar` | 下に浮くカプセルのタブ | 選択の滴、スクロールで縮む | AdwViewSwitcherBar | P1 |
| `LgSearchEntry` | カプセルの検索欄 | レイヤ 0 | GtkSearchEntry | P2 |
| `LgPopover` / `LgMenuButton` | メニュー | デスクトップガラス（使えるとき）/ MENU 材質 | GtkPopover | P2 |
| `LgApplicationWindow` / `LgGlassPane` | デスクトップガラスのサイドバー | Tier 2 | AdwApplicationWindow / AdwOverlaySplitView | P2 |
| CSS テーマ `lg-style.css` | libadwaita の部品をガラスの上・横で馴染ませる | — | Adwaita の CSS 変数 | P0 |

作らないもの: チェックボックス、ラジオ、リスト行、入力欄、スピンボタン、カレンダーなど（CSS で馴染ませる）。

### 10.3 デザイントークン（規則で決める）

| トークン | 規則・値（初期案） |
|---|---|
| 余白 | 4px 刻み（4, 8, 12, 16, 20, 24） |
| コントロールの高さ | 28（小）/ 36（標準）/ 44（大）/ 52（タブバー） |
| 角の半径 | カプセル = 高さ/2。入れ子は `内側 = max(外側 − 余白, 4)`（同心） |
| 窓の角 | Adwaita の窓の半径に合わせる（実行時に CSS から読む）⚠️ |
| 前景色 | CSS 変数 `--lg-fg-*`（§9.5）。アクセント色は `--accent-bg-color`（Adwaita）を使う |
| 文字 | システムのフォント（Adwaita Sans）。太さはラベル 600、タイトル 700 |
| アイコン | Adwaita のシンボリックアイコン（16 / 20 / 24px） |
| 動き | スプリング: 応答 0.35s・減衰 0.8（標準）、応答 0.25s・減衰 0.65（弾む：押下）。既存拡張のスプリング（stiffness/damping/mass）を C に移植する |
| 押下 | 拡大 1.0 → 1.06、光の強さ 0 → 1（触れた点から） |

### 10.4 使わないもの・名前の注意

- **使わない**: SF Pro などのフォント、SF Symbols、Apple の画像・アイコン・サウンド、Apple と同じ寸法表のそのままの写し。SF のフォントとシンボルは、ライセンス上 Apple のプラットフォーム以外では使えない。
- **名前**: 「Liquid Glass」は Apple が材質の名前として使っている語。既存拡張の README には免責が書かれている。他のアプリが依存するライブラリの名前としては、**中立な名前の方が安全**（§17 Q1）。この文書では仮に名前空間 `Lg` を使う。

### 10.5 CSS テーマ（`lg-style.css`）

- `.lg-glass` の子孫にある libadwaita の部品の背景を透明にし、前景を `--lg-fg-*` に置き換える（ガラスの上の「前景」になる）。
- `button.flat` はガラスの上ではホバー時に薄い光だけを出す。
- `.lg-on-light` / `.lg-on-dark` で色を切り替える。
- ハイコントラストのときは枠線を足し、背景を不透明にする。

### 10.6 アクセシビリティの必須事項

- 自作の部品には正しい `GtkAccessibleRole` を付ける（switch、slider、tab/tab-list、radio-group）。状態（checked、value）も更新する。
- キーボード: フォーカスリングはガラスの上でも見えるようにする（前景色に追従）。矢印・Space・Enter で操作できること。
- 動きを減らす設定のときは、滴の移動をクロスフェードにする。

---

## 11. 公開 API（C / GObject Introspection）

### 11.1 型の一覧

| 型 | 種類 | 主なプロパティ・メソッド |
|---|---|---|
| `LgContext` | シングルトン | `capabilities`、`renderer`（full/lite）、`profile`、`reduce-transparency`、`lg_init()` |
| `LgGlassView` | GtkWidget | `content`、`add_overlay()`、`remove_overlay()` |
| `LgToolbarView` | GtkWidget | `content`、`add_top_bar()`、`add_bottom_bar()`、`scroll-edge-effect`、`top-inset`（読み取り） |
| `LgGlass` | 抽象 GtkWidget | `material`、`tint`、`prominent`、`interactive`、`corner-radius`、`adaptive`、`appearance`（読み取り）、vfunc `collect_shapes()` |
| `LgGlassBin` | LgGlass | `child` |
| `LgGlassGroup` | GtkWidget | `child`、`spacing` |
| `LgButton` / `LgSwitch` / `LgSlider` / `LgSegmentedControl` / `LgTabBar` / `LgSearchEntry` / `LgPopover` | 各種 | GTK の同等部品に合わせる |
| `LgApplicationWindow` | AdwApplicationWindow | `desktop-glass`、`desktop-glass-state`（none/blur/glass） |
| `LgGlassPane` | GtkWidget | `child`、`material`（既定 THICK） |
| `LgMaterial` / `LgAppearance` / `LgAdaptiveMode` | enum | §6、§9 |

### 11.2 使用例

TypeScript（GJS）:

```ts
import Adw from 'gi://Adw?version=1';
import Gtk from 'gi://Gtk?version=4.0';
import Lg from 'gi://Lg?version=1';

Lg.init();

const grid = new Gtk.GridView({ /* 写真のグリッド */ });
const scrolled = new Gtk.ScrolledWindow({ child: grid });

const view = new Lg.ToolbarView({ content: scrolled, scroll_edge_effect: true });

const header = new Lg.GlassBin({ material: Lg.Material.REGULAR });
header.set_child(new Gtk.Label({ label: 'Photos', css_classes: ['title-3'] }));
view.add_top_bar(header);

const tabs = new Lg.TabBar();
tabs.append('library', 'Library', 'image-x-generic-symbolic');
tabs.append('search', 'Search', 'system-search-symbolic');
view.add_bottom_bar(tabs);
```

Python:

```python
import gi
gi.require_version('Lg', '1')
from gi.repository import Lg, Gtk

Lg.init()
sw = Lg.Switch(active=True)             # ドラッグ中だけつまみがレンズになる
seg = Lg.SegmentedControl()
for key, label in (('day', 'Day'), ('week', 'Week'), ('month', 'Month')):
    seg.append(key, label)
```

C:

```c
LgGlassBin *bar = lg_glass_bin_new ();
lg_glass_set_material (LG_GLASS (bar), LG_MATERIAL_REGULAR);
lg_glass_set_interactive (LG_GLASS (bar), TRUE);
lg_toolbar_view_add_top_bar (view, GTK_WIDGET (bar));
```

---

## 12. デモアプリ

### 12.1 概要

- 名前（仮）: **Glass Gallery**、アプリ ID `io.github.ryohsuke1231.LiquidGlass.Gallery`
- 言語: **TypeScript → GJS**（拡張機能と同じ道具。`@girs` の型、`tsc`）＋ libadwaita ＋ `Lg`
- 目的: (1) ショーケース、(2) 検証用ハーネス（どの機能もここで A/B できる）、(3) GI バインディングが実際に使えることの証明

### 12.2 ページ

| ページ | 内容 | 検証すること |
|---|---|---|
| **Photos** | 明暗さまざまな写真（CC0 を同梱）のグリッドが、上のバーと下のタブバーの下を流れる。スクロールするとタブバーが縮む | 屈折、同じフレームでの一致（1 フレームのずれがないこと）、前景の light/dark 切り替え、スクロール端の効果、滴の移動 |
| **Controls** | Switch / Slider / Segmented / Button の各種。背景を切り替えられる（縞・市松・グラデーション・写真・アニメーション） | 操作中のレンズ、押下の光、キーボード操作、a11y |
| **Material Lab** | 材質の選択、System Profile の値を直接いじる（拡張機能の Glass ページと同じ項目）、デバッグビュー（形状・変位・Adaptive のセル）、Full/Lite の切り替え、フレーム時間の HUD | 画質の比較、性能の計測 |
| **Morph** | spacing のスライダー、近づくとつながり離れると分かれるボタン群 | smooth union、スプリング |
| **Desktop** | 浮くサイドバー（デスクトップガラス）付きの窓。今の経路（glass / blur / none）を表示 | Tier 2、フォールバック、リサイズ中の追従 |
| **Accessibility** | 透明度を下げる・ハイコントラスト・動きを減らすを、その場で切り替える | §6.4 |

### 12.3 配布

- 開発中: `meson devenv` でビルド済みのライブラリを使って `gjs -m` で起動する。
- 配布: Flatpak（`org.gnome.Platform//50`、ライブラリはモジュールとして同梱、`--talk-name=io.github.ryohsuke1231.LiquidGlass`）。

---

## 13. リポジトリ・ビルド・配布・ライセンス

### 13.1 新しいリポジトリ（`~/Projects/GitHub/` の下）

```
liquid-glass-kit/          ← 名前は仮（§17 Q1）
├── README.md  LICENSE  meson.build  meson_options.txt
├── docs/                  設計書（本書の後継）、ADR（決定の記録）、memo（地雷の記録。既存の運用を踏襲）
├── spec/
│   ├── material-schema.json
│   ├── adaptive.md  adaptive-vectors.json
│   ├── dbus/io.github.ryohsuke1231.LiquidGlass1.xml
│   └── wayland/xx-lg-glass-v1.xml          （草案）
├── shaders/               §5.1（単一ソース）
├── lib/                   libliquidglass-1（C）
│   ├── lg-context.c  lg-glass-view.c  lg-toolbar-view.c  lg-glass.c  lg-glass-group.c
│   ├── render/  lg-backdrop-renderer.c  lg-renderer-full.c  lg-renderer-lite.c  lg-gl.c
│   ├── adaptive/  lg-adaptive.c
│   ├── desktop/  lg-desktop-glass.c  lg-transport-dbus.c
│   ├── widgets/  lg-button.c  lg-switch.c  lg-slider.c  lg-segmented-control.c  lg-tab-bar.c ...
│   ├── motion/  lg-spring.c
│   └── style/  lg-style.css（GResource）
├── demo/                  Glass Gallery（TypeScript）
├── tests/                 C の単体テスト、ゴールデン画像、D-Bus の契約テスト
├── tools/                 シェーダのハーネス、同期スクリプト、計測
└── flatpak/               デモの manifest
```

- **拡張機能（App Glass Service）の置き場所**: 💡 推奨は既存の `liquid-glass` リポジトリの中。`tools/sync-shaders.sh` で kit の `shaders/` を取り込み、`shaders/VERSION` を照合する（§17 Q2）。
- **Git**: 作成時に `git init`。作業はユーザーの方針どおり `dev` ブランチで、`main` はリリースだけ。

### 13.2 ビルド

- ライブラリ: meson。依存は gtk4 (≥ 4.22)、libadwaita-1 (≥ 1.9)、epoxy（GL）。GIR / typelib / VAPI を生成する。
  - 事前に `libgtk-4-dev libadwaita-1-dev libepoxy-dev gobject-introspection libgirepository1.0-dev valac`（VAPI 用・任意）の導入が要る。
- シェーダ: meson の `custom_target` で展開し、GResource に入れる。`glslangValidator` で GLES 3.0 と、Cogl 相当（GLSL 1.10 / ES 1.00）の構文を CI で検査する。
- デモ: `tsc` → GJS。型は `@girs` と、自前の GIR から生成した型（ts-for-gir）。

### 13.3 ライセンス

- 💡 推奨: **全体を MIT**（既存の拡張・シェーダと同じ。採用の障壁が最も低い）。代案は、ライブラリだけ LGPL-2.1-or-later（GNOME のライブラリの慣習）。§17 Q6。

---

## 14. テスト戦略

### 14.1 シェーダのゴールデン画像

- `tools/glass-harness`: ヘッドレスの EGL/GLES3（または Cogl を使わない GLSL 1.10 のエミュレーション）で、固定の入力（背景画像・形状・パラメータ）から出力画像を作る。
- **リファクタの受け入れ条件**: 既存の `glass.frag`（拡張の出荷版）と、新しい Core の Cogl ターゲットの出力の差が、全画素で **≤ 1 LSB**（ディザを固定したうえで）。
- GLES3 ターゲットと Cogl ターゲットの差も同じ基準で見る（精度の差を検出する）。

### 14.2 その他

| 対象 | 方法 |
|---|---|
| Adaptive | `adaptive-vectors.json` で C と TS の両方をテストする（同じ入力 → 同じ判定） |
| 形状・融合 | SDF と勾配の数値テスト（有限差分との一致、1-Lipschitz 性） |
| D-Bus | 契約テスト: 他人の窓を指定したら拒否されること、上限、unicast、NameOwnerChanged での後始末 |
| ウィジェット | GTK のテスト（`gtk_test_*`）＋ a11y ツリーの検査 |
| 性能 | デモの HUD と、`tools/perf/` の既存スクリプトの流用（GPU busy%、フレーム時間）。スクロール中と静止中を分けて測る |
| 手動 | チェックリスト（HiDPI 1x/1.25x/2x、マルチモニタ、ダーク/ライト、ハイコントラスト、Flatpak、拡張の ON/OFF、GNOME 51） |

---

## 15. ロードマップ

規模は S（小）/ M（中）/ L（大）の相対値。**各スパイクには Go/No-Go の基準を置き、結果を docs/ADR に記録する。**

### Phase 0: スパイク（実現性の確認。本実装の前に必ず行う）

| ID | 内容 | Go の基準 | 規模 |
|---|---|---|---|
| S1 | **Full レンダラ**: C の最小プログラム。スクロールする写真の上に 1 本のバー。`render_texture` → 読み出し → GLES3 の Glass Core → `GdkGLTexture` | 780M・1x でスクロール中 60fps、追加の CPU ≤ 3ms/frame、中身とガラスのずれが 0 フレーム、snapshot 中の `render_texture` が安全（または専用レンダラで回避できる） | M |
| S2 | **Lite レンダラ**: copy/paste + blur + 照明テクスチャ（＋任意で拡大の近似） | S1 と並べて見比べ、「ガラスに見える」かをユーザーが判断する | S |
| S3 | **B2（blit）**: 拡張機能で、登録した窓の 1 領域の背景をフレームバッファから取る | 背後で動画を再生・窓をドラッグ・窓を開閉しても、黒枠・古い縁・ずれが出ない | M |
| S4 | **窓の同定**: 付録 A の LG スクリプトで、GTK4 の窓の `gtk_window_object_path` と unique name が取れることを確かめる（ポップアップも） | 対象の GTK アプリで値が取れる | S |
| S5 | **Core の切り出し**: `glass.frag` を Core + Cogl ターゲットに分け、ゴールデン画像で一致を確認する | 差 ≤ 1 LSB | M |
| S6 | **GNOME 51 の確認**（VM や GNOME OS で）: ext-bg-effect の実際の見た目、GTK 4.24 がこれを使う条件 | 挙動を文書にできる | S |

### Phase 1: 土台（L）

シェーダ Core（融合・角ごとの半径を含む）、マテリアルのスキーマとコード生成、ゴールデン画像の CI、リポジトリの初期化。

### Phase 2: アプリ内ガラス（L）

`LgContext`、`LgGlassView`、`LgGlassBin`、`LgGlassGroup`、Full/Lite レンダラ、Adaptive（アプリ内）、CSS テーマ、`LgToolbarView`。

### Phase 3: 部品（L）

`LgButton`、`LgSwitch`、`LgSlider`、`LgSegmentedControl`、`LgTabBar`、スプリング、レイヤ 1 の純レンズ、a11y。

### Phase 4: デモ v1（M）

Photos / Controls / Material Lab / Morph の各ページ。Flatpak。

### Phase 5: デスクトップガラス（L）

D-Bus 仕様、拡張機能のサービス（S3 の結果で B1 か B2 を選ぶ）、クライアントの Transport、`LgApplicationWindow`、`LgGlassPane`、Adaptive のイベント、Desktop ページ。

### Phase 6: GNOME 51 以降（M）

ext-bg-effect によるぼかしのフォールバック、GTK 4.24 との衝突回避、Mutter 51 の内部の調査、上流への提案（GTK: displacement ノード。Mutter: 背景エフェクトの拡張点）。

### Phase 7: 仕上げ（M）

性能、ドキュメント、パッケージ、多言語、(任意) Mutter パッチ版の検証。

---

## 16. リスクと対策

| リスク | 影響 | 対策 |
|---|---|---|
| snapshot 中の `render_texture` が GTK の内部状態と衝突する | Full が使えない | 専用レンダラで回避する。最悪は Lite |
| dGPU で読み出しが遅い | スクロール中にカクつく | 縮小（s=0.5）、キャッシュ、材質ごとの縮小率。GTK への提案（§7.7） |
| GTK の非公開部分が変わる | — | 公開 API だけを使う（本設計は非公開 API に依存しない） |
| B2 の再描画クリップを JS で完全には補えない | 縁が古く残る | damaged の監視、サンプリングの clamp、ダメなら B1 |
| 拡張機能のレビュー（EGO）や GNOME の更新で壊れる | デスクトップガラスが止まる | フォールバック（§4.3）で文字は読めるまま。バージョンごとの互換層 |
| 商標・トレードドレス | 名前の変更を迫られる | 中立な名前、Apple の資産を使わない、免責（§10.4） |
| 範囲が膨らむ（部品を作りすぎる） | 完成しない | §10.2 の P0/P1 に絞る。コンテンツ層は libadwaita のまま |
| 1 人で C と GL と GObject を書く負担 | 進まない | スパイクで型を固めてから広げる。AI の支援を前提に、小さく検証できる単位で進める。memo.md の運用（地雷の記録）を新リポジトリでも続ける |

---

## 17. 未決事項（ユーザーへの質問）

| # | 質問 | 推奨 |
|---|---|---|
| Q1 | 名前: 「Liquid Glass」を名前に使い続けますか？（リポジトリ名・GI 名前空間・D-Bus 名に影響） | 他のアプリが依存するライブラリは**中立な名前**（例: "Hyaline"、"Lucent"）。拡張機能の名前は現状のままでもよい |
| Q2 | App Glass Service を既存の拡張に入れますか、新しい拡張にしますか？ | **既存の拡張に入れる**（GPU 資源・設定・フックの共有） |
| Q3 | 対象の GNOME: 50 を主対象にして 51 に追随、でよいですか？ | **はい**（今の環境が 50。51 は S6 で追う） |
| Q4 | ライブラリの言語: C でよいですか？（Rust も可能だが、GJS/Python から使えなくなる） | **C** |
| Q5 | デモアプリの言語: TypeScript（GJS）でよいですか？ | **TypeScript**（今の道具がそのまま使える） |
| Q6 | ライセンス: MIT でよいですか？ | **MIT** |
| Q7 | 将来、Mutter にパッチを当てる路線（厳密な同期。代わりに配布が難しい）を検討しますか？ | 今は**しない**。草案だけ残し、上流への提案を優先 |
| Q8 | 対象のツールキット: GTK4/libadwaita だけでよいですか？（Qt・Electron・Flutter は対象外） | **はい** |
| Q9 | 部品の範囲: §10.2 の P0/P1 でよいですか？ | **はい** |

---

## 付録 A: 検証用 Looking Glass スクリプト

S4 の一部。GTK4 の窓について、Mutter が D-Bus の身元（unique name と窓のオブジェクトパス）を持っているかを確かめる。
Looking Glass（Alt+F2 → `lg`）で実行し、`journalctl --user -f | grep lg-dbusid` で結果を見る。

複数行版（読む用）:

```js
const rows = global.get_window_actors()
  .map(a => a.get_meta_window())
  .map(w => [
    w.get_wm_class(),
    w.get_gtk_application_id(),
    w.get_gtk_unique_bus_name(),
    w.get_gtk_window_object_path(),
    w.get_tag(),
    w.get_client_type() === 0 ? 'wayland' : 'x11',
  ].join(' | '));
log(['[lg-dbusid] ' + rows.length + ' windows', ...rows].join('\n'));
```

1 行版（貼る用）:

```js
log(['[lg-dbusid] windows', ...global.get_window_actors().map(a => a.get_meta_window()).map(w => [w.get_wm_class(), w.get_gtk_application_id(), w.get_gtk_unique_bus_name(), w.get_gtk_window_object_path(), w.get_tag(), w.get_client_type() === 0 ? 'wayland' : 'x11'].join(' | '))].join('\n'));
```

期待される結果: GTK4 の `GtkApplicationWindow`（例: 設定、Nautilus、テキストエディタ）で、3 列目が `:1.123` の形、4 列目が `/org/gnome/.../window/1` の形になる。

---

## 付録 B: このマシンでの計測値（2026-09-23）

| 計測 | 結果 |
|---|---|
| 既定のレンダラ | GskVulkanRenderer |
| `render_texture` の戻り値の型 | Vulkan: `GdkDmabufTexture` / GL: `GdkDmabufTexture` |
| `render_texture`（単色ノード 512x256、20 回平均） | Vulkan 0.26ms / GL 0.24ms |
| `render_texture`（色ノード 60 個、Python でのノード生成込み） | 600x120: 1.30ms / 1200x240: 1.31ms / 1920x1080: 1.70ms |
| `GdkTextureDownloader` の CPU 読み出し | 600x120: 0.27ms / 1200x240: 0.84ms / 1920x1080: 3.75ms |
| `GskGLShaderNode` | 両レンダラで `The renderer does not support gl shaders`、出力はピンク（255,105,180） |
| CSS `backdrop-filter: blur(6px)` | 動作（10px 幅の白黒の縞が 98〜157 の値に平均化された） |
| GtkSvg の `BackgroundImage` | 出力が透明（下の縞がそのまま見える）＝ウィジェットの背景にはならない |
| アプリ用 GL コンテキスト | GLES 3.2 |

計測に使ったスクリプトは `/tmp/claude-1000/.../scratchpad/probe_svg.py` ほか（一時ファイル）。

---

## 付録 C: 参考資料

- GTK: [Gsk ドキュメント](https://docs.gtk.org/gsk4/)、[ノードの書式（displacement ノードの記述あり）](https://docs.gtk.org/gsk4/node-format.html)、[NEWS](https://gitlab.gnome.org/GNOME/gtk/-/raw/main/NEWS)（4.21.2 copy/paste、4.21.4 displacement・isolation、4.23.3 ext-background-effect）
- Mutter: [MR !5071 wayland: Add ext-background-effect-v1 blur support](https://gitlab.gnome.org/GNOME/mutter/-/merge_requests/5071)、[Issue #3023 Background blur API for toolkits and applications](https://gitlab.gnome.org/GNOME/mutter/-/work_items/3023)
- 報道: [Phoronix: GNOME Lands ext-background-effect-v1](https://www.phoronix.com/news/GNOME-Mutter-Background-Blur)、[UbuntuHandbook: Mutter 51 Beta Added Native Background Blur](https://ubuntuhandbook.org/index.php/2026/08/gnome-mutter-51-beta-added-native-background-blur/)、[Phoronix: GNOME 51 Released](https://www.phoronix.com/news/GNOME-51-Released)
- プロトコル: `/usr/share/wayland-protocols/staging/ext-background-effect/ext-background-effect-v1.xml`
- 既存資産: `~/Projects/GitHub/liquid-glass/memo.md`（地雷の記録）、`performance-plan.md`、`liquid-glass@thinkingcoding1231.gmail.com/shaders/glass.frag`
