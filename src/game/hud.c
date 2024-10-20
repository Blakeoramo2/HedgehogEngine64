#include <PR/ultratypes.h>

#include "sm64.h"
#include "actors/common1.h"
#include "gfx_dimensions.h"
#include "game_init.h"
#include "level_update.h"
#include "camera.h"
#include "print.h"
#include "ingame_menu.h"
#include "hud.h"
#include "segment2.h"
#include "area.h"
#include "save_file.h"
#include "print.h"
#include "engine/surface_load.h"
#include "engine/math_util.h"
#include "puppycam2.h"
#include "puppyprint.h"

#include "config.h"

/* @file hud.c
 * This file implements HUD rendering and power meter animations.
 * That includes stars, lives, coins, camera status, power meter, timer
 * cannon reticle, and the unused keys.
 **/

#ifdef BREATH_METER
#define HUD_BREATH_METER_X         40
#define HUD_BREATH_METER_Y         32
#define HUD_BREATH_METER_HIDDEN_Y -20
#endif

// ------------- FPS COUNTER ---------------
// To use it, call print_fps(x,y); every frame.
#define FRAMETIME_COUNT 30

OSTime frameTimes[FRAMETIME_COUNT];
u8 curFrameTimeIndex = 0;

#include "PR/os_convert.h"

#ifdef USE_PROFILER
float profiler_get_fps();
#else
// Call once per frame
f32 calculate_and_update_fps() {
    OSTime newTime = osGetTime();
    OSTime oldTime = frameTimes[curFrameTimeIndex];
    frameTimes[curFrameTimeIndex] = newTime;

    curFrameTimeIndex++;
    if (curFrameTimeIndex >= FRAMETIME_COUNT) {
        curFrameTimeIndex = 0;
    }
    return ((f32)FRAMETIME_COUNT * 1000000.0f) / (s32)OS_CYCLES_TO_USEC(newTime - oldTime);
}
#endif

void print_fps(s32 x, s32 y) {
#ifdef USE_PROFILER
    f32 fps = profiler_get_fps();
#else
    f32 fps = calculate_and_update_fps();
#endif
    char text[14];

    sprintf(text, "FPS %2.2f", fps);
#ifdef PUPPYPRINT
    print_small_text(x, y, text, PRINT_TEXT_ALIGN_LEFT, PRINT_ALL, FONT_OUTLINE);
#else
    print_text(x, y, text);
#endif
}

// ------------ END OF FPS COUNER -----------------

struct PowerMeterHUD {
    s8 animation;
    s16 x;
    s16 y;
};

struct CameraHUD {
    s16 status;
};

// Stores health segmented value defined by numHealthWedges
// When the HUD is rendered this value is 8, full health.
static s16 sPowerMeterStoredHealth;

static struct PowerMeterHUD sPowerMeterHUD = {
    POWER_METER_HIDDEN,
    HUD_POWER_METER_X,
    HUD_POWER_METER_HIDDEN_Y,
};

// Power Meter timer that keeps counting when it's visible.
// Gets reset when the health is filled and stops counting
// when the power meter is hidden.
s32 sPowerMeterVisibleTimer = 0;

#ifdef BREATH_METER
static s16 sBreathMeterStoredValue;
static struct PowerMeterHUD sBreathMeterHUD = {
    BREATH_METER_HIDDEN,
    HUD_BREATH_METER_X,
    HUD_BREATH_METER_HIDDEN_Y,
};
s32 sBreathMeterVisibleTimer = 0;
#endif

static struct CameraHUD sCameraHUD = { CAM_STATUS_NONE };

/**
 * Renders a rgba16 16x16 glyph texture from a table list.
 */
void render_hud_tex_lut(s32 x, s32 y, Texture *texture) {
    Gfx *tempGfxHead = gDisplayListHead;

    gDPPipeSync(tempGfxHead++);
    gDPSetTextureImage(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture);
    gSPDisplayList(tempGfxHead++, &dl_hud_img_load_tex_block);
    gSPTextureRectangle(tempGfxHead++, x << 2, y << 2, (x + 15) << 2, (y + 15) << 2,
                        G_TX_RENDERTILE, 0, 0, 4 << 10, 1 << 10);

    gDisplayListHead = tempGfxHead;
}

/**
 * Renders a rgba16 8x8 glyph texture from a table list.
 */
void render_hud_small_tex_lut(s32 x, s32 y, Texture *texture) {
    Gfx *tempGfxHead = gDisplayListHead;

    gDPSetTile(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
                G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD);
    gDPTileSync(tempGfxHead++);
    gDPSetTile(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 2, 0, G_TX_RENDERTILE, 0,
                G_TX_CLAMP, 3, G_TX_NOLOD, G_TX_CLAMP, 3, G_TX_NOLOD);
    gDPSetTileSize(tempGfxHead++, G_TX_RENDERTILE, 0, 0, (8 - 1) << G_TEXTURE_IMAGE_FRAC, (8 - 1) << G_TEXTURE_IMAGE_FRAC);
    gDPPipeSync(tempGfxHead++);
    gDPSetTextureImage(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture);
    gDPLoadSync(tempGfxHead++);
    gDPLoadBlock(tempGfxHead++, G_TX_LOADTILE, 0, 0, 8 * 8 - 1, CALC_DXT(8, G_IM_SIZ_16b_BYTES));
    gSPTextureRectangle(tempGfxHead++, x << 2, y << 2, (x + 7) << 2, (y + 7) << 2, G_TX_RENDERTILE,
                        0, 0, 4 << 10, 1 << 10);

    gDisplayListHead = tempGfxHead;
}

