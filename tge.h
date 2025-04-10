#ifndef __YED_TGE_H__
#define __YED_TGE_H__

#include <yed/plugin.h>

#include <pthread.h>

#define DBG_LOG_ON

#define LOG__XSTR(x) #x
#define LOG_XSTR(x) LOG__XSTR(x)

#define LOG(...)                                                   \
do {                                                               \
    LOG_FN_ENTER();                                                \
    yed_log(__VA_ARGS__);                                          \
    LOG_EXIT();                                                    \
} while (0)

#define ELOG(...)                                                  \
do {                                                               \
    LOG_FN_ENTER();                                                \
    yed_log("[!] " __VA_ARGS__);                                   \
    LOG_EXIT();                                                    \
} while (0)

#ifdef DBG_LOG_ON
#define DBG(...)                                                   \
do {                                                               \
    if (yed_var_is_truthy("tge-log")) {                            \
        LOG_FN_ENTER();                                            \
        yed_log(__FILE__ ":" LOG_XSTR(__LINE__) ": " __VA_ARGS__); \
        LOG_EXIT();                                                \
    }                                                              \
} while (0)
#define EDBG(...)                                                  \
do {                                                               \
    if (yed_var_is_truthy("tge-log")) {                            \
        ELOG(__FILE__ ":" LOG_XSTR(__LINE__) ": " __VA_ARGS__);    \
    }                                                              \
} while (0)
#else
#define DBG(...) ;
#define EDBG(...) ;
#endif


enum {
    TGE_TAKE_KEYS  = 1u << 0u,
    TGE_TAKE_MOUSE = 1u << 1u,
};



typedef struct {
    u32 *pixels;
    u32  height;
    u32  width;
    u32  off_x;
    u32  off_y;
} TGE_Screen;

struct TGE_Game;
struct TGE_Widget;

typedef struct {
    void (*key)  (struct TGE_Game*, struct TGE_Widget*, int);
    void (*paint)(struct TGE_Game*, struct TGE_Widget*);
    void (*focus)(struct TGE_Game*, struct TGE_Widget*);
    void (*free) (struct TGE_Game*, struct TGE_Widget*);
} TGE_Widget_Ops;

typedef struct TGE_Widget {
    void           *data;
    TGE_Widget_Ops  ops;
    int             hitbox_left;
    int             hitbox_top;
    int             hitbox_width;
    int             hitbox_height;
} TGE_Widget;

enum {
    TGE_OBJECT_INT,
    TGE_OBJECT_COLOR,
    TGE_OBJECT_FLOAT,
    TGE_OBJECT_STRUCT,
};

static const char *_tge_type_strs[] = {
    "int",
    "color",
    "float",
    "struct",
};

typedef struct {
    void *addr;
    int   type;
    int   shape;
} TGE_Object;

typedef TGE_Object *TGE_Object_Ptr;
use_tree_c(str_t, TGE_Object_Ptr, strcmp);

typedef struct {
    char *name;
    u32   offset;
    int   type;
    int   shape;
} TGE_Object_Shape_Field;

typedef struct {
    char    *name;
    array_t  fields;
} TGE_Object_Shape;

static inline void _tge_init_screen(TGE_Screen *screen, u32 height, u32 width) {
    u32 n_bytes;

    screen->height = height;
    screen->width  = width;
    screen->off_x  = 0;
    screen->off_y  = 0;

    n_bytes = height * width * sizeof(*screen->pixels);

    screen->pixels = malloc(n_bytes);
    memset(screen->pixels, 0x7f, n_bytes);
}

typedef struct TGE_Game {
    yed_plugin                   *plugin;
    int                           flags;
    yed_event_handler             draw_handler;
    yed_event_handler             pump_handler;
    yed_event_handler             key_handler;
    u32                           fps;
    int                           stop;
    pthread_t                     update_thr;
    u64                           t;
    void                        (*frame_callback)(struct TGE_Game*);
    TGE_Screen                    screen;
    array_t                       keys;
    array_t                       widgets;
    TGE_Widget                   *focused_widget;
    bucket_array_t                shapes;
    bucket_array_t                objects;
    tree(str_t, TGE_Object_Ptr)   named_objects;
} TGE_Game;


static inline const char *tge_object_type_string(TGE_Game *game, TGE_Object *object) {
    switch (object->type) {
        case TGE_OBJECT_STRUCT:
            return ((TGE_Object_Shape*)bucket_array_item(game->shapes, object->shape))->name;
        default:
            return _tge_type_strs[object->type];
    }

    return NULL;
}

static inline void _tge_paint_screen(TGE_Screen *screen) {
    yed_glyph    half_block;
    u32          r;
    u32          c;
    yed_attrs    attrs;
    int          y;
    int          x;

    memset(half_block.bytes, 0, sizeof(half_block.bytes));
    memcpy(half_block.bytes, "▀", 3);

    for (r = 0; r < screen->height; r += 2) {
        for (c = 0; c < screen->width; c += 1) {
            attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
            attrs.fg    = screen->pixels[r       * screen->width + c];
            attrs.bg    = screen->pixels[(r + 1) * screen->width + c];

            y = screen->off_y + 1 + (r >> 1);
            x = screen->off_x + 1 + c;

            if (y <= ys->term_rows && x <= ys->term_cols) {
                yed_set_cursor(y, x);
                yed_set_attr(attrs);
                yed_screen_print_single_cell_glyph(half_block);
            }
        }
    }
}

static inline void _tge_paint(TGE_Game *game) {
    char         buff[128];
    TGE_Screen  *screen;
    yed_attrs    attrs;
    TGE_Widget **wit;
    TGE_Widget  *widget;

    screen = &game->screen;

    if (ys->term_cols     < (int)screen->width
    ||  ys->term_rows * 2 < (int)screen->height) {

        attrs = yed_active_style_get_active();
        attrs.flags ^= ATTR_INVERSE;
        yed_set_attr(attrs);
        yed_set_cursor(1, 1);
        snprintf(buff, sizeof(buff), "TERMINAL TOO SMALL (%3d x %3d)", ys->term_cols, ys->term_rows);
        yed_screen_print_n_over(buff, strlen(buff));
        yed_set_cursor(2, 1);
        snprintf(buff, sizeof(buff), "REQUIRED SIZE:      %3d x %3d ", screen->width, (screen->height / 2) + (screen->height & 1));
        yed_screen_print_n_over(buff, strlen(buff));
        return;
    }

    screen->off_x = (ys->term_cols - screen->width) / 2;
    screen->off_y = (ys->term_rows - (screen->height / 2)) / 2;

    _tge_paint_screen(screen);

    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
    attrs.fg    = 0;
    attrs.bg    = 0x7f0000;

    array_traverse(game->widgets, wit) {
        widget = *wit;
        if (widget == game->focused_widget) { continue; }
        widget->ops.paint(game, widget);

        yed_set_attr(attrs);
        yed_set_cursor(widget->hitbox_top, widget->hitbox_left + widget->hitbox_width - 1);
        yed_screen_print_n("✗", 3);
    }

    if (game->focused_widget != NULL) {
        game->focused_widget->ops.paint(game, game->focused_widget);

        attrs.bg    = 0xff0000;
        yed_set_attr(attrs);
        yed_set_cursor(game->focused_widget->hitbox_top, game->focused_widget->hitbox_left + game->focused_widget->hitbox_width - 1);
        yed_screen_print_n("✗", 3);
    }
}

static inline void _tge_edraw(yed_event *event) {
    TGE_Game *game;

    game = event->aux_data;

    _tge_paint(game);
}

static inline void _tge_frame(TGE_Game *game) {
    if (game->frame_callback != NULL) {
        game->frame_callback(game);
    }
}

