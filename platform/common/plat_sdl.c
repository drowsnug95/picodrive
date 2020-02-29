/*
 * PicoDrive
 * (C) notaz, 2013
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include <stdio.h>

#include "../libpicofe/input.h"
#include "../libpicofe/plat_sdl.h"
#include "../libpicofe/in_sdl.h"
#include "../libpicofe/gl.h"
#include "emu.h"
#include "menu_pico.h"
#include "input_pico.h"
#include "version.h"

#include <pico/pico.h>

static void *shadow_fb;

//for rs-90
void downscale_224to160(uint16_t *dst, uint16_t *src);
void downscale_224to160_crop(uint32_t *dst, uint32_t *src);

const struct in_default_bind in_sdl_defbinds[] __attribute__((weak)) = {
	{ SDLK_UP,     IN_BINDTYPE_PLAYER12, GBTN_UP },
	{ SDLK_DOWN,   IN_BINDTYPE_PLAYER12, GBTN_DOWN },
	{ SDLK_LEFT,   IN_BINDTYPE_PLAYER12, GBTN_LEFT },
	{ SDLK_RIGHT,  IN_BINDTYPE_PLAYER12, GBTN_RIGHT },
	{ SDLK_z,      IN_BINDTYPE_PLAYER12, GBTN_A },
	{ SDLK_x,      IN_BINDTYPE_PLAYER12, GBTN_B },
	{ SDLK_c,      IN_BINDTYPE_PLAYER12, GBTN_C },
	{ SDLK_a,      IN_BINDTYPE_PLAYER12, GBTN_X },
	{ SDLK_s,      IN_BINDTYPE_PLAYER12, GBTN_Y },
	{ SDLK_d,      IN_BINDTYPE_PLAYER12, GBTN_Z },
	{ SDLK_RETURN, IN_BINDTYPE_PLAYER12, GBTN_START },
	{ SDLK_f,      IN_BINDTYPE_PLAYER12, GBTN_MODE },
	{ SDLK_ESCAPE, IN_BINDTYPE_EMU, PEVB_MENU },
	{ SDLK_TAB,    IN_BINDTYPE_EMU, PEVB_RESET },
	{ SDLK_F1,     IN_BINDTYPE_EMU, PEVB_STATE_SAVE },
	{ SDLK_F2,     IN_BINDTYPE_EMU, PEVB_STATE_LOAD },
	{ SDLK_F3,     IN_BINDTYPE_EMU, PEVB_SSLOT_PREV },
	{ SDLK_F4,     IN_BINDTYPE_EMU, PEVB_SSLOT_NEXT },
	{ SDLK_F5,     IN_BINDTYPE_EMU, PEVB_SWITCH_RND },
	{ SDLK_F6,     IN_BINDTYPE_EMU, PEVB_PICO_PPREV },
	{ SDLK_F7,     IN_BINDTYPE_EMU, PEVB_PICO_PNEXT },
	{ SDLK_F8,     IN_BINDTYPE_EMU, PEVB_PICO_SWINP },
	{ SDLK_BACKSPACE, IN_BINDTYPE_EMU, PEVB_FF },
	{ 0, 0, 0 }
};

const struct menu_keymap in_sdl_key_map[] __attribute__((weak)) =
{
	{ SDLK_UP,	PBTN_UP },
	{ SDLK_DOWN,	PBTN_DOWN },
	{ SDLK_LEFT,	PBTN_LEFT },
	{ SDLK_RIGHT,	PBTN_RIGHT },
	{ SDLK_RETURN,	PBTN_MOK },
	{ SDLK_ESCAPE,	PBTN_MBACK },
	{ SDLK_SEMICOLON,	PBTN_MA2 },
	{ SDLK_QUOTE,	PBTN_MA3 },
	{ SDLK_LEFTBRACKET,  PBTN_L },
	{ SDLK_RIGHTBRACKET, PBTN_R },
};

const struct menu_keymap in_sdl_joy_map[] __attribute__((weak)) =
{
	{ SDLK_UP,	PBTN_UP },
	{ SDLK_DOWN,	PBTN_DOWN },
	{ SDLK_LEFT,	PBTN_LEFT },
	{ SDLK_RIGHT,	PBTN_RIGHT },
	/* joystick */
	{ SDLK_WORLD_0,	PBTN_MOK },
	{ SDLK_WORLD_1,	PBTN_MBACK },
	{ SDLK_WORLD_2,	PBTN_MA2 },
	{ SDLK_WORLD_3,	PBTN_MA3 },
};