/**
 * Renders power meter health segment texture using a table list.
 */
void render_power_meter_health_segment(s16 numHealthWedges) {
    Texture *(*healthLUT)[] = segmented_to_virtual(&power_meter_health_segments_lut);
    Gfx *tempGfxHead = gDisplayListHead;

    gDPPipeSync(tempGfxHead++);
    gDPSetTextureImage(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1,
                       (*healthLUT)[numHealthWedges - 1]);
    gDPLoadSync(tempGfxHead++);
    gDPLoadBlock(tempGfxHead++, G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES));
    gSP1Triangle(tempGfxHead++, 0, 1, 2, 0);
    gSP1Triangle(tempGfxHead++, 0, 2, 3, 0);

    gDisplayListHead = tempGfxHead;
}

/**
 * Renders power meter display lists.
 * That includes the "POWER" base and the colored health segment textures.
 */
void render_dl_power_meter(s16 numHealthWedges) {
    Mtx *mtx = alloc_display_list(sizeof(Mtx));

    if (mtx == NULL) {
        return;
    }

    guTranslate(mtx, (f32) sPowerMeterHUD.x, (f32) sPowerMeterHUD.y, 0);

    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx++),
              G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPDisplayList(gDisplayListHead++, &dl_power_meter_base);

    if (numHealthWedges != 0) {
        gSPDisplayList(gDisplayListHead++, &dl_power_meter_health_segments_begin);
        render_power_meter_health_segment(numHealthWedges);
        gSPDisplayList(gDisplayListHead++, &dl_power_meter_health_segments_end);
    }

    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

/**
 * Power meter animation called when there's less than 8 health segments
 * Checks its timer to later change into deemphasizing mode.
 */
void animate_power_meter_emphasized(void) {
    s16 hudDisplayFlags = gHudDisplay.flags;

    if (!(hudDisplayFlags & HUD_DISPLAY_FLAG_EMPHASIZE_POWER)) {
        if (sPowerMeterVisibleTimer == 45.0f) {
            sPowerMeterHUD.animation = POWER_METER_DEEMPHASIZING;
        }
    } else {
        sPowerMeterVisibleTimer = 0;
    }
}

/**
 * Power meter animation called after emphasized mode.
 * Moves power meter y pos speed until it's at 200 to be visible.
 */
static void animate_power_meter_deemphasizing(void) {
    s16 speed = 5;

    if (sPowerMeterHUD.y > HUD_POWER_METER_Y - 20) speed = 3;
    if (sPowerMeterHUD.y > HUD_POWER_METER_Y - 10) speed = 2;
    if (sPowerMeterHUD.y > HUD_POWER_METER_Y -  5) speed = 1;

    sPowerMeterHUD.y += speed;

    if (sPowerMeterHUD.y > HUD_POWER_METER_Y) {
        sPowerMeterHUD.y = HUD_POWER_METER_Y;
        sPowerMeterHUD.animation = POWER_METER_VISIBLE;
    }
}

/**
 * Power meter animation called when there's 8 health segments.
 * Moves power meter y pos quickly until it's at 301 to be hidden.
 */
static void animate_power_meter_hiding(void) {
    sPowerMeterHUD.y += 20;
    if (sPowerMeterHUD.y > HUD_POWER_METER_HIDDEN_Y) {
        sPowerMeterHUD.animation = POWER_METER_HIDDEN;
        sPowerMeterVisibleTimer = 0;
    }
}

/**
 * Handles power meter actions depending of the health segments values.
 */
void handle_power_meter_actions(s16 numHealthWedges) {
    // Show power meter if health is not full, less than 8
    if (numHealthWedges < 8 && sPowerMeterStoredHealth == 8
        && sPowerMeterHUD.animation == POWER_METER_HIDDEN) {
        sPowerMeterHUD.animation = POWER_METER_EMPHASIZED;
        sPowerMeterHUD.y = HUD_POWER_METER_EMPHASIZED_Y;
    }

    // Show power meter if health is full, has 8
    if (numHealthWedges == 8 && sPowerMeterStoredHealth == 7) {
        sPowerMeterVisibleTimer = 0;
    }

    // After health is full, hide power meter
    if (numHealthWedges == 8 && sPowerMeterVisibleTimer > 45.0f) {
        sPowerMeterHUD.animation = POWER_METER_HIDING;
    }

    // Update to match health value
    sPowerMeterStoredHealth = numHealthWedges;

#ifndef BREATH_METER
    // If Mario is swimming, keep power meter visible
    if (gPlayerCameraState->action & ACT_FLAG_SWIMMING) {
        if (sPowerMeterHUD.animation == POWER_METER_HIDDEN
            || sPowerMeterHUD.animation == POWER_METER_EMPHASIZED) {
            sPowerMeterHUD.animation = POWER_METER_DEEMPHASIZING;
            sPowerMeterHUD.y = HUD_POWER_METER_EMPHASIZED_Y;
        }
        sPowerMeterVisibleTimer = 0;
    }
#endif
}

/**
 * Renders the power meter that shows when Mario is in underwater
 * or has taken damage and has less than 8 health segments.
 * And calls a power meter animation function depending of the value defined.
 */