static inline void _tge_epump(yed_event *event) {
    TGE_Game *game;
    u64       now;

    game = event->aux_data;

    now = measure_time_now_us();

    if (now - game->t >= 1000000 / game->fps) {
        _tge_frame(game);
        game->t = now;
        array_clear(game->keys);
    }
}

static inline void _tge_close_widget(TGE_Game *game, TGE_Widget *widget) {
    TGE_Widget **wit;
    int          idx;

    widget->ops.free(game, widget);

    if (game->focused_widget == widget) {
        game->focused_widget = NULL;
    }

    idx = 0;
    array_traverse(game->widgets, wit) {
        if (*wit == widget) {
            array_delete(game->widgets, idx);
            break;
        }
        idx += 1;
    }
}

static inline int _tge_click_in_widget_hitbox(int key, TGE_Widget *widget) {
    return    MOUSE_COL(key) >= widget->hitbox_left
           && MOUSE_COL(key) <  widget->hitbox_left + widget->hitbox_width
           && MOUSE_ROW(key) >= widget->hitbox_top
           && MOUSE_ROW(key) <  widget->hitbox_top + widget->hitbox_height;
}

static inline void _tge_ekey(yed_event *event) {
    TGE_Game    *game;
    int          key;
    TGE_Widget **wit;
    TGE_Widget  *widget;

    game = event->aux_data;
    key  = event->key;

    if (IS_MOUSE(key)) {
        if (!(game->flags & TGE_TAKE_MOUSE)) { return; }
    } else {
        if (!(game->flags & TGE_TAKE_KEYS)) { return; }
    }

    event->cancel = 1;

    if (IS_MOUSE(key)) {
        /* Don't worry about the hitbox if we're dragging around the focused widget. */
        if (game->focused_widget != NULL
        &&  MOUSE_BUTTON(key)    == MOUSE_BUTTON_MIDDLE
        &&  MOUSE_KIND(key)      == MOUSE_DRAG) {

            game->focused_widget->hitbox_left = MOUSE_COL(key);
            game->focused_widget->hitbox_top  = MOUSE_ROW(key);
            return;
        }

        array_rtraverse(game->widgets, wit) {
            widget = *wit;

            if (_tge_click_in_widget_hitbox(key, widget)) {
                if (MOUSE_BUTTON(key) == MOUSE_BUTTON_LEFT
                &&  MOUSE_KIND(key)   == MOUSE_PRESS
                &&  MOUSE_ROW(key)    == widget->hitbox_top
                &&  MOUSE_COL(key)    == widget->hitbox_left + widget->hitbox_width - 1) {

                    _tge_close_widget(game, widget);
                    return;
                }

                if (game->focused_widget == NULL
                ||  !_tge_click_in_widget_hitbox(key, game->focused_widget)) {

                    if (MOUSE_KIND(key) == MOUSE_PRESS) {
                        game->focused_widget = widget;
                        game->focused_widget->ops.focus(game, game->focused_widget);
                        return;
                    }
                }

                if (MOUSE_BUTTON(key) != MOUSE_BUTTON_MIDDLE) {
                    key = MK_MOUSE(MOUSE_KIND(key),
                                   MOUSE_BUTTON(key),
                                   1 + MOUSE_ROW(key) - widget->hitbox_top,
                                   1 + MOUSE_COL(key) - widget->hitbox_left);

                    widget->ops.key(game, widget, key);
                }
                return;
            }
        }

        game->focused_widget = NULL;

        if (MOUSE_ROW(key) - 1 <  (int)game->screen.off_y
        ||  MOUSE_ROW(key) - 1 > (int)(game->screen.off_y + (game->screen.height / 2) - 1)
        ||  MOUSE_COL(key) - 1 <  (int)game->screen.off_x
        ||  MOUSE_COL(key) - 1 > (int)(game->screen.off_x + game->screen.width - 1)) {

            event->cancel = 0;
            return;
        }

        key = MK_MOUSE(MOUSE_KIND(key),
                       MOUSE_BUTTON(key),
                       (MOUSE_ROW(key) - 1 - game->screen.off_y) * 2,
                       MOUSE_COL(key) - 1 - game->screen.off_x);
    } else {
        if (game->focused_widget != NULL) {
            game->focused_widget->ops.key(game, game->focused_widget, key);
            return;
        }
    }

    array_push(game->keys, key);
}

static inline void *_tge_update_thr(void *arg) {
    TGE_Game *game;

    game = arg;

    while (!game->stop) {
        if (game->fps > 0) {
            yed_force_update();
            usleep(1000000 / game->fps);
        } else {
            usleep(1000000);
        }
    }

    return NULL;
}

static inline TGE_Game * tge_new_game(yed_plugin *plugin, u32 height, u32 width, u32 fps, int flags) {
    TGE_Game *game;

    game = malloc(sizeof(*game));
    memset(game, 0, sizeof(*game));

    game->plugin = plugin;
    game->flags  = flags;
    game->fps    = fps > 0 ? fps : 1;
    game->t      = measure_time_now_ms();

    pthread_create(&game->update_thr, NULL, _tge_update_thr, game);

    game->draw_handler.kind     = EVENT_PRE_DIRECT_DRAWS;
    game->draw_handler.fn       = _tge_edraw;
    game->draw_handler.aux_data = game;
    yed_plugin_add_event_handler(plugin, game->draw_handler);

    game->pump_handler.kind     = EVENT_PRE_PUMP;
    game->pump_handler.fn       = _tge_epump;
    game->pump_handler.aux_data = game;
    yed_plugin_add_event_handler(plugin, game->pump_handler);

    game->key_handler.kind     = EVENT_KEY_PRESSED;
    game->key_handler.fn       = _tge_ekey;
    game->key_handler.aux_data = game;
    yed_plugin_add_event_handler(plugin, game->key_handler);

    _tge_init_screen(&game->screen, height, width);

    game->keys           = array_make(int);
    game->widgets        = array_make(TGE_Widget*);
    game->focused_widget = NULL;
    game->shapes         = bucket_array_make(16, TGE_Object_Shape);
    game->objects        = bucket_array_make(256, TGE_Object);
    game->named_objects  = tree_make(str_t, TGE_Object_Ptr);

    return game;
}

static inline void tge_finish_game(TGE_Game *game) {
    if (game->stop) { return; }

    game->stop = 1;

    pthread_join(game->update_thr, NULL);


    yed_delete_event_handler(game->pump_handler);
    yed_delete_event_handler(game->draw_handler);
    yed_delete_event_handler(game->key_handler);

    free(game->screen.pixels);
    free(game);
}

static inline void tge_screen_set_pixel(TGE_Screen *screen, u32 x, u32 y, u32 rgb) {
    if (x >= screen->width)  { return; }
    if (y >= screen->height) { return; }

    screen->pixels[y * screen->width + x] = rgb;
}


static inline void tge_set_pixel(TGE_Game *game, u32 x, u32 y, u32 rgb) {
    tge_screen_set_pixel(&game->screen, x, y, rgb);
}

static inline void tge_clear(TGE_Game *game, u32 color) {
    u32 x;
    u32 y;

    for (x = 0; x < game->screen.width; x += 1) {
        for (y = 0; y < game->screen.height; y += 1) {
            tge_set_pixel(game, x, y, color);
        }
    }
}

static inline int tge_key_pressed(TGE_Game *game, int key) {
    int *k;

    array_traverse(game->keys, k) {
        if (*k == key) { return 1; }
    }

    return 0;
}

typedef struct {
    u32 *pixels;
    u8  *mask;
} TGE_Sprite_Animation_Step;

typedef struct {
    u32     height;
    u32     width;
    array_t anim_steps;
} TGE_Sprite;

typedef struct {
    TGE_Sprite *sprite;
    u32         which_step;
} TGE_Drawable;


