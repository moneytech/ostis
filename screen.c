#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "screen.h"
#include "shifter.h"
#include "debug/display.h"

static int disable = 0;
static SDL_Surface *screen;
static SDL_Texture *texture = NULL;
static SDL_Window *window;
static SDL_Renderer *renderer;
int screen_window_id;
static SDL_Texture *rasterpos_indicator[2];
static int rasterpos_indicator_cnt = 0;
static int screen_grabbed = 0;

#define PADDR(x, y) (screen->pixels + \
                         ((y) + BORDER_SIZE) * screen->pitch + \
                         ((x) + BORDER_SIZE) * screen->format->BytesPerPixel)

void screen_make_texture(const char *scale)
{
  static const char *old_scale = "";
  int pixelformat = SDL_PIXELFORMAT_BGR24;
  if(debugger) pixelformat = SDL_PIXELFORMAT_RGB24;

  if(strcmp(scale, old_scale) == 0)
    return;

  if(texture != NULL)
    SDL_DestroyTexture(texture);

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, scale);
  texture = SDL_CreateTexture(renderer,
			      pixelformat,
			      SDL_TEXTUREACCESS_STREAMING,
			      2*512, 314);
}

SDL_Texture *screen_generate_rasterpos_indicator(int color)
{
  Uint32 rmask, gmask, bmask, amask;
  SDL_Surface *rscreen;
  SDL_Texture *rtext;
  int i;
  char *p;
  
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
  amask = 0x00000000;
  rmask = 0x00ff0000;
  gmask = 0x0000ff00;
  bmask = 0x000000ff;
#else
  amask = 0x00000000;
  rmask = 0x000000ff;
  gmask = 0x0000ff00;
  bmask = 0x00ff0000;
#endif

  rscreen = SDL_CreateRGBSurface(0, 4, 1, 24, rmask, gmask, bmask, amask);

  p = rscreen->pixels;
  
  for(i=0;i<rscreen->w;i++) {
    p[i*rscreen->format->BytesPerPixel+0] = (color<<16)&0xff;
    p[i*rscreen->format->BytesPerPixel+1] = (color<<8)&0xff;
    p[i*rscreen->format->BytesPerPixel+2] = color&0xff;
  }

  rtext = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGR24,
                            SDL_TEXTUREACCESS_STREAMING,
                            4, 1);
  
  SDL_UpdateTexture(rtext, NULL, rscreen->pixels, rscreen->pitch);
  
  return rtext;
}

void screen_init()
{
  /* should be rewritten with proper error checking */
  Uint32 rmask, gmask, bmask, amask;
  
  if(disable) return;
#if 0
  SDL_Init(SDL_INIT_VIDEO);
  atexit(SDL_Quit);
#endif
  
#if SDL_BYTEORDER != SDL_BIG_ENDIAN
  amask = 0x00000000;
  rmask = 0x00ff0000;
  gmask = 0x0000ff00;
  bmask = 0x000000ff;
#else
  amask = 0x00000000;
  rmask = 0x000000ff;
  gmask = 0x0000ff00;
  bmask = 0x00ff0000;
#endif


  //  if(debugger) {
  //    screen = SDL_CreateRGBSurface(0, 2*512, 314, 24,
  //    				  rmask, gmask, bmask, amask);
  //  } else {
    window = SDL_CreateWindow("Main screen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 628, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    screen = SDL_CreateRGBSurface(0, 2*512, 314, 24,
    				  rmask, gmask, bmask, amask);
    renderer = SDL_CreateRenderer(window, -1, 0);
    screen_window_id = SDL_GetWindowID(window);
    printf("DEBUG: screen_window_id == %d\n", screen_window_id);
    screen_make_texture(SDL_SCALING_NEAREST);
    //  }

  if(screen == NULL) {
    fprintf(stderr, "Did not get a video mode\n");
    exit(1);
  }

  rasterpos_indicator[0] = screen_generate_rasterpos_indicator(0xffffff);
  rasterpos_indicator[1] = screen_generate_rasterpos_indicator(0xff0000);
}

void screen_copyimage(unsigned char *src)
{
#if 0
  int i;
#endif

  //  memcpy(screen->pixels, src, 512*314*3);
#if 0
  for(i=0;i<314;i++) {
    memcpy(PADDR(0, i), src+512*3*i, 512*3);
  }
#endif
}

void screen_clear()
{
  int i;
  unsigned char *p;

  p = screen->pixels;

  for(i=0;i<512*314;i++) {
    if((((i/512)&1) && (i&1)) ||
       (!((i/512)&1) && !(i&1)))
      p[i*3+0] = p[i*3+1] = p[i*3+2] = 0;
    else {
      if(shifter_on_display(i)) {
	p[i*3+0] = p[i*3+1] = p[i*3+2] = 0xf0;
      } else {
	p[i*3+0] = p[i*3+1] = p[i*3+2] = 0xff;
      }
    }
  }
}

void screen_swap(int indicate_rasterpos)
{
  SDL_Rect dst;
  
  if(disable) return;

    if(debugger) {
      display_swap_screen();
    }
    SDL_UpdateTexture(texture, NULL, screen->pixels, screen->pitch);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    if(indicate_rasterpos) {
      int rasterpos = shifter_get_vsync();
      dst.x = 2*(rasterpos%512)-8;
      dst.y = 2*(rasterpos/512);
      dst.w = 8;
      dst.h = 2;
      SDL_RenderCopy(renderer, rasterpos_indicator[rasterpos_indicator_cnt&1], NULL, &dst);
      rasterpos_indicator_cnt++;
    }
    SDL_RenderPresent(renderer);
    //  }
}

void screen_disable(int yes)
{
  if(yes)
    disable = 1;
  else
    disable = 0;
}

int screen_check_disable()
{
  return disable;
}

void *screen_pixels()
{
  return screen->pixels;
}

void screen_toggle_grab()
{
  if(screen_grabbed) {
    SDL_SetWindowGrab(window, SDL_FALSE);
    SDL_ShowCursor(1);
    screen_grabbed = 0;
  } else {
    SDL_SetWindowGrab(window, SDL_TRUE);
    SDL_ShowCursor(0);
    screen_grabbed = 1;
  }
}