void render_hud_power_meter(void) {
    s16 shownHealthWedges = gHudDisplay.wedges;
    if (sPowerMeterHUD.animation != POWER_METER_HIDING) handle_power_meter_actions(shownHealthWedges);
    if (sPowerMeterHUD.animation == POWER_METER_HIDDEN) return;
    switch (sPowerMeterHUD.animation) {
        case POWER_METER_EMPHASIZED:    animate_power_meter_emphasized();    break;
        case POWER_METER_DEEMPHASIZING: animate_power_meter_deemphasizing(); break;
        case POWER_METER_HIDING:        animate_power_meter_hiding();        break;
        default:                                                             break;
    }
    render_dl_power_meter(shownHealthWedges);
    sPowerMeterVisibleTimer++;
}

#ifdef BREATH_METER
/**
 * Renders breath meter health segment texture using a table list.
 */
void render_breath_meter_segment(s16 numBreathWedges) {
    Texture *(*breathLUT)[];
    breathLUT = segmented_to_virtual(&breath_meter_segments_lut);
    Gfx *tempGfxHead = gDisplayListHead;

    gDPPipeSync(tempGfxHead++);
    gDPSetTextureImage(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (*breathLUT)[numBreathWedges - 1]);
    gDPLoadSync(tempGfxHead++);
    gDPLoadBlock(tempGfxHead++, G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES));
    gSP1Triangle(tempGfxHead++, 0, 1, 2, 0);
    gSP1Triangle(tempGfxHead++, 0, 2, 3, 0);

    gDisplayListHead = tempGfxHead;
}

/**
 * Renders breath meter display lists.
 * That includes the base and the colored segment textures.
 */
void render_dl_breath_meter(s16 numBreathWedges) {
    Mtx *mtx = alloc_display_list(sizeof(Mtx));

    if (mtx == NULL) {
        return;
    }

    guTranslate(mtx, (f32) sBreathMeterHUD.x, (f32) sBreathMeterHUD.y, 0);
    gSPMatrix(      gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx++),
                    G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPDisplayList( gDisplayListHead++, &dl_breath_meter_base);
    if (numBreathWedges != 0) {
        gSPDisplayList(gDisplayListHead++, &dl_breath_meter_health_segments_begin);
        render_breath_meter_segment(numBreathWedges);
        gSPDisplayList(gDisplayListHead++, &dl_breath_meter_health_segments_end);
    }
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

/**
 * Breath meter animation called after emphasized mode.
 * Moves breath meter y pos speed until it's visible.
 */
static void animate_breath_meter_sliding_in(void) {
    approach_s16_symmetric_bool(&sBreathMeterHUD.y, HUD_BREATH_METER_Y, 5);
    if (sBreathMeterHUD.y         == HUD_BREATH_METER_Y) {
        sBreathMeterHUD.animation = BREATH_METER_VISIBLE;
    }
}

/**
 * Breath meter animation called when there's 8 health segments.
 * Moves breath meter y pos quickly until it's hidden.
 */
static void animate_breath_meter_sliding_out(void) {
    approach_s16_symmetric_bool(&sBreathMeterHUD.y, HUD_BREATH_METER_HIDDEN_Y, 20);
    if (sBreathMeterHUD.y         == HUD_BREATH_METER_HIDDEN_Y) {
        sBreathMeterHUD.animation = BREATH_METER_HIDDEN;
    }
}

/**
 * Handles breath meter actions depending of the health segments values.
 */
void handle_breath_meter_actions(s16 numBreathWedges) {
    // Show breath meter if health is not full, less than 8
    if ((numBreathWedges < 8) && (sBreathMeterStoredValue == 8) && sBreathMeterHUD.animation == BREATH_METER_HIDDEN) {
        sBreathMeterHUD.animation = BREATH_METER_SHOWING;
        // sBreathMeterHUD.y         = HUD_BREATH_METER_Y;
    }
    // Show breath meter if breath is full, has 8
    if ((numBreathWedges == 8) && (sBreathMeterStoredValue  == 7)) sBreathMeterVisibleTimer  = 0;
    // After breath is full, hide breath meter
    if ((numBreathWedges == 8) && (sBreathMeterVisibleTimer > 45)) sBreathMeterHUD.animation = BREATH_METER_HIDING;
    // Update to match breath value
    sBreathMeterStoredValue = numBreathWedges;
    // If Mario is swimming, keep breath meter visible
    if (gPlayerCameraState->action & ACT_FLAG_SWIMMING) {
        if (sBreathMeterHUD.animation == BREATH_METER_HIDDEN) {
            sBreathMeterHUD.animation = BREATH_METER_SHOWING;
        }
        sBreathMeterVisibleTimer = 0;
    }
}

void render_hud_breath_meter(void) {
    s16 shownBreathAmount = gHudDisplay.breath;
    if (sBreathMeterHUD.animation != BREATH_METER_HIDING) handle_breath_meter_actions(shownBreathAmount);
    if (sBreathMeterHUD.animation == BREATH_METER_HIDDEN) return;
    switch (sBreathMeterHUD.animation) {
        case BREATH_METER_SHOWING:       animate_breath_meter_sliding_in();  break;
        case BREATH_METER_HIDING:        animate_breath_meter_sliding_out(); break;
        default:                                                             break;
    }
    render_dl_breath_meter(shownBreathAmount);
    sBreathMeterVisibleTimer++;
}
#endif


/**
 * Renders the amount of lives Mario has.
 */
void render_hud_mario_lives(void) {
    char str[10];
    sprintf(str, "☺×%d", gHudDisplay.lives);
    print_text(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(22), HUD_TOP_Y, str);
}

#ifdef VANILLA_STYLE_CUSTOM_DEBUG
void render_debug_mode(void) {
    print_text(180, 40, "DEBUG MODE");
    print_text_fmt_int(5, 20, "Z %d", gMarioState->pos[2]);
    print_text_fmt_int(5, 40, "Y %d", gMarioState->pos[1]);
    print_text_fmt_int(5, 60, "X %d", gMarioState->pos[0]);
    print_text_fmt_int(10, 100, "SPD %d", (s32) gMarioState->forwardVel);
    print_text_fmt_int(10, 120, "ANG 0×%04x", (u16) gMarioState->faceAngle[1]);
    print_fps(10,80);
}
#endif

/**
 * Renders the amount of coins collected.
 */
void render_hud_coins(void) {
    char str[10];
    sprintf(str, "✪×%d", gHudDisplay.coins);
    print_text(HUD_COINS_X, HUD_TOP_Y, str);
}

/**
 * Renders the amount of stars collected.
 * Disables "X" glyph when Mario has 100 stars or more.
 */
void render_hud_stars(void) {
    char str[10];
    if (gHudFlash == HUD_FLASH_STARS && gGlobalTimer & 0x8) return;
    if (gHudDisplay.stars < 100) {
        sprintf(str, "★×%d", gHudDisplay.stars);
    } else {
        sprintf(str, "★%d", gHudDisplay.stars);
    }
    print_text(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_STARS_X), HUD_TOP_Y, str);
}

