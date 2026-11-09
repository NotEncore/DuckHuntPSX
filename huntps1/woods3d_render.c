#include <stdlib.h>
#include <string.h>
#include <psxgte.h>
#include <inline_c.h>
#include "woods3d_render.h"
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
#include "woods3d_approved_assets.h"
#endif

#ifdef HUNTPS1_WOODS_RENDER_O3
#pragma GCC optimize("O3")
#endif

#define RGB(r,g,b) ((r)<<16 | (g)<<8 | (b))
enum { NEAR = 96, FAR = 1950 };
static W3Frame *out;
static W3Camera view;
static int zone, eye_y, sy, cy, sp, cp;
static MATRIX identity = {.m = {{4096,0,0},{0,4096,0},{0,0,4096}}};
static uint16_t foliage_page, foliage_clut;
enum { TWO_SIDED = 1, LEAF = 2, BARK = 4, TRAIL = 6, GROUND = 8, WATER = 10,
 MATERIAL = 14, SECOND_TRI = 16, ROUNDED = 32, WATER_SHORE = 64,
 FLOOR_ORDER = 128, FLOOR_TILE = 256, WET_BANK = 512, WATER_CLEAR = 1024,
 WORLD_GRASS = 2048, GRASS_TALL = 4096, WILD_FLOWER = 8192, WATER_DOUBLE = 16384 };
typedef struct { int sky, horizon, grass, dirt, bark, leaf; } Light;
static const Light lights[5] = {
 {RGB(91,138,165),RGB(208,215,165),RGB(79,116,56),RGB(170,124,77),RGB(139,93,58),RGB(74,119,57)},
 {RGB(78,125,151),RGB(181,204,170),RGB(70,108,54),RGB(151,110,67),RGB(127,87,56),RGB(67,110,59)},
 {RGB(86,150,182),RGB(197,219,189),RGB(91,128,68),RGB(168,131,83),RGB(140,100,60),RGB(86,130,66)},
 {RGB(60,96,126),RGB(134,168,152),RGB(51,84,48),RGB(122,97,67),RGB(101,71,48),RGB(55,93,59)},
 {RGB(128,123,159),RGB(246,191,125),RGB(112,121,59),RGB(190,135,76),RGB(152,91,53),RGB(109,133,62)}
};
static Light light;
#ifdef HUNTPS1_WOODS_ART
static int bird_fade;
#endif
#ifdef HUNTPS1_WOODS_MEADOW
static int dive_sin, dive_cos=4096;
#endif

static int blend(int a, int b, int t)
{
 int r = (((a>>16)&255)*(256-t)+((b>>16)&255)*t)>>8;
 int g = (((a>>8)&255)*(256-t)+((b>>8)&255)*t)>>8;
 int v = ((a&255)*(256-t)+(b&255)*t)>>8;
 return RGB(r,g,v);
}

static void scene_light(int z)
{
#ifdef HUNTPS1_WOODS_SCENE_ONE
 (void)z;
#ifdef HUNTPS1_WOODS_LAKE
 light = (Light){RGB(101,132,158),RGB(174,181,166),RGB(61,76,52),
  RGB(124,103,79),RGB(114,92,76),RGB(78,99,68)};
#else
 light = (Light){RGB(91,132,153),RGB(164,176,151),RGB(68,80,51),
  RGB(130,105,79),RGB(119,98,78),RGB(86,105,66)};
#endif
#else
 /* Match the scripted camera stops and blend continuously along the rail. */
 int n = z < 410 ? 0 : (z-410)/1750;
 if (n >= 4) {
  light = lights[4];
  return;
 }
 int t = z < 410 ? 0 : (z-410)%1750 * 256/1750;
 const Light *a=lights+n, *b=a+1;
 light = (Light){blend(a->sky,b->sky,t),blend(a->horizon,b->horizon,t),
  blend(a->grass,b->grass,t),blend(a->dirt,b->dirt,t),
  blend(a->bark,b->bark,t),blend(a->leaf,b->leaf,t)};
#endif
}

#ifdef HUNTPS1_WOODS_SCENE_ONE
enum { TEXTURE_W = 64, TEXTURE_H = 48 };
static uint16_t entrance_clut[5];
#ifdef HUNTPS1_WOODS_DETAIL
enum { ENTRANCE_GROUND_ROWS = 21 };
static int ground_tints[ENTRANCE_GROUND_ROWS][2];
#endif
enum { BROADLEAF, NEEDLES, FERNS,
#ifdef HUNTPS1_WOODS_ART
 BRAMBLE,
#endif
#ifdef HUNTPS1_WOODS_MARSH
 REEDS,
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 APPROVED_LEAF, GRASS_BLADES=APPROVED_LEAF+5, TREE_IMPOSTOR=GRASS_BLADES+2,
 LAKE_MIST=TREE_IMPOSTOR+5, FLOWER_TILE, CUTOUT_COUNT, CUTOUT_SIZE=64 };
static uint16_t approved_leaf_page, approved_leaf_clut, tree_impostor_clut, mist_clut, flower_clut;
static uint16_t floor_page, floor_clut, wet_bank_clut;
static uint16_t bark_page;
static uint16_t cutout_projection[FAR];
#else
 CUTOUT_COUNT, CUTOUT_SIZE = 64 };
#endif
static uint16_t cutout_page, cutout_clut;
#ifdef HUNTPS1_WOODS_MARSH
static uint16_t reed_page, reed_clut, water_bank_clut;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
static uint16_t water_clear_clut;
#endif
#endif
static uint32_t cutout_texels[CUTOUT_COUNT*CUTOUT_SIZE*CUTOUT_SIZE/8];
#ifdef HUNTPS1_WOODS_DETAIL
typedef struct { int left, top, right, bottom; } FoliageBounds;
static FoliageBounds foliage_bounds[CUTOUT_COUNT];
#endif

static unsigned texture_noise(unsigned x, unsigned y)
{
 unsigned n = x * 374761393u + y * 668265263u;
 n = (n ^ (n >> 13)) * 1274126177u;
 return n ^ (n >> 16);
}

#ifdef HUNTPS1_WOODS_SHOWCASE
enum { WATER_TEX_SIZE=128 };
static uint16_t water_page, water_clut;
static unsigned water_frame, water_scroll;

static void scroll_water(POLY_FT3 *p)
{
 p->v0=(p->v0&WATER_TEX_SIZE)|water_scroll;
 p->v1=(p->v1&WATER_TEX_SIZE)|water_scroll;
 p->v2=(p->v2&WATER_TEX_SIZE)|water_scroll;
}

static void upload_water(void)
{
#ifdef HUNTPS1_WOODS_PACKED_WATER
 static const uint8_t index4[2][24]={
  {1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3},
  {4,4,4,4,4,4,4,4,5,5,6,6,6,6,6,6,6,7,7,7,8,8,8,8}
 };
 static const uint16_t colors[3][16] __attribute__((aligned(4)))={
  {0,0x4a72,0x4e93,0x52b4,0x39ee,0x3e0f,0x3e0f,0x4230,0x4230},
  {0,0x4a72,0x4e93,0x52b4,0x25ab,0x25ab,0x29cc,0x29cc,0x2ded},
  {0,0x4a72,0x4e93,0x52b4,0x4651,0x4a72,0x4a72,0x4e93,0x4e93}
 };
 static uint32_t pixels[WATER_TEX_SIZE*WATER_TEX_SIZE*2/8];
#else
 static uint32_t pixels[WATER_TEX_SIZE*WATER_TEX_SIZE*2/4];
 static uint16_t palette[256] __attribute__((aligned(4)));
#endif

 static int water_generated;
 if (!water_generated) {
  for (unsigned y=0;y<WATER_TEX_SIZE;y++) {
   for (unsigned x=0;x<WATER_TEX_SIZE;x++) {
    int bend=isin(x*32)*4/4096+isin(x*96+y*32)*2/4096;
    int crest=(y+bend+128)&15;
    int slow=texture_noise(x/16,y/7)%24;
    int fleck=texture_noise(x,y)%22;
 #ifdef HUNTPS1_WOODS_MARSH
    int index=65+slow/2+fleck/5+(crest==0 ? 8 : crest<3 ? 3 : 0);
    unsigned drift=(x+bend*2+128)&127;
    unsigned patch=texture_noise(drift/9,y/4)%64+texture_noise(drift/23,y/15)%64;
    if (patch+fleck/3>89) index|=128;
 #else
    int index=65+slow+fleck+(crest==0 ? 46 : crest<3 ? 21 : 0);
    if ((texture_noise(x/7,y)&15)==0) index+=18;
 #endif
    unsigned pixel=y*WATER_TEX_SIZE+x;
 #ifdef HUNTPS1_WOODS_PACKED_WATER
    pixels[pixel/8]|=(uint32_t)index4[index>>7][(index&127)-65]<<((pixel&7)*4);
 #else
    pixels[pixel/4]|=(uint32_t)index<<((pixel&3)*8);
 #endif
   }
  }
  water_generated=1;
 }
#ifdef HUNTPS1_WOODS_PACKED_WATER
 memcpy(pixels+WATER_TEX_SIZE*WATER_TEX_SIZE/8,pixels,sizeof(pixels)/2);
 RECT tex={896,0,WATER_TEX_SIZE/4,WATER_TEX_SIZE*2};
 LoadImage(&tex,pixels);
 DrawSync(0);
#ifdef HUNTPS1_WOODS_WIDE_WATER
 RECT second_tile=tex;
 second_tile.x+=tex.w;
 LoadImage(&second_tile,pixels);
 DrawSync(0);
#endif
 for (int i=0;i<3;i++) {
  /* Keep the CLUT transfers at the SDK's existing 16-word row shape. */
  RECT clut={512,i,16,1};
  LoadImage(&clut,(const uint32_t *)colors[i]);
  DrawSync(0);
 }
 water_page=getTPage(0,0,tex.x,tex.y);
 water_clut=getClut(512,0);
 water_bank_clut=getClut(512,1);
 water_clear_clut=getClut(512,2);
#else
 for (int i=1;i<256;i++) {
#ifdef HUNTPS1_WOODS_MARSH
  int shade=12+(i&127)*14/127;
  if (i&128) shade-=4;
  palette[i]=(shade-1) | (shade<<5) | ((shade-1)<<10);
#else
  int shade=4+i*23/255;
  palette[i]=shade | ((shade+2)<<5) | ((shade+3)<<10);
#endif
 }
 /* Duplicate the tile vertically so an affine polygon can scroll across
    the seam without wrapping between its vertices. */
 memcpy(pixels+WATER_TEX_SIZE*WATER_TEX_SIZE/4,pixels,WATER_TEX_SIZE*WATER_TEX_SIZE);
 RECT tex={896,0,WATER_TEX_SIZE/2,WATER_TEX_SIZE*2};
 RECT clut={512,0,256,1};
 LoadImage(&tex,pixels);
 DrawSync(0);
 LoadImage(&clut,(const uint32_t *)palette);
 DrawSync(0);
 water_page=getTPage(1,0,tex.x,tex.y);
 water_clut=getClut(clut.x,clut.y);
#ifdef HUNTPS1_WOODS_MARSH
 for (int i=128;i<256;i++) {
  int shade=7+(i&127)*12/127;
  palette[i]=(shade-2) | (shade<<5) | ((shade-4)<<10);
 }
 clut.y++;
 LoadImage(&clut,(const uint32_t *)palette);
 DrawSync(0);
 water_bank_clut=getClut(clut.x,clut.y);
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 for (int i=1;i<256;i++) {
  int shade=12+(i&127)*14/127;
  if (i&128) shade-=1;
  palette[i]=(shade-1)|(shade<<5)|((shade-1)<<10);
 }
 clut.y++;
 LoadImage(&clut,(const uint32_t *)palette);
 DrawSync(0);
 water_clear_clut=getClut(clut.x,clut.y);
#endif
#endif
#endif
}
#endif

static void upload_cutouts(void)
{
 static uint16_t palette[16] __attribute__((aligned(4)));
 static const unsigned char lobes[2][7][4] = {
  {{17,32,14,13},{27,20,15,16},{42,23,15,17},{49,35,13,13},
   {37,40,16,14},{23,42,14,12},{32,29,17,16}},
  {{30,15,12,13},{20,29,17,9},{42,29,16,10},{28,39,22,10},
   {44,43,16,8},{16,48,14,7},{34,51,18,8}}
 };
 static int cutouts_generated;
 if (!cutouts_generated) {
 #ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  for (int kind=0; kind<APPROVED_LEAF; kind++) {
 #else
  for (int kind=0; kind<CUTOUT_COUNT; kind++) {
 #endif
 #ifdef HUNTPS1_WOODS_DETAIL
   FoliageBounds bounds={CUTOUT_SIZE-1,CUTOUT_SIZE-1,0,0};
 #endif
   for (int y=0; y<CUTOUT_SIZE; y++) {
    for (int x=0; x<CUTOUT_SIZE; x++) {
     unsigned grain=texture_noise(x,y+kind*64);
     int index=0;
 #ifdef HUNTPS1_WOODS_MARSH
     if (kind == REEDS) {
      for (int stem=0;stem<13;stem++) {
       unsigned shape=texture_noise(stem,19);
       int top=6+shape%25, lean=(int)((shape>>8)%11)-5;
       int dx=x-(2+stem*5+(63-y)*lean/48);
       if (y<top) continue;
       if (y<top+7 && abs(dx)<=1) index=14+grain%2;
       else if (abs(dx)<=1 || (y>top+12 && abs(dx)<10 &&
        ((y+abs(dx)*2+stem*3)%17)<2)) {
        if (index<14) index=3+(63-y)/13+grain%4;
       }
      }
     } else
 #endif
     if (kind == FERNS) {
      int dx=abs(x-31);
      int branch=y+dx*3/4;
      if ((dx < 2 && y > 12) ||
          (branch > 16 && branch < 63 && dx < 9+(branch-16)/2 &&
           ((branch+dx/4)%9 < 3+(int)(grain&1))))
       index=6+(grain%7);
     }
 #ifdef HUNTPS1_WOODS_ART
     else if (kind == BRAMBLE) {
      static const uint8_t tips[][2]={{10,46},{16,33},{23,41},{26,23},
       {33,35},{39,19},{44,31},{52,39},{49,49},{34,51},{21,53}};
      for (unsigned i=0;i<sizeof(tips)/sizeof(tips[0]);i++) {
       int dx=x-tips[i][0], dy=y-tips[i][1];
       int leaf=3*dx*dx+4*dy*dy+2*dx*dy;
       if (leaf < 216-(int)(grain%64))
        index=4+(63-y)/13+grain%5;
      }
     }
 #endif
     else {
      int coverage=0;
      for (int i=0; i<7; i++) {
       const unsigned char *l=lobes[kind][i];
       int dx=x-l[0], dy=y-l[1];
       int d=dx*dx*64/(l[2]*l[2])+dy*dy*64/(l[3]*l[3]);
       if (d < 64 && 64-d > coverage) coverage=64-d;
      }
      if (coverage > (int)(grain%22))
       index=3+(63-y)/15+coverage/18+grain%5;
     }
     unsigned pixel=(kind*CUTOUT_SIZE+y)*CUTOUT_SIZE+x;
     cutout_texels[pixel/8] |= (uint32_t)index << ((pixel%8)*4);
 #ifdef HUNTPS1_WOODS_DETAIL
     if (index) {
      if (x<bounds.left) bounds.left=x;
      if (x>bounds.right) bounds.right=x;
      if (y<bounds.top) bounds.top=y;
      if (y>bounds.bottom) bounds.bottom=y;
     }
 #endif
    }
   }
 #ifdef HUNTPS1_WOODS_DETAIL
   /* Keep one transparent texel of padding at each edge. */
   if (bounds.left>0) bounds.left--;
   if (bounds.top>0) bounds.top--;
   if (bounds.right<CUTOUT_SIZE-1) bounds.right++;
   if (bounds.bottom<CUTOUT_SIZE-1) bounds.bottom++;
   foliage_bounds[kind]=bounds;
 #endif
  }
  cutouts_generated=1;
 }
 for (int i=1; i<16; i++) {
  int g=6+i;
  palette[i]=(g-1) | (g<<5) | ((g-3)<<10);
 }
 /* The framebuffer pair ends at x=319 and the font starts at x=960.
    This page is independent of the material textures at x=768. */
 RECT image={832,0,CUTOUT_SIZE/4,CUTOUT_SIZE*CUTOUT_COUNT};
#ifdef HUNTPS1_WOODS_MARSH
 image.h=REEDS*CUTOUT_SIZE;
#endif
#ifdef HUNTPS1_WOODS_ART
 /* With four cards the image fills the page. Keep its CLUT alongside it. */
 RECT colors={848,192,16,1};
#else
 RECT colors={832,192,16,1};
#endif
 LoadImage(&image,cutout_texels);
 DrawSync(0);
 LoadImage(&colors,(const uint32_t *)palette);
 DrawSync(0);
 cutout_page=getTPage(0,0,832,0);
 cutout_clut=getClut(colors.x,colors.y);
#ifdef HUNTPS1_WOODS_MARSH
 RECT reed_image={384,0,CUTOUT_SIZE/4,CUTOUT_SIZE};
 RECT reed_colors={448,64,16,1};
 palette[14]=25 | (25<<5) | (18<<10);
 palette[15]=30 | (29<<5) | (23<<10);
 LoadImage(&reed_image,cutout_texels+REEDS*CUTOUT_SIZE*CUTOUT_SIZE/8);
 DrawSync(0);
 LoadImage(&reed_colors,(const uint32_t *)palette);
 DrawSync(0);
 reed_page=getTPage(0,0,reed_image.x,reed_image.y);
 reed_clut=getClut(reed_colors.x,reed_colors.y);
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 /* Second VRAM page, clear of the framebuffers, font and water tiles. */
 for (int i=0;i<CUTOUT_COUNT-APPROVED_LEAF;i++) {
  uint32_t *data=cutout_texels+(APPROVED_LEAF+i)*512;
  memcpy(data,approved_leaf_masks[i],2048);
  FoliageBounds bounds={63,63,0,0};
  for (int y=0;y<64;y++) for (int x=0;x<64;x++) {
   int pixel=y*64+x;
   if (!((data[pixel/8]>>((pixel&7)*4))&15)) continue;
   if (x<bounds.left) bounds.left=x;
   if (x>bounds.right) bounds.right=x;
   if (y<bounds.top) bounds.top=y;
   if (y>bounds.bottom) bounds.bottom=y;
  }
  if (bounds.left>0) bounds.left--;
  if (bounds.top>0) bounds.top--;
  if (bounds.right<63) bounds.right++;
  if (bounds.bottom<63) bounds.bottom++;
  foliage_bounds[APPROVED_LEAF+i]=bounds;
  RECT tile={640+(i/4)*16,256+(i%4)*64,16,64};
  LoadImage(&tile,data);
  DrawSync(0);
 }
 for (int i=1;i<16;i++) palette[i]=(i+5)*0x421;
 RECT leaf_colors={704,256,16,1};
 LoadImage(&leaf_colors,(const uint32_t *)palette);
 DrawSync(0);
 approved_leaf_page=getTPage(0,0,640,256);
 approved_leaf_clut=getClut(704,256);
 for (int i=1;i<16;i++) {
  int shade=i&7;
  palette[i]=i<8 ? (7+shade)|((5+shade)<<5)|((3+shade)<<10) :
   (6+shade)|((9+shade)<<5)|((5+shade)<<10);
 }
 leaf_colors.y++;
 LoadImage(&leaf_colors,(const uint32_t *)palette);
 DrawSync(0);
 tree_impostor_clut=getClut(leaf_colors.x,leaf_colors.y);
 for (int i=1;i<16;i++) palette[i]=0x8000|(i+10)*0x421;
 leaf_colors.y++;
 LoadImage(&leaf_colors,(const uint32_t *)palette);
 DrawSync(0);
 mist_clut=getClut(leaf_colors.x,leaf_colors.y);
 static const uint16_t flowers[16] __attribute__((aligned(4)))={
  0,0,0x1567,0x19a9,0x1a0c,0,0,0x4e9a,0x673f,0,0x56b8,0x6f7d,
  0x19b6,0x2e7d,0,0x131e
 };
 leaf_colors.y++;
 LoadImage(&leaf_colors,(const uint32_t *)flowers);
 DrawSync(0);
 flower_clut=getClut(leaf_colors.x,leaf_colors.y);
 for (int z=NEAR;z<FAR;z++) cutout_projection[z]=(174*4096+z/2)/z;
 /* Red, green and blue components in the GPU's 5-bit palette. */
#define SOIL(r,g,b) ((r)|((g)<<5)|((b)<<10))
 static const uint16_t soil_palette[16] __attribute__((aligned(4)))={
  0,SOIL(8,7,5),SOIL(10,9,6),SOIL(12,10,7),SOIL(13,12,8),SOIL(15,13,9),
  SOIL(7,9,5),SOIL(9,11,6),SOIL(10,12,7),SOIL(11,13,7),SOIL(12,14,8),
  SOIL(13,15,9),SOIL(14,16,10),SOIL(13,12,8),SOIL(16,14,10),SOIL(17,15,11)
 };
#undef SOIL
 RECT floor_pixels={512,256,32,128}, floor_colors={544,256,16,1};
 LoadImage(&floor_pixels,forest_floor_texels);
 DrawSync(0);
 LoadImage(&floor_colors,(const uint32_t *)soil_palette);
 DrawSync(0);
 floor_page=getTPage(0,0,512,256);
 floor_clut=getClut(544,256);
 for (int i=1;i<16;i++) {
  int shade=7+(i*10)/15;
  palette[i]=(shade+2)|((shade+1)<<5)|((shade-2)<<10);
 }
 floor_colors.y++;
 LoadImage(&floor_colors,(const uint32_t *)palette);
 DrawSync(0);
 wet_bank_clut=getClut(floor_colors.x,floor_colors.y);
 static uint32_t bark_texels[2048];
 static int bark_generated;
 if (!bark_generated) {
  for (unsigned y=0;y<128;y++) for (unsigned x=0;x<128;x++) {
   unsigned ridge=(x+(y/11+x/19)%5)%19;
   unsigned coarse=texture_noise(x/5,y/19)%5;
   unsigned grain=texture_noise(x,y)%4;
   unsigned shade=ridge<2 ? 2+grain : 5+coarse+grain+(ridge<7 ? 2 : 0);
   unsigned pixel=y*128+x;
   bark_texels[pixel/8]|=shade<<((pixel&7)*4);
  }
  bark_generated=1;
 }
 RECT bark_image={576,256,32,128};
 LoadImage(&bark_image,bark_texels);
 DrawSync(0);
 bark_page=getTPage(0,0,576,256);
#endif
}

