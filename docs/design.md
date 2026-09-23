# glass-lib 設計書

- 版: **v0.2（v1 の範囲を確定した版）**
- 日付: 2026-09-24
- 状態: 設計確定・実装未着手（Phase 0 のスパイクから開始）
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
| 9 | 部品の範囲 | 🔒 **ただのガラスだけ**（アイコン・テキストなどを載せられるもの） | スライダー・スイッチ・タブバーなどは v2 以降（§19） |
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
| `GlassContext` | ライブラリ全体の設定（レンダラの選択、透明度を下げる、光学パラメータ） |
| Full レンダラ | 既存の `glass.frag` と同じ見た目（屈折・色収差・リム・影・AO）をアプリ内で描く |
| フォールバック | GL が使えないとき、または `GlassView` の外に置かれた `GlassPanel` を、CSS の `backdrop-filter` ですりガラスとして描く |
| Adaptive | ガラスの下の明るさから、載せた文字・アイコンの色（明/暗）を自動で切り替える |
| CSS テーマ | ガラスの上に載せた libadwaita の部品（ラベル、アイコン、flat ボタン）を馴染ませる |
| デモアプリ | Glass Gallery（§14） |
| シェーダ Core | `glass.frag` を分解した単一ソース。ゴールデン画像で見た目の一致を保証 |

### 3.2 作らないもの（v1）

- スライダー、スイッチ、セグメント、タブバー、検索欄などの専用部品
- ガラス同士の融合（液滴のようにくっつく表現）とモーフィング
- 押したときの膨らみ・光（interactive）
- ガラスの上のガラス（重なったパネルが互いを屈折させること）
- デスクトップが透けるガラス（Tier 2）と D-Bus サービス
- GNOME 51 の `ext-background-effect-v1` 対応
- 大きなパネル用の材質（THICK / MENU）、角ごとの半径

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
void            glass_context_reset_param       (GlassContext *self, const char *key);
const char * const *glass_context_list_params   (GlassContext *self);

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
| `GlassMaterial` | `REGULAR` / `CLEAR` | §11.2 |
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
- 既定（NULL）は `AdwStyleManager:dark` に追従する窓の背景色にする（libadwaita の `--window-bg-color` 相当。値は ⚠️ 実装時に libadwaita 1.9 の CSS から確認する）。

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
取り込み矩形 C = パネルの矩形 + ぼかしの余白（3σ）を、ビューの範囲で切り詰めたもの
縮小率 k   = 1 / blur-downscale（既定 1/2。既存拡張の glass-blur-downscale と同じ）
node       = transform( scale(S·k) · translate(−C.x, −C.y) ) { clip(C) { backdrop } }
tex        = gsk_renderer_render_texture (renderer, node, C を S·k 倍した矩形)
```

- `S` は `gdk_surface_get_scale()`（分数スケールに対応）。
- `renderer` は窓のもの（`gtk_native_get_renderer`）を使う。
  ⚠️ **snapshot の中で `render_texture` を呼んでよいか**は S1 で確かめる。問題があれば、背景の取り込み専用に `GskRenderer` をディスプレイごとに 1 つ作る（`gsk_renderer_realize_for_display`。窓と同じ種類のレンダラ）。代わりにグリフのキャッシュが二重になる。

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
- フルスクリーンの四角形を 1 枚描き、Core の `glass_main()` を呼ぶ（§10）。
- 入力:
  - ぼかし済みテクスチャ（取り込み矩形 C の座標系）と、その実際のテクセル数（既存の `blur_tex_w/h` と同じく、拡大時の滑らかな補間に使う）
  - 形状（矩形・半径。デバイス px）
  - 材質と光学パラメータ（論理 px の値を `S` 倍）
  - 見た目（Adaptive の結果）

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

### 10.1 分割

```
shaders/
├── core/
│   ├── glass_sdf.glsl        角丸矩形の SDF と勾配（sdRoundRect / sdRoundRectDir）
│   ├── glass_profile.glsl    高さプロファイル・法線（normalizedDepth / profileHeight / heightGradient）
│   ├── glass_optics.glsl     屈折（getDisplacement）、edge lensing、フットプリントのタップ、色収差
│   ├── glass_tone.glsl       SCB、base / custom ティント
│   ├── glass_lighting.glsl   リム・スペキュラ・シーン・内側 AO
│   ├── glass_shadow.glsl     umbra / penumbra のドロップシャドウ
│   ├── glass_output.glsl     スクリーン合成・色相を保つクランプ・事前乗算・ディザ
│   └── glass_main.glsl       glass_main(): 上をつなぐ本体（早期リターンを含む）
├── targets/
│   ├── gl/glass.frag         glass-lib 用（GLES 3.0 / GL 3.3 core）
│   ├── gl/blur_gaussian.frag ぼかし（カーネルは C から生成して埋め込む）
│   ├── gl/quad.vert
│   └── compat/cogl_prelude.glsl   テスト用: 元の glass.frag を GL で動かすための定義
└── tests/                    ゴールデン画像の入力と期待値
```

- `#include` は小さな展開スクリプト（`tools/glsl-include.py`）で展開し、meson の `custom_target` で GResource に入れる。
- **関数と定数は元の `glass.frag` から意味を変えずに移す。** コメントのうち、式の根拠（なぜこの式か）は Core に残す。拡張機能固有の経緯のコメントは残さない。