/**
 * Unused function that renders the amount of keys collected.
 * Leftover function from the beta version of the game.
 */
void render_hud_keys(void) {
    s16 i;

    for (i = 0; i < gHudDisplay.keys; i++) {
        print_text((i * 16) + 220, 142, "|"); // unused glyph - beta key
    }
}

LangArray textTime = DEFINE_LANGUAGE_ARRAY(
    "TIME %0d'%02d\"%d",
    "TEMPS %0d'%02d\"%d",
    "ZEIT %0d'%02d\"%d",
    "TIME %0d'%02d\"%d",
    "TIEM. %0d'%02d\"%d");

/**
 * Renders the timer when Mario start sliding in PSS.
 */
void render_hud_timer(void) {
    char str[20];
    u16 timerValFrames = gHudDisplay.timer;
    u16 timerMins = timerValFrames / (30 * 60);
    u16 timerSecs = (timerValFrames - (timerMins * 1800)) / 30;
    u16 timerFracSecs = ((timerValFrames - (timerMins * 1800) - (timerSecs * 30)) & 0xFFFF) / 3;

    sprintf(str, LANG_ARRAY(textTime), timerMins, timerSecs, timerFracSecs);
    print_text_aligned(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(28), 185, str, TEXT_ALIGN_RIGHT);
}

/**
 * Sets HUD status camera value depending of the actions
 * defined in update_camera_status.
 */
void set_hud_camera_status(s16 status) {
    sCameraHUD.status = status;
}

/**
 * Renders camera HUD glyphs using a table list, depending of
 * the camera status called, a defined glyph is rendered.
 */
void render_hud_camera_status(void) {
    Texture *(*cameraLUT)[6] = segmented_to_virtual(&main_hud_camera_lut);
    s32 x = GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_CAMERA_X);
    s32 y = 205;

    if (sCameraHUD.status == CAM_STATUS_NONE) {
        return;
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    render_hud_tex_lut(x, y, (*cameraLUT)[GLYPH_CAM_CAMERA]);

    switch (sCameraHUD.status & CAM_STATUS_MODE_GROUP) {
        case CAM_STATUS_MARIO:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_MARIO_HEAD]);
            break;
        case CAM_STATUS_LAKITU:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_LAKITU_HEAD]);
            break;
        case CAM_STATUS_FIXED:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_FIXED]);
            break;
    }

    switch (sCameraHUD.status & CAM_STATUS_C_MODE_GROUP) {
        case CAM_STATUS_C_DOWN:
            render_hud_small_tex_lut(x + 4, y + 16, (*cameraLUT)[GLYPH_CAM_ARROW_DOWN]);
            break;
        case CAM_STATUS_C_UP:
            render_hud_small_tex_lut(x + 4, y - 8, (*cameraLUT)[GLYPH_CAM_ARROW_UP]);
            break;
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}

#include "../buffers/framebuffers.h"

//#define FB_SEG (u8 *)gFramebuffers[sRenderedFramebuffer]

//#if 0

/**
 * zlib License
 *
 * Copyright (C) 2024 Tharo
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 */

// Note about integration: This assumes there is at least 0xC750 bytes of free memory following each framebuffer for
// successively downscaled images. Ensure this is the case!

#define TMEM_SIZE 0x1000

// Option to stop downscaling at 40x30 instead of 20x15, may improve visual clarity but bright patches won't bleed into
// surroundings as much
//#define SCALE_40_30

#define qu102(x) ((u32)((x) * (1 <<  2)))
#define qs105(x) ((s32)((x) * (1 <<  5)))
#define qs510(x) ((s32)((x) * (1 << 10)))

//static_assert(SCREEN_WIDTH == 320, "Bloom shader expects 320x240 screen size");
//static_assert(SCREEN_HEIGHT == 240, "Bloom shader expects 320x240 screen size");
#define HALF_SCREEN_WIDTH (SCREEN_WIDTH / 2) // 160
#define QUARTER_SCREEN_WIDTH (SCREEN_WIDTH / 4) // 80
#define EIGHTH_SCREEN_WIDTH (SCREEN_WIDTH / 8) // 40
#define SIXTEENTH_SCREEN_WIDTH (SCREEN_WIDTH / 16) // 20
#define THIRTYTWO_SCREEN_WIDTH (SCREEN_WIDTH / 32) // 10
#define SIXTYFOUR_SCREEN_WIDTH (SCREEN_WIDTH / 64) // 5