extern const char * const in_sdl_key_names[] __attribute__((weak));

static const struct in_pdata in_sdl_platform_data = {
	.defbinds = in_sdl_defbinds,
	.key_map = in_sdl_key_map,
	.kmap_size = sizeof(in_sdl_key_map) / sizeof(in_sdl_key_map[0]),
	.joy_map = in_sdl_joy_map,
	.jmap_size = sizeof(in_sdl_joy_map) / sizeof(in_sdl_joy_map[0]),
	.key_names = in_sdl_key_names,
};

/* YUV stuff */
static int yuv_ry[32], yuv_gy[32], yuv_by[32];
static unsigned char yuv_u[32 * 2], yuv_v[32 * 2];
static unsigned char yuv_y[256];
static struct uyvy {  unsigned int y:8; unsigned int vyu:24; } yuv_uyvy[65536];

void bgr_to_uyvy_init(void)
{
  int i, v;

  /* init yuv converter:
    y0 = (int)((0.299f * r0) + (0.587f * g0) + (0.114f * b0));
    y1 = (int)((0.299f * r1) + (0.587f * g1) + (0.114f * b1));
    u = (int)(8 * 0.565f * (b0 - y0)) + 128;
    v = (int)(8 * 0.713f * (r0 - y0)) + 128;
  */
  for (i = 0; i < 32; i++) {
    yuv_ry[i] = (int)(0.299f * i * 65536.0f + 0.5f);
    yuv_gy[i] = (int)(0.587f * i * 65536.0f + 0.5f);
    yuv_by[i] = (int)(0.114f * i * 65536.0f + 0.5f);
  }
  for (i = -32; i < 32; i++) {
    v = (int)(8 * 0.565f * i) + 128;
    if (v < 0)
      v = 0;
    if (v > 255)
      v = 255;
    yuv_u[i + 32] = v;
    v = (int)(8 * 0.713f * i) + 128;
    if (v < 0)
      v = 0;
    if (v > 255)
      v = 255;
    yuv_v[i + 32] = v;
  }
  // valid Y range seems to be 16..235
  for (i = 0; i < 256; i++) {
    yuv_y[i] = 16 + 219 * i / 32;
  }
  // everything combined into one large array for speed
  for (i = 0; i < 65536; i++) {
     int r = (i >> 11) & 0x1f, g = (i >> 6) & 0x1f, b = (i >> 0) & 0x1f;
     int y = (yuv_ry[r] + yuv_gy[g] + yuv_by[b]) >> 16;
     yuv_uyvy[i].y = yuv_y[y];
     yuv_uyvy[i].vyu = (yuv_v[r-y + 32] << 16) | (yuv_y[y] << 8) | yuv_u[b-y + 32];
  }
}

void rgb565_to_uyvy(void *d, const void *s, int pixels)
{
  unsigned int *dst = d;
  const unsigned short *src = s;

  for (; pixels > 0; src += 4, dst += 2, pixels -= 4)
  {
    struct uyvy *uyvy0 = yuv_uyvy + src[0], *uyvy1 = yuv_uyvy + src[1];
    struct uyvy *uyvy2 = yuv_uyvy + src[2], *uyvy3 = yuv_uyvy + src[3];
    dst[0] = (uyvy1->y << 24) | uyvy0->vyu;
    dst[1] = (uyvy3->y << 24) | uyvy2->vyu;
  }
}

void plat_video_flip(void)
{
	if (plat_sdl_overlay != NULL) {
		SDL_Rect dstrect =
			{ 0, 0, plat_sdl_screen->w, plat_sdl_screen->h };

		SDL_LockYUVOverlay(plat_sdl_overlay);
		rgb565_to_uyvy(plat_sdl_overlay->pixels[0], shadow_fb,
				g_screen_ppitch * g_screen_height);
		SDL_UnlockYUVOverlay(plat_sdl_overlay);
		SDL_DisplayYUVOverlay(plat_sdl_overlay, &dstrect);
	}
	else if (plat_sdl_gl_active) {
		gl_flip(shadow_fb, g_screen_ppitch, g_screen_height);
	}
	else {
		if (SDL_MUSTLOCK(ScreenSurface))
			SDL_UnlockSurface(ScreenSurface);
        
        switch (currentConfig.scaling){
            case 1: //old-full
                downscale_224to160((uint16_t*)plat_sdl_screen->pixels,(uint16_t*)ScreenSurface->pixels);
                break;
            case 2: //new full
                downscale_224to160_subpixel((uint16_t*)plat_sdl_screen->pixels,(uint16_t*)ScreenSurface->pixels);     
                break;
            default: //crop
                downscale_224to160_crop((uint32_t*)plat_sdl_screen->pixels,(uint32_t*)ScreenSurface->pixels);
        }
        
		SDL_Flip(ScreenSurface);
        
		g_screen_ptr = plat_sdl_screen->pixels;
		PicoDrawSetOutBuf(g_screen_ptr, g_screen_ppitch * 2);
	}
}