### 10.2 隠れた「FBO の大きさ」への依存を明示する（重要）

元の `glass.frag` では、次の 2 つの値が `resolution`（FBO の大きさ）から決まっている。
既存拡張のドック・メニューなどはモニタ全面の FBO なので、実際には次の値で見た目が承認されている。

| 元の式 | 全面 FBO（1920x1080）での値 | 小さなパネル（例 300x60）でそのまま使った場合 |
|---|---|---|
| `gradientStep = clamp(min(res) / 560, 0.45, 1.20)` | **1.20** | 0.45 → 法線の平滑化が弱まり、縁の見た目が変わる |
| `max_disp_px = 0.30 * min(res)` | **324px**（実質的に効かない） | 18px → 屈折が大きく弱まる |

→ Core ではこの 2 つを**明示的な uniform**（`u_gradient_step`、`u_max_disp_px`）にする。
glass-lib は `1.20 × S` と `324 × S` を渡す（承認済みの見た目と同じ）。
拡張機能から Core を使う場合は、元の式で計算した値を渡せば今と完全に同じになる。

同様に、px 単位の定数（`EDGE_LENS_REACH 96.0`、フットプリントの上限 64px、RGSS のオフセット）は、Core の中では `× u_device_scale` して使う。HiDPI でも論理 px での見た目を保つため。

### 10.3 Core に入れるもの・入れないもの

| 入れる | 入れない（ターゲット側またはテスト側で扱う） |
|---|---|
| 形状・屈折・トーン・照明・影・AO・ディザ、2 つの早期リターン | `dock_*`・`isDock`・`padding`（形状は正確な矩形で渡す） |
| `edge_taps_enabled`・`early_exit_enabled`（A/B 用。既定 1） | `panel_bg_*`・`panel_rect_*`（Quick Settings 専用の塗り） |
| `debug_view`（1・2 に加えて 3 = 変位の可視化） | `multi_region_mode` の「最も近い 1 つを選ぶ」処理（v1 は 1 パス 1 形状。v2 で融合と一緒に作り直す） |
| 形状は**配列で受ける**（v1 では要素 1 個。v2 の融合に備える） | 未使用の `pointer_x/y`・`mouse_radius`・`bg_glow_intensity` |

### 10.4 GLES 3.0 と GL 3.3 の両対応

- `texture2D` → `texture`、`cogl_color_out` → `out vec4 frag_color`、`cogl_tex_coord_in[0]` → `in vec2 v_uv` に置き換える（Core は `glass_sample(uv)` という関数を通して読む。ターゲットがその実装を持つ）。
- 精度: GLES では `precision highp float;`（SDF と px の計算に mediump は足りない）。
- `glslangValidator` で両方の版を CI で検査する（`-S frag`、`#version 300 es` と `#version 330`）。

### 10.5 ゴールデン画像

- `tools/glass-harness`（C）: `gdk_display_create_gl_context()` で窓なしの GL コンテキストを作り、固定の入力（背景画像・形状・パラメータ）から出力を作って PNG に保存する。
- **元の `glass.frag` も同じハーネスで動かす**: `compat/cogl_prelude.glsl` で `cogl_sampler1`・`cogl_tex_coord_in`・`cogl_color_out` などを定義し、既存のシェーダをそのまま読み込む。
- 受け入れ条件: 10 種類以上の場面（縞・市松・写真・グラデーション・文字、カプセル・角丸、小・大、影あり・なし）で、Core の出力と元の出力の差が**全画素で 1 LSB 以下**（ディザを固定した状態で。`resolution` 依存の 2 値は §10.2 のとおりそろえる）。

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
- 範囲（min/max）は拡張機能の設定画面（`prefs.js`）と同じにする（実装時に写す）。範囲外は clamp して `g_warning`。
- **光学の設定はこれ以上増やさない**（既存拡張の方針 7）。`EDGE_LENS_FALLOFF`・`EDGE_LENS_REACH` は定数のまま。
- 定義は `spec/params.json` に 1 か所で書き、C のヘッダをビルド時に生成する。

### 11.2 材質（v1 は 2 種類）