#define HALF_SCREEN_HEIGHT (SCREEN_HEIGHT / 2) // 120
#define QUARTER_SCREEN_HEIGHT (SCREEN_HEIGHT / 4) // 60
#define EIGHTH_SCREEN_HEIGHT (SCREEN_HEIGHT / 8) // 30
#define SIXTEENTH_SCREEN_HEIGHT (SCREEN_HEIGHT / 16) // 15
#define THIRTYTWO_SCREEN_HEIGHT (SCREEN_HEIGHT / 32) // 7
#define SIXTYFOUR_SCREEN_HEIGHT (SCREEN_HEIGHT / 64) // 3

#define FB_SEG (u32)gFramebuffers[sRenderedFramebuffer]
#define FB_SEG_ALT gFramebuffers[sRenderedFramebuffer]
#define PREV_FB_SEG gSavedFB

static u32 PreRender_Downscale4to1(Gfx** gfxP, u32 fbSegment, u32 width, u32 height) {
    Gfx* gfx = *gfxP;
    u32 newWidth = width / 2;
    u32 newHeight = height / 2;
    u32 newFbSegment = fbSegment + ALIGN8(width * height * G_IM_SIZ_16b_BYTES);
    u32 nRows;
    u32 rowsRemaining;
    u32 curRow;

    gDPPipeSync(gfx++);
    gDPSetColorImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, newWidth, newFbSegment);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, newWidth, newHeight);

    nRows = TMEM_SIZE / (width * G_IM_SIZ_16b_BYTES);
    if (nRows & 1) {
        // We must load an even number of rows for texture sampling reasons, decrement so it fits in TMEM
        nRows--;
    }
    rowsRemaining = height;
    curRow = 0;

    while (rowsRemaining != 0) {
        u32 uls;
        u32 lrs;
        u32 ult;
        u32 lrt;

        // Make sure that we don't load past the end of the source image
        nRows = MIN(rowsRemaining, nRows);

        uls = 0;
        lrs = width;
        ult = curRow;
        lrt = curRow + nRows;

        // Load a horizontal strip of the source image in RGBA16 format
        gDPLoadTextureTile(gfx++, fbSegment, G_IM_FMT_RGBA, G_IM_SIZ_16b, width, height, uls, ult, lrs - 1, lrt - 1, 0,
                           G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                           G_TX_NOLOD);

        // Draw that horizontal strip to the destination image, downscaling 4-to-1 (2-to-1 in each axis)
        // The texture coordinates are offset by 0.5 for correct average filtering, dsdx and dtdy step by 2 input texels
        // as each output texel combines 2 pixels per row/column (4 pixels square)
        gSPTextureRectangle(gfx++, qu102(uls / 2), qu102(ult / 2), qu102(lrs / 2), qu102(lrt / 2), G_TX_RENDERTILE,
                            qs105(uls) + qs105(0.5), qs105(ult) + qs105(0.5), qs510(2.0), qs510(2.0));

        // Continue to next lines
        rowsRemaining -= nRows;
        curRow += nRows;
    }
    *gfxP = gfx;
    return newFbSegment;
}

///are you fucking kidding me, it was THAT easy?
//RGBA16 *gSavedFB;
//can't seem to make this a variable-length array. yes I tried alloc_display_list
RGBA16 gSavedFB[SCREEN_WIDTH * SCREEN_HEIGHT];

