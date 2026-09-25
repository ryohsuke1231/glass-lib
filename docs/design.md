# glass-lib 設計書

- 版: **v0.3（ウィンドウ部品群を v1 に入れた版）**
- 日付: 2026-09-25
- 状態: Phase 0（S1・S5）完了。Phase 2（ライブラリ本体）とウィンドウ部品群、デモの最初の版を実装（2026-09-25）
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
| 9 | 部品の範囲 | 🔒 **ただのガラス（アイコン・テキストなどを載せられるもの）＋ウィンドウ部品群**（ツールバー・ヘッダーバー・サイドバー・セグメント・ボタン。§6.6） | ウィンドウ部品群は 2026-09-25 のユーザーの決定で v2 から前倒し。スライダー・スイッチ・ポップオーバー・ダイアログは v2 以降（§19） |
| 10 | アプリへの適用のしかた | 🔒 **アプリ内の中身に対するガラスだけ**（macOS Tahoe と同じ） | Tier 2（窓そのものが透けるガラス）は v1 から外す（§2） |

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

### 2.2 v1 で Tier 2 を作らない理由

1. **macOS Tahoe のアプリも、ガラスを窓そのものではなく、アプリ内の中身に対して掛けている**（ユーザーの観察）。
   これは Apple の設計（ガラスはコンテンツの上に浮くコントロール層）とも一致する。サイドバーも「窓の中身の上に浮くガラスの板」として扱われている。
2. 「窓を透かしたい」という需要には、**既存の拡張機能の Application Windows がすでに応えている**。
3. Tier 2 は最も壊れやすい部分（アプリと拡張機能の通信、commit との同期、窓の同定、背後の取り込み）で、v1 の価値に対してコストが大きい。

### 2.3 将来のための確認結果（S4、完了）

窓の同定に使う情報が取れることは確認できた（付録 A.1）。

- `org.gnome.Settings`（GtkApplicationWindow）→ unique name `:1.57`、窓のパス `/org/gnome/Settings/window/1` ✅
- `kitty`（GTK ではない）→ 何も取れない（想定どおり）

Tier 2 を将来作る場合も、この方法で「アプリは自分の窓にしか指示を出せない」ようにできる。設計は v0.1 の §8 に残してある。

---

## 3. v1 の範囲

### 3.1 作るもの

| 部品 | 内容 |
|---|---|
| `GlassView` | 中身（content）の上にガラスの層を持つコンテナ。ガラスの本体はこれが描く |
| `GlassPanel` | ガラスの板。子を 1 つ持ち、アイコン・テキスト・ボタンなどを載せられる。カプセル型または角丸矩形 |
| ウィンドウ部品群（§6.6） | `GlassToolbarView`（スクロール端の効果つき）、`GlassHeaderBar`、`GlassSplitView`（浮くサイドバー）、`GlassToggleGroup`（セグメント）、`GlassButton` |
| `GlassContext` | ライブラリ全体の設定（レンダラの選択、透明度を下げる、光学パラメータ） |
| Full レンダラ | 既存の `glass.frag` と同じ見た目（屈折・色収差・リム・影・AO）をアプリ内で描く |
| フォールバック | GL が使えないとき、または `GlassView` の外に置かれた `GlassPanel` を、CSS の `backdrop-filter` ですりガラスとして描く |
| Adaptive | ガラスの下の明るさから、載せた文字・アイコンの色（明/暗）を自動で切り替える |
| CSS テーマ | ガラスの上に載せた libadwaita の部品（ラベル、アイコン、flat ボタン）を馴染ませる |
| デモアプリ | Glass Gallery（§14） |
| シェーダ Core | `glass.frag` を分解した単一ソース。ゴールデン画像で見た目の一致を保証 |

### 3.2 作らないもの（v1）

- スライダー、スイッチ、タブバー、検索欄などの専用部品（セグメントは §6.6 の簡易版を作る）
- ポップオーバー・メニュー（別の Wayland サーフェスなので今の方式では描けない）、ダイアログ（libadwaita の窓の内部に置かれ、`GlassView` の中に入らない）
- ガラス同士の融合（液滴のようにくっつく表現）とモーフィング
- 押したときの膨らみ・光（interactive）
- ガラスの上のガラス（重なったパネルが互いを屈折させること）
- デスクトップが透けるガラス（Tier 2）と D-Bus サービス
- GNOME 51 の `ext-background-effect-v1` 対応
- メニュー用の材質（MENU）、角ごとの半径

