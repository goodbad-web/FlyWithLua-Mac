# Panel Graphics API

## English

FlyWithLua-Mac exposes a small explicit Panel Graphics API on macOS when the
running X-Plane provides the required SDK symbols. The API uses X-Plane panel
coordinates: `x` increases to the right and `y` increases upward.

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
for bundled Panel scripts. Do not mix `glBegin/glEnd` with `panel_begin/panel_end`.

`panel_capabilities()` returns `primitives`, `text`, `texture`, `mesh`, and
`version`. Panel textures are invalidated during script reload and must not be
used after `panel_destroy_texture`.

## 日本語

macOS では、実行中の X-Plane が必要な SDK symbol を提供している場合に、
明示的な Panel Graphics API を利用できます。座標は X-Plane の panel 座標で、
`x` は右方向、`y` は上方向です。

Panel Graphics 用のコードは `do_every_panel_draw` に登録してください。
`panel_*` API は OpenGL に暗黙フォールバックしません。利用できない場合は
`false` または `nil` を返します。不正な使い方をしたスクリプトは、他のスクリプトを
停止せず、次回 reload までメモリ上で隔離されます。
Texture の load / destroy も Panel の描画 callback 内で実行してください。

既存の `gl*` API は互換性のため維持されます。通常の `do_every_draw` と既存の
OpenGL Floating Window は従来の経路を使用します。`glBegin/glEnd` と
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