void w3_upload_foliage(void)
{
#ifdef HUNTPS1_WOODS_DETAIL
 /* The standalone route samples the same world rows on each pass. */
 static const unsigned char shade_by_row[]={95,70,28,14,56,102,67,25};
 for (int row=0;row<ENTRANCE_GROUND_ROWS;row++) {
  int z=(row-1)*128;
  int shade=8+texture_noise((unsigned)z,11)%30;
  int cover=shade_by_row[((unsigned)z>>7)&7];
  ground_tints[row][0]=blend(blend(RGB(130,105,79),RGB(83,72,54),shade),RGB(62,58,45),cover);
  ground_tints[row][1]=blend(blend(RGB(73,88,56),RGB(40,48,35),shade+10),RGB(46,56,38),cover);
 }
#endif
 static uint32_t texels[5*TEXTURE_W*TEXTURE_H/8];
 /* GPU DMA rounds transfers of this size to 16-word blocks. */
 static uint16_t palette[6][16] __attribute__((aligned(4)));
#ifdef HUNTPS1_WOODS_DETAIL
 static const int tint[5][3] = {{0,1,-1},{1,0,-1},{1,0,-1},{0,1,-2},{0,0,0}};
#else
 static const int tint[5][3] = {{0,1,-1},{1,0,-1},{1,0,-1},{0,1,-2},{-3,1,3}};
#endif
 static int materials_generated;
 for (int material=0; material<5; material++) {
  for (unsigned y=0;!materials_generated && y<TEXTURE_H; y++) {
   for (unsigned x=0; x<TEXTURE_W; x++) {
    int grain=texture_noise(x,y)%5;
    int broad=texture_noise(x/5,y/4)%5;
    int index=3+grain+broad;
    switch (material) {
    case 0:
#ifdef HUNTPS1_WOODS_MEADOW
     index=6+grain/2+((y+(x/9)+((x/17)&1))%13<3)*4;
#else
     index+=texture_noise((x+y/2)/3,y/2)%4;
#endif
     break;
    case 1: {
#ifdef HUNTPS1_WOODS_ART
     int groove=(x+((y/6+x/11)%4))%13;
     index=groove < 2 ? 2+grain/2 : 5+grain+broad/2+(groove<6)*2;
     if ((texture_noise(x/3,y/9)&15)==0) index-=index/3;
#else
     unsigned grain_x=(x+texture_noise(x/7,y/11)%4)%TEXTURE_W;
     index=2+texture_noise(grain_x,1)%10+grain/2;
     if (grain_x%9==0) index/=2;
#endif
     break;
    }
    case 2:
#ifdef HUNTPS1_WOODS_ART
     index=5+grain/2+broad/2;
     if ((texture_noise(x/4,y/3)&7)==0) index+=4;
#else
     index+=texture_noise(x/2,y/2)%4;
     if ((texture_noise(x/3,y/2)&15)==0) index=13;
#endif
     break;
    case 3:
#ifdef HUNTPS1_WOODS_ART
     index=4+broad+(grain>>1);
     if ((x+3*y)%11<2) index+=3;
#else
     index+=texture_noise((x+2*y)/3,y/3)%3;
#endif
     break;
    default:
     index=4+grain+(y%8<2?5:0);
     break;
    }
#ifdef HUNTPS1_WOODS_DETAIL
    if (material == 1) {
     int furrow=(x+((y+x*3)/11)%5)%17;
     if (furrow == 0 || furrow == 1) index=2+grain/2;
     else if (furrow == 2) index=12+grain/2;
    }
    if (material == 2 || material == 3) {
     unsigned leaf=texture_noise(x/7,y/6);
     int px=(int)(x%7)-(int)(leaf%3)-1;
     int py=(int)(y%6)-2;
     if ((leaf&7)<3 && 2*px*px+3*py*py+px*py<15)
      index=7+(leaf>>8)%6+(px==0);
    }
    /* Scene One does not use the water tile. Its slot supplies neutral
       strand and feather detail for both animal palettes. */
    if (material == 4) index=9+grain/2+(texture_noise((x+y/3)/2,y/5)%3);
#endif
    unsigned pixel=(material*TEXTURE_H+y)*TEXTURE_W+x;
    texels[pixel/8] |= (uint32_t)index << ((pixel%8)*4);
   }
  }
  for (int index=1; index<16; index++) {
   int base=10+(index*3)/4;
   int r=base+tint[material][0], g=base+tint[material][1], b=base+tint[material][2];
   palette[material][index]=r | (g<<5) | (b<<10);
  }
  entrance_clut[material]=getClut(768,240+material);
 }
 materials_generated=1;
 RECT pixels={768,0,TEXTURE_W/4,TEXTURE_H*5}, colors={768,240,16,6};
 LoadImage(&pixels,texels);
 DrawSync(0);
 LoadImage(&colors,(const uint32_t *)palette);
 DrawSync(0);
 foliage_page=getTPage(0,0,768,0);
 upload_cutouts();
#ifdef HUNTPS1_WOODS_SHOWCASE
 upload_water();
#endif
}
#else
enum { TEXTURE_W = 32, TEXTURE_H = 32 };
/* Five 32x32 opaque 4-bit materials, one resident page and CLUT. */
void w3_upload_foliage(void)
{
 static uint32_t texels[5*128];
 static uint16_t palette[16] __attribute__((aligned(4)));
 uint32_t seed = 0x567a21d3;
 for (int m = 0; m < 5; m++) {
  for (int y = 0; y < 32; y++) {
   for (int x = 0; x < 32; x++) {
    seed = seed*1664525u + 1013904223u;
    int grain = seed >> 30;
    int i;
    switch (m) {
    case 0: i=3+grain+((x+2*(y/3))%7<3)*3+((y+x/3)%6<2)*2; break;
    case 1: i=2+grain+((x+(y>>3))%9<4)*4+((x+y/12)%7==1)*4; break;
    case 2: i=5+grain+((x/2+y*3)%17<3)*3; break;
    case 3: i=4+grain+((x+y/2)%11<3)*3+((y+x/4)%9<3)*2; break;
    default:i=5+grain+((y+(x/6))%8==2)*5; break;
    }
    texels[(m*32+y)*4+x/8] |= (uint32_t)i << (x%8*4);
   }
  }
 }
 for (int i = 1; i < 16; i++) {
  int g = 9+i;
  palette[i] = g | (g<<5) | (g<<10);
 }
 RECT pixels={768,0,8,160}, colors={768,160,16,1};
 LoadImage(&pixels,texels);
 DrawSync(0);
 LoadImage(&colors,(const uint32_t *)palette);
 DrawSync(0);
 foliage_page=getTPage(0,0,768,0);
 foliage_clut=getClut(768,160);
}
#endif
int w3_path(int z)
{
 int p = (z + 4800) % 4800;
 int t = p < 2400 ? p : 4800 - p;
 return W3_STAGING_SIGN*((t * t / 2400) * (7200 - 2 * t) / 12000);
}

int w3_ground(int z)
{
 int p = (z + 2400) % 2400;
 return (p < 1200 ? p : 2400 - p) / 35;
}
#ifdef HUNTPS1_WOODS_LAKE
static int pond_edge(int z,int right)
{
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 if (right) return 2250;
#endif
#ifdef HUNTPS1_WOODS_MARSH
 static const short shore[][2]={{-195,280},{-285,492},{-510,667},{-558,820},
  {-510,868},{-471,831},{-388,756},{-351,710}};
#else
 static const short shore[][2]={{-254,352},{-420,562},{-542,710},{-553,837},
  {-502,868},{-473,829},{-351,710}};
#endif
 if (z<=W3_POND_NEAR) return shore[0][right];
 if (z>=W3_POND_FAR) return shore[sizeof(shore)/sizeof(shore[0])-1][right];
 int offset=z-W3_POND_NEAR;
 int row=offset/256;
 int span=W3_POND_FAR-W3_POND_NEAR-row*256;
 if (span>256) span=256;
 return shore[row][right]+(shore[row+1][right]-shore[row][right])*(offset%256)/span;
}

#if defined(HUNTPS1_WOODS_APPROVED_ASSETS) && defined(HUNTPS1_WOODS_MARSH)
/* The shallow bank and water mesh share these five seam vertices. */
static const int lake_inlet_depth[] = {0, 57, 31, 69, 0};

static int lake_inlet_x(int seam)
{
 int first = pond_edge(W3_POND_NEAR,0);
 int middle = first+215;
 return seam ? middle+(730-middle)*(seam-1)/3 : first;
}

static int lake_inlet_z(int x)
{
 for (int i=1;i<5;i++) {
  int a=lake_inlet_x(i-1), b=lake_inlet_x(i);
  if (x>=a && x<b)
   return W3_POND_NEAR+lake_inlet_depth[i-1]+
    (lake_inlet_depth[i]-lake_inlet_depth[i-1])*(x-a)/(b-a);
 }
 return W3_POND_NEAR;
}
#endif

int w3_in_pond(int x,int z)
{
#if defined(HUNTPS1_WOODS_APPROVED_ASSETS) && defined(HUNTPS1_WOODS_MARSH)
 if (z<lake_inlet_z(x)) return 0;
#endif
 return z>=W3_POND_NEAR && z<W3_POND_FAR && x>=pond_edge(z,0) && x<=pond_edge(z,1);
}
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
static int bank_floor(int z)
{
 if (z>=W3_POND_NEAR && z<=W3_POND_FAR) return W3_WATER_Y+8;
 if (z>=W3_POND_NEAR-128 && z<W3_POND_NEAR) {
  int start=w3_ground(W3_POND_NEAR-128);
  return start+(W3_WATER_Y+8-start)*(z-(W3_POND_NEAR-128))/128;
 }
 return w3_ground(z);
}

W3Point w3_dry_bank(W3Point p)
{
 if (p.z>=W3_POND_NEAR && p.z<W3_POND_FAR) {
  int limit=pond_edge(p.z,0)-49;
  if (p.x>limit) p.x=limit;
 }
 p.y=bank_floor(p.z);
 return p;
}

W3Point w3_flush_cover(int station, int side)
{
 W3Point p=w3_dry_bank((W3Point){w3_path(station)+side*150,0,station});
 p.x-=39;
 return p;
}
#endif
#endif

static W3Point transform(W3Point p)
{
 int x = p.x - view.x;
 int y = p.y - eye_y;
 int z = p.z - view.z;
 int forward = (z * cy + x * sy) >> 12;
 return (W3Point){(x * cy - z * sy) >> 12,
  (y * cp - forward * sp) >> 12, (forward * cp + y * sp) >> 12};
}

#ifdef HUNTPS1_WOODS_SPLIT_PAN
int w3_camera_depth(W3Point p) { return transform(p).z; }
#endif

static W3Point clip(W3Point a, W3Point b)
{
 int t = NEAR - a.z;
 int dz = b.z - a.z;
 return (W3Point){a.x + (b.x - a.x) * t / dz,
  a.y + (b.y - a.y) * t / dz, NEAR};
}

static int edge(int ax, int ay, int bx, int by, int x, int y)
{
 return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
}

static int channel(int color, int shift, int fog)
{
 int c = (color >> shift) & 255;
 int s = (light.horizon >> shift) & 255;
 return c + (s - c) * fog / 256;
}

static void emit_projected(const uint32_t xy[3],int depth,int color,int target,int flags)
{
 int x0 = (short)xy[0], y0 = (short)(xy[0] >> 16);
 int x1 = (short)xy[1], y1 = (short)(xy[1] >> 16);
 int x2 = (short)xy[2], y2 = (short)(xy[2] >> 16);
 int area = edge(x0,y0,x1,y1,x2,y2);
 if (depth >= FAR || !area || (!(flags & 1) && area > 0) ||
     (x0 < 0 && x1 < 0 && x2 < 0) || (x0 >= 320 && x1 >= 320 && x2 >= 320) ||
     (y0 < 0 && y1 < 0 && y2 < 0) || (y0 >= 240 && y1 >= 240 && y2 >= 240))
  return;
 if (out->count == W3_POLYS) {
  out->overflow++;
  return;
 }
 int bin = 1 + depth / 10;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 if (flags&FLOOR_ORDER) bin+=15;
#endif
 int fog = depth > 420 ? (depth - 420) * 224 / (FAR - 420) : 0;
 int index = out->count++;
 W3Face *f = &out->face[index];
 *f = (W3Face){{x0,x1,x2},{y0,y1,y2},bin,target};
 int r = channel(color,16,fog), g = channel(color,8,fog), b0 = channel(color,0,fog);
#ifdef HUNTPS1_WOODS_ART
 if (bird_fade) {
  int sky_bottom=126+174*sp/cp;
  int y=(y0+y1+y2)/3;
  int sky_t=y <= 0 ? 0 : y >= sky_bottom ? 256 : y*256/sky_bottom;
  int faded=blend(RGB(r,g,b0),blend(light.sky,light.horizon,sky_t),bird_fade);
  r=(faded>>16)&255;
  g=(faded>>8)&255;
  b0=faded&255;
 }
#endif
 int material = flags & MATERIAL;
 if (material) {
  POLY_FT3 *p = &out->poly[index].textured;
  setPolyFT3(p);
  setRGB0(p,r,g,b0);
  setXY3(p,x0,y0,x1,y1,x2,y2);
  int material_index = (material >> 1)-1;
  int v = material_index*TEXTURE_H;
  int bottom = v+TEXTURE_H-1, right = TEXTURE_W-1;
#ifdef HUNTPS1_WOODS_LAKE
  if (flags & SECOND_TRI) {
   setUV3(p,0,bottom,right,v,0,v);
  } else {
   setUV3(p,0,bottom,right,bottom,right,v);
  }
#else
  if (material == LEAF) {
   setUV3(p,0,bottom,right,bottom,right/2,v);
  } else if (flags & SECOND_TRI) {
   setUV3(p,0,bottom,right,v,0,v);
  } else {
   setUV3(p,0,bottom,right,bottom,right,v);
  }
#endif
  p->tpage = foliage_page;
#ifdef HUNTPS1_WOODS_SCENE_ONE
  p->clut = entrance_clut[material_index];
#else
  p->clut = foliage_clut;
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  if (material==BARK) {
   p->tpage=bark_page;
   if (flags&SECOND_TRI) setUV3(p,0,127,127,0,0,0);
   else setUV3(p,0,127,127,127,127,0);
  }
  if (flags&FLOOR_TILE) {
   p->tpage=floor_page;
   p->clut=flags&WET_BANK ? wet_bank_clut : floor_clut;
  }
  if (flags&WORLD_GRASS) {
   p->tpage=approved_leaf_page;
   p->clut=(flags&WILD_FLOWER) ? flower_clut : approved_leaf_clut;
   f->target=(flags&WILD_FLOWER) ? -100-(FLOWER_TILE-GRASS_BLADES) :
    (flags&GRASS_TALL) ? -101 : -100;
  }
#endif
#ifdef HUNTPS1_WOODS_SHOWCASE
  if (material==LEAF) {
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   if (out->water_count<(int)(sizeof(out->water_slots)/sizeof(out->water_slots[0])))
    out->water_slots[out->water_count]=index;
   out->water_count++;
#endif
   int far_edge=WATER_TEX_SIZE-1;
#ifdef HUNTPS1_WOODS_WIDE_WATER
   if (flags&WATER_DOUBLE) far_edge=255;
#endif
   if (flags&SECOND_TRI) setUV3(p,0,WATER_TEX_SIZE,far_edge,0,0,0);
   else setUV3(p,0,WATER_TEX_SIZE,far_edge,WATER_TEX_SIZE,far_edge,0);
   p->tpage=water_page;
   p->clut=water_clut;
#ifdef HUNTPS1_WOODS_MARSH
   if (flags&WATER_SHORE) p->clut=water_bank_clut;
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   if (flags&WATER_CLEAR) p->clut=water_clear_clut;
#endif
   scroll_water(p);
  }
#endif
  addPrim(&out->ot[bin],p);
 } else {
  POLY_F3 *p = &out->poly[index].flat;
  setPolyF3(p);
  setRGB0(p,r,g,b0);
  setXY3(p,x0,y0,x1,y1,x2,y2);
  addPrim(&out->ot[bin],p);
 }
}

static void emit(W3Point a, W3Point b, W3Point c, int color, int target, int flags)
{
 SVECTOR va = {a.x,-a.y,a.z,0}, vb = {b.x,-b.y,b.z,0}, vc = {c.x,-c.y,c.z,0};
 uint32_t xy[3];
 gte_ldv3(&va,&vb,&vc);
 gte_rtpt();
 gte_stsxy3(&xy[0],&xy[1],&xy[2]);
 emit_projected(xy,(a.z+b.z+c.z)/3,color,target,flags);
}

/* Extra lateral clipping keeps wide ground strips inside the GPU's
   legal polygon span without altering the visible part of the mesh. */
static int plane(W3Point p, int side)
{
 return p.z - p.x * side;
}

#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
static void clip_bark(const W3Point *points,int color,int flags)
{
 typedef struct { W3Point p; int u,v; } BarkVertex;
 BarkVertex src[7]={{points[0],0,127},{points[1],127,(flags&SECOND_TRI) ? 0 : 127},
  {points[2],(flags&SECOND_TRI) ? 0 : 127,0}}, dst[7];
 BarkVertex *in=src, *out_vertices=dst;
 int count=3;
 for (int side=-1;side<=1;side++) {
  int next=0;
  for (int i=0;i<count;i++) {
   BarkVertex a=in[i], b=in[(i+1==count) ? 0 : i+1];
   int pa=side ? plane(a.p,side) : a.p.z-NEAR;
   int pb=side ? plane(b.p,side) : b.p.z-NEAR;
   if (pa>=0) out_vertices[next++]=a;
   if ((pa>=0)!=(pb>=0)) {
    int den=pa-pb;
    int z=a.p.z+(b.p.z-a.p.z)*pa/den;
    W3Point p={side ? side*z : a.p.x+(b.p.x-a.p.x)*pa/den,
     a.p.y+(b.p.y-a.p.y)*pa/den,side ? z : NEAR};
    out_vertices[next++]=(BarkVertex){p,a.u+(b.u-a.u)*pa/den,a.v+(b.v-a.v)*pa/den};
   }
  }
  BarkVertex *swap=in;
  in=out_vertices;
  out_vertices=swap;
  count=next;
 }
 for (int i=1;i+1<count;i++) {
  int index=out->count;
  emit(in[0].p,in[i].p,in[i+1].p,color,-1,flags);
  if (out->count!=index)
   setUV3(&out->poly[index].textured,in[0].u,in[0].v,in[i].u,in[i].v,in[i+1].u,in[i+1].v);
 }
}
#endif