void plat_video_wait_vsync(void)
{
}

void plat_video_menu_enter(int is_rom_loaded)
{
	plat_sdl_change_video_mode(g_menuscreen_w, g_menuscreen_h, 0);
	g_screen_ptr = shadow_fb;
}

void plat_video_menu_begin(void)
{
	if (plat_sdl_overlay != NULL || plat_sdl_gl_active) {
		g_menuscreen_ptr = shadow_fb;
	}
	else {
		if (SDL_MUSTLOCK(plat_sdl_screen))
			SDL_LockSurface(plat_sdl_screen);
		//g_menuscreen_ptr = plat_sdl_screen->pixels;
        g_menuscreen_ptr = ScreenSurface->pixels;
        g_menuscreen_pp = 240;
	}
}

void plat_video_menu_end(void)
{
	if (plat_sdl_overlay != NULL) {
		SDL_Rect dstrect =
			{ 0, 0, plat_sdl_screen->w, plat_sdl_screen->h };

		SDL_LockYUVOverlay(plat_sdl_overlay);
		rgb565_to_uyvy(plat_sdl_overlay->pixels[0], shadow_fb,
				g_menuscreen_pp * g_menuscreen_h);
		SDL_UnlockYUVOverlay(plat_sdl_overlay);

		SDL_DisplayYUVOverlay(plat_sdl_overlay, &dstrect);
	}
	else if (plat_sdl_gl_active) {
		gl_flip(g_menuscreen_ptr, g_menuscreen_pp, g_menuscreen_h);
	}
	else {
		if (SDL_MUSTLOCK(plat_sdl_screen))
			SDL_UnlockSurface(plat_sdl_screen);
		SDL_Flip(plat_sdl_screen);
	}
	g_menuscreen_ptr = NULL;

}

void plat_video_menu_leave(void)
{
}

void plat_video_loop_prepare(void)
{
	plat_sdl_change_video_mode(g_screen_width, g_screen_height, 0);

	if (plat_sdl_overlay != NULL || plat_sdl_gl_active) {
		g_screen_ptr = shadow_fb;
	}
	else {
		if (SDL_MUSTLOCK(plat_sdl_screen))
			SDL_LockSurface(plat_sdl_screen);
		g_screen_ptr = plat_sdl_screen->pixels;
	}
	PicoDrawSetOutBuf(g_screen_ptr, g_screen_ppitch * 2);
}

void plat_early_init(void)
{
}

static void plat_sdl_quit(void)
{
	// for now..
	exit(1);
}

void plat_init(void)
{
	int shadow_size;
	int ret;

	ret = plat_sdl_init();
	if (ret != 0)
		exit(1);

	plat_sdl_quit_cb = plat_sdl_quit;

	SDL_WM_SetCaption("PicoDrive " VERSION, NULL);

	g_menuscreen_w = plat_sdl_screen->w;
	g_menuscreen_h = plat_sdl_screen->h;
	g_menuscreen_pp = g_menuscreen_w;
	g_menuscreen_ptr = NULL;

	shadow_size = g_menuscreen_w * g_menuscreen_h * 2;
	if (shadow_size < 320 * 480 * 2)
		shadow_size = 320 * 480 * 2;

	shadow_fb = malloc(shadow_size);
	g_menubg_ptr = calloc(1, shadow_size);
	if (shadow_fb == NULL || g_menubg_ptr == NULL) {
		fprintf(stderr, "OOM\n");
		exit(1);
	}

	g_screen_width = 320;
	g_screen_height = 240;
	g_screen_ppitch = 320;
	g_screen_ptr = shadow_fb;

	in_sdl_init(&in_sdl_platform_data, plat_sdl_event_handler);
	in_probe();

	bgr_to_uyvy_init();
}

