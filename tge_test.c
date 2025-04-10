#include <yed/plugin.h>

#include "tge.h"

#define WIDTH  (160)
#define HEIGHT (144)
#define FPS    (60)

static yed_plugin *Self;
static TGE_Game   *game;
static int         game_over;
static u64 t;

static u8 circle_mask[] = {
    0, 1, 1, 0,
    1, 0, 0, 1,
    1, 0, 0, 1,
    0, 1, 1, 0,
};
static u32 circle_pixels[] = {
    0, 0xff0000, 0xff0000, 0,
    0xff0000, 0, 0, 0xff0000,
    0x0000ff, 0, 0, 0x0000ff,
    0, 0x0000ff, 0x0000ff, 0,
};
static TGE_Sprite *circle_sprite;

typedef struct {
    int x, y;
} Vec2i;

typedef struct {
    int x, y, z;
} Vec3i;

typedef struct {
    float x, y;
} Vec2f;

typedef struct {
    float x, y, z;
} Vec3f;

typedef struct {
    TGE_Drawable draw;
    Vec2i        pos;
    Vec2f        speed;
} Circle;

typedef struct {
    Vec2f        pos;
    Vec2f        speed;
    int          size;
    int          color;
} Square;

static Circle circle;
static Square square;

static void frame(TGE_Game *game);
static void unload(yed_plugin *self);

#define DECLARE_SHAPE() _DECLARE_SHAPE(SHAPE_T)
#define _DECLARE_SHAPE(_SHAPE_T) __DECLARE_SHAPE(_SHAPE_T)
#define __DECLARE_SHAPE(_SHAPE_T) \
    int _SHAPE_T##_shape = tge_new_shape((SHAPE_GAME_PTR), #_SHAPE_T)