static void triangle_camera(const W3Point *a, const W3Point *b, const W3Point *c,
 int color, int target, int flags)
{
 W3Point src[7], dst[7];
 src[0] = *a; src[1] = *b; src[2] = *c;
 int clip_mask[3];
 for (int i = 0; i < 3; i++) {
  W3Point p = src[i];
  clip_mask[i] = (p.z < NEAR) | ((p.x < -p.z) << 1) |
   ((p.x > p.z) << 2) | ((p.z >= FAR) << 3);
 }
 if (clip_mask[0] & clip_mask[1] & clip_mask[2])
  return;
 if (!((clip_mask[0] | clip_mask[1] | clip_mask[2]) & 7)) {
  emit(src[0],src[1],src[2],color,target,flags);
  return;
 }
#ifdef HUNTPS1_WOODS_DETAIL
 int material=flags&MATERIAL;
 if (target == -1 && (material == BARK
#ifdef HUNTPS1_WOODS_LAKE
     || material == LEAF
#endif
 )) {
  unsigned mask=(clip_mask[0]|clip_mask[1]|clip_mask[2])&7;
  /* The GPU can clip modest lateral spans to the draw area itself.
     Keep larger spans and near-plane intersections on the CPU path. */
  if (!(mask&1) &&
      abs(a->x)<2*a->z && abs(a->y)<a->z &&
      abs(b->x)<2*b->z && abs(b->y)<b->z &&
      abs(c->x)<2*c->z && abs(c->y)<c->z) {
   emit(*a,*b,*c,color,target,flags);
   return;
  }
 }
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 if (target==-1 && (flags&MATERIAL)==BARK) {
  clip_bark(src,color,flags);
  return;
 }
#endif
 int n = 0;
 for (int i = 0; i < 3; i++) {
  W3Point start = src[i], end = src[(i+1)%3];
  if (start.z >= NEAR)
   dst[n++] = start;
  if ((start.z >= NEAR) != (end.z >= NEAR))
   dst[n++] = clip(start,end);
 }
 W3Point *vertices = dst;
 for (int side = -1; side <= 1; side += 2) {
  W3Point *result = vertices == dst ? src : dst;
  int m = 0;
  for (int i = 0; i < n; i++) {
   W3Point u = vertices[i], v = vertices[i+1 == n ? 0 : i+1];
   int pu = plane(u,side), pv = plane(v,side);
   if (pu >= 0) result[m++] = u;
   if ((pu >= 0) != (pv >= 0)) {
    int den = pu - pv;
    int z = u.z + (v.z-u.z)*pu/den;
    result[m++] = (W3Point){side*z,u.y+(v.y-u.y)*pu/den,z};
   }
  }
  vertices = result;
  n = m;
 }
 for (int i = 1; i + 1 < n; i++)
  emit(vertices[0],vertices[i],vertices[i+1],color,target,flags & (1|FLOOR_ORDER));
}
static void triangle(W3Point a, W3Point b, W3Point c, int color, int target, int flags)
{
 W3Point points[] = {transform(a),transform(b),transform(c)};
 triangle_camera(points,points+1,points+2,color,target,flags);
}
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
/* Indexed meshes reuse the same projected vertex across adjacent faces.
   Use RTPT once per group of three and keep clipping for intersecting faces. */
static void project_mesh(const W3Point *points,uint32_t *xy,int count)
{
 for (int i=0;i<count;i+=3) {
  W3Point a=points[i], b=points[i+1<count ? i+1 : i], c=points[i+2<count ? i+2 : i];
  SVECTOR va={a.x,-a.y,a.z,0},vb={b.x,-b.y,b.z,0},vc={c.x,-c.y,c.z,0};
  uint32_t result[3];
  gte_ldv3(&va,&vb,&vc);
  gte_rtpt();
  gte_stsxy3(&result[0],&result[1],&result[2]);
  xy[i]=result[0];
  if (i+1<count) xy[i+1]=result[1];
  if (i+2<count) xy[i+2]=result[2];
 }
}

static void mesh_face(const W3Point *p,const uint32_t *xy,int a,int b,int c,int color,int flags)
{
 int za=p[a].z, zb=p[b].z, zc=p[c].z;
 if (za<NEAR || zb<NEAR || zc<NEAR) {
  triangle_camera(p+a,p+b,p+c,color,-1,flags);
  return;
 }
 uint32_t projected[]={xy[a],xy[b],xy[c]};
 emit_projected(projected,(za+zb+zc)/3,color,-1,flags);
}

static void project_quad(const W3Point *a,const W3Point *b,const W3Point *c,
 const W3Point *d,uint32_t xy[4])
{
 SVECTOR v0={a->x,-a->y,a->z,0}, v1={b->x,-b->y,b->z,0};
 SVECTOR v2={c->x,-c->y,c->z,0}, v3={d->x,-d->y,d->z,0};
 gte_ldv3(&v0,&v1,&v2);
 gte_rtpt();
 gte_stsxy3c(xy);
 gte_ldv0(&v3);
 gte_rtps();
 gte_stsxy(xy+3);
}

static int inside_side_planes(const W3Point *p)
{
 return p->z>=NEAR && abs(p->x)<=p->z;
}

static void emit_quad_pair(const W3Point *a,const W3Point *b,const W3Point *c,
 const W3Point *d,int color,int flags)
{
 uint32_t xy[4];
 project_quad(a,b,c,d,xy);
 emit_projected(xy,(a->z+b->z+c->z)/3,color,-1,flags);
 uint32_t other[]={xy[0],xy[2],xy[3]};
 emit_projected(other,(a->z+c->z+d->z)/3,color,-1,flags|SECOND_TRI);
}
#endif
static void quad_camera(const W3Point *a, const W3Point *b, const W3Point *c,
 const W3Point *d, int color, int material)
{
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 if (inside_side_planes(a) && inside_side_planes(b) &&
     inside_side_planes(c) && inside_side_planes(d)) {
  emit_quad_pair(a,b,c,d,color,TWO_SIDED|material);
  return;
 }
#endif
 triangle_camera(a,b,c,color,-1,TWO_SIDED | material);
 triangle_camera(a,c,d,color,-1,TWO_SIDED | material | SECOND_TRI);
}
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
static void floor_quad(const W3Point *a,const W3Point *b,const W3Point *c,
 const W3Point *d,int color,int flip,int flags)
{
 if (a->x>a->z && b->x>b->z && c->x>c->z && d->x>d->z) return;
 if (a->x<-a->z && b->x<-b->z && c->x<-c->z && d->x<-d->z) return;
 typedef struct { W3Point p; int u,v; } FloorVertex;
 int grass=flags&WORLD_GRASS;
 int limit=grass ? 63 : 127;
 int offset_u=0,offset_v=0;
 if (grass) {
  int tile=(flags&WILD_FLOWER) ? FLOWER_TILE-APPROVED_LEAF :
   GRASS_BLADES-APPROVED_LEAF+!!(flags&GRASS_TALL);
  offset_u=(tile/4)*64;
  offset_v=(tile%4)*64;
 }
 const FloorVertex corners[]={{*a,flip,limit},{*b,limit-flip,limit},
  {*c,limit-flip,0},{*d,flip,0}};
 FloorVertex clipped[5];
 int count=0;
 for (int i=0;i<4;i++) {
  FloorVertex p=corners[i], q=corners[(i+1)&3];
  int inside=p.p.z>=NEAR;
  if (inside) clipped[count++]=p;
  if (inside!=(q.p.z>=NEAR)) {
   int t=NEAR-p.p.z, dz=q.p.z-p.p.z;
   clipped[count++]=(FloorVertex){clip(p.p,q.p),p.u+(q.u-p.u)*t/dz,p.v+(q.v-p.v)*t/dz};
  }
 }
 for (int i=1;i+1<count;i++) {
  int index=out->count;
  emit(clipped[0].p,clipped[i].p,clipped[i+1].p,color,-1,
   TWO_SIDED|GROUND|(grass ? 0 : FLOOR_TILE)|flags);
  if (index!=out->count)
   setUV3(&out->poly[index].textured,clipped[0].u+offset_u,clipped[0].v+offset_v,
    clipped[i].u+offset_u,clipped[i].v+offset_v,clipped[i+1].u+offset_u,clipped[i+1].v+offset_v);
 }
}
#endif

static void quad(W3Point a, W3Point b, W3Point c, W3Point d, int color, int material)
{
 W3Point points[] = {transform(a),transform(b),transform(c),transform(d)};
 quad_camera(points,points+1,points+2,points+3,color,material);
}
/* Six shaded faces, split into triangles. Winding faces outward. */
static void box_rot(W3Point p, int x, int y, int z, int color, int target, int s, int c, int material)
{
 W3Point v[] = {{p.x-x,p.y-y,p.z-z},{p.x+x,p.y-y,p.z-z},
  {p.x+x,p.y+y,p.z-z},{p.x-x,p.y+y,p.z-z},
  {p.x-x,p.y-y,p.z+z},{p.x+x,p.y-y,p.z+z},
  {p.x+x,p.y+y,p.z+z},{p.x-x,p.y+y,p.z+z}};
 if (s || c != 4096) {
  for (int i = 0; i < 8; i++) {
   int x0 = v[i].x-p.x, z0 = v[i].z-p.z;
   v[i].x = p.x + ((x0*c + z0*s) >> 12);
   v[i].z = p.z + ((z0*c - x0*s) >> 12);
  }
 }
 int ex = view.x-p.x, ez = view.z-p.z;
 int eye_x = (ex*c-ez*s)>>12, eye_z = (ez*c+ex*s)>>12;
 int eye_h = eye_y-p.y;
 unsigned seen = (eye_z < -z) | ((eye_z > z)<<1) |
  ((eye_x < -x)<<2) | ((eye_x > x)<<3) |
  ((eye_h > y)<<4) | ((eye_h < -y)<<5);
  if (material == BARK || (material & ROUNDED)) {
  /* Taper only the trunk's upper ring. Its old box remains a conservative
     face-culling bound, while screen winding handles slanted surfaces. */
  static const unsigned top[] = {2,3,6,7};
  for (unsigned i = 0; i < sizeof(top)/sizeof(top[0]); i++) {
   W3Point *q=v+top[i];
   int scale=material == BARK ? 2 : 3;
   q->x=p.x+(q->x-p.x)*scale/4;
   if (material == BARK) q->z=p.z+(q->z-p.z)/2;
  }
 }
 static const uint8_t faces[6][4] = {
  {0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}
 };
 static const uint8_t shade[] = {231,205,182,215,255,152};
 unsigned transformed = 0;
 for (int i = 0; i < 6; i++) {
  if (!(seen & (1u << i))) continue;
  const uint8_t *f = faces[i];
  int s = shade[i];
  int tint = RGB(((color>>16)&255)*s/255,((color>>8)&255)*s/255,(color&255)*s/255);
  for (int j = 0; j < 4; j++) {
   unsigned bit = 1u << f[j];
   if (!(transformed & bit)) {
    v[f[j]] = transform(v[f[j]]);
    transformed |= bit;
   }
  }
  triangle_camera(v+f[0],v+f[1],v+f[2],tint,target,material);
  triangle_camera(v+f[0],v+f[2],v+f[3],tint,target,material);
 }
}

static void box(W3Point p, int x, int y, int z, int color, int target)
{
 box_rot(p,x,y,z,color,target,0,4096,0);
}
static void crown(W3Point p, int radius, int height, int color, int material)
{
 W3Point top = {p.x-12,p.y+height,p.z+10};
 W3Point ring[] = {{p.x-radius,p.y,p.z},{p.x,p.y-12,p.z-radius},
  {p.x+radius,p.y,p.z},{p.x,p.y+10,p.z+radius}};
#ifdef HUNTPS1_WOODS_SCENE_ONE
 int lean = (int)(texture_noise((unsigned)p.x,(unsigned)p.z)%41)-20;
 top.x += lean;
 top.y -= height/6;
 ring[0].y += radius/4;
 ring[1].x -= radius/3;
 ring[2].y += radius/6 + lean;
 ring[3].x += radius/4;
#endif
 top = transform(top);
 for (int i = 0; i < 4; i++) ring[i] = transform(ring[i]);
 for (int i = 0; i < 4; i++)
  triangle_camera(ring+i,ring+((i+1)&3),&top,color - i*RGB(4,5,2),-1,TWO_SIDED | material);
}

static W3Point at(W3Point p, int x, int y, int z)
{
 return (W3Point){p.x+x,p.y+y,p.z+z};
}

void w3_begin(W3Frame *frame, W3Camera camera, int site)
{
#ifdef HUNTPS1_WOODS_SHOWCASE
 water_scroll=(++water_frame/3)&(WATER_TEX_SIZE-1);
#endif
 out = frame;
 view = camera;
 zone = site;
 scene_light(camera.z);
 eye_y = w3_ground(camera.z)+166;
#ifdef HUNTPS1_WOODS_CINEMATIC
 eye_y += camera.bob;
#endif
 sy = isin(camera.yaw * 5 / 2); cy = icos(camera.yaw * 5 / 2);
 sp = isin(camera.pitch * 5 / 2); cp = icos(camera.pitch * 5 / 2);
 gte_SetRotMatrix(&identity);
 gte_SetTransMatrix(&identity);
 gte_SetGeomOffset(160,116);
 gte_SetGeomScreen(174);
 frame->count = frame->overflow = frame->active = 0;
 frame->center[0][0] = frame->center[1][0] = -1;
 ClearOTagR(frame->ot,W3_OT);
}

#ifdef HUNTPS1_WOODS_PAN_CACHE
static int pan_frame=-1;
#endif
#ifdef HUNTPS1_WOODS_FAST_SHOOT
static int fast_shoot_view;
void w3_use_fast_shoot_view(int active) { fast_shoot_view=active; }
#endif
static void ground_underlay(int color,uint32_t *ot)
{
 /* A fog-colored base behind the mesh masks subpixel cracks along
    the GPU's independently rasterized ground triangles. */
 int start = 116 + 174*sp/cp;
 if (start < 0) start = 0;
 if (start >= 240) return;
 int bands = sizeof(out->underlay)/sizeof(out->underlay[0]);
 for (int i = 0; i < bands; i++) {
  int y0 = start + (240-start)*i/bands;
  int y1 = start + (240-start)*(i+1)/bands;
  TILE *p = &out->underlay[i];
  int distance = 166*174/(1+(y0+y1)/2-start);
  if (distance > FAR) distance = FAR;
  int fog = distance > 420 ? (distance-420)*224/(FAR-420) : 0;
#ifdef HUNTPS1_WOODS_PAN_CACHE
  if ((pan_frame>=0
#ifdef HUNTPS1_WOODS_LOW_OVERDRAW
       || out->early_backdrop
#endif
#ifdef HUNTPS1_WOODS_FAST_SHOOT
       || fast_shoot_view
#endif
      ) && out->draw.clip.w==320 && out->draw.clip.h==240 &&
      !(out->draw.clip.x&15)) {
   /* Fill uses absolute VRAM coordinates and a 16-pixel X alignment. */
   setFill(p);
   setXY0(p,out->draw.clip.x,out->draw.clip.y+y0);
  } else
#endif
  {
   setTile(p);
   setXY0(p,0,y0);
  }
  setWH(p,320,y1-y0);
  setRGB0(p,channel(color,16,fog),channel(color,8,fog),channel(color,0,fog));
  addPrim(ot,p);
 }
#ifdef HUNTPS1_WOODS_FAST_SHOOT
 if ((fast_shoot_view
#ifdef HUNTPS1_WOODS_LOW_OVERDRAW
      || out->early_backdrop==1
#endif
#ifdef HUNTPS1_WOODS_FAR_LAKE_LOD
      || pan_frame>=0
#endif
     ) && out->draw.clip.w==320 && out->draw.clip.h==240 &&
     !(out->draw.clip.x&15)) {
  /* Fill the sky in RGB555 bands, behind the mountain
     silhouette. The VRAM fill unit avoids full-width Gouraud raster. */
  int height=start+10;
  int bottom=height<240 ? height : 240;
  for (int i=0;i<16;i++) {
   int y0=bottom*i/16, y1=bottom*(i+1)/16;
   if (y0==y1) continue;
   int shade=blend(light.sky,light.horizon,(y0+y1)*128/height);
   TILE *band=&out->sky_bands[i];
   setFill(band);
   setXY0(band,out->draw.clip.x,out->draw.clip.y+y0);
   setWH(band,320,y1-y0);
   setRGB0(band,(shade>>16)&255,(shade>>8)&255,shade&255);
   addPrim(ot,band);
  }
  return;
 }
#endif
 POLY_G4 *sky = &out->sky;
 setPolyG4(sky);
 setXY4(sky,0,0,320,0,0,start+10,320,start+10);
 setRGB0(sky,(light.sky>>16)&255,(light.sky>>8)&255,light.sky&255);
 setRGB1(sky,(light.sky>>16)&255,(light.sky>>8)&255,light.sky&255);
 setRGB2(sky,(light.horizon>>16)&255,(light.horizon>>8)&255,light.horizon&255);
 setRGB3(sky,(light.horizon>>16)&255,(light.horizon>>8)&255,light.horizon&255);
 addPrim(ot,sky);
}
static void far_tree(W3Point p, int trunk, int tall, int radius)
{
 /* The distant silhouette uses three triangles. The near tiers retain
    solid geometry, so the cheap view is only seen through the haze. */
 quad(at(p,-trunk,0,0),at(p,trunk,0,0),at(p,trunk/2,tall,0),
  at(p,-trunk/2,tall,0),light.bark,0);
 triangle(at(p,-radius,tall-96,0),at(p,radius,tall-96,0),at(p,8,tall+145,0),
  light.leaf,-1,TWO_SIDED);
}
static void tree_branch(W3Point p, int side, int width, int length)
{
 W3Point base = at(p,side*width,0,0);
 W3Point tip = at(p,side*length,length/2,12);
 triangle(at(base,0,-9,-8),at(base,0,10,-8),tip,light.bark,-1,TWO_SIDED);
 triangle(at(base,0,10,-8),at(base,0,3,11),tip,
  blend(light.bark,light.horizon,34),-1,TWO_SIDED);
}

static void grass_tuft(W3Point p, int k)
{
 int green = blend(light.grass,light.leaf,160);
 triangle(at(p,-13,0,0),at(p,8,0,2),at(p,-12-k%11,25,4),green,-1,TWO_SIDED);
 triangle(at(p,-3,0,-5),at(p,12,0,6),at(p,20,18+k%9,-4),
  blend(green,light.horizon,30),-1,TWO_SIDED);
}
#ifdef HUNTPS1_WOODS_SCENE_ONE
static void cutout(W3Point p, int width, int height, int color, int kind)
{
 W3Point q=transform(p);
 if (q.z < NEAR || q.z >= FAR) return;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 if (abs(q.x)>q.z+width/2) return;
 int projection=cutout_projection[q.z];
 int x=160+q.x*projection/4096, y=116-q.y*projection/4096;
 int half_w=width*projection/8192, half_h=height*projection/8192;
#else
 int x=160+q.x*174/q.z, y=116-q.y*174/q.z;
 int half_w=width*87/q.z, half_h=height*87/q.z;
#endif
 int l=x-half_w, r=x+half_w, t=y-half_h, b=y+half_h;
#ifdef HUNTPS1_WOODS_DETAIL
 const FoliageBounds *bounds=foliage_bounds+kind;
 int width_px=r-l, height_px=b-t, tile_span=CUTOUT_SIZE-1;
 r=l+width_px*bounds->right/tile_span;
 b=t+height_px*bounds->bottom/tile_span;
 l+=width_px*bounds->left/tile_span;
 t+=height_px*bounds->top/tile_span;
#endif
 if (l==r || t==b || r < 0 || l >= 320 || b < 0 || t >= 240)
  return;
 if (out->count == W3_POLYS) {
  out->overflow++;
  return;
 }
 int i=out->count++, bin=1+q.z/10;
 int fog=q.z > 420 ? (q.z-420)*224/(FAR-420) : 0;
 out->face[i]=(W3Face){{l,r,l},{t,t,b},bin,-2-kind};
 POLY_FT4 *poly=&out->poly[i].cutout;
 int v=kind*CUTOUT_SIZE, last=v+CUTOUT_SIZE-1;
#ifdef HUNTPS1_WOODS_MARSH
 if (kind==REEDS) v=0;
#endif
 int u=0;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 if (kind>=APPROVED_LEAF) {
  u=(kind-APPROVED_LEAF)/4*64;
  v=(kind-APPROVED_LEAF)%4*64;
 }
#endif
 setPolyFT4(poly);
 setXY4(poly,l,t,r,t,l,b,r,b);
#ifdef HUNTPS1_WOODS_DETAIL
 setUV4(poly,u+bounds->left,v+bounds->top,u+bounds->right,v+bounds->top,
  u+bounds->left,v+bounds->bottom,u+bounds->right,v+bounds->bottom);
#else
 setUV4(poly,0,v,CUTOUT_SIZE-1,v,0,last,CUTOUT_SIZE-1,last);
#endif
 setRGB0(poly,channel(color,16,fog),channel(color,8,fog),channel(color,0,fog));
 poly->tpage=cutout_page;
 poly->clut=cutout_clut;
#ifdef HUNTPS1_WOODS_MARSH
 if (kind==REEDS) {
  poly->tpage=reed_page;
  poly->clut=reed_clut;
 }
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 if (kind>=APPROVED_LEAF) {
  poly->tpage=approved_leaf_page;
  poly->clut=kind>=TREE_IMPOSTOR ? tree_impostor_clut : approved_leaf_clut;
 }
 if (kind==LAKE_MIST) {
  poly->tpage=getTPage(0,3,640,256);
  poly->clut=mist_clut;
  setSemiTrans(poly,1);
  out->face[i].depth=W3_OT;
  if (bin>2) bin-=2;
 }
#endif
 addPrim(&out->ot[bin],poly);
}