void plat_finish(void)
{
	free(shadow_fb);
	shadow_fb = NULL;
	free(g_menubg_ptr);
	g_menubg_ptr = NULL;
	plat_sdl_finish();
}

#define RSHIFT(X) (((X) & 0xF7DE) >>1)
#define RSHIFT32(X) (((X) & 0xF7DEF7DE) >>1)
/*convert 224px to 160px by drowsnug */
void downscale_224to160_crop(uint32_t* __restrict__ src, uint32_t* __restrict__ dst)
{
    uint16_t y=4;
    uint32_t* __restrict__ buffer_mem;
    
    const uint16_t ix=1, iy=7;
    
    for(int H=0; H < 160/5; H += 1)
    {
	    buffer_mem = &src[y*160];
        uint16_t x = 20;
        for(int W=0; W<120; W++) 
        {
            uint32_t a,b,c,d,e,f,g;
            
            a = RSHIFT32(buffer_mem[x]);
            b = RSHIFT32(buffer_mem[x+160]);
            c = RSHIFT32(buffer_mem[x+160*2]);
            d = RSHIFT32(buffer_mem[x+160*3]);
            e = RSHIFT32(buffer_mem[x+160*4]);
            f = RSHIFT32(buffer_mem[x+160*5]);
            g = RSHIFT32(buffer_mem[x+160*6]);          

            *dst =  a +  RSHIFT32(a + b);
	        *(dst+120) = b + c;
	        *(dst+120*2) = d + RSHIFT32(d + RSHIFT32(c + e));
	        *(dst+120*3) = e + f;
	        *(dst+120*4) = g + RSHIFT32(f + g);
 	        dst++;
            x += ix;
        }
        dst += 120*4;
        y += iy;
    }
}

/*convert 224px to 160px by drowsnug */
//
// downscale 5 by 4 pixels into 4 by 3 pixels 
//
//  a1 a2 a3 a4 a5
//  b1 b2 b3 b4 b5
//  c1 c2 c3 c4 c5
//  d1 d2 d3 d4 d5
//
//   into 
//
//  A1 A2 A3 A4
//  B1 B2 B3 B4
//  C1 C2 C3 C4

    void downscale_224to160(uint16_t* __restrict__ src, uint16_t* __restrict__ dst)
{
    uint16_t y=10;
    uint16_t* __restrict__ buffer_mem;

    const uint16_t ix=5, iy=4;
    
    for(int H=0; H < 160/3; H++)
    {
	    buffer_mem = &src[y*320];
        uint16_t x = 10;
        for(int W=0; W< 240/4; W++) 
        {
            uint16_t a1,a2,a3,a4,a5,b1,b2,b3,b4,b5,c1,c2,c3,c4,c5,d1,d2,d3,d4,d5;
            
            a1 = RSHIFT(buffer_mem[x]);
            a2 = RSHIFT(buffer_mem[x+1]);
            a3 = RSHIFT(buffer_mem[x+2]);
            a4 = RSHIFT(buffer_mem[x+3]);
            a5 = RSHIFT(buffer_mem[x+4]);
            
            b1 = RSHIFT(buffer_mem[x+320]);
            b2 = RSHIFT(buffer_mem[x+320+1]);
            b3 = RSHIFT(buffer_mem[x+320+2]);
            b4 = RSHIFT(buffer_mem[x+320+3]);
            b5 = RSHIFT(buffer_mem[x+320+4]);
            
            c1 = RSHIFT(buffer_mem[x+320*2]);
            c2 = RSHIFT(buffer_mem[x+320*2+1]);
            c3 = RSHIFT(buffer_mem[x+320*2+2]);
            c4 = RSHIFT(buffer_mem[x+320*2+3]);
            c5 = RSHIFT(buffer_mem[x+320*2+4]);

            d1 = RSHIFT(buffer_mem[x+320*3]);
            d2 = RSHIFT(buffer_mem[x+320*3+1]);
            d3 = RSHIFT(buffer_mem[x+320*3+2]);
            d4 = RSHIFT(buffer_mem[x+320*3+3]);
            d5 = RSHIFT(buffer_mem[x+320*3+4]);

            //A1
            *dst =a1 + RSHIFT(b1 + RSHIFT(a2 + RSHIFT(a1 + b2)));
            //A2
            *(dst+1) = a2 + RSHIFT(a3 + RSHIFT(b2 + b3));
            //A3
            *(dst+2) = a4 + RSHIFT(a3 +  RSHIFT(b3 + b4));
            //A4
            *(dst+3) = a5 + RSHIFT(b5+ RSHIFT(a4 + RSHIFT(a5 + b4)));
            
            //B1
	        *(dst+240) = RSHIFT(b1 + RSHIFT(b1 + b2)) + RSHIFT(c1 + RSHIFT(c1 + c2));
            //B2
            *(dst+240+1) = RSHIFT(b2 + c2) + RSHIFT(RSHIFT(b3 + RSHIFT(b2 + b3)) + RSHIFT(c3 + RSHIFT(c2 + c3)));
            //B3
            *(dst+240+2) = RSHIFT(b4 + c4) + RSHIFT(RSHIFT(b3 + RSHIFT(b3 + b4)) + RSHIFT(c3 + RSHIFT(c3 + c4)));
            //B4
            *(dst+240+3) = RSHIFT(b5 +  RSHIFT(b4 + b5)) + RSHIFT(c5 + RSHIFT(c4 + c5));
            
            //C1
	        *(dst+240*2) = d1 + RSHIFT(c1 + RSHIFT(d2 + RSHIFT(c2 + d1)));
            //C2
            *(dst+240*2+1) = d2 + RSHIFT(d3 + RSHIFT(c2 + c3));
            //C3
            *(dst+240*2+2) = d4 + RSHIFT(d3 + RSHIFT(c3 + c4));
            //C4
            *(dst+240*2+3) = d5 + RSHIFT(c5 + RSHIFT(d4 + RSHIFT(c4 + d5)));
            
            dst+=4;
            x += ix;
        }
        dst += 240*2;
        y += iy;
    }
}