void PreRender_BloomShader(Gfx** gfxP, u32 yl, u32 yh, u8 alpha1, u8 alpha2) {
	
	//const u32 tmemAddr = TMEM_SIZE - ALIGN8(SCREEN_WIDTH * SCREEN_HEIGHT * G_IM_SIZ_16b_BYTES);
	
	//const u32 tmemAddr = TMEM_SIZE - ALIGN8(HALF_SCREEN_WIDTH * HALF_SCREEN_HEIGHT * G_IM_SIZ_16b_BYTES);
	
	//const u32 tmemAddr = TMEM_SIZE - ALIGN8(QUARTER_SCREEN_WIDTH * QUARTER_SCREEN_HEIGHT * G_IM_SIZ_16b_BYTES);
	
//#ifdef SCALE_40_30
    const u32 tmemAddr = TMEM_SIZE - ALIGN8(EIGHTH_SCREEN_WIDTH * EIGHTH_SCREEN_HEIGHT * G_IM_SIZ_16b_BYTES);
//#else /* SCALE_20_15 */
    //const u32 tmemAddr = TMEM_SIZE - ALIGN8(SIXTEENTH_SCREEN_WIDTH * SIXTEENTH_SCREEN_HEIGHT * G_IM_SIZ_16b_BYTES);
//#endif

	//const u32 tmemAddr = TMEM_SIZE - ALIGN8(THIRTYTWO_SCREEN_WIDTH * THIRTYTWO_SCREEN_HEIGHT * G_IM_SIZ_16b_BYTES);
	
	//const u32 tmemAddr = TMEM_SIZE - ALIGN8(SIXTYFOUR_SCREEN_WIDTH * SIXTYFOUR_SCREEN_HEIGHT * G_IM_SIZ_16b_BYTES) - 1;

    Gfx* gfx = *gfxP;
    u32 fbSegment;
    u32 nRows;
    u32 rowsRemaining;
    u32 curRow;

    if (alpha1 == 0 || alpha2 == 0) {
        // Don't do anything if either alpha is 0
        return;
    }

    // Set up downscaling with average filtering
    gDPPipeSync(gfx++);
    gDPSetOtherMode(gfx++,
                    G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_AVERAGE | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | CVG_DST_FULL | G_RM_NOOP | G_RM_NOOP2);
    gDPSetCombineLERP(gfx++, 0, 0, 0, TEXEL0, 0, 0, 0, 1,
                             0, 0, 0, TEXEL0, 0, 0, 0, 1);

    // Execute downscaling
    fbSegment = FB_SEG;
    // 320x240 -> 160x120
    fbSegment = PreRender_Downscale4to1(&gfx, fbSegment, SCREEN_WIDTH, SCREEN_HEIGHT);
    // 160x120 -> 80x60
    fbSegment = PreRender_Downscale4to1(&gfx, fbSegment, HALF_SCREEN_WIDTH, HALF_SCREEN_HEIGHT);
    // 80x60 -> 40x30
    fbSegment = PreRender_Downscale4to1(&gfx, fbSegment, QUARTER_SCREEN_WIDTH, QUARTER_SCREEN_HEIGHT);
//#ifndef SCALE_40_30
    // 40x30 -> 20x15
    //fbSegment = PreRender_Downscale4to1(&gfx, fbSegment, EIGHTH_SCREEN_WIDTH, EIGHTH_SCREEN_HEIGHT);
//#endif
	//fbSegment = PreRender_Downscale4to1(&gfx, fbSegment, SIXTEENTH_SCREEN_WIDTH, SIXTEENTH_SCREEN_HEIGHT);
	//fbSegment = PreRender_Downscale4to1(&gfx, fbSegment, THIRTYTWO_SCREEN_WIDTH, THIRTYTWO_SCREEN_HEIGHT);

    // Upload downscaled 20x15 rgba16 image to TMEM
	
    //gDPLoadMultiBlock(gfx++, fbSegment, tmemAddr / 8, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, SCREEN_HEIGHT, 0,
                      //G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, 0, 0);	
					  
	//gDPLoadMultiBlock(gfx++, fbSegment, tmemAddr / 8, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, HALF_SCREEN_WIDTH, HALF_SCREEN_HEIGHT, 0,
                      //G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, 1, 1);
					  
	//gDPLoadMultiBlock(gfx++, fbSegment, tmemAddr / 8, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, QUARTER_SCREEN_WIDTH, QUARTER_SCREEN_HEIGHT, 0,
                      //G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, 2, 2);	
	
//#ifdef SCALE_40_30
    gDPLoadMultiBlock(gfx++, fbSegment, tmemAddr / 8, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, EIGHTH_SCREEN_WIDTH, EIGHTH_SCREEN_HEIGHT, 0,
                      G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, 3, 3);
    // 3 = log2(320/40), 3 = log2(240/30)
//#else /* SCALE_20_15 */
   //gDPLoadMultiBlock(gfx++, fbSegment, tmemAddr / 8, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, SIXTEENTH_SCREEN_WIDTH, EIGHTH_SCREEN_HEIGHT, 0,
                      //G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, 4, 4);
    // 4 = log2(320/20), 4 = log2(240/15)
//#endif

   //gDPLoadMultiBlock(gfx++, fbSegment, tmemAddr / 8, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, THIRTYTWO_SCREEN_WIDTH, THIRTYTWO_SCREEN_HEIGHT, 0,
                      //G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, 5, 5);
					  
	//gDPLoadMultiBlock(gfx++, fbSegment, tmemAddr / 8, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, SIXTYFOUR_SCREEN_WIDTH, SIXTYFOUR_SCREEN_HEIGHT, 0,
                      //G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, 6, 6);

    // Account for bilerp oversampling of the small image by shifting the tile size, this avoids shifting pixels
    // of the original framebuffer out of place while maintaining correct bilinear sampling
	
	//gDPSetTileSize(gfx++, 1, qu102(0+0.5), qu102(0+0.5), qu102(SCREEN_WIDTH-1+0.5), qu102(SCREEN_HEIGHT-1+0.5));
	
	//gDPSetTileSize(gfx++, 1, qu102(0+0.5), qu102(0+0.5), qu102(HALF_SCREEN_WIDTH-1+0.5), qu102(HALF_SCREEN_HEIGHT-1+0.5));
	
	//gDPSetTileSize(gfx++, 1, qu102(0+0.5), qu102(0+0.5), qu102(QUARTER_SCREEN_WIDTH-1+0.5), qu102(QUARTER_SCREEN_HEIGHT-1+0.5));
	
//#ifdef SCALE_40_30
    gDPSetTileSize(gfx++, 1, qu102(0+0.5), qu102(0+0.5), qu102(EIGHTH_SCREEN_WIDTH-1+0.5), qu102(EIGHTH_SCREEN_HEIGHT-1+0.5));
//#else
    //gDPSetTileSize(gfx++, 1, qu102(0+0.5), qu102(0+0.5), qu102(SIXTEENTH_SCREEN_WIDTH-1+0.5), qu102(SIXTEENTH_SCREEN_HEIGHT-1+0.5));
//#endif

	//gDPSetTileSize(gfx++, 1, qu102(0+0.5), qu102(0+0.5), qu102(THIRTYTWO_SCREEN_WIDTH-1+0.5), qu102(THIRTYTWO_SCREEN_HEIGHT-1+0.5));
	
	//gDPSetTileSize(gfx++, 1, qu102(0+0.5), qu102(0+0.5), qu102(SIXTYFOUR_SCREEN_WIDTH-1+0.5), qu102(SIXTYFOUR_SCREEN_HEIGHT-1+0.5));

    // Return to original framebuffer
    gDPSetColorImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, FB_SEG);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    // For each FB line blend (using CC to avoid overflows) with the downscaled image

    gDPSetOtherMode(gfx++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_2CYCLE | G_PM_NPRIMITIVE,
                    // IM_RD is needed to save coverage for the VI AA pass :(
                    G_AC_NONE | G_ZS_PRIM | CVG_DST_SAVE | ZMODE_OPA | IM_RD | G_RM_NOOP | G_RM_NOOP2);
    gDPSetCombineLERP(gfx++, TEXEL1, TEXEL0, PRIM_LOD_FRAC,   TEXEL0,   0, 0, 0, 1, 
                             TEXEL0, 0,      PRIMITIVE_ALPHA, COMBINED, 0, 0, 0, COMBINED);
    gDPSetPrimColor(gfx++, 0, alpha1, 255, 255, 255, alpha2);

    nRows = tmemAddr / (SCREEN_WIDTH * G_IM_SIZ_16b_BYTES);
    rowsRemaining = yh - yl;
    curRow = yl;

    while (rowsRemaining != 0) {
        u32 uls;
        u32 lrs;
        u32 ult;
        u32 lrt;

        // Make sure that we don't load past the end of the source image
        nRows = MIN(rowsRemaining, nRows);

        uls = 0;
        lrs = SCREEN_WIDTH;
        ult = curRow;
        lrt = curRow + nRows;

        // Load the framebuffer rows
        gDPLoadTextureTile(gfx++, FB_SEG, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, SCREEN_HEIGHT, uls, ult, lrs - 1, lrt - 1, 0,
                           G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                           G_TX_NOLOD);

        // Redraw, blending in the resampled image
        gSPTextureRectangle(gfx++, qu102(uls), qu102(ult), qu102(lrs), qu102(lrt), G_TX_RENDERTILE,
                            qs105(uls), qs105(ult), qs510(1.0), qs510(1.0));

        rowsRemaining -= nRows;
        curRow += nRows;
    }
	
	gDPPipeSync(gfx++);
	//gDPSetCycleType(gfx++, G_CYC_1CYCLE);
	//gDPSetRenderMode(gfx++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
	//gDPSetCombineLERP(gfx++, NOISE, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, NOISE, 0, SHADE, 0, TEXEL0, 0, SHADE, 0);
	//gSPLoadGeometryMode(gfx++, G_ZBUFFER | G_SHADE | G_CULL_BACK | G_SHADING_SMOOTH);
	//gSPSetOtherMode(gfx++, G_SETOTHERMODE_H, 4, 20, G_AD_NOISE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_1PRIMITIVE);
	//gSPSetOtherMode(gfx++,G_SETOTHERMODE_L, 0, 32, G_AC_NONE | G_ZS_PIXEL | G_RM_AA_ZB_XLU_SURF | G_RM_AA_ZB_XLU_SURF2);
	////gDPSetOtherMode(gfx++,
                        ////G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                            ////G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                        ////G_AC_NONE | G_ZS_PRIM | G_RM_OPA_SURF | G_RM_OPA_SURF2);
	//gDPPipeSync(gfx++);
	//gDPSetCycleType(gfx++, G_CYC_1CYCLE);
	//gDPSetRenderMode(gfx++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);

    *gfxP = gfx;
}