#ifdef HUNTPS1_WOODS_ART
static void timber(W3Point base, W3Point tip, int width, int end_width, int color)
{
 /* A tapered limb with three distinct planes. The endpoints may lean in
    depth, so forks and roots can cross the camera's view at an angle. */
 W3Point a=at(base,0,width,-width), b=at(base,0,-width,-width);
 W3Point c=at(base,0,0,width);
 W3Point end=at(tip,0,-end_width,-end_width);
 triangle(b,a,end,color,-1,TWO_SIDED|BARK);
 triangle(c,b,end,blend(color,RGB(53,47,40),85),-1,TWO_SIDED|BARK);
}

static void mature_trunk(W3Point base, int tall, int radius, int lean)
{
 static const int ring_x[]={16,8,-8,-16,-8,8};
 static const int ring_z[]={0,14,14,0,-14,-14};
 W3Point rings[3][6];
 for (int j=0;j<3;j++) {
  int width=j==0 ? radius*5/4 : j==1 ? radius*3/4 : radius/4;
  int height=j==0 ? 0 : j==1 ? tall/3 : tall;
  for (int i=0;i<6;i++) {
   int offset=j==0 ? (i&1)*7 : 0;
   rings[j][i]=transform(at(base,ring_x[i]*(width+offset)/16+lean*j/2,
    height,ring_z[i]*(width+offset)/16-j*5));
  }
 }
 for (int i=0;i<6;i++) {
  int next=(i+1)%6;
  int dx=view.x-base.x, dz=view.z-base.z;
  /* Keep grazing sides and account for the lean of the upper ring. */
  if (dx*(ring_x[i]+ring_x[next])+dz*(ring_z[i]+ring_z[next]) < -radius*32)
   continue;
  int color=blend(light.bark,RGB(62,53,42),i*19);
  for (int j=0;j<2;j++) {
   triangle_camera(&rings[j][i],&rings[j][next],&rings[j+1][next],color,-1,BARK);
   triangle_camera(&rings[j][i],&rings[j+1][next],&rings[j+1][i],color,-1,BARK|SECOND_TRI);
  }
 }
}
#endif

static void entrance_tree(W3Point tree, int tall, int trunk, int radius, int k)
{
 int distance=tree.z-view.z;
 int shape=k&1 ? BROADLEAF : NEEDLES;
 int canopy=blend(light.leaf,RGB(103,116,78),(k%5)*42);
 int bark=light.bark;
#ifdef HUNTPS1_WOODS_DETAIL
 if (k%5==2) shape=BRAMBLE;
 canopy=blend(canopy,RGB(61,73,47),(k%4)*27);
 bark=blend(bark,RGB(80,72,57),(k%3)*36);
#endif
 if (distance > 780) {
  quad(at(tree,-trunk,0,0),at(tree,trunk,0,0),at(tree,trunk/3,tall,0),
   at(tree,-trunk/3,tall,0),bark,0);
  cutout(at(tree,0,tall-26,0),radius*2+62,218,canopy,shape);
  return;
 }
#ifdef HUNTPS1_WOODS_ART
 if (distance < 440 && k < 2) {
  int lean=k&1 ? -34 : 40;
  mature_trunk(tree,tall,trunk,lean);
  timber(at(tree,lean/2,tall*2/3,-7),at(tree,-radius,tall-76,33),trunk/2,3,bark);
  if (k < 2)
   timber(at(tree,lean/2,tall*3/4,0),at(tree,radius*3/4,tall-22,-31),trunk/3,2,bark);
  timber(at(tree,0,18,-5),at(tree,-trunk*2,3,-22),trunk/3,2,bark);
 } else {
  box_rot(at(tree,0,tall/2-18,0),trunk,tall/2-18,trunk-2,bark,-1,0,4096,BARK);
 }
#else
 box_rot(at(tree,0,tall/2-18,0),trunk,tall/2-18,trunk-2,bark,-1,0,4096,BARK);
 if (distance < 540) {
  tree_branch(at(tree,0,tall*2/3,0),-1,trunk,83);
  tree_branch(at(tree,0,tall*3/4,0),1,trunk,96);
  triangle(at(tree,-trunk-26,2,-11),at(tree,-7,9,-trunk),at(tree,-trunk/2,52,0),
   bark,-1,TWO_SIDED|BARK);
 }
#endif
 cutout(at(tree,-radius/2,tall-73,19),radius*2,151,canopy,shape);
 cutout(at(tree,radius/2,tall-27,-17),radius*2,180,blend(canopy,light.horizon,20),shape);
#ifdef HUNTPS1_WOODS_CINEMATIC
 if (distance < 640 && k%3 == 0)
#else
 if (distance < 640)
#endif
  cutout(at(tree,k%2 ? -18 : 28,tall+41,36),radius*2,168,
   blend(canopy,light.horizon,32),shape);
}

static void entrance_understory(void)
{
 /* Authored clusters flank the route. The opening above the flush bush
    stays clear of foreground geometry in the shooting view. */
 static const short patches[][3] = {
  {-168,360,54},{-219,445,68},{125,475,51},{-205,610,72},
  {257,650,76},{-182,805,61},{328,900,86},{-235,1010,81}
 };
 for (unsigned i=0;i<sizeof(patches)/sizeof(patches[0]);i++) {
  int z=patches[i][1], r=patches[i][2];
  W3Point p={w3_path(z)+W3_STAGING_SIGN*patches[i][0],w3_ground(z)+6,z};
#ifdef HUNTPS1_WOODS_LAKE
  if (w3_in_pond(p.x,p.z)) continue;
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  if (z>=W3_POND_NEAR-64 && p.x+r>=pond_edge(z,0)-66) continue;
#endif
  if (z < view.z+70 || z > view.z+850) continue;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  /* One anchored shrub avoids the intersecting near-ground leaf cards. */
  int h=r*4/5;
  cutout((W3Point){p.x,bank_floor(z)+h/2,z},r*2,h,RGB(92,111,65),BRAMBLE);
#elif defined(HUNTPS1_WOODS_ART)
  cutout(at(p,r/3,r/2,r/4),r*2,r,RGB(101,121,74),BRAMBLE);
  cutout(at(p,-r/3,r/4,-r/4),r*3/2,r*3/4,RGB(67,85,52),BROADLEAF);
#else
  cutout(at(p,0,r/3,0),r*2,r,RGB(87,109,64),BROADLEAF);
#endif
#ifndef HUNTPS1_WOODS_APPROVED_ASSETS
  cutout(at(p,-r/2,r/4,-r/3),r,r*2/3,RGB(104,121,69),FERNS);
#endif
 }
 for (int z=384; z<=1152; z+=64) {
  if (z < view.z+105 || z > view.z+700) continue;
  int s=((z&64) ? 1 : -1)*W3_STAGING_SIGN;
  int x=w3_path(z)+s*(100+(int)(texture_noise(z,3)%49));
#ifdef HUNTPS1_WOODS_LAKE
  if (w3_in_pond(x,z)) continue;
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  if (z>=W3_POND_NEAR-44 && x+23>=pond_edge(z,0)-66) continue;
  cutout((W3Point){x,bank_floor(z)+13,z},46,31,RGB(96,108,65),FERNS);
#else
  cutout((W3Point){x,w3_ground(z)+13,z},46,31,RGB(96,108,65),FERNS);
#endif
 }
 /* Place low clusters and a second treeline on the visible shoulders.
    The main trail and the launch opening stay clear. */
 static const short background[][4] = {
  {-446,1268,244,238},{342,1304,188,259},{-328,1472,234,309},
  {473,1552,283,267},{-510,1696,274,331},{295,1816,205,288},
  {-234,1880,221,244},{459,2004,269,309}
 };
 for (unsigned i=0;i<sizeof(background)/sizeof(background[0]);i++) {
  int z=background[i][1], width=background[i][2], height=background[i][3];
  W3Point p={w3_path(z)+W3_STAGING_SIGN*background[i][0],w3_ground(z),z};
#ifdef HUNTPS1_WOODS_MEADOW
  if (p.x>-160 && p.x<630) p.x+=p.x<0 ? -205 : 430;
#endif
#ifdef HUNTPS1_WOODS_LAKE
  if (w3_in_pond(p.x,p.z)) continue;
#endif
#ifndef HUNTPS1_WOODS_CINEMATIC
  quad(at(p,-13,0,0),at(p,10,0,0),at(p,4,height,0),at(p,-3,height,0),
   RGB(87,81,65),0);
  cutout(at(p,0,height,0),width,height,RGB(72,93,70),i&1 ? NEEDLES : BROADLEAF);
#endif
  cutout(at(p,W3_STAGING_SIGN*width/3,39,-38),width+49,109,RGB(64,84,57),BROADLEAF);
 }
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 static const short blooms[][4]={
  {-134,375,54,35},{113,522,72,39},{-146,561,61,37},{-21,586,52,27},
  {-309,679,63,42},{-394,800,73,47}
 };
 for (unsigned i=0;i<sizeof(blooms)/sizeof(blooms[0]);i++) {
  const short *p=blooms[i];
  int z=p[1], half=p[2]/2, dz=(i&1) ? 16 : -12;
  if (z<view.z+70 || z>view.z+620) continue;
  int za=z-dz, zb=z+dz, ya=bank_floor(za), yb=bank_floor(zb);
  W3Point a=transform((W3Point){p[0]-half,ya,za});
  W3Point b=transform((W3Point){p[0]+half,yb,zb});
  W3Point c=transform((W3Point){p[0]+half,yb+p[3],zb});
  W3Point d=transform((W3Point){p[0]-half,ya+p[3],za});
  floor_quad(&a,&b,&c,&d,RGB(111,116,105),(i&1)*63,WORLD_GRASS|WILD_FLOWER);
 }
#else
 static const short rocks[][3] = {
  {-136,373,17},{132,529,14},{-162,613,24},{123,714,12},{-152,853,19}
 };
 for (unsigned i=0;i<sizeof(rocks)/sizeof(rocks[0]);i++) {
  int z=rocks[i][1], r=rocks[i][2];
  if (z < view.z+80 || z > view.z+530) continue;
  W3Point p={w3_path(z)+W3_STAGING_SIGN*rocks[i][0],w3_ground(z)+6,z};
#ifdef HUNTPS1_WOODS_LAKE
  if (w3_in_pond(p.x,p.z)) continue;
#endif
  box_rot(p,r,r/2,r*2/3,RGB(94,93,79),-1,1567,3784,ROUNDED);
 }
#ifdef HUNTPS1_WOODS_ART
 W3Point branch={w3_path(632)-W3_STAGING_SIGN*168,w3_ground(632)+19,632};
 timber(branch,at(branch,-W3_STAGING_SIGN*115,-9,53),12,3,RGB(104,83,65));
 timber(at(branch,-W3_STAGING_SIGN*49,-3,20),at(branch,-W3_STAGING_SIGN*73,-3,67),6,1,RGB(112,90,68));
#endif
#endif
}
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
typedef struct {
 W3Point a,b;
 int row_z,z,height,color,flags,flip;
 unsigned seed;
} BankGrass;
enum { BANK_GRASS_CAP=100, WALK_VIS_CAP=85 };
static BankGrass bank_patches[BANK_GRASS_CAP];
static unsigned bank_count;
static uint8_t walk_grass[WALK_VIS_CAP][BANK_GRASS_CAP], walk_grass_count[WALK_VIS_CAP];
static int walk_vis_count, walk_vis_frame=-1;
#ifdef HUNTPS1_WOODS_WALK_CACHE
#if defined(HUNTPS1_WOODS_INTEGRATED)
/* Deluxe keeps the other games resident. Cache terrain rather than the
   full walk ordering table, leaving room for the pan and sniff poses. */
enum { WALK_PACKET_WORDS=82000 };
#elif defined(HUNTPS1_WOODS_PAN_CACHE)
/* The approved oak and pine push the last walk pose past the old arena. */
enum { WALK_PACKET_WORDS=220160 };
#else
enum { WALK_PACKET_WORDS=217088 };
#endif
static uint32_t walk_packets[WALK_PACKET_WORDS];
static unsigned walk_packet_start[WALK_VIS_CAP+1], walk_packet_frames;
volatile unsigned g_woods_walk_cache_bytes, g_woods_walk_cache_frames;
#ifdef HUNTPS1_WOODS_PAN_CACHE
enum { PAN_PACKET_WORDS=44*1024, PAN_FRAMES=44 };
static uint32_t pan_packets[PAN_PACKET_WORDS];
static unsigned pan_packet_start[PAN_FRAMES+1];
static W3Camera pan_cameras[PAN_FRAMES];
static int pan_collecting;
#ifdef HUNTPS1_WOODS_DOG_BATCH
enum { SNIFF_FRAMES=15 };
static unsigned sniff_packet_start[SNIFF_FRAMES+1], sniff_frames;
static W3Camera sniff_cameras[SNIFF_FRAMES];
static int sniff_frame=-1;
void w3_use_sniff_packets(int frame) { sniff_frame=frame; }
#endif
volatile unsigned g_woods_pan_cache_bytes, g_woods_pan_cache_frames;

void w3_use_pan_packets(int frame) { pan_frame=frame; }
#endif
#endif

static void prepare_bank_grass(void)
{
 /* Broad short grass mats on the clearing and narrower taller clumps
    along the bank. The rows overlap in projection instead of forming a path. */
 static const short rows[][4]={
  {296,-405,7,111},{353,-420,8,107},{409,-463,9,101},
  {466,-470,9,103},{520,-481,9,105},{565,-473,9,103},{610,-494,9,109},
  {662,-629,5,87},{716,-655,5,88},{773,-694,5,93},{834,-715,5,92},
  {900,-777,5,99},{973,-814,4,108},{1055,-858,4,111},{1146,-893,4,120}
 };
 for (unsigned row=0;row<sizeof(rows)/sizeof(rows[0]);row++) {
  int base_z=rows[row][0];
  for (int i=0;i<rows[row][2];i++) {
   unsigned seed=texture_noise(row,i);
   int variation=seed%31;
   int x=rows[row][1]+i*rows[row][3]+variation-15;
   int z=base_z+(int)((seed>>10)%77)-38;
   int width=rows[row][3]+18+variation;
   if (z>=W3_POND_NEAR-44 && x+width/2>=pond_edge(z,0)-66) continue;
   int tall=z>=W3_POND_NEAR && ((i+row)&1);
   int height=(tall ? 29 : 13)+variation/2;
   int tint=RGB(94+variation/2,112+variation/2,64+variation/3);
   int dx=width/2, dz=(seed&32) ? width/4 : -width/4;
   int za=z-dz, zb=z+dz;
   int xa=x-dx, xb=x+dx;
   if ((za>=W3_POND_NEAR-44 && xa>=pond_edge(za,0)-66) ||
       (zb>=W3_POND_NEAR-44 && xb>=pond_edge(zb,0)-66)) continue;
   int ya=bank_floor(za), yb=bank_floor(zb);
   if (bank_count<BANK_GRASS_CAP)
    bank_patches[bank_count++]=(BankGrass){{xa,ya,za},{xb,yb,zb},base_z,z,height,tint,
     WORLD_GRASS|(tall ? GRASS_TALL : 0),(seed&64) ? 63 : 0,seed};
  }
 }
}

static int grass_in_range(const BankGrass *p,int z)
{
 return p->row_z>=z+33 && p->row_z<=z+620 &&
  !(p->z>z+420 ? (p->seed&1) : p->seed%5==0);
}

void w3_prepare_walk_visibility(W3Camera (*path)(int),int frames)
{
 if (walk_vis_count) return;
 prepare_bank_grass();
 if (frames>WALK_VIS_CAP) return;
 for (int frame=0;frame<frames;frame++) {
  view=path(frame);
  eye_y=w3_ground(view.z)+166+view.bob;
  sy=isin(view.yaw*5/2); cy=icos(view.yaw*5/2);
  sp=isin(view.pitch*5/2); cp=icos(view.pitch*5/2);
  for (unsigned i=0;i<bank_count;i++) {
   const BankGrass *p=bank_patches+i;
   if (!grass_in_range(p,view.z)) continue;
   W3Point a=transform(p->a), b=transform(p->b);
   W3Point c=transform(at(p->b,0,p->height,0)), d=transform(at(p->a,0,p->height,0));
   if (a.z<NEAR && b.z<NEAR && c.z<NEAR && d.z<NEAR) continue;
   if (a.x>a.z && b.x>b.z && c.x>c.z && d.x>d.z) continue;
   if (a.x<-a.z && b.x<-b.z && c.x<-c.z && d.x<-d.z) continue;
   walk_grass[frame][walk_grass_count[frame]++]=i;
  }
 }
 walk_vis_count=frames;
}

void w3_use_walk_visibility(int frame) { walk_vis_frame=frame; }

static void bank_grass(void)
{
 int cached=walk_vis_frame>=0 && walk_vis_frame<walk_vis_count;
 unsigned count=cached ? walk_grass_count[walk_vis_frame] : bank_count;
 for (unsigned i=0;i<count;i++) {
  unsigned index=cached ? walk_grass[walk_vis_frame][i] : i;
  const BankGrass *p=bank_patches+index;
#ifdef HUNTPS1_WOODS_LOW_OVERDRAW
  /* Retain the ground texture and alternating world-space tufts.
     Use placement parity so camera visibility does not change density. */
  if (index&1) continue;
#else
#ifdef HUNTPS1_WOODS_FAST_PAN
  /* The terrain already supplies continuous grass. Keep alternating
     detail patches during the non-interactive turn into the lake. */
  if ((pan_frame>=0
#ifdef HUNTPS1_WOODS_FAST_SHOOT
       || fast_shoot_view
#endif
      ) && (i&1)) continue;
#endif
#endif
  if (!cached && !grass_in_range(p,view.z)) continue;
  W3Point a=transform(p->a), b=transform(p->b);
  W3Point c=transform(at(p->b,0,p->height,0)), d=transform(at(p->a,0,p->height,0));
  floor_quad(&a,&b,&c,&d,p->color,p->flip,p->flags);
 }
}
#endif

#ifdef HUNTPS1_WOODS_CINEMATIC
#ifdef HUNTPS1_WOODS_SHOWCASE
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
typedef struct {
 const short (*points)[3];
 const unsigned short (*bark)[3];
 const short (*cards)[5];
 int points_count, bark_count, cards_count, height, wood, leaf;
} ApprovedTree;