TGE_Sprite_Animation_Step tge_new_sprite_animation_step(u32 height, u32 width, u32 *pixels, u8 *mask) {
    u32                       n;
    TGE_Sprite_Animation_Step step;

    n = height * width;

    step.pixels = malloc(sizeof(*step.pixels) * n);
    step.mask   = malloc(sizeof(*step.mask) * n);

    memcpy(step.pixels, pixels, sizeof(*step.pixels) * n);
    memcpy(step.mask, mask, sizeof(*step.mask) * n);

    return step;
}

TGE_Sprite * tge_new_sprite(TGE_Game *game, u32 height, u32 width, u32 *pixels, u8 *mask) {
    TGE_Sprite                *sprite;
    TGE_Sprite_Animation_Step  initial_step;

    if (height > game->screen.height) { return NULL; }
    if (width  > game->screen.width)  { return NULL; }

    sprite             = malloc(sizeof(*sprite));
    sprite->height     = height;
    sprite->width      = width;
    sprite->anim_steps = array_make(TGE_Sprite_Animation_Step);
    initial_step       = tge_new_sprite_animation_step(height, width, pixels, mask);
    array_push(sprite->anim_steps, initial_step);

    return sprite;
}

static inline void tge_draw(TGE_Game *game, TGE_Drawable *drawable, u32 x, u32 y) {
    TGE_Sprite                *sprite;
    TGE_Sprite_Animation_Step *step;
    u32                        i;
    u32                        j;
    u32                        pixel_idx;

    sprite = drawable->sprite;
    step   = array_item(sprite->anim_steps, drawable->which_step);

    for (i = 0; i < sprite->height; i += 1) {
        for (j = 0; j < sprite->width; j += 1) {
            pixel_idx = i * sprite->width + j;
            if (step->mask[pixel_idx]) {
                tge_set_pixel(game, x + j, y + i, step->pixels[pixel_idx]);
            }
        }
    }
}

static inline void _tge_place_new_widget(TGE_Game *game, TGE_Widget *widget) {
    TGE_Widget **it;

    if (game->focused_widget != NULL) {
        if (game->focused_widget->hitbox_left + game->focused_widget->hitbox_width + widget->hitbox_width > ys->term_cols) {
            widget->hitbox_left = game->focused_widget->hitbox_top + 1;
            widget->hitbox_top  = game->focused_widget->hitbox_top + 1;
        } else {
            widget->hitbox_left = game->focused_widget->hitbox_left + game->focused_widget->hitbox_width;
            widget->hitbox_top  = game->focused_widget->hitbox_top;
        }
    } else {
        widget->hitbox_left = 1;
        widget->hitbox_top  = 1;
    }

again:;
    array_traverse(game->widgets, it) {
        if (*it == widget) { continue; }

        if ((*it)->hitbox_left == widget->hitbox_left
        &&  (*it)->hitbox_top  == widget->hitbox_top) {

            widget->hitbox_left += 4;
            widget->hitbox_top  += 4;
            goto again;
        }
    }
}


#define TGE_OBJECT_EDITOR_WIDTH  (30)
#define TGE_OBJECT_EDITOR_HEIGHT (4)

enum {
    TGE_OBJECT_EDITOR_COLOR_MODE_RGB,
    TGE_OBJECT_EDITOR_COLOR_MODE_HSV,
    TGE_OBJECT_EDITOR_COLOR_MODE_HEX,
};

enum {
    TGE_OBJECT_EDITOR_COLOR_FIELD_RH,
    TGE_OBJECT_EDITOR_COLOR_FIELD_GS,
    TGE_OBJECT_EDITOR_COLOR_FIELD_BV,
};

typedef struct {
    TGE_Object object;
    array_t    text;
    int        cursor_pos;
    int        color_mode;
    int        color_field;
} TGE_Object_Editor;

static inline void _tge_object_editor_paint_int(TGE_Game *game, TGE_Widget *widget, TGE_Object_Editor *editor) {
    yed_attrs attrs;
    int       x;
    char      buff[256];

    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
    attrs.fg    = 0;
    attrs.bg    = game->focused_widget == widget ? 0xcccccc : 0x555555;

    yed_set_attr(attrs);
    for (x = 1; x < widget->hitbox_width - 1; x += 1) {
        yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + 1);
    if (game->focused_widget == widget) {
        array_zero_term(editor->text);
        snprintf(buff, sizeof(buff), "value: %s", (char*)array_data(editor->text));
    } else {
        snprintf(buff, sizeof(buff), "value: %d", *(int*)editor->object.addr);
    }
    yed_screen_print(buff);

    if (game->focused_widget == widget) {
        yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + 1 + strlen("value: ") + editor->cursor_pos);
        attrs.bg = 0xaf0000;
        yed_set_attr(attrs);
        if (editor->cursor_pos >= array_len(editor->text)) {
            yed_screen_print_n(" ", 1);
        } else {
            yed_screen_print_n((char*)array_item(editor->text, editor->cursor_pos), 1);
        }
    }
}

static inline void _tge_object_editor_paint_float(TGE_Game *game, TGE_Widget *widget, TGE_Object_Editor *editor) {
    yed_attrs attrs;
    int       x;
    char      buff[256];

    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
    attrs.fg    = 0;
    attrs.bg    = game->focused_widget == widget ? 0xcccccc : 0x555555;

    yed_set_attr(attrs);
    for (x = 1; x < widget->hitbox_width - 1; x += 1) {
        yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + 1);
    if (game->focused_widget == widget) {
        array_zero_term(editor->text);
        snprintf(buff, sizeof(buff), "value: %s", (char*)array_data(editor->text));
    } else {
        snprintf(buff, sizeof(buff), "value: %.2f", *(float*)editor->object.addr);
    }
    yed_screen_print(buff);

    if (game->focused_widget == widget) {
        yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + 1 + strlen("value: ") + editor->cursor_pos);
        attrs.bg = 0xaf0000;
        yed_set_attr(attrs);
        if (editor->cursor_pos >= array_len(editor->text)) {
            yed_screen_print_n(" ", 1);
        } else {
            yed_screen_print_n((char*)array_item(editor->text, editor->cursor_pos), 1);
        }
    }
}