| 材質 | 用途 | ぼかし半径 | ティント | 取り込みの縮小 | 備考 |
|---|---|---|---|---|---|
| `REGULAR` | ツールバー、タイトル、ボタンの台 | 5px | 白 0.12 | `blur-downscale` | 既存拡張のドックの既定値と同じ |
| `CLEAR` | 写真・動画の上 | 1.5px | 白 0.04 | 1（縮小しない） | ぼかしが弱いと縮小が見えるため等倍。Adaptive が mixed のときは暗幕（黒 0.25）を足す |

- 見た目（明/暗）でティントを変えるか（Apple は変える）は、**v1 では変えない**（既存拡張と同じ: 白いティント固定、前景色だけ切り替える）。
  デモの Lab で「見た目に応じたティント」を A/B で試せるようにし、良ければ v1.x で材質の既定にする。
- アプリが変えられるのは `tint`・`corner-radius`・`has-shadow`・`material` だけ。光学パラメータはアプリごとではなく全体の設定（`GlassContext`）。

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
├── tests/                     C の単体テスト、ゴールデン画像
└── tools/                     glsl-include.py  glass-harness.c  計測スクリプト
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
meson devenv -C build gjs -m demo/dist/main.js
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
| シェーダ Core | ゴールデン画像（§10.5） | 元の `glass.frag` と 1 LSB 以内 |
| シェーダの構文 | `glslangValidator`（GLES 3.0 / GL 3.3） | エラーなし |
| ぼかし | カーネルの係数を既存拡張の TS 実装と比較 | 係数が一致 |
| Adaptive | `adaptive-vectors.json` を C で再生 | 全ベクタで一致 |
| パラメータ | clamp・既定値・`list_params` | 表（§11.1）と一致 |
| ウィジェット | GTK のテスト（登録と解除、content 側に置いたときのフォールバック、unrealize で GL 資源が解放されること） | 警告・リークなし |
| 性能 | デモの HUD、GPU busy%（既存の計測方法） | §8.8 の予算内 |
| 手動 | チェックリスト: 1x / 1.25x / 2x、ライト/ダーク、ハイコントラスト、Vulkan / GL（`GSK_RENDERER=gl`）、X11、Flatpak | 文字が常に読める・ずれがない |

---

## 17. ロードマップ

規模は S（小）/ M（中）/ L（大）の相対値。**各スパイクの結果は `docs/memo.md` に記録し、この文書に反映する。**

### Phase 0: スパイク

| ID | 内容 | Go の基準 | 規模 |
|---|---|---|---|
| S1 | **Full レンダラの最小試作**（C 1 ファイル）: スクロールする写真の上に 1 枚のパネル。取り込み → 読み出し → GL のぼかし → 元の `glass.frag` を互換プレリュードで動かす → `GdkGLTexture` | (1) 中身とガラスのずれが 0 フレーム、(2) 780M・1x でスクロール中 60fps、(3) 追加の CPU ≤ 3ms/frame、(4) Vulkan と GL の両レンダラで動く、(5) snapshot 中の `render_texture` が安全（または専用レンダラで回避できる） | M |
| S5 | **Core の切り出しとハーネス**（§10） | 元と 1 LSB 以内 | M |
| ~~S4~~ | 窓の同定（Tier 2 用） | ✅ 完了（§2.3） | – |

S2（GSK だけで描く Lite）、S3（拡張機能での blit）、S6（GNOME 51）は、v1 の範囲外になったので行わない。

### Phase 1: 土台（M）

meson の骨組み、`spec/params.json` とヘッダの生成、シェーダ Core、ハーネスとゴールデン画像、CI（GitHub Actions: ビルド・単体テスト・シェーダ検査）。

### Phase 2: ライブラリ本体（L）

`glass_init`、`GlassContext`、`GlassView`、`GlassPanel`、Full レンダラ、フォールバック、Adaptive、CSS、GIR/typelib。

### Phase 3: デモ（M）

Photos / Playground / Lab。

### Phase 4: 仕上げ（M）

性能の調整、アクセシビリティの確認、API ドキュメント（gi-docgen）、Flatpak（デモ）、README のスクリーンショット。

---

## 18. リスク

| リスク | 影響 | 対策 |
|---|---|---|
| snapshot 中の `render_texture` が GTK と衝突する | Full が使えない | 専用レンダラ（§8.2）。S1 で確かめる |
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

---

## 20. 残っている確認事項

| # | 内容 | 決め方 |
|---|---|---|
| C1 | `backdrop-color` の既定値（libadwaita 1.9 の `--window-bg-color` の実際の値） | 実装時に libadwaita の CSS から確認 |
| C2 | 材質 `REGULAR` / `CLEAR` の値（§11.2 は初期案） | デモの Playground・Lab で見て、ユーザーが決める |
| C3 | 見た目に応じたティント（Apple 流）を既定にするか | デモの Lab の A/B で、ユーザーが決める |
| C4 | snapshot 中の `render_texture` の安全性 | S1 |
| C5 | 既存拡張の `prefs.js` の各パラメータの範囲 | 実装時に写す |

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
