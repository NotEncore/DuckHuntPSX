#ifndef HUNT_WOODS3D_RENDER_H
#define HUNT_WOODS3D_RENDER_H

#include <stdint.h>
#include <psxgpu.h>

#ifdef HUNTPS1_WOODS_LEFT_FLUSH
enum { W3_STAGING_SIGN = -1 };
#else
enum { W3_STAGING_SIGN = 1 };
#endif

enum { W3_OT = 256,
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 W3_POLYS = 1200
#else
 W3_POLYS = 1900
#endif
};
typedef struct { int x, y, z; } W3Point;
typedef struct {
 int x, z, yaw, pitch;
#ifdef HUNTPS1_WOODS_CINEMATIC
 int bob;
#endif
} W3Camera;
typedef struct { short x[3], y[3]; unsigned short depth; short target; } W3Face;
typedef struct {
 uint32_t ot[W3_OT], forest_ot[W3_OT];
 W3Camera forest_camera;
 int forest_site, forest_station, forest_side, forest_rustle;
 int forest_count, forest_overflow, forest_valid;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 uint16_t water_slots[256];
 int water_count;
#endif
 union {
  POLY_F3 flat;
  POLY_FT3 textured;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  POLY_F4 ridge;
#endif
#ifdef HUNTPS1_WOODS_SCENE_ONE
  POLY_FT4 cutout;
#endif
 } poly[W3_POLYS];
 W3Face face[W3_POLYS];
 int count, overflow, active;
 int center[2][2];
 TILE underlay[6];
#ifdef HUNTPS1_WOODS_EARLY_DOG_BACKGROUND
 uint32_t backdrop_ot;
 int early_backdrop, forest_early_backdrop;
#endif
#ifdef HUNTPS1_WOODS_FAST_SHOOT
 TILE sky_bands[16];
#endif
 POLY_G4 sky;
 DRAWENV draw;
#ifdef HUNTPS1_WOODS_FAST_SHOOT
 int forest_fast_shoot;
#endif
 DISPENV display;
} W3Frame;

void w3_upload_foliage(void);
int w3_path(int z);
int w3_ground(int z);
#ifdef HUNTPS1_WOODS_LAKE
enum { W3_WATER_Y=44, W3_POND_FAR=2304,
#ifdef HUNTPS1_WOODS_MARSH
 W3_POND_NEAR=640
#else
 W3_POND_NEAR=768
#endif
};
#ifdef HUNTPS1_WOODS_SHOWCASE
enum { W3_DUCK_FOLDED=-128 };
#endif
int w3_in_pond(int x,int z);
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
W3Point w3_dry_bank(W3Point p);
W3Point w3_flush_cover(int station, int side);
#endif
void w3_pond_ripple(W3Point p,int age);
#endif
void w3_begin(W3Frame *frame, W3Camera camera, int zone);
#ifdef HUNTPS1_WOODS_SPLIT_PAN
int w3_camera_depth(W3Point p);
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
void w3_prepare_walk_visibility(W3Camera (*path)(int),int frames);
void w3_use_walk_visibility(int frame);
#endif
#ifdef HUNTPS1_WOODS_WALK_CACHE
void w3_prepare_walk_packets(W3Frame *scratch,W3Camera (*path)(int),int frames,int station,int side);
#endif
void w3_forest(int station, int side, int rustle);
#ifdef HUNTPS1_WOODS_FAST_SHOOT
void w3_use_fast_shoot_view(int active);
#endif
#ifdef HUNTPS1_WOODS_PAN_CACHE
void w3_prepare_pan_packets(W3Frame *scratch,W3Camera (*path)(int),int frames,int station,int side);
void w3_use_pan_packets(int frame);
#ifdef HUNTPS1_WOODS_DOG_BATCH
void w3_prepare_sniff_packets(W3Frame *scratch,W3Camera (*path)(int),int frames,int station,int side);
void w3_use_sniff_packets(int frame);
#endif
#endif
void w3_dog(W3Point at, int gait, int pose, int heading, int pose_tick);
void w3_duck(W3Point at, int flap, int direction, int target
#ifdef HUNTPS1_WOODS_ART
 , int fade
#endif
);
int w3_pick(const W3Frame *frame, int x, int y);

#endif