#define SHAPE_FIELD_INT(_name) _SHAPE_FIELD_INT(SHAPE_T, _name)
#define _SHAPE_FIELD_INT(_SHAPE_T, _name) __SHAPE_FIELD_INT(_SHAPE_T, _name)
#define __SHAPE_FIELD_INT(_SHAPE_T, _name) \
    tge_shape_add_int_field((SHAPE_GAME_PTR), _SHAPE_T##_shape, #_name, offsetof(_SHAPE_T, _name))

#define SHAPE_FIELD_FLOAT(_name) _SHAPE_FIELD_FLOAT(SHAPE_T, _name)
#define _SHAPE_FIELD_FLOAT(_SHAPE_T, _name) __SHAPE_FIELD_FLOAT(_SHAPE_T, _name)
#define __SHAPE_FIELD_FLOAT(_SHAPE_T, _name) \
    tge_shape_add_float_field((SHAPE_GAME_PTR), _SHAPE_T##_shape, #_name, offsetof(_SHAPE_T, _name))

#define SHAPE_FIELD_COLOR(_name) _SHAPE_FIELD_COLOR(SHAPE_T, _name)
#define _SHAPE_FIELD_COLOR(_SHAPE_T, _name) __SHAPE_FIELD_COLOR(_SHAPE_T, _name)
#define __SHAPE_FIELD_COLOR(_SHAPE_T, _name) \
    tge_shape_add_color_field((SHAPE_GAME_PTR), _SHAPE_T##_shape, #_name, offsetof(_SHAPE_T, _name))

#define SHAPE_FIELD_STRUCT(_name, _field_shape) _SHAPE_FIELD_STRUCT(SHAPE_T, _name, _field_shape##_shape)
#define _SHAPE_FIELD_STRUCT(_SHAPE_T, _name, _field_shape) __SHAPE_FIELD_STRUCT(_SHAPE_T, _name, _field_shape)
#define __SHAPE_FIELD_STRUCT(_SHAPE_T, _name, _field_shape) \
    tge_shape_add_struct_field((SHAPE_GAME_PTR), _SHAPE_T##_shape, #_name, offsetof(_SHAPE_T, _name), _field_shape)

int yed_plugin_boot(yed_plugin *self) {
    YED_PLUG_VERSION_CHECK();

    Self = self;

    yed_plugin_set_unload_fn(Self, unload);

    game = tge_new_game(Self, HEIGHT, WIDTH, FPS, TGE_TAKE_KEYS | TGE_TAKE_MOUSE);
    game->frame_callback = frame;

    circle_sprite = tge_new_sprite(game, 4, 4, circle_pixels, circle_mask);
    circle.draw.sprite = circle_sprite;

    square.speed.x = 30.0;
    square.speed.y = 30.0;
    square.size    = 16;
    square.color   = 0xffffff;


    #define SHAPE_GAME_PTR (game)

    #define SHAPE_T Vec2f
        DECLARE_SHAPE(); SHAPE_FIELD_FLOAT(x); SHAPE_FIELD_FLOAT(y);
    #undef SHAPE_T

    #define SHAPE_T Vec3f
        DECLARE_SHAPE(); SHAPE_FIELD_FLOAT(x); SHAPE_FIELD_FLOAT(y); SHAPE_FIELD_FLOAT(z);
    #undef SHAPE_T

    #define SHAPE_T Vec2i
        DECLARE_SHAPE(); SHAPE_FIELD_INT(x); SHAPE_FIELD_INT(y);
    #undef SHAPE_T

    #define SHAPE_T Vec3i
        DECLARE_SHAPE(); SHAPE_FIELD_INT(x); SHAPE_FIELD_INT(y); SHAPE_FIELD_INT(z);
    #undef SHAPE_T

    #define SHAPE_T Circle
        DECLARE_SHAPE(); SHAPE_FIELD_STRUCT(pos, Vec2i); SHAPE_FIELD_STRUCT(speed, Vec2f);
    #undef SHAPE_T

    #define SHAPE_T Square
        DECLARE_SHAPE(); SHAPE_FIELD_STRUCT(pos, Vec2f); SHAPE_FIELD_STRUCT(speed, Vec2f); SHAPE_FIELD_INT(size); SHAPE_FIELD_COLOR(color);
    #undef SHAPE_T

    TGE_Object *circle_object = tge_new_struct_object(game, &circle, Circle_shape);
        tge_name_object(game, circle_object, "circle");
    TGE_Object *square_object = tge_new_struct_object(game, &square, Square_shape);
        tge_name_object(game, square_object, "square");

    tge_new_object_finder(game);

    t = measure_time_now_ms();

    return 0;
}

static void frame(TGE_Game *game) {
    u64    now;
    float  dt;
    int   *key;
    int    i;
    int    j;

    now = measure_time_now_ms();
    dt  = ((float)(now - t)) / 1000.0;
    t   = now;

    (void)dt;

    array_traverse(game->keys, key) {
        if (IS_MOUSE(*key)) {
            if (MOUSE_BUTTON(*key) == MOUSE_BUTTON_LEFT
            &&  (  MOUSE_KIND(*key) == MOUSE_PRESS
                 || MOUSE_KIND(*key) == MOUSE_DRAG)) {

                circle.pos.x = MOUSE_COL(*key);
                if ((u32)circle.pos.x > WIDTH - circle.draw.sprite->width) { circle.pos.x = WIDTH - circle.draw.sprite->width; }
                circle.pos.y = MOUSE_ROW(*key);
                if ((u32)circle.pos.y > HEIGHT - circle.draw.sprite->height) { circle.pos.y = HEIGHT - circle.draw.sprite->height; }
            }
        }
    }

    if (tge_key_pressed(game, 'q')) {
        tge_finish_game(game);
        game_over = 1;
        yed_force_update();
        return;
    } else if (tge_key_pressed(game, ARROW_UP)) {
        if (circle.pos.y >= 1) { circle.pos.y -= 1; }
    } else if (tge_key_pressed(game, ARROW_DOWN)) {
        if (circle.pos.y + circle.draw.sprite->height < HEIGHT) {
            circle.pos.y += 1;
        }
    } else if (tge_key_pressed(game, ARROW_LEFT)) {
        if (circle.pos.x >= 1) { circle.pos.x -= 1; }
    } else if (tge_key_pressed(game, ARROW_RIGHT)) {
        if (circle.pos.x + circle.draw.sprite->width < WIDTH) {
            circle.pos.x += 1;
        }
    }

    tge_clear(game, 0);

    /* Draw moving square. */
    square.pos.x = (square.pos.x + (square.speed.x * dt));
    if ((int)square.pos.x > WIDTH - square.size) {
        square.pos.x   = WIDTH - square.size;
        square.speed.x = -square.speed.x;
    } else if (square.pos.x < 0) {
        square.pos.x   = 0;
        square.speed.x = -square.speed.x;
    }

    square.pos.y = (square.pos.y + (square.speed.y * dt));
    if ((int)square.pos.y > HEIGHT - square.size) {
        square.pos.y   = HEIGHT - square.size;
        square.speed.y = -square.speed.y;
    } else if (square.pos.y < 0) {
        square.pos.y   = 0;
        square.speed.y = -square.speed.y;
    }

    for (i = 0; i < square.size; i += 1) {
        for (j = 0; j < square.size; j += 1) {
            game->screen.pixels[(i+(int)square.pos.y)*WIDTH+(j+(int)square.pos.x)] = square.color;
        }
    }

    /* Draw circle sprite. */
    tge_draw(game, &circle.draw, circle.pos.x, circle.pos.y);

}

static void unload(yed_plugin *self) {
    (void)self;

    if (!game_over) {
        tge_finish_game(game);
        game_over = 1;
    }
}