//#endif

void render_bloom() {
	Gfx *bloomDL = gDisplayListHead;
	PreRender_BloomShader(&bloomDL, 0, SCREEN_HEIGHT, 8, 20);
	//last argument bloom intensity, previous argument bloom visibility
	gDisplayListHead = bloomDL;
}

//#if 0

void PreRender_MotionBlurImpl(Gfx** gfxp, u8 envA) {
    Gfx* gfx = *gfxp;

    gDPPipeSync(gfx++);
	
	void* buf;
	void* bufSave;

    if (envA == 255) {
		buf = FB_SEG_ALT;
		bufSave = PREV_FB_SEG;
        gDPSetOtherMode(gfx++,
                        G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                            G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                        G_AC_NONE | G_ZS_PRIM | G_RM_OPA_SURF | G_RM_OPA_SURF2);
    } else {
		buf = PREV_FB_SEG;
		bufSave = FB_SEG_ALT;
        gDPSetOtherMode(gfx++,
                        G_AD_NOISE | G_CD_NOISE | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                            G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                        G_AC_NONE | G_ZS_PRIM | G_RM_CLD_SURF | G_RM_CLD_SURF2);
    }
	

    gDPSetEnvColor(gfx++, 255, 255, 255, envA);
    gDPSetCombineLERP(gfx++, TEXEL0, 0, ENVIRONMENT, 0, 0, 0, 0, ENVIRONMENT, TEXEL0, 0, ENVIRONMENT, 0, 0, 0, 0,
                      ENVIRONMENT);
    gDPSetColorImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, bufSave);

    //gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	
	//change to 44x44?
	u32 nRows;
    u32 rowsRemaining;
    u32 curRow;
	
	nRows = TMEM_SIZE / (SCREEN_WIDTH * G_IM_SIZ_16b_BYTES);
    if (nRows & 1) {
        // We must load an even number of rows for texture sampling reasons, decrement so it fits in TMEM
        nRows--;
    }
    rowsRemaining = SCREEN_HEIGHT;
    curRow = 0;
	
    while (rowsRemaining != 0) {
        u32 uls;
        u32 lrs;
        u32 ult;
        u32 lrt;

        // Make sure that we don't load past the end of the source image
        nRows = MIN(rowsRemaining, nRows);

        uls = 0;
        lrs = SCREEN_WIDTH;
        ult = curRow;
        lrt = curRow + nRows;

        // Load the framebuffer rows
        gDPLoadTextureTile(gfx++, buf, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, SCREEN_HEIGHT, uls, ult, lrs - 1, lrt - 1, 0,
                           G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                           G_TX_NOLOD);

        // Redraw, blending in the resampled image
        gSPTextureRectangle(gfx++, qu102(uls), qu102(ult), qu102(lrs), qu102(lrt), G_TX_RENDERTILE,
                            qs105(uls), qs105(ult), qs510(1.0), qs510(1.0));

        rowsRemaining -= nRows;
        curRow += nRows;
    }
	

    //Prerender_DrawBackground2D(&gfx, buf, 0, this->width, this->height, G_IM_FMT_RGBA, G_IM_SIZ_16b, G_TT_NONE, 0, 0.0f,
                              // 0.0f, 1.0f, 1.0f, BG2D_FLAGS_1 | BG2D_FLAGS_2 | BG2D_FLAGS_LOAD_S2DEX2);
    gDPPipeSync(gfx++);
    gDPSetColorImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, FB_SEG);

    *gfxp = gfx;
}