//Fullscreen by sub-pixel scaling
void downscale_224to160_subpixel(uint16_t* __restrict__ src, uint16_t* __restrict__ dst)
{
    #define RMASK 0b1111100000000000
    #define GMASK 0b0000011111100000
    #define BMASK 0b0000000000011111
    
    uint16_t y=10;
    uint16_t* __restrict__ buffer_mem;
    uint16_t* d = dst;

    const uint16_t ix=5, iy=4;
    
    for(int H=0; H < 160/3; H++)
    {
	    buffer_mem = &src[y*320];
        uint16_t x = 10;
        for(int W=0; W< 240/4; W++) 
        {
            uint16_t c[4][4];
            for(int j=0 ; j<4; j++){
                uint16_t r[5],g[5],b[5],R[4],G[4],B[4];
                for (int i = 0; i< 5; i++){
                    r[i] = (buffer_mem[x + i + 320 * j]) & RMASK;
                    g[i] = (buffer_mem[x + i + 320 * j]) & GMASK;
                    b[i] = (buffer_mem[x + i + 320 * j]) & BMASK;
                }
                R[0] = r[0];
                G[0] = g[0];
                B[0] = ((b[0] + b[1])>>1) & BMASK;

                R[1] = r[1];
                G[1] = ((g[1] + ((g[1] + g[2])>>1))>>1) & GMASK;
                B[1] = b[2];

                R[2] = r[2];
                G[2] = ((g[3] + ((g[3] + g[2])>>1))>>1) & GMASK;
                B[2] = b[3];

                R[3] = ((r[3]>>1) + (r[4]>>1)) & RMASK;
                G[3] = g[4];
                B[3] = b[4];
            
                c[0][j] = R[0] | G[0] | B[0];
                c[1][j] = R[1] | G[1] | B[1];
                c[2][j] = R[2] | G[2] | B[2];
                c[3][j] = R[3] | G[3] | B[3];
            }
            for(int i = 0; i < 4; i++){
                *(d + i) = RSHIFT(c[i][0]) + RSHIFT(RSHIFT(c[i][0]) + RSHIFT(c[i][1])); 
                *(d + i + 240) = RSHIFT(c[i][1]) + RSHIFT(c[i][2]); 
                *(d + i + 240 *2) = RSHIFT(c[i][3]) + RSHIFT(RSHIFT(c[i][2]) + RSHIFT(c[i][3])); 
            }
            d+=4;
            x += ix;
        }
        d += 240*2;
        y += iy;
    }
    
}