いずれも v2 以降の候補として §19 に理由と入口を残す。**v1 の設計は、これらを後から足しても作り直しにならないようにする**（形状は配列で持つ、材質は enum で持つ、など）。

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
2. **幾何は snapshot の中で読む**（allocation 確定後の値。memo 地雷18 と同じ理由）。
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
/* 光学パラメータ（上級者・デモの Lab 用）。キーは §11.1 の表。範囲外は clamp して警告 */
gboolean        glass_context_set_param         (GlassContext *self, const char *key, double value);
double          glass_context_get_param         (GlassContext *self, const char *key);
gboolean        glass_context_is_param_set      (GlassContext *self, const char *key);
double          glass_context_get_effective_param (GlassContext *self, GlassMaterial material, const char *key); /* その材質で実際に使われる値 */
void            glass_context_reset_param       (GlassContext *self, const char *key);
const char * const *glass_context_list_params   (GlassContext *self);
gboolean        glass_context_get_param_range   (GlassContext *self, const char *key, double *min, double *max, double *def);

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
void            glass_panel_set_tint            (GlassPanel *self, const GdkRGBA *tint); /* alpha = 強さ。NULL = 材質の既定 */
void            glass_panel_set_has_shadow      (GlassPanel *self, gboolean has_shadow);
void            glass_panel_set_adaptive        (GlassPanel *self, GlassAdaptiveMode mode);
GlassAppearance glass_panel_get_appearance      (GlassPanel *self);  /* 読み取り専用（notify あり） */
```

### 6.2 列挙型

| 型 | 値 | 意味 |
|---|---|---|
| `GlassRendererMode` | `AUTO` / `FULL` / `FALLBACK` | AUTO = GL が使えれば FULL |
| `GlassMaterial` | `REGULAR` / `CLEAR` / `THICK` | §11.2。`THICK` は大きな板（サイドバー）用 |
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
| | `tint` | GdkRGBA（NULL = 材質の既定） |
| | `has-shadow` | boolean、TRUE |
| | `adaptive` | `GlassAdaptiveMode`、`AUTO` |
| | `appearance` | `GlassAppearance`（読み取り専用） |
| `GlassContext` | `renderer` | `GlassRendererMode`、`AUTO`（環境変数 `GLASS_RENDERER=full\|fallback` で上書き） |
| | `reduce-transparency` | boolean、FALSE |

### 6.4 CSS

| 種類 | 名前 | 用途 |
|---|---|---|
| CSS ノード名 | `glassview`、`glasspanel` | |
| クラス（ライブラリが付ける） | `.glass-light` / `.glass-dark` | Adaptive の結果（§12） |
| クラス（ライブラリが付ける） | `.glass-fallback` | CSS で描いているとき（§9） |
| CSS 変数 | `--glass-fg-color`、`--glass-dim-fg-color`、`--glass-hover-color`、`--glass-active-color` | ガラスの上の前景色 |

CSS はライブラリの GResource から `GTK_STYLE_PROVIDER_PRIORITY_SETTINGS`（400）で読み込む。
libadwaita（THEME、200）より上、アプリ（APPLICATION、600）より下なので、アプリはいつでも上書きできる。

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
| `GlassHeaderBar` | `AdwHeaderBar` | `pack_start`・`pack_end` の部品は、それぞれ**1 つのガラスのカプセル**にまとめて浮かせる。タイトルはガラスなし（スクロール端の効果の上に載る）。窓のボタン（閉じる等）も小さなカプセルに入れる（どんな中身の上でも見えるように）。空の所はドラッグで窓を動かせる（`GtkWindowHandle`） |
| `GlassSplitView` | `AdwOverlaySplitView` | サイドバーは**窓の中に浮く角丸の板**（材質 `THICK`、窓の縁から 8px 内側、半径 14px）。中身はサイドバーの下まで伸び、サイドバーが覆う幅は `content-inset`（読み取り専用）で出す。`show-sidebar` でスライドして出し入れする |
| `GlassToggleGroup` | `AdwToggleGroup` | カプセルの中に並んだトグル。選ばれたものの下を明るい丸い板がスライドする。**板はガラスではない**（ガラスの上のガラスは v2 の層が要る。Apple の「つまんでいる間だけレンズ」も v2）。トグルは `GtkToggleButton` ではなく `GtkButton`＋`.active` クラス（テーマの `button:checked` の塗りが板を覆うため。memo 地雷12） |
| `GlassButton` | `GtkButton` ＋ `.circular` / `.pill` | それ自体がガラスのボタン（アイコンか文字）。押している間はガラスが少し明るくなる。`GtkActionable` |

- 部品の中のボタンは自動で `flat` の見た目になる（ガラスの上に libadwaita の塗りの背景を重ねない）。前景色は Adaptive に従う。
- **ガラスの上のガラスは描かない**: `GlassPanel` の子孫にある `GlassPanel`（例: サイドバーの中のヘッダーバーのカプセル）はガラスを描かず、`.glass-nested` として薄い面だけにする。
  `GlassPanel` の中に置いた `GlassView` は背景色を塗らない（下のガラスを隠さないため）。
- スクロール端の効果は `GlassView` が描く（中身の直後、ガラスの前）。ガラスの取り込みもこの効果を含む（ガラスの後ろに見えているものと同じ）。
  SOFT: バーの高さ＋最大 32px の帯で、中身を GSK の blur（10px）でぼかし、背景色を 55% 重ね、バーの半分まで不透明・帯の終わりで透明になるマスクをかける。HARD: バーの高さの帯を blur 16px・背景色 72% で覆い、前景色 15% の 1px の線を引く。
- 実装済み（2026-09-25）。API は `lib/glass-toolbar-view.h` などの各ヘッダ。CSS: ヘッダーバーのカプセルは `.header-capsule`（窓のボタンは `.window-controls` も）、サイドバーは `.sidebar`、セグメントは `.toggle-group`（板の色は子ノード `pill` の `color`）、ボタンは `.glass-button`。

---

## 7. GlassView の詳細

### 7.1 レイアウト

`GtkOverlay` と同じ規則にする（GTK の開発者が迷わないように）。

- `content` はビュー全体に広がる。ビューの自然な大きさは `content` の大きさ。
- overlay 子は、自分の `measure` の結果と `halign` / `valign` / `margin-*` で配置する。
- パネルの下に中身が隠れる分の余白（例: 最初の写真が上のパネルに隠れない）は、v1 ではアプリが中身に `margin` を付けて調整する。
  自動の「中身の余白」は v2 の候補（§19）。

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
- `renderer` は窓のもの（`gtk_native_get_renderer`）を使う。
  ✅ **snapshot の中で `render_texture` を呼んでよい**（S1 で確認。Vulkan・GL の両レンダラ、Wayland・X11）。
  背景の取り込み専用の `GskRenderer` は不要だった（試作では `--private-renderer` で比べたが、性能も結果も同じ）。
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

- パネルごとに最大 3 枚。`gdk_gl_texture_builder_build()` の破棄通知で、GTK が使い終わったテクスチャをプールに戻す。
- `GLsync` を `gdk_gl_texture_builder_set_sync()` で付ける（GTK 側が描画完了を待てるように）。
- 3 枚とも使用中なら、そのフレームは前回のテクスチャを再利用する（書き換え中のテクスチャを GTK に渡さない）。

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
- 実測（2026-09-25、Glass Gallery の Photos を自動スクロール、ビュー 2 つ・パネル 8 枚・取り込み 3 回/フレーム）: 60fps、ガラスの CPU 約 3.3ms/フレーム、GPU busy 31%。
  予算をわずかに超える。残りはサイドバーの取り込み（下の見た目が変わらなくても中身のノードが変わるたびに取り直す）。候補は memo 追記6。
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
- Adaptive はフォールバックでも動かす（§12.1 のフォールバック用の取り込み）。
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
│   ├── glass_params.glsl         uniform と定数（EDGE_LENS_FALLOFF / EDGE_LENS_REACH、測ったフットプリントのタップ数）
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
| `lens_px_scale` | `EDGE_LENS_REACH`（96px）とフットプリントの上限（64px）の倍率 | `S`（HiDPI で論理サイズを保つ） | `1` |
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

### 10.4 GLES 3.0 と GL 3.3 の両対応

- Core は GLSL 1.30 以降の書き方（`texture`、`in`/`out`）を使わず、ターゲットに任せる。精度は GLES で `precision highp float;`（ローダが付ける）。
- `meson test` で `glslangValidator` が Core・参照・quad を GLES 3.00 と GL 3.30 core の両方で検査する（6 件）。

### 10.5 ゴールデンテスト（`tests/golden/glass-golden.c`）

- Mesa の surfaceless EGL で窓なしの GLES 3.0 コンテキストを作り、参照（`compat/reference.frag`）と Core（`gl/glass.frag`）を同じ入力で描いて、画素を比べる。使えない環境では skip（77）。
- 場面: 10 通りの設定（拡張の値・アプリ内の値・色収差なし・早期リターンなし・デバッグ 1/2・スーパーサンプリング・2 倍スケール・小さい面・面の端）× 4 形状（カプセル・パネル・大・正方形）× 5 背景（縞・市松・グラデーション・同心円・ノイズ）= 200 ケース。
- 受け入れ条件: 全画素で差が 1/255 以下。**結果は全ケースで 0/255。**
- 失敗したケースは `--write DIR` で参照・Core・差（32 倍）の PNG を保存できる。
- 空振りしていないことも確かめた。`EDGE_LENS_FALLOFF` を 2.4 → 2.5 に変えると 141 ケースが失敗する。

---

## 11. パラメータとマテリアル

### 11.1 光学パラメータ（全体の設定）

既定値は**既存拡張の gschema の既定値と同じ**（2026-09-24 時点）。キー名も拡張機能と同じ意味・同じ単位にする。

| キー（`glass_context_set_param`） | 既定 | 単位 | 拡張機能のキー |
|---|---|---|---|
| `max-z` | 25.0 | px | `glass-max-z` |
| `displacement-scale` | 78.5 | px | `glass-displacement-scale` |
| `edge-smoothing` | 2.0 | px | `glass-edge-smoothing` |
| `profile-shape-n` | 7.0 | – | `glass-profile-shape-n` |
| `ior` | 2.40 | – | `glass-ior` |
| `chroma-strength` | 1.5 | px | `glass-chroma-strength` |
| `specular-intensity` | 0.0 | – | `glass-specular-intensity` |
| `shininess` | 42.0 | – | `glass-shininess` |
| `rim-width` | 5.0 | px | `glass-rim-width` |
| `rim-intensity` | 0.6 | – | `glass-rim-intensity` |
| `rim-directional-power` | 2.7 | – | `glass-rim-directional-power` |
| `rim-power` | 6.0 | – | `glass-rim-power` |
| `rim-light-color-intensity` | 1.4 | – | `glass-rim-light-color-intensity` |
| `sheen-intensity` | 0.32 | – | `glass-sheen-intensity` |
| `light-angle-deg` | 50.0 | deg | `glass-light-angle-deg` |
| `shadow-radius` | 30.0 | px | `shadow-radius` |
| `shadow-intensity` | 0.55 | – | `shadow-intensity` |
| `ao-intensity` | 0.25 | – | `glass-ao-intensity` |
| `ao-radius` | 7.5 | px | `glass-ao-radius` |
| `blur-downscale` | 2 | 2 / 4 | `glass-blur-downscale` |

- px の値は**論理 px**。描画時に `S`（サーフェスのスケール）倍する。
- 影（`shadow-radius`・`shadow-intensity`）の表の値は拡張と同じだが、**アプリ内では材質ごとの値を使う**（§11.2）。拡張の値は壁紙の上に浮くドック・メニュー向けで、明るい窓の中の小さな部品には強すぎる（ユーザーの指摘、`docs/memo.md` 地雷5）。
- 範囲（min/max）は拡張機能の設定画面（`prefs.js`）と同じにする（実装時に写す）。範囲外は clamp して `g_warning`。
- **光学の設定はこれ以上増やさない**（既存拡張の方針 7）。`EDGE_LENS_FALLOFF`・`EDGE_LENS_REACH` は定数のまま。
- 定義は `spec/params.json` に 1 か所で書き、C のヘッダをビルド時に生成する。

### 11.2 材質（v1 は 2 種類）

| 材質 | 用途 | ぼかし半径 | ティント | 影（半径・強さ） | 取り込みの縮小 | 備考 |
|---|---|---|---|---|---|---|
| `REGULAR` | ツールバー、タイトル、ボタンの台 | 2px | 白 0.12 | 16px・0.20 | `blur-downscale` | ティントは既存拡張のドックの既定値と同じ。ぼかし（拡張のドックは 5）と影は S1 でユーザーと比べて決めた（2026-09-25） |
| `CLEAR` | 写真・動画の上 | 1.5px | 白 0.04 | 16px・0.20 | 1（縮小しない） | ぼかしが弱いと縮小が見えるため等倍。Adaptive が mixed のときは暗幕（黒 0.25）を足す |
| `THICK` | サイドバー（大きな板） | 12px | 窓の背景色 0.55（ライト/ダークに追従） | 24px・0.16 | `blur-downscale` | 大きな板は下の中身が場所ごとに違うので、前景色を切り替えず（Adaptive なし、テーマの色のまま）、濃いティントで読めるようにする |

- 🔒 **縁の値（アプリ内用）**: 拡張の光学の値は大きなガラス向けの絶対 px で、小さなアプリ内のガラスでは縁が太く濁る（ユーザーの指摘、`docs/memo.md` 地雷8・追記4）。
  アプリ内の材質は、縁に効く値（`edge_smoothing`・`rim_width`・`rim_power`・`ao_radius`・`ao_intensity`・`chroma_strength`・`profile_shape_n`・`displacement_scale`・`max_z`）を**材質自身の値**として持つ。
  🔒 値は S1 のプリセット **`crisp-soft`**（ユーザーの決定、2026-09-25。値は memo 追記5 の表）: `edge_smoothing` 0.75、`rim_width` 2、`rim_power` 9、`ao_radius` 3、`ao_intensity` 0.10、`chroma_strength` 0.8、`profile_shape_n` 7、`max_z` 14、`displacement_scale` 45。
  縁の**線**（輪郭のぼかし・リムの光・内側の影・色のにじみ）は細くし、**レンズ**（縁の近くで背景が曲がる帯＝ガラス感）は拡張のドームの形のまま弱めにした。設定の種類は増やさない（値の選び方だけ）。
  スーパーサンプリング 4x と測ったフットプリントは既定 ON（§8.5）。
  §11.1 の全体の値（拡張と同じ）は、大きなガラス（将来のサイドバーなど）と比較用に残す。
- 見た目（明/暗）でティントを変えるか（Apple は変える）は、**v1 では変えない**（既存拡張と同じ: 白いティント固定、前景色だけ切り替える）。
  デモの Lab で「見た目に応じたティント」を A/B で試せるようにし、良ければ v1.x で材質の既定にする。
- アプリが変えられるのは `tint`・`corner-radius`・`has-shadow`・`material` だけ。光学パラメータはアプリごとではなく全体の設定（`GlassContext`）。
- 🔒 **値の決まり方**: 各キーの値は「`glass_context_set_param()` で明示された値 → 材質の値 → §11.1 の既定値（拡張と同じ）」の順に最初にあるもの。
  つまり Lab などで明示した値はすべての材質に効き、`glass_context_reset_param()` で材質の値に戻る。ぼかし半径も同じ仕組みのキー `blur-radius` で扱う（既存拡張も要素ごとのぼかし半径を設定に持っているので、新しい光学の設定ではない）。

### 11.3 既存拡張の設定に追従する（v1.x・任意）

サンドボックス外のアプリは、既存拡張の設定（`/org/gnome/shell/extensions/liquid-glass/` の `glass-*`）を読める。
拡張機能のスキーマは拡張機能のディレクトリにあるので、`GSettingsSchemaSource.new_from_directory()` で読み込む。
これを有効にすると、**拡張機能の Glass ページで調整した見た目がアプリにもそのまま反映される**。Flatpak のアプリでは読めないので既定値を使う。
v1 の必須ではない（`GlassContext` のプロパティ `follow-shell-settings` として後から足す）。

---

## 12. Adaptive（前景色の自動切り替え）

### 12.1 入力

- Full: §8.3 で読み出した取り込み画素（縮小済み・ぼかし前）を使う。
- フォールバック: パネルの矩形を小さく（例 32x8）`render_texture` して読み出す。中身が変わったときだけ、最大 10 回/秒。
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
| 動きを減らす | `gtk-enable-animations` | 前景色の遷移を 0ms に |
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
| **Lab** | 光学パラメータ（§11.1）のスライダー、デバッグビュー（形状/影・変位）、Full / フォールバックの切り替え、HUD（§8.8）、「見た目に応じたティント」の A/B | 画質と性能の比較、既存拡張との見比べ |

- 写真は同梱しない（ライセンスのため）。実行時に `/usr/share/backgrounds` の画像を読み、加えて「フォルダを開く」（ファイル選択のポータル）と、コードで生成するテスト模様を使う。
- 開発中は `meson devenv` でビルドしたライブラリ（typelib）を使って `gjs -m` で起動する。Flatpak は Phase 4。

### 14.1 ショーケース用デモ（将来）: 天気アプリ（仮称 Glass Weather）

Glass Gallery は**検証用のハーネス**。開発者を惹きつけるための「見せる」デモは別に作る。ユーザーの提案で**天気アプリ**にする（2026-09-24。作成は v1 のライブラリが固まってから）。

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

### 15.1 構成（予定）

```
glass-lib/
├── README.md  LICENSE（MIT）  .gitignore
├── meson.build  meson_options.txt
├── docs/
│   ├── design.md              ← 本書
│   ├── memo.md                地雷の記録（既存リポジトリと同じ運用。Phase 0 で作る）
│   └── archive/design-v0.1.md
├── spec/
│   ├── params.json            光学パラメータと材質の定義（C ヘッダを生成）
│   └── adaptive-vectors.json  Adaptive のテストベクタ
├── shaders/                   §10.1
├── lib/
│   ├── glass.h  glass-version.h.in  glass-main.c
│   ├── glass-context.[ch]  glass-view.[ch]  glass-panel.[ch]  glass-enums.[ch]
│   ├── render/  glass-renderer.[ch]  glass-capture.c  glass-blur.c  glass-pass.c  glass-gl.[ch]   （非公開）
│   ├── adaptive/  glass-adaptive.[ch]   （非公開）
│   └── style/  glass.css  glass.gresource.xml
├── demo/                      Glass Gallery（TypeScript）: package.json  tsconfig.json  src/  data/
├── tests/                     C の単体テスト、golden/（Core と参照の比較。§10.5）
├── spikes/                    Phase 0 の試作（s1-full-renderer）
└── tools/                     glsl-include.py（#include の展開）、計測スクリプト
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
meson devenv -C build -w . gjs -m demo/dist/main.js
```

- デモの型: `@girs/gtk-4.0`・`@girs/adw-1` と、ビルドした `Glass-1.gir` から `@ts-for-gir/cli` で生成した型。
- 依存: gtk4 ≥ 4.22、libadwaita-1 ≥ 1.9、epoxy。

### 15.3 Git の運用

- `main` はリリース用、作業は `dev` ブランチ（既存リポジトリと同じ）。
- 最初のコミットで `dev` を作る。

---

## 16. テスト

| 対象 | 方法 | 合格条件 |
|---|---|---|
| シェーダ Core | ゴールデンテスト（§10.5、`meson test`） | 参照 `glass.frag` と 1/255 以内（実績 0/255） |
| シェーダの構文 | `glslangValidator`（GLES 3.0 / GL 3.3） | エラーなし |
| ぼかし | カーネルの係数を既存拡張の TS 実装と比較 | 係数が一致 |
| Adaptive | `adaptive-vectors.json` を C で再生（`tests/test-adaptive.c`） | 全ベクタで一致 ✅ |
| パラメータ | clamp・既定値・`list_params`・材質の解決（`tests/test-params.c`） | 表（§11.1）と一致 ✅ |
| ウィジェット | 登録と解除、content 側・入れ子・レンダラ設定でのフォールバック、部品の報告値（`tests/test-widgets.c`。ディスプレイが要る） | 警告なし ✅ |
| 性能 | デモの HUD、GPU busy%（既存の計測方法） | §8.8 の予算内 |
| 手動 | チェックリスト: 1x / 1.25x / 2x、ライト/ダーク、ハイコントラスト、Vulkan / GL（`GSK_RENDERER=gl`）、X11、Flatpak | 文字が常に読める・ずれがない |

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

### Phase 1: 土台（M）— CI 以外は完了

- ✅ meson の骨組み、`spec/params.json` とヘッダの生成、シェーダ Core、ハーネスとゴールデン画像（Phase 0 と Phase 2 の中で作った）
- ⬜ CI（GitHub Actions: ビルド・単体テスト・シェーダ検査）

### Phase 2: ライブラリ本体（L）— 実装済み（2026-09-25。memo 追記6）

- ✅ `glass_init`、`GlassContext`、`GlassView`、`GlassPanel`、Full レンダラ、フォールバック、Adaptive、CSS、GIR/typelib
- ✅ ウィンドウ部品群（§6.6。2026-09-25 のユーザーの決定で v2 から前倒し）
- ⬜ 残り: フォールバックの Adaptive（§12.1 の小さな取り込み）、スクロール中の CPU を予算（3ms）内へ（サイドバーの取り込み。memo 追記6）、HiDPI（2x・分数スケール）と dGPU での確認、Python からの利用の確認

### Phase 3: デモ（M）— 最初の版を実装済み（2026-09-25）

- ✅ Photos / Playground / Lab
- ⬜ 「見た目に応じたティント」の A/B（C3）、材質の値を見比べて決める（C2）

### Phase 4: 仕上げ（M）

性能の調整、アクセシビリティの確認（ハイコントラスト・透明度を下げる・動きを減らす）、API ドキュメント（gi-docgen）、Flatpak（デモ）、README のスクリーンショット、GNOME 51 のランタイムでの CI。

### v1 の後

- v1.x: 既存拡張の設定への追従（§11.3）、見た目に応じたティント（C3 の結果しだい）
- ショーケース: 天気アプリ Glass Weather（§14.1）
- v2: §19 の候補（ガラスの融合・押したときの反応・ガラスの上のガラス・専用部品・ポップオーバー／メニュー／ダイアログ・Tier 2 など）

---

## 18. リスク

| リスク | 影響 | 対策 |
|---|---|---|
| snapshot 中の `render_texture` が GTK と衝突する | Full が使えない | ✅ S1 で衝突しないことを確認。ただし GL レンダラでは current のコンテキストが変わる（§8.2、地雷1） |
| dGPU で読み出しが遅い | スクロール中にカクつく | 縮小・キャッシュ。上流に dmabuf の取り出しか displacement ノードの公開を提案（§19） |
| パネルを重ねたときに互いを屈折しない | 見た目の違和感 | v1 では重ねない前提を文書化。v2 で DAG の層にする |
| content の背景が透明だとガラスが黒ずむ | 見た目 | `backdrop-color`（§7.3） |
| ビューとパネルの間のコンテナに背景がある | ガラスが隠れる | 文書と CSS の既定、デバッグ時の警告 |
| GTK の将来の変更（4.24 以降） | 動作の変化 | 公開 API だけを使う。GNOME 51 のランタイムで CI を回す（Phase 4） |
| 名前「Liquid Glass」への言及 | 商標 | ライブラリ名は glass-lib。README に Apple との無関係を明記 |

---

## 19. v2 以降の候補（保留したもの）

| 候補 | 入口（v1 の何を拡張するか） |
|---|---|
| ガラスの融合・モーフィング（`GlassGroup`、smooth union） | Core の形状配列、勾配の解析的な混合（v0.1 §5.3） |
| 押したときの膨らみ・光（`interactive`） | `GlassPanel` のプロパティ、スプリング（既存拡張のスプリングを移植） |
| ガラスの上のガラス・純レンズ（スライダーのつまみ、タブの滴） | ビューの描画順序を層にする（v0.1 §7.5） |
| 専用部品（Switch, Slider, SegmentedControl, TabBar, SearchEntry） | 上の 3 つが揃ってから |
| 大きなパネル（サイドバー: THICK / MENU）、角ごとの半径、中身の自動の余白 | 材質の enum、Core の半径 vec4 |
| Tier 2（窓が透けるガラス）を既存拡張のモジュールとして | v0.1 §8（D-Bus 仕様・窓の同定は S4 で確認済み） |
| GNOME 51 の `ext-background-effect-v1` | Tier 2 と一緒に |
| 既存拡張の設定への追従 | §11.3 |
| 既存拡張が Core を使う（シェーダの一本化） | §10.2 の明示化で、拡張機能は今と同じ値を渡せば一致する |
| 上流 GTK への提案（displacement ノードの公開、dmabuf の取り出し） | 実現すれば読み出しが不要になり、GPU 内で完結する |

### 19.1 macOS のようなウィンドウ部品群を既定で提供するか（2026-09-24 のユーザーの問い）

> **2026-09-25 更新**: ユーザーの決定で、優先度 1〜3（ツールバー・スクロール端・サイドバー・セグメント・ボタン）を v1 に前倒しした（§6.6）。
> 以下の表の 4・5（ダイアログ・ポップオーバー・スイッチ・スライダー）は v2 のまま。

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
| C2 | 材質 `REGULAR` / `CLEAR` の値（§11.2 は初期案。影は S1 で 16px・0.20 に仮決め） | デモの Playground・Lab で見て、ユーザーが決める |
| C3 | 見た目に応じたティント（Apple 流）を既定にするか | デモの Lab の A/B で、ユーザーが決める |
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