//what's the point of having extra arguments if they're always gonna be 255?
//makes sense to inline these

ALWAYS_INLINE void PreRender_MotionBlurOpaque(Gfx** gfxp) {
    //PreRender_MotionBlurImpl(this, gfxp, buf, bufSave, 255);
	PreRender_MotionBlurImpl(gfxp, 255);
}


ALWAYS_INLINE void PreRender_MotionBlur(Gfx** gfxp, s32 alpha) {
    //PreRender_MotionBlurImpl(this, gfxp, this->fbufSave, this->fbuf, alpha);
	PreRender_MotionBlurImpl(gfxp, alpha);
}

//alpha = R_MOTION_BLUR_ALPHA; = 90

enum {
    /* 0 */ MOTION_BLUR_OFF,
    /* 1 */ MOTION_BLUR_ON
} MotionBlurStatus;

u8 gMotionBlurStatus = 0;

void Play_DrawMotionBlur(u8 alpha) {
    Gfx* gfx;
	//alpha = 90

    //if (gMotionBlurStatus > MOTION_BLUR_OFF) {
        gfx = gDisplayListHead;
		

        PreRender_MotionBlur(&gfx, alpha);
		
		/**
		* Saves fbuf to fbufSave
		*/

        //if ((gSavedFB != NULL) && (gFramebuffers[sRenderedFramebuffer] != NULL)) {
			PreRender_MotionBlurOpaque(&gfx);
		//}

		gDisplayListHead = gfx;
    //}
}

//void render_motion_blur() {
	//if (gCurrLevelNum == LEVEL_CASTLE_GROUNDS) 
		//Play_DrawMotionBlur(136);
//}
//#endif

//extern u8 gMotionBlurThreshold;

/**
 * Render HUD strings using hudDisplayFlags with it's render functions,
 * excluding the cannon reticle which detects a camera preset for it.
 */
void render_hud(void) {
    s16 hudDisplayFlags = gHudDisplay.flags;

    if (hudDisplayFlags == HUD_DISPLAY_NONE) {
        sPowerMeterHUD.animation = POWER_METER_HIDDEN;
        sPowerMeterStoredHealth = 8;
        sPowerMeterVisibleTimer = 0;
#ifdef BREATH_METER
        sBreathMeterHUD.animation = BREATH_METER_HIDDEN;
        sBreathMeterStoredValue = 8;
        sBreathMeterVisibleTimer = 0;
#endif
    } else {
#ifdef VERSION_EU
        // basically create_dl_ortho_matrix but guOrtho screen width is different
        Mtx *mtx = alloc_display_list(sizeof(*mtx));

        if (mtx == NULL) {
            return;
        }

        create_dl_identity_matrix();
        guOrtho(mtx, -16.0f, SCREEN_WIDTH + 16, 0, SCREEN_HEIGHT, -10.0f, 10.0f, 1.0f);
        gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx),
                  G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
#else
        create_dl_ortho_matrix();
#endif

        if (gCurrentArea != NULL && gCurrentArea->camera->mode == CAMERA_MODE_INSIDE_CANNON) {
            render_hud_cannon_reticle();
        }

#ifdef ENABLE_LIVES
        if (hudDisplayFlags & HUD_DISPLAY_FLAG_LIVES) {
            render_hud_mario_lives();
        }
#endif

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_COIN_COUNT) {
            render_hud_coins();
        }

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_STAR_COUNT) {
            render_hud_stars();
        }

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_KEYS) {
            render_hud_keys();
        }

#ifdef BREATH_METER
        if (hudDisplayFlags & HUD_DISPLAY_FLAG_BREATH_METER) render_hud_breath_meter();
#endif

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_CAMERA_AND_POWER) {
            render_hud_power_meter();
#ifdef PUPPYCAM
            if (!gPuppyCam.enabled) {
#endif
            render_hud_camera_status();
#ifdef PUPPYCAM
            }
#endif
        }

        if (hudDisplayFlags & HUD_DISPLAY_FLAG_TIMER) {
            render_hud_timer();
        }

#ifdef VANILLA_STYLE_CUSTOM_DEBUG
        if (gCustomDebugMode) {
            render_debug_mode();
        }
#endif
    }
}