#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))
#define APPROVED_TREE(name,h,b,l) {tree_##name##_points,tree_##name##_bark,tree_##name##_cards, \
 COUNT_OF(tree_##name##_points),COUNT_OF(tree_##name##_bark),COUNT_OF(tree_##name##_cards),h,b,l}
static const ApprovedTree approved_trees[]={
 APPROVED_TREE(oak,300,RGB(108,84,60),RGB(118,137,79)),
 APPROVED_TREE(pine,336,RGB(98,75,55),RGB(86,110,74)),
 APPROVED_TREE(birch,333,RGB(192,177,146),RGB(136,147,81)),
 APPROVED_TREE(willow,260,RGB(112,95,69),RGB(119,139,79)),
 APPROVED_TREE(alder,276,RGB(98,81,62),RGB(95,126,69))
};
#undef APPROVED_TREE

#ifdef HUNTPS1_WOODS_TREE_STUDY
typedef struct {
 short x, y, z, width, height;
 unsigned char mask, shade;
} StudyCrown;

static void study_tree(W3Point p,int height,int radius,int scale,int flip,int depth,int pine)
{
 /* Clusters follow the main limbs. Width and vertical gaps define each
    silhouette at gameplay resolution, with smaller tips in the foreground. */
 static const StudyCrown crowns[2][7]={
  {{-69,179,20,108,67,3,82},{73,213,-29,118,78,0,100},
   {-49,247,-27,105,68,0,110},{32,272,29,110,66,1,119},
   {-10,288,-13,87,56,3,122},{-105,196,7,59,43,1,96},{86,246,14,65,47,3,112}},
  {{-56,150,19,108,31,2,79},{54,178,-20,109,33,2,89},
   {-49,214,-13,101,34,2,96},{44,247,12,86,31,2,105},
   {-26,276,15,77,31,2,114},{24,302,-16,64,30,2,120},{1,325,4,49,34,2,126}}
 };
 int model_height=pine ? 336 : 300;
 int wood=pine ? RGB(98,77,59) : RGB(112,87,66);
 int lean=flip*(pine ? -radius/4 : radius);
 W3Point fork=at(p,lean/3,height*5/12,0);
 W3Point tip=at(p,lean,height*(pine ? 97 : 88)/100,-8);
 int base=radius*(pine ? 2 : 3)/3, neck=radius/2;
 quad(at(p,-base,0,0),at(p,base,0,0),at(fork,neck,0,0),at(fork,-neck,0,0),wood,BARK);
 quad(at(fork,-neck,0,0),at(fork,neck,0,0),at(tip,2,0,0),at(tip,-2,0,0),wood,BARK);
 if (depth<440) {
  int branch_count=pine ? 4 : 2;
  for (int i=0;i<branch_count;i++) {
   const StudyCrown *c=&crowns[pine][i];
   W3Point limb=at(p,c->x*scale*flip/128,c->y*height/model_height,c->z*scale/128);
   W3Point joint=pine ? at(p,lean/2,limb.y-p.y-height/8,0) : fork;
   timber(joint,limb,pine ? radius/5+2 : radius/3+2,2,wood);
  }
 }
 int count=pine || depth<380 ? 7 : 5;
 for (int i=0;i<count;i++) {
  const StudyCrown *c=&crowns[pine][i];
  int shade=c->shade;
  int tint=pine ? RGB(shade*3/4,shade,shade*4/5) : RGB(shade,shade*9/8,shade*2/3);
  cutout(at(p,c->x*scale*flip/128,c->y*height/model_height,c->z*scale/128),
   c->width*scale/128,c->height*height/model_height,tint,APPROVED_LEAF+c->mask);
 }
}
#endif

static void showcase_tree(W3Point p,int height,int radius,int width,unsigned kind,int mirrored)
{
 static const unsigned char mask[]={0,2,1,4,3};
 const ApprovedTree *tree=approved_trees+kind;
 W3Point center=transform(p);
 int scale_x=width*128/190, flip=mirrored ? -1 : 1;
 if (center.z<NEAR/2 || abs(center.x)>center.z+width/2) return;
 if (center.z>=640) {
  int extent=height*64/54;
  cutout(at(p,0,height/2+height/27,0),extent,extent,RGB(127,128,119),
   TREE_IMPOSTOR+mask[kind]);
  return;
 }
#ifdef HUNTPS1_WOODS_TREE_STUDY
 if (kind==0 || kind==1) {
  study_tree(p,height,radius,scale_x,flip,center.z,kind==1);
  return;
 }
#endif
 if (center.z>=180 && center.z<282 && abs(center.x)<center.z*3/4) {
  W3Point points[240];
  uint32_t screen[240];
  for (int i=0;i<tree->points_count;i++) {
   const short *v=tree->points[i];
   points[i]=transform(at(p,v[0]*scale_x*flip/128,v[1]*height/tree->height,v[2]*scale_x/128));
  }
  project_mesh(points,screen,tree->points_count);
  for (int i=0;i<tree->bark_count;i++) {
   const unsigned short *f=tree->bark[i];
   mesh_face(points,screen,f[0],f[mirrored ? 2 : 1],f[mirrored ? 1 : 2],
    tree->wood,BARK|((i&1) ? SECOND_TRI : 0));
  }
 } else {
  const short *tip=tree->cards[tree->cards_count-1];
  int lean=tip[0]*scale_x*flip/256;
  int lower=center.z<720 ? radius*2/3 : radius/2;
  quad(at(p,-lower,0,0),at(p,lower,0,0),at(p,lean+radius/6,height*4/5,0),
   at(p,lean-radius/6,height*4/5,0),tree->wood,center.z<720 ? BARK : 0);
  if (center.z<600 && (kind==0 || kind==2 || kind>=3)) {
   const short *branch=tree->cards[0];
   int dx=branch[0]*scale_x*flip/128, y=branch[1]*height/tree->height;
   triangle(at(p,-radius/4,height/2,0),at(p,dx,y,0),
    at(p,radius/4,height/2,0),tree->wood,-1,TWO_SIDED);
  }
 }
 int stride=center.z<380 ? 2 : center.z<980 ? 3 : 5;
 for (int i=0;i<tree->cards_count;i+=stride) {
  const short *card=tree->cards[i];
  cutout(at(p,card[0]*scale_x*flip/128,card[1]*height/tree->height,card[2]*scale_x/128),
   card[3]*scale_x/128,card[4]*height/tree->height,tree->leaf,APPROVED_LEAF+mask[kind]);
 }
}
#else
typedef struct {
 signed char x,y,z;
 unsigned char width,height,mask,light;
} TreeCluster;
typedef struct {
 unsigned bark,crown;
 short lean,branch_y,branch_span;
 TreeCluster cluster[5];
} ShoreTree;

static const ShoreTree shore_trees[]={
 /* Oak, pine, birch, willow, elm. Layer dimensions are percentages of crown width/height. */
 {RGB(111,86,62),RGB(77,104,58),32,61,104,
  {{-49,-10,11,112,34,BROADLEAF,0},{43,-7,-24,123,37,BROADLEAF,12},
   {3,12,14,119,39,BRAMBLE,28},{-80,-18,-8,69,23,BRAMBLE,4},{75,4,28,73,27,BROADLEAF,20}}},
 {RGB(96,76,56),RGB(63,89,68),-12,47,74,
  {{0,0,0,130,34,NEEDLES,0},{-14,15,15,103,34,NEEDLES,15},
   {12,30,-16,74,29,NEEDLES,30},{-33,-16,18,101,23,NEEDLES,4},{25,41,8,48,23,NEEDLES,37}}},
 {RGB(179,170,146),RGB(104,125,75),-40,65,70,
  {{-31,1,17,83,34,BRAMBLE,4},{34,9,-21,69,35,BRAMBLE,16},
   {-10,23,13,70,36,BROADLEAF,30},{-55,12,-9,62,23,BRAMBLE,21},{47,24,20,52,26,BRAMBLE,33}}},
 {RGB(110,97,69),RGB(91,113,72),54,58,114,
  {{-36,0,14,138,31,BROADLEAF,8},{53,-10,-16,119,32,BROADLEAF,19},
   {4,14,16,115,33,BRAMBLE,26},{-74,-22,26,77,35,NEEDLES,2},{81,-20,16,76,35,NEEDLES,12}}},
 {RGB(103,85,71),RGB(82,109,62),-30,68,91,
  {{-29,-5,11,106,33,BRAMBLE,4},{39,6,-25,108,42,BROADLEAF,12},
   {-11,20,14,97,33,BROADLEAF,32},{-58,7,20,69,27,BRAMBLE,19},{54,23,17,65,25,BRAMBLE,29}}}
};

static void showcase_tree(W3Point p,int height,int radius,int width,unsigned kind,int mirrored)
{
 const ShoreTree *tree=shore_trees+kind;
 int facing=mirrored ? -1 : 1;
 int distance=p.z-view.z;
 int lean=tree->lean*facing;
 W3Point fork=at(p,lean/2,height*tree->branch_y/100,0);
 if (distance<455) {
  box_rot(at(p,lean/3,height/3,0),radius,height/3,radius*3/4,tree->bark,-1,0,4096,BARK);
  timber(fork,at(p,-width*tree->branch_span*facing/100,height*79/100,35),
   radius/3+2,2,tree->bark);
  timber(fork,at(p,width*facing*3/4,height*96/100,-23),radius/3,2,tree->bark);
  timber(at(p,0,15,-4),at(p,-radius*2*facing,1,-29),radius/4,2,tree->bark);
 } else {
  quad(at(p,-radius,0,0),at(p,radius,0,0),at(p,lean+radius/4,height,0),
   at(p,lean-radius/4,height,0),tree->bark,0);
 }
 int layers=distance<400 ? 5 : distance<785 ? 3 : 2;
 for (int i=0;i<layers;i++) {
  const TreeCluster *cluster=tree->cluster+i;
  W3Point crown=at(p,lean+cluster->x*width*facing/100,
   height*(86+cluster->y)/100,cluster->z*width/100);
  cutout(crown,width*cluster->width/100,height*cluster->height/100,
   blend(tree->crown,light.horizon,cluster->light),cluster->mask);
 }
}
#endif
#endif
static void cinematic_trees(void)
{
 /* The larger trunks frame the approach. Leaning crowns intersect overhead,
    while the opening to the upper right stays clear during the flush. */
 static const short trees[][5] = {
  {-183,390,530,38,157},{201,431,570,43,166},
  {-242,582,470,34,133},{-162,704,524,29,134},{303,637,564,41,165},
  {-376,708,593,44,168},{379,794,507,37,163},{-241,875,530,36,144},
  {298,1046,520,32,145},{-158,1020,421,25,120},{-416,1088,585,37,153},
  {425,1168,510,29,152},{-272,1237,518,26,127},{172,1345,395,25,115},
  {15,1536,410,24,114},{-214,1502,467,28,145},{-448,1489,526,32,160},
  {383,1557,511,33,171},{-62,1808,439,22,149},{230,1740,481,24,141}
 };
 for (unsigned i=0;i<sizeof(trees)/sizeof(trees[0]);i++) {
  int z=trees[i][1], height=trees[i][2];
  int offset=trees[i][0];
#ifdef HUNTPS1_WOODS_ART
  /* Leave a diagonal break through the crowns above the escape path.
     The nearer trunks still enclose the walking view. */
  if (i == 3 || i == 7 || i == 9) offset -= 140;
  if (i == 14 || i == 18) offset += 135;
  if (i == 7 || i == 10 || i == 16) offset -= 190;
  if (i == 9 || i == 12 || i == 15) height -= 145;
#endif
#ifdef HUNTPS1_WOODS_MEADOW
  /* Leave the near woodland intact. Beyond it the right bank opens
     into grass, with these trunks set along the meadow perimeter. */
  if (z>560 && offset<0) offset-=z<1100 ? 165 : 260;
#endif
  W3Point p={w3_path(z)+W3_STAGING_SIGN*offset,w3_ground(z),z};
#ifdef HUNTPS1_WOODS_LAKE
  if (w3_in_pond(p.x,z)) {
   int right=p.x>130;
   p.x=pond_edge(z,right)+(right ? 54 : -54);
  }
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  if (p.x>0 && z>=W3_POND_NEAR-80) continue;
#else
  W3Point q=transform(p);
  if (q.z < 30 || abs(q.x) > q.z+260 || q.z >= FAR) continue;
#endif
#ifdef HUNTPS1_WOODS_SHOWCASE
  showcase_tree(p,height,trees[i][3],trees[i][4]*3/2,i%5,i&1);
#else
  entrance_tree(p,height,trees[i][3],trees[i][4],i);
#endif
 }
#ifdef HUNTPS1_WOODS_SHOWCASE
 static const short grove[][5]={
  {-420,523,520,26,178},{245,562,413,23,145},{-460,689,575,32,184},
  {344,688,429,24,152},{-390,817,493,30,161},{433,869,530,27,156},
  {-512,976,517,23,171},{605,1092,525,26,165},{-605,1170,590,31,190},
  {738,1267,497,24,145},{-658,1375,518,26,181},{860,1480,570,30,174},
  {-652,1560,568,24,185},{949,1650,517,25,165},{-634,1730,487,22,161},
  {-560,1940,432,21,141},{925,1950,495,25,168},
  {-410,2305,285,14,127},{-198,2316,267,15,113},{-8,2310,314,18,135},
  {168,2314,291,15,124},{343,2309,345,18,151},{518,2311,293,16,124},
  {686,2305,310,18,136},{837,2312,324,17,142}};
 for (unsigned i=0;i<sizeof(grove)/sizeof(grove[0]);i++) {
  W3Point p={grove[i][0],w3_ground(grove[i][1]),grove[i][1]};
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  if (p.z>=W3_POND_FAR) continue;
  if (p.x>0 && p.z>=W3_POND_NEAR-80) continue;
#endif
  if (p.z>=W3_POND_FAR) p.y=W3_WATER_Y;
  if (w3_in_pond(p.x,p.z)) continue;
  W3Point q=transform(p);
  if (q.z<50 || q.z>=FAR || abs(q.x)>q.z+230) continue;
  showcase_tree(p,grove[i][2],grove[i][3],grove[i][4],(i*3+1)%5,i&1);
 }
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 static const short inner_woods[][3]={
  {-277,337,442},{-387,436,516},{245,471,485},{-469,516,423},{-319,559,398},
  {-473,623,470},{-579,722,548},{-455,786,404},{-614,864,536},{-562,953,444},
  {-741,1058,566},{-681,1156,495},{-747,1308,430},{-668,1462,465},
  {-752,1563,446},{-702,1668,435},{-824,1729,507},{-788,1872,414},
  {-692,1990,376},{-611,2088,407},{-859,2051,523},{-545,2173,351}
 };
 for (unsigned i=0;i<COUNT_OF(inner_woods);i++) {
#ifdef HUNTPS1_WOODS_LEAN_GROVE
  /* Two overlapping crowns immediately behind the flush-side trees. */
  if (i==4 || i==5) continue;
#endif
  int x=inner_woods[i][0], z=inner_woods[i][1];
  if (z<view.z+44 || z>=view.z+FAR) continue;
  if (z>=W3_POND_NEAR && x>pond_edge(z,0)-55) continue;
  int h=inner_woods[i][2];
  showcase_tree((W3Point){x,bank_floor(z),z},h,17+i%4*4,h/3,(i*3+2)%5,i&1);
 }
#endif
#endif
#ifdef HUNTPS1_WOODS_LEFT_FLUSH
 /* Low, fixed canopy at the mouth of the trail. The camera passes under
    it, so the sky reveal comes from parallax and the rightward turn. */
 cutout((W3Point){-110,317,410},225,125,RGB(70,88,56),BROADLEAF);
 cutout((W3Point){76,340,427},204,124,RGB(84,103,66),BRAMBLE);
#endif
}
#endif
#ifdef HUNTPS1_WOODS_MEADOW
#ifndef HUNTPS1_WOODS_LAKE
static void meadow_pond(void)
{
 static const short shoreline[][2]={{-501,724},{-425,661},{-302,686},{-242,775},
  {-271,907},{-403,979},{-527,924},{-566,808}};
 W3Point center={-400,62,820};
 W3Point q=transform(center);
 if (q.x<-q.z-265 || q.x>q.z+265) return;
 W3Point water[8], bank[8];
 for (int i=0;i<8;i++) {
  W3Point a={shoreline[i][0],62,shoreline[i][1]};
  water[i]=transform(a);
  bank[i]=transform((W3Point){center.x+(a.x-center.x)*6/5,58,center.z+(a.z-center.z)*6/5});
 }
 for (int i=0;i<8;i++) {
  int next=(i+1)&7;
  /* Only the near and trail-facing rim are exposed from this route. */
  if (i<4 || i==7)
   quad_camera(bank+i,bank+next,water+next,water+i,RGB(101,92,65),GROUND);
  triangle_camera(water+i,water+next,&q,RGB(75+i*3,98+i*3,92+i*3),-1,TWO_SIDED|LEAF);
  if (i==1 || i==4 || i==6)
   cutout((W3Point){shoreline[i][0],87,shoreline[i][1]},69,71,RGB(90,107,61),FERNS);
 }
}
#else
static void pond_strip(int front,int rear)
{
 int left=pond_edge(front,0), right=pond_edge(front,1);
 int back_left=pond_edge(rear,0), back_right=pond_edge(rear,1);
 W3Point a[4], b[4];
 for (int i=0;i<4;i++) {
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  int ax=i==0 ? left : i==1 ? left+215 : i==2 ? 730 : right;
  int bx=i==0 ? back_left : i==1 ? back_left+215 : i==2 ? 730 : back_right;
  int az=front;
#ifdef HUNTPS1_WOODS_MARSH
  if (front==W3_POND_NEAR) az=lake_inlet_z(ax);
#endif
  a[i]=transform((W3Point){ax,W3_WATER_Y,az});
  b[i]=transform((W3Point){bx,W3_WATER_Y,rear});
#else
  int fraction=i==0 ? 0 : i==1 ? 58 : i==2 ? 202 : 256;
  a[i]=transform((W3Point){left+(right-left)*fraction/256,W3_WATER_Y,front});
  b[i]=transform((W3Point){back_left+(back_right-back_left)*fraction/256,W3_WATER_Y,rear});
#endif
 }
#ifdef HUNTPS1_WOODS_MARSH
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 int clear=front==W3_POND_NEAR ? WATER_CLEAR : 0;
 if (clear) {
  W3Point bank=transform((W3Point){left,W3_WATER_Y+8,front});
  W3Point water=a[0];
  for (int i=1;i<5;i++) {
   int x=lake_inlet_x(i);
   W3Point next_bank=transform((W3Point){x,W3_WATER_Y+8,front});
   W3Point next_water=transform((W3Point){x,W3_WATER_Y,lake_inlet_z(x)});
   floor_quad(&bank,&next_bank,&next_water,&water,RGB(106,103,84),0,WET_BANK);
   bank=next_bank;
   water=next_water;
  }
 }
 quad_camera(a,a+1,b+1,b,RGB(137,145,138),LEAF|(clear ? clear : WATER_SHORE));
#ifdef HUNTPS1_WOODS_FAR_LAKE_LOD
 /* Open water past the detailed near and bank tiles occupies a thin
    band at these cameras. Keep the world-space shoreline and depth. */
 if ((pan_frame>=0 && front-view.z>=590) ||
     (fast_shoot_view && front-view.z>=720)) {
  quad_camera(a+1,a+3,b+3,b+1,RGB(139,162,152),0);
 } else
#endif
 {
 /* Keep each tile within the GPU's reliable textured span. The old
    single wide quad dropped its texture when clipped on the left pan. */
 for (int lane=1;lane<=2;lane++) {
  int ax=lane==1 ? left+215 : 730, bx=lane==1 ? back_left+215 : 730;
  int end_a=lane==1 ? 730 : right, end_b=lane==1 ? 730 : back_right;
  int count=lane==1 ? 3 : 6;
  W3Point start_a=a[lane], start_b=b[lane];
  for (int i=1;i<=count;i++) {
   /* In this yaw range increasing world x moves away from the right
      clip plane, so no later tile in the lane can come back into view. */
   if (cy>abs(sy) && start_a.x>start_a.z+3 && start_b.x>start_b.z+3) break;
   int x=ax+(end_a-ax)*i/count;
   W3Point next_a=i==count ? a[lane+1] :
    transform((W3Point){x,W3_WATER_Y,clear ? lake_inlet_z(x) : front});
   W3Point next_b=i==count ? b[lane+1] :
    transform((W3Point){bx+(end_b-bx)*i/count,W3_WATER_Y,rear});
#ifdef HUNTPS1_WOODS_WIDE_WATER
   if ((out->early_backdrop==1
#ifdef HUNTPS1_WOODS_FAST_SHOOT
       || (fast_shoot_view && front-view.z>=720)
#endif
       ) &&
       !clear && i<count &&
       inside_side_planes(&start_a) && inside_side_planes(&start_b)) {
    int next=i+1;
    W3Point pair_a=next==count ? a[lane+1] :
     transform((W3Point){ax+(end_a-ax)*next/count,W3_WATER_Y,front});
    W3Point pair_b=next==count ? b[lane+1] :
     transform((W3Point){bx+(end_b-bx)*next/count,W3_WATER_Y,rear});
    if (inside_side_planes(&pair_a) && inside_side_planes(&pair_b)) {
     quad_camera(&start_a,&pair_a,&pair_b,&start_b,RGB(142,148,142),LEAF|WATER_DOUBLE);
     start_a=pair_a;
     start_b=pair_b;
     i++;
     continue;
    }
   }
#endif
   quad_camera(&start_a,&next_a,&next_b,&start_b,RGB(142,148,142),LEAF|clear);
   start_a=next_a;
   start_b=next_b;
  }
 }
 }
#else
 quad_camera(a,a+1,b+1,b,RGB(132,141,130),LEAF|WATER_SHORE);
 quad_camera(a+1,a+2,b+2,b+1,RGB(142,148,142),LEAF);
 quad_camera(a+2,a+3,b+3,b+2,RGB(136,143,134),LEAF);
#endif
#else
 int glint=(front&256) ? 4 : 0;
 int far_shade=front>=1792 ? (front-1792)/12 : 0;
#ifdef HUNTPS1_WOODS_SHOWCASE
 int bank_shade=((front>>7)&3)*3;
 quad_camera(a,a+1,b+1,b,RGB(89-bank_shade,119-bank_shade,104-bank_shade),LEAF);
 quad_camera(a+1,a+2,b+2,b+1,RGB(95+glint,124+glint-far_shade/2,129+glint-far_shade),LEAF);
 quad_camera(a+2,a+3,b+3,b+2,RGB(86-bank_shade,114-bank_shade,105-bank_shade),LEAF);
#else
 quad_camera(a,a+1,b+1,b,RGB(84,117,115),LEAF);
 quad_camera(a+1,a+2,b+2,b+1,RGB(90+glint,126+glint-far_shade/2,133+glint-far_shade),LEAF);
 quad_camera(a+2,a+3,b+3,b+2,RGB(85,119,119),LEAF);
#endif
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 W3Point bank_front=transform((W3Point){left-66,W3_WATER_Y+8,front});
 W3Point bank_rear=transform((W3Point){back_left-66,W3_WATER_Y+8,rear});
 floor_quad(&bank_front,a,b,&bank_rear,RGB(116,111,96),0,WET_BANK);
 if (front-view.z<620) {
  W3Point a0=bank_front, b0=bank_rear;
  for (int i=1;i<=3;i++) {
   W3Point a1=transform((W3Point){left-66-i*230,W3_WATER_Y+8,front});
   W3Point b1=transform((W3Point){back_left-66-i*230,W3_WATER_Y+8,rear});
   floor_quad(&a1,&a0,&b0,&b1,RGB(132-i*7,138-i*7,126-i*6),(i&1)*127,FLOOR_ORDER);
   a0=a1;
   b0=b1;
  }
 } else {
  W3Point a0=transform((W3Point){left-740,W3_WATER_Y+8,front});
  W3Point b0=transform((W3Point){back_left-740,W3_WATER_Y+8,rear});
  quad_camera(&a0,&bank_front,&bank_rear,&b0,RGB(76,92,60),FLOOR_ORDER);
 }
#else
 quad((W3Point){left-41,W3_WATER_Y+8,front},
  (W3Point){left,W3_WATER_Y,front},(W3Point){back_left,W3_WATER_Y,rear},
  (W3Point){back_left-51,W3_WATER_Y+7,rear},RGB(98,100,66),GROUND);
 quad((W3Point){right,W3_WATER_Y,front},(W3Point){right+51,W3_WATER_Y+7,front},
  (W3Point){back_right+38,W3_WATER_Y+9,rear},(W3Point){back_right,W3_WATER_Y,rear},
  RGB(104,106,70),GROUND);
#endif
}

static void pond_reeds(void)
{
#ifdef HUNTPS1_WOODS_MARSH
 for (int i=0;i<19;i++) {
  int z=679+i*77+(i%3)*9;
  int h=85+(i*13)%40;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  cutout((W3Point){pond_edge(z,0)-122,W3_WATER_Y+h/2+6,z},
   105,h,RGB(105,124,91),REEDS);
#else
  cutout((W3Point){pond_edge(z,0)+24,W3_WATER_Y+h/2-3,z},148,h,RGB(119,132,100),REEDS);
#endif
 }
#ifndef HUNTPS1_WOODS_APPROVED_ASSETS
 for (int i=0;i<8;i++) {
  int h=54+(i*17)%28;
  cutout((W3Point){-348+i*146,W3_WATER_Y+h/2,W3_POND_FAR-29-(i%3)*13},
   153,h,RGB(120,131,99),REEDS);
 }
#endif
#else
 static const short beds[][3]={{-233,769,66},{129,770,60},{-301,883,75},
  {445,946,74},{-456,1100,74},{625,1201,93},{779,1480,90}};
 for (unsigned i=0;i<sizeof(beds)/sizeof(beds[0]);i++) {
  W3Point p={beds[i][0],W3_WATER_Y+21,beds[i][1]};
  cutout(p,beds[i][2],68+i%3*11,RGB(98,113,64),FERNS);
 }
#endif
}

#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
static void lake_mist(void)
{
 static const short wisps[][4]={
  {18,1033,448,29},{465,1190,620,25},{-51,1353,559,35},
  {438,1610,687,40},{-92,1824,680,43},{630,1920,700,40}
 };
 for (unsigned i=0;i<COUNT_OF(wisps);i++) {
  const short *w=wisps[i];
  cutout((W3Point){w[0],W3_WATER_Y+w[3]/2+7,w[1]},w[2],w[3],
   RGB(70,79,76),LAKE_MIST);
 }
}
#endif

void w3_pond_ripple(W3Point p,int age)
{
 static const int rim[][2]={{8,0},{6,6},{0,8},{-6,6},{-8,0},{-6,-6},{0,-8},{6,-6}};
 int radius=12+age*3;
 int color=blend(RGB(155,185,178),RGB(75,111,112),age*11);
 W3Point outer[8],inner[8];
 for (int i=0;i<8;i++) {
  outer[i]=transform((W3Point){p.x+rim[i][0]*radius/8,W3_WATER_Y+3,
   p.z+rim[i][1]*radius/8});
  inner[i]=transform((W3Point){p.x+rim[i][0]*(radius-4)/8,W3_WATER_Y+3,
   p.z+rim[i][1]*(radius-4)/8});
 }
 for (int i=0;i<8;i++) {
  int next=(i+1)&7;
  quad_camera(outer+i,outer+next,inner+next,inner+i,color,0);
 }
}
#endif

static void meadow_horizon(void)
{
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 /* Flat distant layers. Their ordering slots sit between the sky and
    all world polygons, so the lake and near woodland mask their bases. */
 static const unsigned char ridge[2][15]={
  {16,20,35,25,43,53,28,42,61,41,32,45,25,36,19},
  {11,16,20,30,22,36,24,19,28,37,30,15,24,17,9}
 };
 int horizon=116+174*sp/cp;
 for (int layer=0;layer<2;layer++) {
  int shift=view.yaw*(layer ? 18 : 16)/64;
  int color=layer ? RGB(119,142,140) : RGB(141,160,161);
  for (int i=0;i<14;i++) {
   int x=-370+i*76-shift, right=x+76;
   if (right<0 || x>=320 || horizon+10<0) continue;
   if (out->count==W3_POLYS) {
    out->overflow++;
    break;
   }
   int index=out->count++;
   out->face[index].depth=W3_OT;
   POLY_F4 *p=&out->poly[index].ridge;
   setPolyF4(p);
   setRGB0(p,(color>>16)&255,(color>>8)&255,color&255);
   setXY4(p,x,horizon-ridge[layer][i],right,horizon-ridge[layer][i+1],
    x,horizon+10,right,horizon+10);
   addPrim(&out->ot[W3_OT-2-layer],p);
  }
 }
 for (int i=0;i<7;i++) {
  int x=-640+i*104, z=W3_POND_FAR-52+i%3*13;
  int h=125+(i*41)%75;
  cutout((W3Point){x,W3_WATER_Y+h/2-6,z},210,h,RGB(99,119,79),
   APPROVED_LEAF+(i%4==0 ? 4 : 3));
  if (!(i&1))
   showcase_tree((W3Point){x+48,W3_WATER_Y-4,z+22},187+i%5*19,19,168,i%5,i&2);
 }
#else
#ifdef HUNTPS1_WOODS_SHOWCASE
 /* A continuous, low shore silhouette sits behind the individually
    placed trees so their crowns read as a single distant woodland. */
#ifdef HUNTPS1_WOODS_MARSH
 static const short shore_ridge[][2]={{-570,172},{-388,184},{-214,178},
  {-32,192},{150,178},{341,185},{505,177},{690,179},{850,167}};
#else
 static const short shore_ridge[][2]={{-570,110},{-388,141},{-214,128},
  {-32,167},{150,142},{341,155},{505,128},{690,146},{850,116}};
#endif
 for (unsigned i=1;i<sizeof(shore_ridge)/sizeof(shore_ridge[0]);i++) {
  int x0=shore_ridge[i-1][0], x1=shore_ridge[i][0], z=W3_POND_FAR-32;
  quad((W3Point){x0,W3_WATER_Y,z},(W3Point){x1,W3_WATER_Y,z},
   (W3Point){x1,shore_ridge[i][1],z},(W3Point){x0,shore_ridge[i-1][1],z},
   RGB(75,93,66),0);
 }
#endif
 static const short copse[][3]={{-230,1990,210},{-48,2110,164},{128,2040,183},
  {304,2190,201},{475,2080,225},{664,2150,158},{800,2060,196}};
 for (unsigned i=0;i<sizeof(copse)/sizeof(copse[0]);i++) {
  W3Point p={copse[i][0],w3_ground(copse[i][1])+102,copse[i][1]};
#ifdef HUNTPS1_WOODS_LAKE
  p.z=W3_POND_FAR+17+(i%3)*20;
#endif
  cutout(p,copse[i][2]+75,copse[i][2],RGB(92,116,85),i&1 ? BROADLEAF : BRAMBLE);
 }
#endif
}
#endif
#endif
typedef struct {
 int x, y;
 W3Point edge[4];
#ifdef HUNTPS1_WOODS_MEADOW
 W3Point meadow;
#endif
#ifdef HUNTPS1_WOODS_SCENE_ONE
 W3Point bank[2];
#endif
} GroundRow;

static GroundRow ground_row(int z)
{
 int x = w3_path(z), y = w3_ground(z);
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 y=bank_floor(z);
#endif
#ifdef HUNTPS1_WOODS_SCENE_ONE
 int left=78+(int)(texture_noise(z,9)%30);
 int right=79+(int)(texture_noise(z,17)%28);
#else
 int left=92, right=92;
#endif
 GroundRow row = {x,y,{
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  transform((W3Point){x-1120,y,z}),
  transform((W3Point){x-left,y,z}),
  transform((W3Point){x+right,y,z}),
  transform((W3Point){x+1120,y,z})
#else
  transform((W3Point){x-1442,y+24+(z&384)/8,z}),
  transform((W3Point){x-left,y,z}),
  transform((W3Point){x+right,y,z}),
  transform((W3Point){x+1442,y+20+((z+128)&384)/8,z})
#endif
 }};
#ifndef HUNTPS1_WOODS_APPROVED_ASSETS
#ifdef HUNTPS1_WOODS_MEADOW
 row.meadow=transform((W3Point){x+510+(z&256)/4,y+25+(z&384)/16,z});
#endif
#ifdef HUNTPS1_WOODS_SCENE_ONE
#ifdef HUNTPS1_WOODS_DETAIL
 static const uint8_t bank_width[8][2]={{107,119},{124,101},{130,108},{108,148},
  {95,159},{120,133},{152,106},{135,100}};
 static const uint8_t bank_height[8][2]={{24,12},{17,20},{12,26},{34,16},
  {40,12},{29,18},{18,32},{16,28}};
 unsigned row_index=((unsigned)z>>7)&7;
 row.bank[0]=transform((W3Point){x-left-bank_width[row_index][0],y+bank_height[row_index][0],z});
 row.bank[1]=transform((W3Point){x+right+bank_width[row_index][1],y+bank_height[row_index][1],z});
#else
 row.bank[0]=transform((W3Point){x-left-114,y+11+z%37,z});
 row.bank[1]=transform((W3Point){x+right+130,y+14+(z+88)%31,z});
#endif
#endif
#endif
 return row;
}
#ifdef HUNTPS1_WOODS_WALK_CACHE
enum { CACHED_WATER = 1 << 8 };
static void restore_packets(const uint32_t *first,const uint32_t *next)
{
 while (next>first) {
  uint32_t header=*--next;
  unsigned words=header&255, bin=header>>16;
  next-=words;
  unsigned i=out->count++;
  uint32_t *packet=(uint32_t *)&out->poly[i];
  out->face[i].depth=W3_OT;
  memcpy(packet+1,next,words*4);
  setlen(packet,words);
  if (header&CACHED_WATER) scroll_water(&out->poly[i].textured);
  addPrim(&out->ot[bin],packet);
 }
}

#ifndef HUNTPS1_WOODS_INTEGRATED
static int restore_walk_packets(void)
{
 if (walk_vis_frame<0 || (unsigned)walk_vis_frame>=walk_packet_frames) return 0;
 ground_underlay(light.grass,&out->ot[W3_OT-1]);
 restore_packets(walk_packets+walk_packet_start[walk_vis_frame],
  walk_packets+walk_packet_start[walk_vis_frame+1]);
 /* Walk frames cannot accept shots and do not enter the stationary cache. */
 out->forest_valid=0;
 return 1;
}
#endif
#endif

void w3_forest(int station, int side, int rustle)
{
#if defined(HUNTPS1_WOODS_WALK_CACHE) && !defined(HUNTPS1_WOODS_INTEGRATED)
 if (!zone && !rustle && restore_walk_packets()) return;
#endif
#ifdef HUNTPS1_WOODS_EARLY_DOG_BACKGROUND
 if (out->early_backdrop) {
  out->backdrop_ot=0xffffff;
  ground_underlay(light.grass,&out->backdrop_ot);
  DrawOTagEnv(&out->backdrop_ot,&out->draw);
 }
#endif
 /* A static camera produces an identical forest. Keep each buffer's
    scenery packets, and restore only its table before adding actors. */
 W3Camera saved = out->forest_camera;
 if (out->forest_valid && saved.x == view.x && saved.z == view.z &&
#ifdef HUNTPS1_WOODS_EARLY_DOG_BACKGROUND
     out->forest_early_backdrop==out->early_backdrop &&
#endif
#ifdef HUNTPS1_WOODS_FAST_SHOOT
     out->forest_fast_shoot == fast_shoot_view &&
#endif
     saved.yaw == view.yaw && saved.pitch == view.pitch &&
#ifdef HUNTPS1_WOODS_CINEMATIC
     saved.bob == view.bob &&
#endif
     out->forest_site == zone && out->forest_station == station &&
     out->forest_side == side && out->forest_rustle == rustle) {
  memcpy(out->ot,out->forest_ot,sizeof(out->ot));
  out->count = out->forest_count;
  out->overflow = out->forest_overflow;
#ifdef HUNTPS1_WOODS_SHOWCASE
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  if (out->water_count<=(int)(sizeof(out->water_slots)/sizeof(out->water_slots[0]))) {
   for (int i=0;i<out->water_count;i++) scroll_water(&out->poly[out->water_slots[i]].textured);
   return;
  }
#endif
  for (int i=0;i<out->count;i++) {
   POLY_FT3 *poly=&out->poly[i].textured;
   if (poly->code==0x24 && poly->tpage==water_page) scroll_water(poly);
  }
#endif
  return;
 }
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 out->water_count=0;
#endif
#ifdef HUNTPS1_WOODS_EARLY_DOG_BACKGROUND
 if (!out->early_backdrop)
#endif
  ground_underlay(light.grass,&out->ot[W3_OT-1]);
#ifdef HUNTPS1_WOODS_PAN_CACHE
 int cached_pan=!zone && !rustle && pan_frame>=0 &&
  (unsigned)pan_frame<g_woods_pan_cache_frames;
#ifdef HUNTPS1_WOODS_INTEGRATED
 int cached_walk=!zone && !rustle && walk_vis_frame>=0 &&
  (unsigned)walk_vis_frame<walk_packet_frames;
#endif
 if (cached_pan) {
  W3Camera expected=pan_cameras[pan_frame];
  cached_pan=expected.x==view.x && expected.z==view.z && expected.yaw==view.yaw &&
   expected.pitch==view.pitch && expected.bob==view.bob;
 }
#ifdef HUNTPS1_WOODS_DOG_BATCH
 int cached_sniff=!zone && !rustle && sniff_frame>=0 && (unsigned)sniff_frame<sniff_frames;
 if (cached_sniff) {
  W3Camera expected=sniff_cameras[sniff_frame];
  cached_sniff=expected.x==view.x && expected.z==view.z && expected.yaw==view.yaw &&
   expected.pitch==view.pitch && expected.bob==view.bob;
 }
#endif
#ifdef HUNTPS1_WOODS_INTEGRATED
 if (cached_walk)
  restore_packets(walk_packets+walk_packet_start[walk_vis_frame],
   walk_packets+walk_packet_start[walk_vis_frame+1]);
 else
#endif
#ifdef HUNTPS1_WOODS_DOG_BATCH
 if (cached_sniff)
  restore_packets(walk_packets+walk_packet_start[walk_packet_frames]+sniff_packet_start[sniff_frame],
   walk_packets+walk_packet_start[walk_packet_frames]+sniff_packet_start[sniff_frame+1]);
 else
#endif
 if (cached_pan)
  restore_packets(pan_packets+pan_packet_start[pan_frame],
   pan_packets+pan_packet_start[pan_frame+1]);
 else {
#endif
 int first = (view.z / 128 - 1) * 128;
 GroundRow row = ground_row(first);
 int ground_distance=FAR;
#ifdef HUNTPS1_WOODS_MEADOW
 ground_distance=1320;
#endif
#ifdef HUNTPS1_WOODS_LAKE
 ground_distance=FAR;
#endif
 for (int z = first, next; z < view.z + ground_distance; z = next) {
#ifdef HUNTPS1_WOODS_CINEMATIC
  next = z + (z-view.z < 720 ? 128 : 256);
#else
  next = z + (z-view.z < 1024 ? 128 : 256);
#endif
#if defined(HUNTPS1_WOODS_APPROVED_ASSETS) && defined(HUNTPS1_WOODS_LAKE)
  if (z<W3_POND_NEAR && next>W3_POND_NEAR) next=W3_POND_NEAR;
#endif
  GroundRow back = ground_row(next);
#ifdef HUNTPS1_WOODS_LAKE
  if (z>=W3_POND_NEAR && z<W3_POND_FAR) {
   pond_strip(z,next<W3_POND_FAR ? next : W3_POND_FAR);
   row=back;
   continue;
  }
#endif
  W3Point *front = row.edge, *rear = back.edge;
  int x = row.x, y = row.y;
#ifdef HUNTPS1_WOODS_DETAIL
  int tint_row=z/128+1;
  if (tint_row<0) tint_row=0;
  if (tint_row>=ENTRANCE_GROUND_ROWS) tint_row=ENTRANCE_GROUND_ROWS-1;
  int dirt=ground_tints[tint_row][0];
  int grass=ground_tints[tint_row][1];
#elif defined(HUNTPS1_WOODS_SCENE_ONE)
  int shade = 8 + texture_noise((unsigned)z,11)%30;
  int dirt = blend(RGB(130,105,79),RGB(83,72,54),shade);
  int grass = blend(RGB(73,88,56),RGB(40,48,35),shade+10);
#else
  int stripe = (z & 128) ? RGB(5,6,3) : 0;
  int dirt = light.dirt + stripe;
  int grass = light.grass + stripe;
#endif
  int near = z-view.z < 960;
#ifdef HUNTPS1_WOODS_MEADOW
  if (z>=896) dirt=blend(dirt,grass,z>=1152 ? 256 : 168);
  if (z-view.z>=1040) {
   int haze=z-view.z>=1250 ? 145 : 65;
   grass=blend(grass,light.horizon,haze);
   dirt=blend(dirt,light.horizon,haze);
  }
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  int litter=blend(grass,RGB(91,82,58),58);
  if (z<W3_POND_NEAR && next>view.z+42) {
   int y0=bank_floor(z), y1=bank_floor(next);
   W3Point a=transform((W3Point){-768,y0,z}), b=transform((W3Point){-768,y1,next});
   for (int tile=0;tile<6;tile++) {
    int edge=-768+(tile+1)*256;
    W3Point c=transform((W3Point){edge,y0,z}), d=transform((W3Point){edge,y1,next});
    int shade=tile<2 ? 113+tile*6 : 126+(tile&1)*4;
    floor_quad(&a,&c,&d,&b,RGB(shade,shade+4,shade-7),((tile+(z>>7))&1)*127,FLOOR_ORDER);
    a=c;
    b=d;
   }
  } else {
   quad_camera(front,front+3,rear+3,rear,litter,FLOOR_ORDER);
  }
#else
  quad_camera(front+1,front+2,rear+2,rear+1,dirt,near ? TRAIL : 0);
#ifdef HUNTPS1_WOODS_SCENE_ONE
  if (near) {
   int verge=blend(grass,dirt,54);
   quad_camera(row.bank,front+1,rear+1,back.bank,verge,GROUND);
   quad_camera(front+2,row.bank+1,back.bank+1,rear+2,verge,GROUND);
   quad_camera(front,row.bank,back.bank,rear,grass,0);
#ifdef HUNTPS1_WOODS_MEADOW
   if (z>=640) {
    int meadow=blend(grass,RGB(115,126,78),83);
    quad_camera(row.bank+1,&row.meadow,&back.meadow,back.bank+1,meadow,GROUND);
    quad_camera(&row.meadow,front+3,rear+3,&back.meadow,
     blend(meadow,light.horizon,28),0);
   } else {
    quad_camera(row.bank+1,front+3,rear+3,back.bank+1,grass,0);
   }
#else
   quad_camera(row.bank+1,front+3,rear+3,back.bank+1,grass,0);
#endif
  } else {
   quad_camera(front,front+1,rear+1,rear,grass,0);
   quad_camera(front+2,front+3,rear+3,rear+2,grass,0);
  }
#else
  quad_camera(front,front+1,rear+1,rear,grass,near ? GROUND : 0);
  quad_camera(front+2,front+3,rear+3,rear+2,grass,near ? GROUND : 0);
#endif
#endif
  row = back;
#ifndef HUNTPS1_WOODS_CINEMATIC
  int k = abs(z / 128);
  if (z < view.z - 64)
   continue;
  for (int s = -1; s <= 1; s += 2) {
   int offset = 230 + (k*163 + s*39 + 256) % 270;
   W3Point tree = {x+s*offset,y,z+52};
   if (abs(z-station) < 275 && (s == side || zone >= 4))
    continue;
   if (zone == 2 && s == side && abs(z-station) < 600)
    continue;
   W3Point camera_tree = transform(tree);
   if (abs(camera_tree.x) > camera_tree.z + 220 || camera_tree.z > FAR + 110) continue;
   int tall = 310 + k*79 % 150;
   int trunk = 24 + k%11, radius = 105+k%4*12;
#ifdef HUNTPS1_WOODS_SCENE_ONE
   entrance_tree(tree,tall,trunk,radius,k);
#else
   if (z-view.z >= 1030) {
    far_tree(tree,trunk,tall,radius);
    continue;
   }
   box_rot(at(tree,0,tall/2,0),trunk,tall/2,trunk-2,light.bark,-1,0,4096,near ? BARK : 0);
   if (z-view.z < 600 && ((k+s+1)&3) == 0)
    tree_branch(at(tree,0,tall*2/3,0),-s,trunk,72+k%24);
   int canopy = blend(light.leaf,light.grass,(k%5)*17);
   crown(at(tree,0,tall-105,0),radius,150+k%3*22,canopy,near ? LEAF : 0);
   if (z-view.z < 1050)
    crown(at(tree,10,tall-35,5),87+k%3*8,130+k%4*11,blend(canopy,light.horizon,48),LEAF);
   if (z-view.z < 530 && ((k+s+1)&3) == 2)
    grass_tuft((W3Point){x+s*(115+k%37),y+3,z+22},k);
   if (k & 1)
    crown(at(tree,-s*62,4,15),56,58,RGB(96,132,56),near ? LEAF : 0);
   else
    box(at(tree,-s*53,8,15),28,12,24,RGB(119,125,104),-1);
#endif
  }
#endif
 }
#ifdef HUNTPS1_WOODS_PAN_CACHE
 }
 if (pan_collecting) return;
#endif
#ifdef HUNTPS1_WOODS_SCENE_ONE
#ifdef HUNTPS1_WOODS_CINEMATIC
 cinematic_trees();
#endif
 entrance_understory();
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 bank_grass();
#endif
#ifdef HUNTPS1_WOODS_MEADOW
#ifdef HUNTPS1_WOODS_LAKE
 pond_reeds();
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
#ifdef HUNTPS1_WOODS_FAST_PAN
 if (pan_frame<0)
#endif
#ifdef HUNTPS1_WOODS_FAST_SHOOT
 if (!fast_shoot_view)
#endif
  lake_mist();
#endif
#else
 meadow_pond();
#endif
 meadow_horizon();
#endif
#endif
 if (zone == 2) {
  int z = station + 180, x = w3_path(z)+side*285, y = w3_ground(z)+18;
  W3Point water[] = {{x-108,y,z-104},{x+119,y,z-78},{x+217,y,z+112},
   {x+104,y,z+290},{x-85,y,z+278},{x-157,y,z+74}};
  W3Point center = {x,y,z+90};
  for (int i=0;i<6;i++) {
   triangle(water[i],water[(i+1)%6],center,RGB(77,134,146)+i*RGB(1,2,2),
    -1,TWO_SIDED|WATER);
  }
  for (int i=0;i<7;i++) {
   W3Point p={x-109+i*24,y-5,z-104+i*4};
   triangle(p,at(p,10,0,4),at(p,4+(i&1)*9,36+i%3*10,0),
    RGB(113,129,61),-1,TWO_SIDED);
  }
 }
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 W3Point bush=w3_flush_cover(station,side);
#else
 W3Point bush = {w3_path(station)+side*150,w3_ground(station),station};
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 cutout(at(bush,-28+rustle,41,-11),115,105,RGB(82,101,62),BROADLEAF);
 cutout(at(bush,26-rustle,46,15),95,111,RGB(99,116,73),BRAMBLE);
 cutout(at(bush,2,31,-32),105,77,RGB(73,92,56),NEEDLES);
#elif defined(HUNTPS1_WOODS_SCENE_ONE)
 cutout(at(bush,-38+rustle,41,-11),148,113,RGB(91,107,65),BROADLEAF);
 cutout(at(bush,39-rustle,46,15),146,121,RGB(101,116,73),
#ifdef HUNTPS1_WOODS_ART
  BRAMBLE
#else
  BROADLEAF
#endif
 );
 cutout(at(bush,2,31,-32),129,84,RGB(79,96,56),NEEDLES);
#else
 crown(at(bush,-44+rustle,8,10),69,100,RGB(99,137,62),LEAF);
 crown(at(bush,36-rustle,6,-15),70,117,RGB(112,151,67),LEAF);
 crown(at(bush,3,18,31),83,135,RGB(85,126,55),LEAF);
#endif
#ifdef HUNTPS1_WOODS_PAN_CACHE
 out->forest_valid = !cached_pan
#ifdef HUNTPS1_WOODS_INTEGRATED
  && !cached_walk
#endif
#ifdef HUNTPS1_WOODS_DOG_BATCH
  && !cached_sniff
#endif
  ;
#else
 out->forest_valid = 1;
#endif
 out->forest_site = zone;
 if (!out->forest_valid)
  return;
 memcpy(out->forest_ot,out->ot,sizeof(out->ot));
 out->forest_camera = view;
#ifdef HUNTPS1_WOODS_EARLY_DOG_BACKGROUND
 out->forest_early_backdrop=out->early_backdrop;
#endif
#ifdef HUNTPS1_WOODS_FAST_SHOOT
 out->forest_fast_shoot=fast_shoot_view;
#endif
 out->forest_station = station;
 out->forest_side = side;
 out->forest_rustle = rustle;
 out->forest_count = out->count;
 out->forest_overflow = out->overflow;
}

#ifdef HUNTPS1_WOODS_WALK_CACHE
static unsigned prepare_packets(W3Frame *scratch,W3Camera (*path)(int),int frames,
 int station,int side,uint32_t *packets,unsigned capacity,unsigned *starts)
{
 unsigned saved_water=water_frame, used=0, completed=0;
 uintptr_t frame_base=(uintptr_t)scratch&0xffffff;
 uintptr_t poly_base=(uintptr_t)&scratch->poly[0]&0xffffff;
 uintptr_t poly_end=poly_base+sizeof(scratch->poly);
 for (int frame=0;frame<frames;frame++) {
#ifdef HUNTPS1_WOODS_PAN_CACHE
  w3_use_walk_visibility(pan_collecting ? -1 : frame);
#else
  w3_use_walk_visibility(frame);
#endif
  w3_begin(scratch,path(frame),0);
#ifdef HUNTPS1_WOODS_FAR_LAKE_LOD
  /* Collect the same water LOD that the pan would submit live. */
  if (pan_collecting) pan_frame=frame;
#endif
  w3_forest(station,side,0);
  unsigned start=used;
  int valid=!scratch->overflow;
  for (int bin=W3_OT-1;bin>=0 && valid;bin--) {
   uintptr_t stop=bin ? ((uintptr_t)&scratch->ot[bin-1]&0xffffff) : 0xffffff;
   uintptr_t address=getaddr(&scratch->ot[bin]);
   unsigned visited=0;
   while (address!=stop) {
    if (address<frame_base || address>=frame_base+sizeof(*scratch) ||
        address&3 || visited++>=W3_POLYS+8) {
     valid=0;
     break;
    }
    const uint32_t *packet=(const uint32_t *)(address|0x80000000u);
    if (address>=poly_base && address<poly_end) {
     unsigned words=getlen(packet);
     if (!words || words>=sizeof(scratch->poly[0])/4 ||
         used+words+1>capacity) {
      valid=0;
      break;
     }
     memcpy(packets+used,packet+1,words*4);
     used+=words;
     const POLY_FT3 *texture=(const POLY_FT3 *)packet;
     unsigned water=texture->code==0x24 && texture->tpage==water_page ? CACHED_WATER : 0;
     packets[used++]=(bin<<16)|water|words;
    }
    address=getaddr(packet);
   }
  }
  if (!valid) {
   used=start;
   break;
  }
  starts[++completed]=used;
 }
 scratch->forest_valid=0;
 water_frame=saved_water;
 water_scroll=(saved_water/3)&(WATER_TEX_SIZE-1);
#ifdef HUNTPS1_WOODS_FAR_LAKE_LOD
 pan_frame=-1;
#endif
 w3_use_walk_visibility(-1);
 return completed;
}

void w3_prepare_walk_packets(W3Frame *scratch,W3Camera (*path)(int),int frames,int station,int side)
{
 static int prepared;
 if (prepared || frames>WALK_VIS_CAP) return;
 prepared=1;
#ifdef HUNTPS1_WOODS_INTEGRATED
 pan_collecting=1;
#endif
 walk_packet_frames=prepare_packets(scratch,path,frames,station,side,
  walk_packets,WALK_PACKET_WORDS,walk_packet_start);
#ifdef HUNTPS1_WOODS_INTEGRATED
 pan_collecting=0;
#endif
 g_woods_walk_cache_frames=walk_packet_frames;
 g_woods_walk_cache_bytes=walk_packet_start[walk_packet_frames]*4;
}

#ifdef HUNTPS1_WOODS_PAN_CACHE
void w3_prepare_pan_packets(W3Frame *scratch,W3Camera (*path)(int),int frames,int station,int side)
{
 if (frames>PAN_FRAMES) return;
 pan_collecting=1;
 g_woods_pan_cache_frames=prepare_packets(scratch,path,frames,station,side,
  pan_packets,PAN_PACKET_WORDS,pan_packet_start);
 for (unsigned i=0;i<g_woods_pan_cache_frames;i++) pan_cameras[i]=path(i);
 g_woods_pan_cache_bytes=pan_packet_start[g_woods_pan_cache_frames]*4;
 pan_collecting=0;
}
#ifdef HUNTPS1_WOODS_DOG_BATCH
void w3_prepare_sniff_packets(W3Frame *scratch,W3Camera (*path)(int),int frames,int station,int side)
{
 if (frames>SNIFF_FRAMES) return;
 unsigned used=walk_packet_start[walk_packet_frames];
 pan_collecting=1;
 sniff_frames=prepare_packets(scratch,path,frames,station,side,
  walk_packets+used,WALK_PACKET_WORDS-used,sniff_packet_start);
 for (unsigned i=0;i<sniff_frames;i++) sniff_cameras[i]=path(i);
 pan_collecting=0;
}
#endif
#endif
#endif

#ifdef HUNTPS1_WOODS_ART
typedef struct { short z, y, width, height; } BodyRing;

/* Authored cross sections share vertices across faces. Yaw is applied
   before projection so the visible silhouette remains the pick mesh. */
static void animal_body(W3Point p, const BodyRing *rings, int count,
 int s, int c, int top, int bottom, int target)
{
 static const int side[]={0,-7,-7,0,7,7};
 static const int up[]={8,4,-4,-8,-4,4};
 W3Point points[5*6];
 for (int j=0;j<count;j++) {
  const BodyRing *r=rings+j;
  for (int i=0;i<6;i++) {
   int x=side[i]*r->width/7;
#ifdef HUNTPS1_WOODS_MEADOW
   int y=r->y+up[i]*r->height/8;
   int vertical=(y*dive_cos+r->z*dive_sin)>>12;
   int forward=(r->z*dive_cos-y*dive_sin)>>12;
   points[j*6+i]=transform(at(p,(x*c+forward*s)>>12,vertical,
    (forward*c-x*s)>>12));
#else
   points[j*6+i]=transform(at(p,(x*c+r->z*s)>>12,r->y+up[i]*r->height/8,
    (r->z*c-x*s)>>12));
#endif
  }
 }
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 uint32_t screen[5*6];
 int inside=1;
 for (int i=0;i<count*6;i++)
  inside &= inside_side_planes(points+i) && points[i].z<FAR;
 if (inside) project_mesh(points,screen,count*6);
#endif
 for (int i=0;i<6;i++) {
  int next=(i+1)%6;
  int color=blend(top,bottom,(8-(up[i]+up[next])/2)*16);
  for (int j=1;j<count;j++) {
   int a=(j-1)*6+i, b=j*6+i, c=j*6+next, d=(j-1)*6+next;
#ifdef HUNTPS1_WOODS_DETAIL
   int material=WATER;
#else
   int material=0;
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   if (inside) {
    uint32_t first[]={screen[a],screen[b],screen[c]};
    uint32_t second[]={screen[a],screen[c],screen[d]};
    emit_projected(first,(points[a].z+points[b].z+points[c].z)/3,color,target,material);
    emit_projected(second,(points[a].z+points[c].z+points[d].z)/3,color,target,material|SECOND_TRI);
    continue;
   }
#endif
   triangle_camera(points+a,points+b,points+c,color,target,material);
   triangle_camera(points+a,points+c,points+d,color,target,material ? material|SECOND_TRI : 0);
  }
 }
}
#endif

#ifdef HUNTPS1_WOODS_MEADOW
static W3Point duck_joint(W3Point p,int x,int y,int z,int s,int c)
{
 int vertical=(y*dive_cos+z*dive_sin)>>12;
 int forward=(z*dive_cos-y*dive_sin)>>12;
 return at(p,(x*c+forward*s)>>12,vertical,(forward*c-x*s)>>12);
}
#endif

#ifdef HUNTPS1_WOODS_DETAIL
static void dog_leg(W3Point p, int x, int z, int stride, int h, int pose, int s, int c)
{
 int front=z>0;
 int swing=stride>=0 ? isin(stride*256)/512 : 0;
 int lift=stride>=0 && stride<8 ? isin(stride*256)/460 : 0;
 int joint=z+(front ? -7 : 10)+swing;
 int paw=z+swing*2;
 if (pose==2) { joint+=front ? 8 : -9; paw+=front ? 17 : -18; lift=12; }
 if (pose==6) joint+=front ? -4 : 6;
 W3Point shape[]={{x-6,39+h,z-8},{x+6,39+h,z+8},
  {x+4,20+h/2+lift/2,joint+5},{x-4,20+h/2+lift/2,joint-5},
  {x-4,3+lift,paw-4},{x+4,3+lift,paw+8}};
 for (int i=0;i<6;i++) {
  W3Point q=shape[i];
  shape[i]=transform(at(p,(q.x*c+q.z*s)>>12,q.y,(q.z*c-q.x*s)>>12));
 }
 int upper=front ? RGB(174,126,78) : RGB(150,102,65);
 int lower=RGB(198,153,102);
 quad_camera(shape,shape+1,shape+2,shape+3,upper,0);
 quad_camera(shape+3,shape+2,shape+5,shape+4,lower,0);
}
#endif

#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
static W3Point dog_clip_point(const SVECTOR *v,W3Point origin,int s,int c)
{
 int x=v->vx,y=v->vy,z=v->vz;
 int forward=(z*c-x*s)>>12;
 return at(origin,(x*c+z*s)>>12,(y*cp-forward*sp)>>12,(forward*cp+y*sp)>>12);
}

static void approved_dog(W3Point p,int gait,int pose,int heading,int tick)
{
 enum { DOG_POINTS=COUNT_OF(springer_pose[0]), DOG_PADDED=(DOG_POINTS+2)/3*3 };
#ifdef HUNTPS1_WOODS_DOG_BATCH
 static uint8_t swim_groups[DOG_PADDED/3];
 static uint16_t swim_faces[COUNT_OF(springer_face)];
 static unsigned swim_count;
 if (!swim_count) {
  for (unsigned i=0;i<COUNT_OF(springer_face);i++) {
   const unsigned short *face=springer_face[i];
   if (!face[5]) continue;
   swim_faces[swim_count++]=i;
   for (int j=0;j<3;j++) swim_groups[face[j]/3]=1;
  }
 }
#endif
 SVECTOR local[DOG_PADDED] __attribute__((aligned(4)));
 uint32_t screen[DOG_PADDED], depth_z[DOG_PADDED];
 int angle=heading-view.yaw*5/2;
 int c=icos(angle), s=isin(angle), swim=pose==7;
 int walk=gait>=0 ? isin(gait*256) : 0;
 int blend=pose==1 ? (tick<7 ? tick*4096/7 : 4096) :
  (pose==8 || pose==10) ? (tick<9 ? tick*4096/9 : 4096) : abs(walk);
 int ear_delay=pose==1 && tick<9 ? blend-(tick>2 ? (tick-2)*4096/7 : 0) : 0;
 int shape=pose==8 || pose==10 ? 4 : pose==1 ? 1 : walk<0 ? 3 : 2;
 int source=0;
 int bob=swim ? 1+isin(tick*190)/2200 : abs(walk)/1850;
 int tilt=pose==4 ? isin(tick*380)/110 : pose==2 ? 35 : pose==6 ? -31 : 0;
 int wag=isin((gait>=0 ? gait : tick)*(pose==8 || pose==10 ? 340 : 215));
 int nod_angle=pose==10 && tick>=12 && tick<45 ? (4096-icos((tick-12)*385))/68 : 0;
 int nod_s=isin(nod_angle), nod_c=icos(nod_angle);
 if (pose==9) {
  static const signed char jump[][3]={{0,0,0},{7,5,0},{11,6,61},
   {16,7,12},{21,8,-55},{24,8,-68}};
  int frame=1;
  while (frame<5 && tick>jump[frame][0]) frame++;
  const signed char *a=jump[frame-1], *b=jump[frame];
  blend=(tick-a[0])*4096/(b[0]-a[0]);
  source=a[1];
  shape=b[1];
  tilt=a[2]+(b[2]-a[2])*blend/4096;
 }
 if (swim) {
  p.y=W3_WATER_Y;
  blend=0;
 }
 W3Point origin=transform(p);
 if (origin.z<0 || origin.z>=FAR+90 || abs(origin.x)>origin.z+120) return;
 int fog=origin.z>420 ? (origin.z-420)*224/(FAR-420) : 0;
 uint8_t colors[3][32];
 for (int i=0;i<32;i++) {
  colors[0][i]=channel(i<<19,16,fog);
  colors[1][i]=channel(i<<11,8,fog);
  colors[2][i]=channel(i<<3,0,fog);
 }
 for (unsigned i=0;i<DOG_POINTS;i++) {
#ifdef HUNTPS1_WOODS_DOG_BATCH
  if (swim && !swim_groups[i/3]) continue;
#endif
  const signed char *a=springer_pose[source][i], *b=springer_pose[shape][i];
#ifdef HUNTPS1_WOODS_DOG_BATCH
  const signed char *end=blend==4096 ? b : a;
  int x=end[0], y=end[1], z=end[2];
  if (blend>0 && blend<4096) {
   x+=(b[0]-a[0])*blend/4096;
   y+=(b[1]-a[1])*blend/4096;
   z+=(b[2]-a[2])*blend/4096;
  }
#else
  int x=a[0]+(b[0]-a[0])*blend/4096;
  int y=a[1]+(b[1]-a[1])*blend/4096;
  int z=a[2]+(b[2]-a[2])*blend/4096;
#endif
  y+=ear_delay*springer_ears[i]/131072;
  z-=ear_delay*springer_ears[i]/114688;
  if (z < -42) x+=wag*(-z-42)*(-z-42)/((pose==8 || pose==10) ? 98304 : 245760);
  if (nod_angle && springer_nod[i]) {
   int forward=z-42, up=y-59;
   int dy=((up*nod_c-forward*nod_s)>>12)-up;
   int dz=((forward*nod_c+up*nod_s)>>12)-forward;
   y+=dy*springer_nod[i]/255;
   z+=dz*springer_nod[i]/255;
  }
  y+=bob+z*tilt/256;
  if (swim) {
   y-=48;
   if (y<0) y=0;
  }
  local[i]=(SVECTOR){x,y,z,0};
 }
 for (unsigned i=DOG_POINTS;i<DOG_PADDED;i++) local[i]=(SVECTOR){0};
 MATRIX dog_matrix={
  .m={{c,0,s},{-(s*sp>>12),-cp,c*sp>>12},{-(s*cp>>12),sp,c*cp>>12}},
  .t={origin.x,-origin.y,origin.z}
 };
 gte_SetRotMatrix(&dog_matrix);
 gte_SetTransMatrix(&dog_matrix);
 for (unsigned i=0;i<DOG_PADDED;i+=3) {
#ifdef HUNTPS1_WOODS_DOG_BATCH
  if (swim && !swim_groups[i/3]) continue;
#endif
  gte_ldv3c(local+i);
  gte_rtpt();
  gte_stsxy3c(screen+i);
  gte_stsz3c(depth_z+i);
 }
 gte_SetRotMatrix(&identity);
 gte_SetTransMatrix(&identity);
#ifdef HUNTPS1_WOODS_DOG_BATCH
 unsigned face_count=swim ? swim_count : COUNT_OF(springer_face);
 for (unsigned i=0;i<face_count;i++) {
  const unsigned short *f=springer_face[swim ? swim_faces[i] : i];
#else
 for (unsigned i=0;i<COUNT_OF(springer_face);i++) {
  const unsigned short *f=springer_face[i];
  if (swim && !f[5]) continue;
#endif
  int rgb=f[3];
  int za=depth_z[f[0]], zb=depth_z[f[1]], zc=depth_z[f[2]];
  if (za<NEAR || zb<NEAR || zc<NEAR) {
   if (za<NEAR && zb<NEAR && zc<NEAR) continue;
   int color=RGB((rgb&31)*8,((rgb>>5)&31)*8,(rgb>>10)*8);
   W3Point a=dog_clip_point(local+f[0],origin,s,c);
   W3Point b=dog_clip_point(local+f[1],origin,s,c);
   W3Point d=dog_clip_point(local+f[2],origin,s,c);
   triangle_camera(&a,&b,&d,color,-1,f[4]);
   continue;
  }
  int x0=(short)screen[f[0]], y0=(short)(screen[f[0]]>>16);
  int x1=(short)screen[f[1]], y1=(short)(screen[f[1]]>>16);
  int x2=(short)screen[f[2]], y2=(short)(screen[f[2]]>>16);
  int area=edge(x0,y0,x1,y1,x2,y2);
  if (!area || (!f[4] && area>0) ||
      (x0<0 && x1<0 && x2<0) || (x0>=320 && x1>=320 && x2>=320) ||
      (y0<0 && y1<0 && y2<0) || (y0>=240 && y1>=240 && y2>=240))
   continue;
  int depth=(za+zb+zc)/3;
  if (depth>=FAR) continue;
  if (out->count==W3_POLYS) {
   out->overflow++;
   continue;
  }
  /* Game F never draws the dog on a frame that accepts shots. */
  POLY_F3 *poly=&out->poly[out->count++].flat;
  setPolyF3(poly);
  setRGB0(poly,colors[0][rgb&31],colors[1][(rgb>>5)&31],colors[2][rgb>>10]);
  setXY3(poly,x0,y0,x1,y1,x2,y2);
  addPrim(&out->ot[1+depth/10],poly);
 }
}
#endif

void w3_dog(W3Point p, int gait, int pose, int heading, int pose_tick)
{
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 approved_dog(p,gait,pose,heading,pose_tick);
#else
 int stride = gait < 0 ? 4 : gait % 16;
 int step = (stride < 8 ? stride : 16 - stride) - 4;
 int nod = pose_tick & 15;
 nod = nod < 8 ? nod : 16-nod;
 int head = pose == 1 ? 32+nod : pose == 4 ? 66+nod*2 : pose == 3 ? 77 : 66;
 int h = pose == 2 ? 8 : 0;
 int ear_swing = gait >= 0 ? step/2 : pose == 1 || pose == 4 ? nod-4 : 0;
#ifdef HUNTPS1_WOODS_LEFT_FLUSH
 if (pose == 5) head=76;
 if (pose == 6) { head=53; h=-7; }
 if (pose == 2) head=69;
#endif
#ifdef HUNTPS1_WOODS_DETAIL
 if (gait>=0) h+=abs(step)/2;
 if (pose==1) head+=nod/2;
#endif
 int s = isin(heading), c = icos(heading);
#ifdef HUNTPS1_WOODS_LAKE
 if (pose==7) {
  int bob=isin(pose_tick*190)/1280;
#ifdef HUNTPS1_WOODS_SHOWCASE
  static const BodyRing swim_head[]={{20,55,11,8},{40,66,16,13},
   {56,65,15,12},{69,60,10,7},{81,59,6,4}};
  animal_body(at(p,0,bob,0),swim_head,5,s,c,RGB(107,69,43),RGB(213,172,121),-1);
#else
  static const BodyRing swim_head[]={{27,58,9,6},{47,67,17,14},
   {61,65,13,10},{78,60,7,5}};
  animal_body(at(p,0,bob,0),swim_head,4,s,c,RGB(136,84,51),RGB(204,159,102),-1);
#endif
  box_rot(at(p,s*77/4096,61+bob,c*77/4096),6,4,6,RGB(39,31,27),-1,s,c,ROUNDED);
  for (int side=-1;side<=1;side+=2) {
   int x=side*16;
   box_rot(at(p,(x*c+41*s)/4096,59+bob,(41*c-x*s)/4096),
    5,9,8,RGB(79,48,32),-1,s,c,ROUNDED);
   W3Point tip=at(p,(side*46*c-37*s)/4096,56,(-37*c-side*46*s)/4096);
   W3Point inner=at(p,(side*36*c-26*s)/4096,55,(-26*c-side*36*s)/4096);
   triangle(at(p,s*45/4096,54,c*45/4096),tip,inner,
    RGB(142,174,170),-1,TWO_SIDED);
  }
  return;
 }
#endif
 #define DOG_M(dx,dy,dz,w,h0,d,tint,shape) box_rot( \
  at(p,((dx)*c+(dz)*s)>>12,dy,((dz)*c-(dx)*s)>>12),w,h0,d,tint,-1,s,c,shape)
 #define DOG(dx,dy,dz,w,h0,d,tint) DOG_M(dx,dy,dz,w,h0,d,tint,0)
#ifdef HUNTPS1_WOODS_ART
#ifdef HUNTPS1_WOODS_SHOWCASE
 static const BodyRing torso[]={{-47,42,7,8},{-33,44,17,18},
  {-5,46,20,19},{20,48,21,21},{42,55,9,14}};
 BodyRing face[]={{30,head-6,7,10},{44,head+2,15,15},
  {56,head+2,15,13},{70,head-4,10,7},{82,head-5,5,4}};
 animal_body(at(p,0,h,0),torso,5,s,c,RGB(83,54,39),RGB(189,142,92),-1);
 animal_body(at(p,0,h,0),face,5,s,c,RGB(142,92,58),RGB(220,177,122),-1);
#else
 static const BodyRing torso[]={{-42,47,5,7},{-25,47,19,20},
  {8,48,22,21},{42,55,7,11}};
 BodyRing face[]={{32,head+3,5,7},{49,head+4,17,16},
  {63,head-1,13,10},{80,head-4,3,3}};
 animal_body(at(p,0,h,0),torso,4,s,c,RGB(113,70,42),RGB(209,155,92),-1);
 animal_body(at(p,0,h,0),face,4,s,c,RGB(161,102,60),RGB(225,177,118),-1);
#endif
#else
 DOG_M(0,46+h,0,23,22,37,RGB(173,113,64),ROUNDED);
 DOG_M(0,55+h,-11,22,14,23,RGB(89,54,36),ROUNDED);
 DOG_M(0,55+h,27,17,20,19,RGB(198,135,77),ROUNDED);
#endif
 for (int side = -1; side <= 1; side += 2) {
#ifdef HUNTPS1_WOODS_DETAIL
  int foot_phase=gait<0 ? -1 : (gait+(side<0 ? 8 : 0))&15;
  dog_leg(p,side*15,25,foot_phase,h,pose,s,c);
  dog_leg(p,side*15,-26,foot_phase<0 ? -1 : (foot_phase+8)&15,h,pose,s,c);
#else
  int movement = pose == 1 || pose == 3 ? 0 : side*step;
  int front = movement > 0 ? movement : 0;
  int back = movement < 0 ? -movement : 0;
  DOG(side*15,17+h+front,23+movement*3,7,18,9,RGB(211,151,81));
  DOG(side*15,17+h+back,-23-movement*3,8,18,10,RGB(189,120,64));
#endif
#ifdef HUNTPS1_WOODS_SHOWCASE
  int ear_x=side*17;
  W3Point root=at(p,(ear_x*c+45*s)>>12,head+7+h,(45*c-ear_x*s)>>12);
  W3Point fold=at(p,(ear_x*c+55*s)>>12,head-11+h,(55*c-ear_x*s)>>12);
  W3Point tip=at(p,((ear_x+side*3)*c+(39+ear_swing)*s)>>12,
   head-26+h,((39+ear_swing)*c-(ear_x+side*3)*s)>>12);
  triangle(root,fold,tip,RGB(70,48,36),-1,TWO_SIDED|WATER);
  triangle(at(root,-s*8/4096,-3,-c*8/4096),root,tip,RGB(89,59,41),-1,TWO_SIDED|WATER);
#else
  DOG_M(side*18,head-10+h,43+ear_swing,6,23,9,RGB(73,42,27),ROUNDED);
#endif
  DOG(side*11,head+6+h,59,3,4,2,RGB(33,28,22));
 }
#ifndef HUNTPS1_WOODS_ART
 DOG_M(0,head+h,44,17,16,19,RGB(193,127,65),ROUNDED);
 DOG_M(0,head-5+h,64,13,9,13,RGB(235,184,116),ROUNDED);
#endif
 DOG(0,head-3+h,76,7,5,5,RGB(37,29,23));
 int wag = gait >= 0 ? step : pose == 4 ? nod-4 : 0;
 DOG(wag*2,65+h,-42,6,8,16,RGB(137,78,44));
 DOG(wag*4,73+h,-54,5,8,9,RGB(137,78,44));
 #undef DOG
 #undef DOG_M
#endif
}
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
static void folded_mallard(W3Point p,int heading)
{
 static const signed char model[][3]={
  {0,3,-36},{-12,3,-23},{12,3,-23},{0,13,-22},
  {-17,4,-1},{17,4,-1},{0,19,-3},{0,0,0},
  {-11,5,18},{11,5,18},{0,14,17},
  {-5,8,25},{5,8,25},{0,14,26},
  {-7,9,33},{7,9,33},{0,18,33},{0,5,34},
  {-4,9,39},{4,9,39},{-5,7,47},{5,7,47},
  {-15,10,-12},{15,10,-12},{-17,8,4},{17,8,4},
  {-15,6,-17},{15,6,-17},{-18,5,-2},{18,5,-2}
 };
 static const struct {uint8_t a,b,c; uint32_t tint;} face[]={
  {0,1,3,RGB(100,83,64)},{0,3,2,RGB(119,104,82)},
  {1,4,6,RGB(146,131,104)},{1,6,3,RGB(105,91,72)},
  {2,3,6,RGB(123,109,86)},{2,6,5,RGB(158,144,116)},
  {1,7,4,RGB(137,130,115)},{2,5,7,RGB(159,150,130)},
  {4,8,10,RGB(165,151,129)},{4,10,6,RGB(142,127,101)},
  {5,6,10,RGB(164,147,116)},{5,10,9,RGB(178,164,141)},
  {4,7,8,RGB(129,118,101)},{5,9,7,RGB(156,143,120)},
  {8,9,10,RGB(134,81,59)},{8,10,11,RGB(138,90,68)},
  {9,12,10,RGB(156,105,74)},{10,12,13,RGB(171,128,97)},
  {10,13,11,RGB(201,191,163)},{11,13,14,RGB(43,82,57)},
  {12,15,13,RGB(49,99,69)},{13,16,14,RGB(42,105,73)},
  {13,15,16,RGB(57,130,92)},{14,16,18,RGB(35,92,64)},
  {15,19,16,RGB(57,122,86)},{16,19,18,RGB(55,110,72)},
  {14,18,17,RGB(31,65,49)},{15,17,19,RGB(37,76,52)},
  {18,19,21,RGB(192,155,64)},{18,21,20,RGB(167,126,45)},
  /* Pale wing bars frame the blue secondary feathers on both sides. */
  {26,22,24,RGB(211,198,170)},{26,24,28,RGB(49,69,102)},
  {27,25,23,RGB(220,205,176)},{27,29,25,RGB(60,82,116)}
 };
 W3Point origin=transform(p);
 if (origin.z<NEAR-45 || origin.z>=FAR || abs(origin.x)>origin.z+65) return;
 W3Point points[COUNT_OF(model)];
 uint32_t screen[COUNT_OF(model)];
 int s=isin(heading), c=icos(heading);
 for (unsigned i=0;i<COUNT_OF(model);i++) {
  int x=model[i][0], y=model[i][1], z=model[i][2];
  points[i]=transform(at(p,(x*c+z*s)>>12,y,(z*c-x*s)>>12));
 }
 project_mesh(points,screen,COUNT_OF(points));
 unsigned count=origin.z>850 ? 30 : COUNT_OF(face);
 for (unsigned i=0;i<count;i++)
  mesh_face(points,screen,face[i].a,face[i].b,face[i].c,face[i].tint,TWO_SIDED);
}
#endif

void w3_duck(W3Point p, int flap, int direction, int target
#ifdef HUNTPS1_WOODS_ART
 , int fade
#endif
)
{
#ifdef HUNTPS1_WOODS_SHOWCASE
 if (flap==W3_DUCK_FOLDED) {
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  folded_mallard(p,direction);
#else
  static const BodyRing folded[]={{-37,4,2,2},{-20,4,14,8},
   {4,3,19,10},{24,3,10,7},{37,3,3,3}};
  static const BodyRing low_head[]={{19,5,3,3},{30,8,8,7},{42,7,5,5}};
  int s=isin(direction), c=icos(direction);
  animal_body(p,folded,5,s,c,RGB(111,93,72),RGB(159,147,117),-1);
  animal_body(p,low_head,3,s,c,RGB(35,87,66),RGB(56,110,80),-1);
  box_rot(at(p,s*47/4096,6,c*47/4096),5,2,8,RGB(172,145,66),-1,s,c,ROUNDED);
#endif
  return;
 }
#endif
#ifdef HUNTPS1_WOODS_DETAIL
 int falling=flap<0;
 if (falling) flap=-flap;
#endif
#ifdef HUNTPS1_WOODS_MEADOW
 if (falling) {
  int pitch=flap<25 ? 76-flap*19 : -399;
  dive_sin=isin(pitch);
  dive_cos=icos(pitch);
 }
#endif
 int phase = flap % 12;
 int wing = (phase < 6 ? phase : 12-phase)*14 - 30;
#ifdef HUNTPS1_WOODS_ART
 bird_fade=fade;
 int yaw_sin=isin(direction), yaw_cos=icos(direction);
 wing=isin(phase*4096/12)*42/4096;
#ifdef HUNTPS1_WOODS_DETAIL
 int wrist_lift=isin((phase*4096/12)-430)*36/4096;
 if (falling) {
  wing=-18+wing/4;
  wrist_lift=-21+wrist_lift/4;
 }
#endif
 static const BodyRing body[]={{-47,7,1,1},{-22,1,15,14},
  {5,0,21,20},{35,19,6,8}};
 static const BodyRing head[]={{20,24,3,3},{34,31,11,11},{49,25,5,5}};
 animal_body(p,body,4,yaw_sin,yaw_cos,RGB(103,82,61),RGB(181,169,142),target);
 animal_body(p,head,3,yaw_sin,yaw_cos,RGB(32,98,73),RGB(51,128,95),target);
#ifdef HUNTPS1_WOODS_MEADOW
 box_rot(duck_joint(p,0,23,49,yaw_sin,yaw_cos),5,3,9,
#else
 box_rot(at(p,49*yaw_sin/4096,23,49*yaw_cos/4096),5,3,9,
#endif
  RGB(192,156,67),target,yaw_sin,yaw_cos,ROUNDED);
#else
 box_rot(p,35,19,20,RGB(139,105,70),target,0,4096,ROUNDED);
 box_rot(at(p,direction*30,19,0),16,18,15,RGB(47,126,89),target,0,4096,ROUNDED);
 box_rot(at(p,direction*50,13,0),12,5,9,RGB(232,169,57),target,0,4096,ROUNDED);
 box(at(p,-direction*34,0,0),14,9,13,RGB(65,67,53),target);
#endif
 for (int s = -1; s <= 1; s += 2) {
#ifdef HUNTPS1_WOODS_ART
 #ifdef HUNTPS1_WOODS_MEADOW
  #define WING_POINT(x,y,z) duck_joint(p,(x),(y),(z),yaw_sin,yaw_cos)
 #else
  #define WING_POINT(x,y,z) at(p,((x)*yaw_cos+(z)*yaw_sin)>>12, (y), \
   ((z)*yaw_cos-(x)*yaw_sin)>>12)
 #endif
  int bank=s*yaw_sin/512;
  W3Point a=WING_POINT(s*9,9,20), b=WING_POINT(s*15,0,-28);
  W3Point c=WING_POINT(s*62,wing+bank,-16), d=WING_POINT(s*47,wing*2/3+bank,-48);
  W3Point shoulder=WING_POINT(s*34,wing/2+bank,9);
#ifdef HUNTPS1_WOODS_DETAIL
  W3Point wrist=WING_POINT(s*48,wrist_lift+bank,-21);
#else
  W3Point wrist=WING_POINT(s*48,wing*3/4+bank,-21);
#endif
  #undef WING_POINT
#ifdef HUNTPS1_WOODS_FAST_SHOOT
  W3Point points[]={transform(a),transform(b),transform(shoulder),
   transform(wrist),transform(c),transform(d)};
  int inside=1;
  for (int i=0;i<6;i++) inside &= inside_side_planes(points+i) && points[i].z<FAR;
  uint32_t screen[6];
  if (inside) project_mesh(points,screen,6);
  static const unsigned char faces[][3]={{0,1,2},{2,1,3},{2,3,4},{4,3,5}};
  static const int colors[]={RGB(168,151,128),RGB(215,211,187),RGB(64,74,91),RGB(100,96,81)};
  for (int i=0;i<4;i++) {
   int j=faces[i][0], k=faces[i][1], l=faces[i][2];
   if (inside) {
    uint32_t xy[]={screen[j],screen[k],screen[l]};
    emit_projected(xy,(points[j].z+points[k].z+points[l].z)/3,colors[i],target,TWO_SIDED);
   } else {
    triangle_camera(points+j,points+k,points+l,colors[i],target,TWO_SIDED);
   }
  }
#else
  triangle(a,b,shoulder,RGB(168,151,128),target,TWO_SIDED);
  triangle(shoulder,b,wrist,RGB(215,211,187),target,TWO_SIDED);
  triangle(shoulder,wrist,c,RGB(64,74,91),target,TWO_SIDED);
  triangle(c,wrist,d,RGB(100,96,81),target,TWO_SIDED);
#endif
#else
  W3Point a=at(p,direction*20,9,s*10), b=at(p,-direction*28,0,s*15);
  int bank = s*direction*8;
  W3Point c=at(p,-direction*16,wing+bank,s*62);
  W3Point d=at(p,-direction*48,wing*2/3+bank,s*47);
  triangle(a,b,c,RGB(223,212,173),target,1);
  triangle(c,b,d,RGB(101,111,126),target,1);
#endif
 }
 if (target >= 0) {
  W3Point q = transform(p);
  if (q.z >= NEAR) {
   out->center[target][0] = 160 + q.x * 174 / q.z;
   out->center[target][1] = 116 - q.y * 174 / q.z;
  }
 }
#ifdef HUNTPS1_WOODS_ART
 bird_fade=0;
#endif
#ifdef HUNTPS1_WOODS_MEADOW
 dive_sin=0;
 dive_cos=4096;
#endif
}

int w3_pick(const W3Frame *frame, int x, int y)
{
 int bin = W3_OT, target = -1;
 int padded_bin = W3_OT, padded_target = -1;
 enum { HIT_MARGIN = 4 };
 if (!frame->active || x < 0 || x >= 320 || y < 0 || y >= 240)
  return -1;
 for (int i = 0; i < frame->count; i++) {
  const W3Face *f = &frame->face[i];
  if (f->depth >= bin)
   continue;
#ifdef HUNTPS1_WOODS_SCENE_ONE
  if (f->target < -1 && f->target > -100) {
   if (x < f->x[0] || x >= f->x[1] || y < f->y[0] || y >= f->y[2])
    continue;
 #ifdef HUNTPS1_WOODS_DETAIL
   const FoliageBounds *bounds=foliage_bounds+(-2-f->target);
   int u=bounds->left+(x-f->x[0])*(bounds->right-bounds->left)/(f->x[1]-f->x[0]);
   int v=bounds->top+(y-f->y[0])*(bounds->bottom-bounds->top)/(f->y[2]-f->y[0]);
 #else
   int u=(x-f->x[0])*(CUTOUT_SIZE-1)/(f->x[1]-f->x[0]);
   int v=(y-f->y[0])*(CUTOUT_SIZE-1)/(f->y[2]-f->y[0]);
 #endif
   unsigned pixel=((-2-f->target)*CUTOUT_SIZE+v)*CUTOUT_SIZE+u;
   if ((cutout_texels[pixel/8] >> ((pixel%8)*4)) & 15) {
    bin=f->depth;
    target=-1;
   }
   continue;
  }
#endif
  /* Only the exact silhouette can block a shot on scenery. */
  if (f->target < 0 &&
      ((x<f->x[0] && x<f->x[1] && x<f->x[2]) ||
       (x>f->x[0] && x>f->x[1] && x>f->x[2]) ||
       (y<f->y[0] && y<f->y[1] && y<f->y[2]) ||
       (y>f->y[0] && y>f->y[1] && y>f->y[2])))
   continue;
  int a = edge(f->x[0],f->y[0],f->x[1],f->y[1],x,y);
  int b = edge(f->x[1],f->y[1],f->x[2],f->y[2],x,y);
  int c = edge(f->x[2],f->y[2],f->x[0],f->y[0],x,y);
  if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) {
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   if (f->target<=-100) {
    const POLY_FT3 *p=&frame->poly[i].textured;
    int area=a+b+c;
    if (!area) continue;
    int u=((b*p->u0+c*p->u1+a*p->u2)/area)&63;
    int v=((b*p->v0+c*p->v1+a*p->v2)/area)&63;
    unsigned pixel=((GRASS_BLADES+(-100-f->target))*64+v)*64+u;
    if (!((cutout_texels[pixel/8]>>((pixel&7)*4))&15)) continue;
    bin=f->depth;
    target=-1;
    continue;
   }
#endif
   bin = f->depth;
   target = f->target;
  } else if (f->target >= 0 && f->depth < padded_bin) {
   /* Expand the visible bird triangles in screen pixels, keeping exact
      hits and nearer scenery ahead of the expanded silhouette. */
   int ma = HIT_MARGIN * (abs(f->x[1]-f->x[0]) + abs(f->y[1]-f->y[0]));
   int mb = HIT_MARGIN * (abs(f->x[2]-f->x[1]) + abs(f->y[2]-f->y[1]));
   int mc = HIT_MARGIN * (abs(f->x[0]-f->x[2]) + abs(f->y[0]-f->y[2]));
   int area = a+b+c;
   if ((area > 0 && a >= -ma && b >= -mb && c >= -mc) ||
       (area < 0 && a <= ma && b <= mb && c <= mc)) {
    padded_bin = f->depth;
    padded_target = f->target;
   }
  }
 }
 return target >= 0 || padded_bin >= bin ? target : padded_target;
}