static inline void _tge_object_editor_paint_color(TGE_Game *game, TGE_Widget *widget, TGE_Object_Editor *editor) {
    int       color;
    int       r;
    int       g;
    int       b;
    yed_attrs attrs;
    int       x;
    char      buff[256];

    if (game->focused_widget == widget && editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_HEX) {
        array_zero_term(editor->text);
        attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
        attrs.fg    = 0;
        attrs.bg    = game->focused_widget == widget ? 0xbbbbbb : 0x555555;
        yed_set_attr(attrs);
        yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + 1);
        yed_screen_print("hex: ");
        yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + 6);
        snprintf(buff, sizeof(buff), "%s", (char*)array_data(editor->text));
        yed_screen_print(buff);
    } else {
        attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
        attrs.fg    = 0;
        attrs.bg    = game->focused_widget == widget ? 0xbbbbbb : 0x555555;
        yed_set_attr(attrs);
        yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + 1);
        yed_screen_print("hex: ");
        yed_set_cursor(widget->hitbox_top + 2, widget->hitbox_left + 6);
        snprintf(buff, sizeof(buff), "%06x", *(int*)editor->object.addr);
        yed_screen_print(buff);
    }

    color = *(int*)editor->object.addr;
    if (game->focused_widget != widget) {
        r = (color & 0xff0000) >> 16; r = r < 0x66 ? 0 : r - 0x66;
        g = (color & 0x00ff00) >> 8;  g = g < 0x66 ? 0 : g - 0x66;
        b = (color & 0x0000ff);       b = b < 0x66 ? 0 : b - 0x66;

        color = (r << 16) | (g << 8) | b;
    }

    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB) | ATTR_UNDERLINE;
    attrs.fg    = 0;
    attrs.bg    = color;
    yed_set_attr(attrs);
    for (x = 1; x < widget->hitbox_width - 1; x += 1) {
        yed_set_cursor(widget->hitbox_top + 3, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }


    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB)
                  | (editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB ? ATTR_UNDERLINE : 0);
    attrs.fg    = editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB ? (game->focused_widget == widget ? 0xffffff : 0x666666) : 0;
    attrs.bg    = editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB ? 0        : 0x666666;
    yed_set_attr(attrs);
    for (x = 2; x < (widget->hitbox_width / 2) - 1; x += 1) {
        yed_set_cursor(widget->hitbox_top + 5, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top + 5, widget->hitbox_left + (((widget->hitbox_width - 2) / 4) - 1));
    yed_screen_print("RGB");

    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB)
                  | (editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_HSV ? ATTR_UNDERLINE : 0);
    attrs.fg    = editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_HSV ? (game->focused_widget == widget ? 0xffffff : 0x666666) : 0;
    attrs.bg    = editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_HSV ? 0        : 0x666666;
    yed_set_attr(attrs);
    for (x = widget->hitbox_width / 2; x < widget->hitbox_width - 2; x += 1) {
        yed_set_cursor(widget->hitbox_top + 5, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top + 5, widget->hitbox_left + ((widget->hitbox_width - 2) / 2) + (((widget->hitbox_width - 2) / 4) - 1));
    yed_screen_print("HSV");

    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
    attrs.fg    = 0;
    attrs.bg    = game->focused_widget == widget ? 0xcccccc : 0x555555;
    yed_set_attr(attrs);

    for (x = 1; x < widget->hitbox_width - 1; x += 1) {
        yed_set_cursor(widget->hitbox_top + 7, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top + 7, widget->hitbox_left + 1);
    if (game->focused_widget == widget && editor->color_field == TGE_OBJECT_EDITOR_COLOR_FIELD_RH) {
        array_zero_term(editor->text);
        snprintf(buff, sizeof(buff), "%c: %s", editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB ? 'R' : 'H', (char*)array_data(editor->text));
    } else {
        snprintf(buff, sizeof(buff), "%c: %d", editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB ? 'R' : 'H',  ((*(int*)editor->object.addr) & 0xff0000) >> 16);
    }
    yed_screen_print(buff);

    for (x = 1; x < widget->hitbox_width - 1; x += 1) {
        yed_set_cursor(widget->hitbox_top + 9, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top + 9, widget->hitbox_left + 1);
    if (game->focused_widget == widget && editor->color_field == TGE_OBJECT_EDITOR_COLOR_FIELD_GS) {
        array_zero_term(editor->text);
        snprintf(buff, sizeof(buff), "%c: %s", editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB ? 'G' : 'S',  (char*)array_data(editor->text));
    } else {
        snprintf(buff, sizeof(buff), "%c: %d", editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB ? 'G' : 'S',  ((*(int*)editor->object.addr) & 0x00ff00) >> 8);
    }
    yed_screen_print(buff);

    for (x = 1; x < widget->hitbox_width - 1; x += 1) {
        yed_set_cursor(widget->hitbox_top + 11, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top + 11, widget->hitbox_left + 1);
    if (game->focused_widget == widget && editor->color_field == TGE_OBJECT_EDITOR_COLOR_FIELD_BV) {
        array_zero_term(editor->text);
        snprintf(buff, sizeof(buff), "%c: %s", editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB ? 'B' : 'V',  (char*)array_data(editor->text));
    } else {
        snprintf(buff, sizeof(buff), "%c: %d", editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB ? 'B' : 'V',  (*(int*)editor->object.addr) & 0x0000ff);
    }
    yed_screen_print(buff);

    if (game->focused_widget == widget) {
        yed_set_cursor(widget->hitbox_top + 7 + (2 * editor->color_field), widget->hitbox_left + 1 + 3 + editor->cursor_pos);
        attrs.bg = 0xaf0000;
        yed_set_attr(attrs);
        if (editor->cursor_pos >= array_len(editor->text)) {
            yed_screen_print_n(" ", 1);
        } else {
            yed_screen_print_n((char*)array_item(editor->text, editor->cursor_pos), 1);
        }
    }
}

static inline void _tge_object_editor_paint_struct(TGE_Game *game, TGE_Widget *widget, TGE_Object_Editor *editor) {
    TGE_Object_Shape       *shape;
    yed_attrs               attrs;
    int                     i;
    TGE_Object_Shape_Field *field;
    TGE_Object              object;
    char                    buff[256];
    int                     color;
    int                     r;
    int                     g;
    int                     b;

    shape = bucket_array_item(game->shapes, editor->object.shape);

    widget->hitbox_height = 3 + array_len(shape->fields);

    i = 0;
    array_traverse(shape->fields, field) {
        attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
        attrs.fg    = 0;
        attrs.bg    = game->focused_widget == widget ? 0xbbbbbb : 0x555555;
        yed_set_attr(attrs);
        yed_set_cursor(widget->hitbox_top + 2 + i, widget->hitbox_left + 1);

        object.addr  = editor->object.addr + field->offset;
        object.type  = field->type;
        object.shape = field->shape;

        switch (object.type) {
            case TGE_OBJECT_INT:
                snprintf(buff, sizeof(buff),
                         "%s (%s) @ %p: %d", field->name, tge_object_type_string(game, &object), object.addr, *(int*)object.addr);
                break;
            case TGE_OBJECT_FLOAT:
                snprintf(buff, sizeof(buff),
                         "%s (%s) @ %p: %f", field->name, tge_object_type_string(game, &object), object.addr, *(float*)object.addr);
                break;
            default:
                snprintf(buff, sizeof(buff),
                         "%s (%s) @ %p", field->name, tge_object_type_string(game, &object), object.addr);
                break;
        }

        yed_screen_print(buff);

        if (object.type == TGE_OBJECT_COLOR) {
            color = *(int*)object.addr;
            if (game->focused_widget != widget) {
                r = (color & 0xff0000) >> 16; r = r < 0x66 ? 0 : r - 0x66;
                g = (color & 0x00ff00) >> 8;  g = g < 0x66 ? 0 : g - 0x66;
                b = (color & 0x0000ff);       b = b < 0x66 ? 0 : b - 0x66;

                color = (r << 16) | (g << 8) | b;
            }

            attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
            attrs.fg    = 0;
            attrs.bg    = color;
            yed_set_attr(attrs);
            yed_set_cursor(widget->hitbox_top + 2 + i, widget->hitbox_left + 1 + strlen(buff) + 1);
            yed_screen_print("  ");
            attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
            attrs.fg    = 0;
            attrs.bg    = game->focused_widget == widget ? 0xbbbbbb : 0x555555;
            yed_set_attr(attrs);
            yed_set_cursor(widget->hitbox_top + 2 + i, widget->hitbox_left + 1 + strlen(buff) + 4);
            snprintf(buff, sizeof(buff), "#%06x", *(int*)object.addr);
            yed_screen_print(buff);
        }

        i += 1;
    }
}

static inline void _tge_object_editor_paint(TGE_Game *game, TGE_Widget *widget) {
    TGE_Object_Editor      *editor;
    TGE_Object_Shape       *shape;
    TGE_Object_Shape_Field *field;
    int                     max_name_width;
    int                     len;
    yed_attrs               attrs;
    int                     x;
    int                     y;
    char                    buff[256];

    editor = widget->data;

    widget->hitbox_width = TGE_OBJECT_EDITOR_WIDTH;

    if (editor->object.type == TGE_OBJECT_STRUCT) {
        shape          = bucket_array_item(game->shapes, editor->object.shape);
        max_name_width = 0;
        array_traverse(shape->fields, field) {
            len = strlen(field->name);
            if (len > max_name_width) {
                max_name_width = len;
            }
        }

        widget->hitbox_width = MAX(TGE_OBJECT_EDITOR_WIDTH,
                                   2 /* padding */
                                   + 2 /* parens */
                                   + max_name_width
                                   + 3 /* @ and surrounding spaces */
                                   + 18 /* hex address */
                                   + 2 /* colon space */
                                   + 12 /* int width */);
    } else if (editor->object.type == TGE_OBJECT_COLOR) {
        widget->hitbox_height = 13;
    }

    snprintf(buff, sizeof(buff), "%p (%s)", editor->object.addr, tge_object_type_string(game, &editor->object));
    widget->hitbox_width = MAX(widget->hitbox_width, (int)strlen(buff));

    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
    attrs.fg    = 0;
    attrs.bg    = game->focused_widget == widget ? 0xbbbbbb : 0x555555;
    yed_set_attr(attrs);

    for (x = 0; x < widget->hitbox_width; x += 1) {
        for (y = 0; y < widget->hitbox_height; y += 1) {
            yed_set_cursor(widget->hitbox_top + y, widget->hitbox_left + x);
            yed_screen_print_n(" ", 1);
        }
    }

    attrs.bg = game->focused_widget == widget ? 0x00bbbb : 0x005555;
    yed_set_attr(attrs);

    for (x = 0; x < widget->hitbox_width; x += 1) {
        yed_set_cursor(widget->hitbox_top, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top, widget->hitbox_left);
    yed_screen_print(buff);


    switch (editor->object.type) {
        case TGE_OBJECT_INT:
            _tge_object_editor_paint_int(game, widget, editor);
            break;
        case TGE_OBJECT_FLOAT:
            _tge_object_editor_paint_float(game, widget, editor);
            break;
        case TGE_OBJECT_COLOR:
            _tge_object_editor_paint_color(game, widget, editor);
            break;
        case TGE_OBJECT_STRUCT:
            _tge_object_editor_paint_struct(game, widget, editor);
            break;

    }
}

static inline TGE_Widget *tge_new_object_editor(TGE_Game *game, TGE_Object *object);

static inline void _tge_object_editor_key_struct(TGE_Game *game, TGE_Widget *widget, TGE_Object_Editor *editor, int key) {
    TGE_Object_Shape       *shape;
    int                     base;
    int                     end;
    TGE_Object_Shape_Field *field;
    TGE_Object              object;

    if (!IS_MOUSE(key)
    ||  MOUSE_BUTTON(key) != MOUSE_BUTTON_LEFT
    ||  MOUSE_KIND(key) != MOUSE_PRESS) {

        return;
    }

    editor = widget->data;
    shape  = bucket_array_item(game->shapes, editor->object.shape);

    base = 3;
    end  = base + array_len(shape->fields) - 1;

    if (MOUSE_ROW(key) >= base && MOUSE_ROW(key) <= end) {
        field = array_item(shape->fields, MOUSE_ROW(key) - base);

        object.addr  = editor->object.addr + field->offset;
        object.type  = field->type;
        object.shape = field->shape;

        tge_new_object_editor(game, &object);
    }
}

static inline void _tge_object_editor_key_int(TGE_Game *game, TGE_Widget *widget, TGE_Object_Editor *editor, int key) {
    (void)game;
    (void)widget;

    switch (key) {
        case BACKSPACE:
            if (editor->cursor_pos > 0) {
                editor->cursor_pos -= 1;
                array_delete(editor->text, editor->cursor_pos);
            }
            break;
        case ARROW_LEFT:
            if (editor->cursor_pos > 0) {
                editor->cursor_pos -= 1;
            }
            break;
        case ARROW_RIGHT:
            if (editor->cursor_pos < array_len(editor->text)) {
                editor->cursor_pos += 1;
            }
            break;
        default:
            if (isprint(key)) {
                array_insert(editor->text, editor->cursor_pos, key);
                editor->cursor_pos += 1;
            }
            break;
    }

    array_zero_term(editor->text);
    sscanf((char*)array_data(editor->text), "%d", (int*)editor->object.addr);
}

static inline void _tge_object_editor_key_float(TGE_Game *game, TGE_Widget *widget, TGE_Object_Editor *editor, int key) {
    (void)game;
    (void)widget;

    switch (key) {
        case BACKSPACE:
            if (editor->cursor_pos > 0) {
                editor->cursor_pos -= 1;
                array_delete(editor->text, editor->cursor_pos);
            }
            break;
        case ARROW_LEFT:
            if (editor->cursor_pos > 0) {
                editor->cursor_pos -= 1;
            }
            break;
        case ARROW_RIGHT:
            if (editor->cursor_pos < array_len(editor->text)) {
                editor->cursor_pos += 1;
            }
            break;
        default:
            if (isprint(key)) {
                array_insert(editor->text, editor->cursor_pos, key);
                editor->cursor_pos += 1;
            }
            break;
    }

    array_zero_term(editor->text);
    sscanf((char*)array_data(editor->text), "%f", (float*)editor->object.addr);
}

static inline void _tge_object_editor_key_color(TGE_Game *game, TGE_Widget *widget, TGE_Object_Editor *editor, int key) {
    int  rgb_component;
    char buff[256];
    int  i;

    (void)game;

    switch (key) {
        case BACKSPACE:
            if (editor->cursor_pos > 0) {
                editor->cursor_pos -= 1;
                array_delete(editor->text, editor->cursor_pos);
            }
            break;
        case ARROW_LEFT:
            if (editor->cursor_pos > 0) {
                editor->cursor_pos -= 1;
            }
            break;
        case ARROW_RIGHT:
            if (editor->cursor_pos < array_len(editor->text)) {
                editor->cursor_pos += 1;
            }
            break;
        default:
            if (IS_MOUSE(key)
            &&  MOUSE_BUTTON(key) == MOUSE_BUTTON_LEFT
            &&  MOUSE_KIND(key)   == MOUSE_PRESS) {

                if (MOUSE_ROW(key) == 6) {
                    if (MOUSE_COL(key) <= widget->hitbox_width / 2) {
                        editor->color_mode = TGE_OBJECT_EDITOR_COLOR_MODE_RGB;
                    } else {
                        editor->color_mode = TGE_OBJECT_EDITOR_COLOR_MODE_HSV;
                    }
                } else if (MOUSE_ROW(key) == 8  && editor->color_field != TGE_OBJECT_EDITOR_COLOR_FIELD_RH) {
                    editor->color_field = TGE_OBJECT_EDITOR_COLOR_FIELD_RH;
                    rgb_component       = (*(int*)editor->object.addr & 0xff0000) >> 16;
                    snprintf(buff, sizeof(buff), "%d", rgb_component);
                    array_clear(editor->text);
                    for (i = 0; i < (int)strlen(buff); i += 1) {
                        array_push(editor->text, buff[i]);
                    }
                    editor->cursor_pos = array_len(editor->text);
                } else if (MOUSE_ROW(key) == 10 && editor->color_field != TGE_OBJECT_EDITOR_COLOR_FIELD_GS) {
                    editor->color_field = TGE_OBJECT_EDITOR_COLOR_FIELD_GS;
                    rgb_component       = (*(int*)editor->object.addr & 0x00ff00) >> 8;
                    snprintf(buff, sizeof(buff), "%d", rgb_component);
                    array_clear(editor->text);
                    for (i = 0; i < (int)strlen(buff); i += 1) {
                        array_push(editor->text, buff[i]);
                    }
                    editor->cursor_pos = array_len(editor->text);
                } else if (MOUSE_ROW(key) == 12 && editor->color_field != TGE_OBJECT_EDITOR_COLOR_FIELD_BV) {
                    editor->color_field = TGE_OBJECT_EDITOR_COLOR_FIELD_BV;
                    rgb_component       = *(int*)editor->object.addr & 0x0000ff;
                    snprintf(buff, sizeof(buff), "%d", rgb_component);
                    array_clear(editor->text);
                    for (i = 0; i < (int)strlen(buff); i += 1) {
                        array_push(editor->text, buff[i]);
                    }
                    editor->cursor_pos = array_len(editor->text);
                }
            } else if (isprint(key)) {
                array_insert(editor->text, editor->cursor_pos, key);
                editor->cursor_pos += 1;
            }
            break;
    }

    array_zero_term(editor->text);

    if (editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB) {
        sscanf((char*)array_data(editor->text), "%d", &rgb_component);
        LIMIT(rgb_component, 0, 255);

        switch (editor->color_field) {
            case TGE_OBJECT_EDITOR_COLOR_FIELD_RH:
                *(int*)editor->object.addr &= 0x00ffff;
                *(int*)editor->object.addr |= rgb_component << 16;
                break;
            case TGE_OBJECT_EDITOR_COLOR_FIELD_GS:
                *(int*)editor->object.addr &= 0xff00ff;
                *(int*)editor->object.addr ^= rgb_component << 8;
                break;
            case TGE_OBJECT_EDITOR_COLOR_FIELD_BV:
                *(int*)editor->object.addr &= 0xffff00;
                *(int*)editor->object.addr ^= rgb_component;
                break;
        }
    } else if (editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_HSV) {
        switch (editor->color_field) {
            case TGE_OBJECT_EDITOR_COLOR_FIELD_RH:
                break;
            case TGE_OBJECT_EDITOR_COLOR_FIELD_GS:
                break;
            case TGE_OBJECT_EDITOR_COLOR_FIELD_BV:
                break;
        }
    }

}

static inline void _tge_object_editor_key(TGE_Game *game, TGE_Widget *widget, int key) {
    TGE_Object_Editor *editor;

    (void)game;

    editor = widget->data;


    switch (editor->object.type) {
        case TGE_OBJECT_INT:
            _tge_object_editor_key_int(game, widget, editor, key);
            break;
        case TGE_OBJECT_FLOAT:
            _tge_object_editor_key_float(game, widget, editor, key);
            break;
        case TGE_OBJECT_COLOR:
            _tge_object_editor_key_color(game, widget, editor, key);
            break;
        case TGE_OBJECT_STRUCT:
            _tge_object_editor_key_struct(game, widget, editor, key);
            break;
    }
}

static inline void _tge_object_editor_focus(TGE_Game *game, TGE_Widget *widget) {
    TGE_Object_Editor *editor;
    char               buff[256];
    int                i;

    (void)game;

    editor = widget->data;

    switch (editor->object.type) {
        case TGE_OBJECT_INT:
            array_clear(editor->text);
            snprintf(buff, sizeof(buff), "%d", *(int*)editor->object.addr);
            for (i = 0; i < (int)strlen(buff); i += 1) {
                array_push(editor->text, buff[i]);
            }
            editor->cursor_pos = array_len(editor->text);
            break;
        case TGE_OBJECT_FLOAT:
            array_clear(editor->text);
            snprintf(buff, sizeof(buff), "%.2f", *(float*)editor->object.addr);
            for (i = 0; i < (int)strlen(buff); i += 1) {
                array_push(editor->text, buff[i]);
            }
            editor->cursor_pos = array_len(editor->text);
            break;
        case TGE_OBJECT_COLOR:
            array_clear(editor->text);
            if (editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_RGB) {
                switch (editor->color_field) {
                    case TGE_OBJECT_EDITOR_COLOR_FIELD_RH:
                        snprintf(buff, sizeof(buff), "%d", ((*(int*)editor->object.addr) & 0xff0000) >> 16);
                        break;
                    case TGE_OBJECT_EDITOR_COLOR_FIELD_GS:
                        snprintf(buff, sizeof(buff), "%d", ((*(int*)editor->object.addr) & 0x00ff00) >> 8);
                        break;
                    case TGE_OBJECT_EDITOR_COLOR_FIELD_BV:
                        snprintf(buff, sizeof(buff), "%d", (*(int*)editor->object.addr) & 0x0000ff);
                        break;
                }
            } else if (editor->color_mode == TGE_OBJECT_EDITOR_COLOR_MODE_HSV) {
                switch (editor->color_field) {
                    case TGE_OBJECT_EDITOR_COLOR_FIELD_RH:
                        break;
                    case TGE_OBJECT_EDITOR_COLOR_FIELD_GS:
                        break;
                    case TGE_OBJECT_EDITOR_COLOR_FIELD_BV:
                        break;
                }
            }
            for (i = 0; i < (int)strlen(buff); i += 1) {
                array_push(editor->text, buff[i]);
            }
            editor->cursor_pos = array_len(editor->text);
            break;
    }
}

static inline void _tge_object_editor_free(TGE_Game *game, TGE_Widget *widget) {
    TGE_Object_Editor *editor;

    (void)game;

    editor = widget->data;

    array_free(editor->text);
    free(editor);
    free(widget);
}

static inline TGE_Widget *tge_new_object_editor(TGE_Game *game, TGE_Object *object) {
    TGE_Widget        *widget;
    TGE_Object_Editor *editor;

    widget = malloc(sizeof(*widget));

    widget->ops.paint = _tge_object_editor_paint;
    widget->ops.key   = _tge_object_editor_key;
    widget->ops.focus = _tge_object_editor_focus;
    widget->ops.free  = _tge_object_editor_free;

    widget->hitbox_left   = 1;
    widget->hitbox_top    = 1;
    widget->hitbox_width  = TGE_OBJECT_EDITOR_WIDTH;
    widget->hitbox_height = TGE_OBJECT_EDITOR_HEIGHT;
    _tge_place_new_widget(game, widget);

    editor = malloc(sizeof(*editor));
    editor->object = *object;
    editor->text   = array_make(char);

    widget->data = editor;

    array_push(game->widgets, widget);
    game->focused_widget = widget;
    widget->ops.focus(game, widget);

    return widget;
}

#define TGE_OBJECT_FINDER_WIDTH  (30)
#define TGE_OBJECT_FINDER_HEIGHT (4)

typedef struct {
    array_t text;
    int     cursor_pos;
    array_t search_names;
    array_t search_objects;
    array_t in_view_objects;
} TGE_Object_Finder;

typedef struct {
    char       *title;
    TGE_Screen  screen;
    array_t     labels;
} TGE_Canvas_Widget;

typedef struct {
    int        x;
    int        y;
    char      *label;
    yed_attrs  attrs;
} TGE_Canvas_Label;

static inline void _tge_object_finder_search(TGE_Game *game, TGE_Object_Finder *finder) {
    char                           *text;
    int                             len;
    tree_it(str_t, TGE_Object_Ptr)  it;
    TGE_Object                     *oit;

    array_clear(finder->search_names);
    array_clear(finder->search_objects);
    array_clear(finder->in_view_objects);

    array_zero_term(finder->text);
    text = (char*)array_data(finder->text);
    len  = strlen(text);

    if (array_len(finder->text) > 0) {
        it = tree_gtr(game->named_objects, text);
        while (tree_it_good(it)) {
            if (strncmp(tree_it_key(it), text, len) == 0) {
                array_push(finder->search_names,   tree_it_key(it));
                array_push(finder->search_objects, *tree_it_val(it));
            }
            tree_it_next(it);
        }
    }

    bucket_array_traverse(game->objects, oit) {
        array_push(finder->in_view_objects, *oit);
    }
}

static inline void _tge_object_finder_paint(TGE_Game *game, TGE_Widget *widget) {
    TGE_Object_Finder  *finder;
    int                 max_name_width;
    TGE_Object_Shape   *shape;
    int                 max_width;
    int                 len;
    char              **it;
    yed_attrs           attrs;
    int                 x;
    int                 y;
    int                 top;
    int                 i;
    char                buff[128];

    finder = widget->data;

    _tge_object_finder_search(game, finder);

    max_name_width = 5;
    bucket_array_traverse(game->shapes, shape) {
        len = strlen(shape->name);
        if (len > max_name_width) {
            max_name_width = len;
        }
    }

    max_width = MAX(TGE_OBJECT_FINDER_WIDTH,
                      2 /* padding */
                    + 2 /* parens */
                    + max_name_width
                    + 3 /* @ and surrounding spaces */
                    + 18 /* hex address */);

    max_name_width = 0;
    array_traverse(finder->search_names, it) {
        len = strlen(*it);
        if (len > max_name_width) {
            max_name_width = len;
        }
    }

    max_width += max_name_width;

    widget->hitbox_width  = max_width;
    widget->hitbox_height = 4 + array_len(finder->search_names) + 1 + array_len(finder->in_view_objects) + 2 + 1;

    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
    attrs.fg    = 0;
    attrs.bg    = game->focused_widget == widget ? 0xbbbbbb : 0x555555;
    yed_set_attr(attrs);

    for (x = 0; x < widget->hitbox_width; x += 1) {
        for (y = 0; y < widget->hitbox_height; y += 1) {
            yed_set_cursor(widget->hitbox_top + y, widget->hitbox_left + x);
            yed_screen_print_n(" ", 1);
        }
    }

    attrs.bg = game->focused_widget == widget ? 0x00bbbb : 0x005555;
    yed_set_attr(attrs);

    for (x = 0; x < widget->hitbox_width; x += 1) {
        yed_set_cursor(widget->hitbox_top, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top, widget->hitbox_left);
    yed_screen_print("Object Finder");


    top = widget->hitbox_top + 2;

    attrs.bg = game->focused_widget == widget ? 0xcccccc : 0x555555;
    yed_set_attr(attrs);
    for (x = 1; x < widget->hitbox_width - 1; x += 1) {
        yed_set_cursor(top, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }

    yed_set_cursor(top, widget->hitbox_left + 1);
    yed_screen_print("search: ");
    yed_set_cursor(top, widget->hitbox_left + 1 + strlen("search: "));
    array_zero_term(finder->text);
    yed_screen_print((char*)array_data(finder->text));

    if (game->focused_widget == widget) {
        yed_set_cursor(top, widget->hitbox_left + 1 + strlen("search: ") + finder->cursor_pos);
        attrs.bg = 0xaf0000;
        yed_set_attr(attrs);
        if (finder->cursor_pos >= array_len(finder->text)) {
            yed_screen_print_n(" ", 1);
        } else {
            yed_screen_print_n((char*)array_item(finder->text, finder->cursor_pos), 1);
        }
    }

    top += 2;

    attrs.fg = 0;
    attrs.bg = game->focused_widget == widget ? 0xbbbbbb : 0x555555;
    yed_set_attr(attrs);

    for (i = 0; i < array_len(finder->search_names); i += 1) {
        yed_set_cursor(top + i, widget->hitbox_left + 1);
        snprintf(buff, sizeof(buff),
                 "%s (%s) @ %p",
                 *(char**)array_item(finder->search_names, i),
                 tge_object_type_string(game, array_item(finder->search_objects, i)),
                 ((TGE_Object*)array_item(finder->search_objects, i))->addr);
        yed_screen_print(buff);
    }

    top += array_len(finder->search_names) + 1;

    attrs.fg = game->focused_widget == widget ? 0xbbbbbb : 0x555555;
    attrs.bg = 0;
    yed_set_attr(attrs);

    for (x = 1; x < widget->hitbox_width - 1; x += 1) {
        yed_set_cursor(top, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(top, widget->hitbox_left + 2);
    yed_screen_print("IN VIEW:");

    top += 2;

    attrs.fg = 0;
    attrs.bg = game->focused_widget == widget ? 0xbbbbbb : 0x555555;
    yed_set_attr(attrs);

    for (i = 0; i < array_len(finder->in_view_objects); i += 1) {
        yed_set_cursor(top + i, widget->hitbox_left + 1);
        snprintf(buff, sizeof(buff),
                 "(%s) @ %p",
                 tge_object_type_string(game, array_item(finder->in_view_objects, i)),
                 ((TGE_Object*)array_item(finder->in_view_objects, i))->addr);
        yed_screen_print(buff);
    }
}

static inline void _tge_object_finder_key(TGE_Game *game, TGE_Widget *widget, int key) {
    TGE_Object_Finder *finder;
    int                base;
    int                end;

    (void)game;

    finder = widget->data;

    switch (key) {
        case BACKSPACE:
            if (finder->cursor_pos > 0) {
                finder->cursor_pos -= 1;
                array_delete(finder->text, finder->cursor_pos);
            }
            break;
        case ARROW_LEFT:
            if (finder->cursor_pos > 0) {
                finder->cursor_pos -= 1;
            }
            break;
        case ARROW_RIGHT:
            if (finder->cursor_pos < array_len(finder->text)) {
                finder->cursor_pos += 1;
            }
            break;
        default:
            if (IS_MOUSE(key)
            &&  MOUSE_BUTTON(key) == MOUSE_BUTTON_LEFT
            &&  MOUSE_KIND(key)   == MOUSE_PRESS) {

                if (array_len(finder->search_objects) > 0) {
                    base = 5;
                    end  = base + array_len(finder->search_objects) - 1;

                    if (MOUSE_ROW(key) >= base && MOUSE_ROW(key) <= end) {
                        tge_new_object_editor(game, (TGE_Object*)array_item(finder->search_objects, MOUSE_ROW(key) - base));
                        goto out;
                    }
                }
                if (array_len(finder->in_view_objects) > 0) {
                    base = 5 + array_len(finder->search_objects) + 1 + 1 + 1;
                    end  = base + array_len(finder->in_view_objects) - 1;

                    if (MOUSE_ROW(key) >= base && MOUSE_ROW(key) <= end) {
                        tge_new_object_editor(game, (TGE_Object*)array_item(finder->in_view_objects, MOUSE_ROW(key) - base));
                        goto out;
                    }
                }
            } else if (isprint(key)) {
                array_insert(finder->text, finder->cursor_pos, key);
                finder->cursor_pos += 1;
            }
out:;
            break;
    }
}

static inline void _tge_object_finder_focus(TGE_Game *game, TGE_Widget *widget) {
    (void)game;
    (void)widget;
}

static inline void _tge_object_finder_free(TGE_Game *game, TGE_Widget *widget) {
    TGE_Object_Finder *finder;

    (void)game;

    finder = widget->data;

    array_free(finder->in_view_objects);
    array_free(finder->search_objects);
    array_free(finder->search_names);
    array_free(finder->text);
    free(finder);
    free(widget);
}

static inline TGE_Widget *tge_new_object_finder(TGE_Game *game) {
    TGE_Widget        *widget;
    TGE_Object_Finder *finder;

    widget = malloc(sizeof(*widget));

    widget->ops.paint = _tge_object_finder_paint;
    widget->ops.key   = _tge_object_finder_key;
    widget->ops.focus = _tge_object_finder_focus;
    widget->ops.free  = _tge_object_finder_free;

    widget->hitbox_left   = 1;
    widget->hitbox_top    = 1;
    widget->hitbox_width  = TGE_OBJECT_FINDER_WIDTH;
    widget->hitbox_height = TGE_OBJECT_FINDER_HEIGHT;
    _tge_place_new_widget(game, widget);

    finder                  = malloc(sizeof(*finder));
    finder->text            = array_make(char);
    finder->search_names    = array_make(char*);
    finder->search_objects  = array_make(TGE_Object);
    finder->in_view_objects = array_make(TGE_Object);

    widget->data = finder;

    array_push(game->widgets, widget);
    game->focused_widget = widget;
    widget->ops.focus(game, widget);

    return widget;
}

static inline void _tge_canvas_widget_paint(TGE_Game *game, TGE_Widget *widget) {
    TGE_Canvas_Widget *canvas;
    yed_attrs          attrs;
    int                x;
    TGE_Canvas_Label  *label;

    canvas = widget->data;

    canvas->screen.off_x = widget->hitbox_left - 1;
    canvas->screen.off_y = widget->hitbox_top;
    _tge_paint_screen(&canvas->screen);

    attrs.flags = ATTR_FG_KIND_BITS(ATTR_KIND_RGB) | ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
    attrs.fg    = 0;
    attrs.bg    = game->focused_widget == widget ? 0x00bbbb : 0x005555;
    yed_set_attr(attrs);

    for (x = 0; x < widget->hitbox_width; x += 1) {
        yed_set_cursor(widget->hitbox_top, widget->hitbox_left + x);
        yed_screen_print_n(" ", 1);
    }
    yed_set_cursor(widget->hitbox_top, widget->hitbox_left);
    yed_screen_print_n(canvas->title, MIN(strlen(canvas->title), widget->hitbox_width));

    array_traverse(canvas->labels, label) {
        yed_set_attr(label->attrs);
        yed_set_cursor(canvas->screen.off_y + 1 + (label->y >> 1), canvas->screen.off_x + 1 + label->x);
        yed_screen_print_over(label->label);
    }
}

static inline void _tge_canvas_widget_key(TGE_Game *game, TGE_Widget *widget, int key) {
    (void)game;
    (void)widget;
    (void)key;
}

static inline void _tge_canvas_widget_focus(TGE_Game *game, TGE_Widget *widget) {
    (void)game;
    (void)widget;
}

static inline void _tge_canvas_widget_free(TGE_Game *game, TGE_Widget *widget) {
    TGE_Canvas_Widget *canvas;
    TGE_Canvas_Label  *label;

    (void)game;

    canvas = widget->data;

    free(canvas->title);
    free(canvas->screen.pixels);
    array_traverse(canvas->labels, label) {
        free(label->label);
    }
    array_free(canvas->labels);
    free(canvas);
    free(widget);
}


static inline TGE_Widget *tge_new_canvas_widget(TGE_Game *game, int width, int height, const char *title) {
    TGE_Widget        *widget;
    TGE_Canvas_Widget *canvas;

    widget = malloc(sizeof(*widget));

    widget->ops.paint = _tge_canvas_widget_paint;
    widget->ops.key   = _tge_canvas_widget_key;
    widget->ops.focus = _tge_canvas_widget_focus;
    widget->ops.free  = _tge_canvas_widget_free;

    widget->hitbox_left   = 1;
    widget->hitbox_top    = 1;
    widget->hitbox_width  = width;
    widget->hitbox_height = height / 2;
    _tge_place_new_widget(game, widget);

    canvas         = malloc(sizeof(*canvas));
    canvas->title  = strdup(title);
    canvas->labels = array_make(TGE_Canvas_Label);
    _tge_init_screen(&canvas->screen, height, width);

    widget->data = canvas;

    array_push(game->widgets, widget);
    game->focused_widget = widget;
    widget->ops.focus(game, widget);

    return widget;
}

static inline void tge_canvas_widget_add_label(TGE_Widget *widget, int x, int y, const char *label, int fg, int bg) {
    TGE_Canvas_Widget *canvas;
    TGE_Canvas_Label   new_label;

    if (widget->ops.free != _tge_canvas_widget_free) { return; }

    canvas = widget->data;

    new_label.label = strdup(label);
    new_label.x     = x;
    new_label.y     = y;

    new_label.attrs = ZERO_ATTR;
    if (fg >= 0) {
        new_label.attrs.flags |= ATTR_FG_KIND_BITS(ATTR_KIND_RGB);
        new_label.attrs.fg     = fg;
    }
    if (bg >= 0) {
        new_label.attrs.flags |= ATTR_BG_KIND_BITS(ATTR_KIND_RGB);
        new_label.attrs.bg     = bg;
    }

    array_push(canvas->labels, new_label);
}

static inline int tge_new_shape(TGE_Game *game, const char *name) {
    TGE_Object_Shape shape;

    shape.name   = strdup(name);
    shape.fields = array_make(TGE_Object_Shape_Field);

    bucket_array_push(game->shapes, shape);

    return bucket_array_len(game->shapes) - 1;
}

static inline void tge_shape_add_int_field(TGE_Game *game, int shape_idx, const char *name, u32 offset) {
    TGE_Object_Shape       *shape;
    TGE_Object_Shape_Field  field;

    (void)game;

    shape = bucket_array_item(game->shapes, shape_idx);

    field.name   = strdup(name);
    field.offset = offset;
    field.type   = TGE_OBJECT_INT;
    field.shape  = -1;

    array_push(shape->fields, field);
}

static inline void tge_shape_add_float_field(TGE_Game *game, int shape_idx, const char *name, u32 offset) {
    TGE_Object_Shape       *shape;
    TGE_Object_Shape_Field  field;

    (void)game;

    shape = bucket_array_item(game->shapes, shape_idx);

    field.name   = strdup(name);
    field.offset = offset;
    field.type   = TGE_OBJECT_FLOAT;
    field.shape  = -1;

    array_push(shape->fields, field);
}

static inline void tge_shape_add_color_field(TGE_Game *game, int shape_idx, const char *name, u32 offset) {
    TGE_Object_Shape       *shape;
    TGE_Object_Shape_Field  field;

    (void)game;

    shape = bucket_array_item(game->shapes, shape_idx);

    field.name   = strdup(name);
    field.offset = offset;
    field.type   = TGE_OBJECT_COLOR;
    field.shape  = -1;

    array_push(shape->fields, field);
}

static inline void tge_shape_add_struct_field(TGE_Game *game, int shape_idx, const char *name, u32 offset, int field_shape_idx) {
    TGE_Object_Shape       *shape;
    TGE_Object_Shape_Field  field;

    (void)game;

    shape = bucket_array_item(game->shapes, shape_idx);

    field.name   = strdup(name);
    field.offset = offset;
    field.type   = TGE_OBJECT_STRUCT;
    field.shape  = field_shape_idx;

    array_push(shape->fields, field);
}

static inline void tge_name_object(TGE_Game *game, TGE_Object *object, const char *name) {
    tree_it(str_t, TGE_Object_Ptr)  lookup;
    char                           *key;

    lookup = tree_lookup(game->named_objects, (char*)name);

    if (tree_it_good(lookup)) {
        key = tree_it_key(lookup);
        tree_delete(game->named_objects, key);
        free(key);
    }

    tree_insert(game->named_objects, strdup(name), object);
}

static inline TGE_Object *tge_new_int_object(TGE_Game *game, int *i) {
    TGE_Object object;

    object.addr  = i;
    object.type  = TGE_OBJECT_INT;
    object.shape = -1;

    return bucket_array_push(game->objects, object);
}

static inline TGE_Object *tge_new_float_object(TGE_Game *game, float *f) {
    TGE_Object object;

    object.addr  = f;
    object.type  = TGE_OBJECT_FLOAT;
    object.shape = -1;

    return bucket_array_push(game->objects, object);
}

static inline TGE_Object *tge_new_struct_object(TGE_Game *game, void *addr, int shape_idx) {
    TGE_Object object;

    object.addr  = addr;
    object.type  = TGE_OBJECT_STRUCT;
    object.shape = shape_idx;

    return bucket_array_push(game->objects, object);
}

#endif
