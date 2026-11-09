#include <psxgpu.h>
#include "clay_render.h"
#include "gfx.h"
#include "logic.h"
#include "generated/clay_background.h"
#include "generated/clay_sprites.h"
#include "generated/clay_rom_hud.h"

typedef struct {
 uint32_t *ot;
 uint8_t **packet;
 uint8_t *end;
} ClayDraw;

/* Above this page, the SDK font occupies VRAM at (960, 0). */
enum { CLAY_TEXTURE_X = 768, CLAY_TEXTURE_Y = 256 };

void hunt_clay_upload(void)
{
 RECT texture={CLAY_TEXTURE_X,CLAY_TEXTURE_Y,256,240};
 LoadImage(&texture,(const uint32_t *)clay_rom_background);
 DrawSync(0);
}
static int wide_x(int x)
{
 return x * 5 / 4;
}

static void tile(ClayDraw *draw, int x, int y, int w, int h, int r, int g, int b, int z)
{
 if (x<0 || x+w>256 || y<0 || y+h>240 || draw->end - *draw->packet < sizeof(TILE)) return;
 TILE *p=(TILE *)*draw->packet;
 *draw->packet += sizeof(*p);
 setTile(p);
 setXY0(p,wide_x(x),y);
 setWH(p,wide_x(x+w)-wide_x(x),h);
 setRGB0(p,r,g,b);
 addPrim(&draw->ot[z],p);
}

static void runs(ClayDraw *draw, const ClayRun *data, unsigned count, int x, int y, int z)
{
 for (unsigned i=0;i<count;i++)
  tile(draw, x+data[i].x,y+data[i].y,data[i].w,1,data[i].r,data[i].g,data[i].b,z);
}
#define PATCH(data,x,y,z) runs(draw, data,sizeof(data)/sizeof((data)[0]),x,y,z)

static void digit(ClayDraw *draw, int number, int x, int y, int green)
{
 for (int r=0;r<8;r++) for (int c=0;c<8;c++)
  if (rom_clay_digits[number][r] & (128>>c))
   tile(draw, x+c,y+r,1,1,green?136:255,green?216:254,green?0:255,0);
 tile(draw, x,y,8,8,0,0,0,0);
}

static void mask(ClayDraw *draw, const HuntClayFlight *target)
{
 for (int y=target->y-13;y<target->y+14;y++) {
  int start=-1;
  for (int x=0;x<=256;x++) {
   int on=x<256 && y>=0 && y<240 && hunt_clay_hit(target,x,y,0);
   if (on && start<0) start=x;
   if (!on && start>=0) {tile(draw, start,y,x-start,1,255,255,255,0);start=-1;}
  }
 }
}

void hunt_clay_draw(uint32_t *ot, uint8_t **packet, uint8_t *end,
 const HuntClayRound *game, int flash, const HuntClayFlight *white)
{
 ClayDraw state={ot,packet,end};
 ClayDraw *draw=&state;

 if (flash) {
  if (white) mask(draw, white);
  tile(draw, 0,0,256,240,0,0,0,3);

  return;
 }
 if (end - *packet < sizeof(POLY_FT4)) return;
 POLY_FT4 *field=(POLY_FT4 *)*packet;
 *packet+=sizeof(*field);
 setPolyFT4(field);
 setRGB0(field,128,128,128);
 setXY4(field,0,0,320,0,0,240,320,240);
 setUV4(field,0,0,255,0,0,240,255,240);
 field->tpage=getTPage(2,0,CLAY_TEXTURE_X,CLAY_TEXTURE_Y);
 addPrim(&ot[3],field);
 for (int slot=1;slot>=0;slot--) {
  const HuntClayFlight *target=&game->target[slot];
  if (target->live && target->cel>=24 && target->cel<=35) {
   int cel=target->cel-24;
   for (int i=0;i<clay_pixel_counts[cel];i++) {
    const ClayPixel *p=&clay_pixels[cel][i];
    if (target->y+p->y<192)
     tile(draw, target->x+p->x,target->y+p->y,1,1,p->r,p->g,p->b,1);
   }
  }
  int age=59-game->broken[slot];
  if (age>=4 && age<41)
   runs(draw, rom_clay_break[age-4],rom_clay_break_counts[age-4],target->x,target->y,1);
 }
 if (game->phase==CLAY_GO) PATCH(rom_clay_go,107,51,0);
 if (game->phase==CLAY_OVER) PATCH(rom_clay_over,90,43,0);
 if (game->phase==CLAY_BONUS && game->bonus) {
  int value=game->bonus;
  for (int i=4;i>=0;i--) {digit(draw,value%10,112+i*8,72,0);value/=10;}
  PATCH(rom_clay_perfect,99,51,0);
 }
 if (game->phase==CLAY_PAIR || game->phase==CLAY_TALLY || game->phase==CLAY_CLEAR)
  for (int i=0;i<2;i++) if (game->popup[i] && game->broken[i]<=8)
   gfx_draw_duck_score(ot,packet,game->round<6?2:game->round<11?3:5,
    game->target[i].x-8,game->target[i].y-3);
 int score=game->score;
 for (int i=5;i>=0;i--) {digit(draw, score%10,192+i*8,208,0);score/=10;}
 digit(draw, game->round%10,40,192,1);
 if (game->round>=10) digit(draw, (game->round/10)%10,32,192,1);
 for (int i=game->ammo;i<3;i++) tile(draw, 24+i*8,208,8,8,0,0,0,0);
 for (int i=0;i<10;i++) if (game->marks[i]) PATCH(rom_clay_red_mark,96+i*8,208,0);
 for (int i=6;i<hunt_hits_needed((int)game->round);i++) PATCH(rom_clay_minimum,96+i*8,216,0);

}


