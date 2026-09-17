# Panel Graphics API

## English

FlyWithLua-Mac exposes a small explicit Panel Graphics API on macOS when the
running X-Plane provides the complete required SDK surface. The API uses X-Plane panel
coordinates: `x` increases to the right and `y` increases upward. Mesh vertices
and scissors are converted to the SDK's top-left mesh coordinate convention at
the backend boundary; texture UV orientation is unchanged.

`DrawBackend = auto` selects Panel Graphics only when `primitives`, `text`,
`texture`, and `mesh` are all available. `DrawBackend = panel` requests the
same backend but safely falls back to OpenGL when it cannot be used.
`DrawBackend = opengl` keeps the legacy OpenGL path. Existing floating windows
preserve their geometry, visibility, pop-out state, and VR state when the
backend is recreated at a flight-loop boundary.

Use `do_every_panel_draw` for Panel Graphics code:

```lua
do_every_panel_draw("draw_panel()")

function draw_panel()
    if not panel_is_available() then
        return
    end

    panel_set_color(0.1, 0.8, 1.0, 1.0)
    panel_draw_rect(10, 10, 110, 40)
    panel_draw_text(15, 18, "Panel Graphics", "sf_pro_text", 14)
end
```

The supported primitive modes are `points`, `lines`, `line_strip`,
`line_loop`, `polygon`, `triangles`, `triangle_strip`, `triangle_fan`,
`quads`, and `quad_strip`.

`panel_draw_mesh(vertices, indices, texture)` accepts 1-based Lua arrays.
Each vertex is a table containing `x`, `y`, `u`, and `v`, with an optional
packed `color` value. Indices are 1-based and must describe triangles. The
texture must be a `PanelTexture` returned by `panel_load_texture`.
Texture load and destruction must also happen inside a Panel draw callback.

`panel_*` calls do not silently fall back to OpenGL. They return `false` or
`nil` when unavailable or invalid. A misuse from an active script quarantines
that script in memory until the next reload; unrelated scripts continue.

Legacy `gl*` functions remain available. They retain their existing names and
arguments and use the existing OpenGL path, or the compatibility Panel path
for bundled Panel scripts. The bundled-script manifest keeps that adapter
separate from user `do_every_panel_draw` callbacks, which must use `panel_*`.
Do not mix `glBegin/glEnd` with `panel_begin/panel_end`.

`panel_capabilities()` returns `available`, `primitives`, `text`, `texture`,
`mesh`, and `version`. Partial raw capability information is reported, but a
Panel window is created only when the complete set is usable. Panel textures
are owned by the creating script, invalidated during script quarantine or
reload, and must not be used after `panel_destroy_texture`.

ImGui builders are cached after the first successful frame. The builder is
rerun when input, geometry, an owned `EveryFrame`/`Often`/`Sometimes` callback,
or an explicit redraw request changes the UI. Use continuous updates only for
animated windows:

```lua
float_wnd_request_imgui_redraw(my_window)
float_wnd_set_imgui_continuous_update(my_window, true)
```

`float_wnd_set_imgui_continuous_update(my_window, false)` returns the window to
event- and callback-driven rebuilding.

For diagnostics and benchmarking, `float_wnd_get_imgui_stats(my_window)`
returns `builder_runs`, `mesh_rebuilds`, and `cached_draws` counters.

## 日本語

macOS では、実行中の X-Plane が必要な SDK symbol を提供している場合に、
明示的な Panel Graphics API を利用できます。公開座標は X-Plane の panel 座標で、
`x` は右方向、`y` は上方向です。mesh の頂点と scissor は backend 境界で
SDK の左上原点へ変換し、texture の UV 向きは変更しません。

`DrawBackend = auto`（既定値）は `primitives`、`text`、`texture`、`mesh` の
全能力が揃った場合だけ Panel Graphics を選択します。`panel` は Panel を
要求しますが利用不能時は安全に OpenGL へフォールバックし、`opengl` は既存の
OpenGL 経路だけを使用します。backend の再生成は flight loop 境界で行い、
geometry、visibility、pop-out、VR 状態を保持します。

Panel Graphics 用のコードは `do_every_panel_draw` に登録してください。
`panel_*` API は OpenGL に暗黙フォールバックしません。利用できない場合は
`false` または `nil` を返します。不正な使い方をしたスクリプトは、他のスクリプトを
停止せず、次回 reload までメモリ上で隔離されます。
Texture の load / destroy も Panel の描画 callback 内で実行してください。

ImGui のビルダーは初回成功後にメッシュをキャッシュし、入力、サイズ、所有
スクリプトの更新 callback、または明示的な再描画要求があるときだけ再実行します。
アニメーション表示では次の API で連続更新を有効化できます。

```lua
float_wnd_request_imgui_redraw(my_window)
float_wnd_set_imgui_continuous_update(my_window, true)
-- 無効化: float_wnd_set_imgui_continuous_update(my_window, false)
local stats = float_wnd_get_imgui_stats(my_window)
-- stats.builder_runs / stats.mesh_rebuilds / stats.cached_draws
```

既存の `gl*` API は互換性のため維持されます。通常の `do_every_draw` と既存の
OpenGL Floating Window は従来の経路を使用します。同梱スクリプトの manifest による
互換 adapter と、ユーザーの `do_every_panel_draw`（`panel_*` 専用）は分離されます。
`glBegin/glEnd` と
`panel_begin/panel_end` を同じ描画処理で混在させないでください。

## Callback scope compatibility

The default callback scope is isolated per script:

```ini
CallbackScope = isolated
```

Scripts that intentionally depend on the old shared callback-local scope can
opt into the compatibility behavior:

```ini
CallbackScope = legacy
```

`legacy` preserves the old shared callback group behavior and also preserves
its broader error scope. New `do_every_panel_draw` callbacks remain script
owned in both modes.
