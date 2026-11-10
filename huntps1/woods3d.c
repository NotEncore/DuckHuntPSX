#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxpad.h>
#include "cal_save.h"
#include "audio.h"
#include "woods3d_render.h"

enum Phase { SELECT, WALK, APPROACH, DOG_FLUSH, CAMERA_REFRAME, SHOOT,
 DUCK_FALL, DOG_RETRIEVE, CAMERA_RETURN, RESUME, RESULTS, CAL_P2, FADE_IN, FADE_OUT
#ifdef HUNTPS1_WOODS_MEADOW
 , CAMERA_SETTLE
#endif
};
enum DuckState { HIDDEN, FLYING, FALLING, LANDED, ESCAPED
#ifdef HUNTPS1_WOODS_ART
 , FLYING_OUT
#endif
};
enum { SHOOT_TICKS = 110, MAX_SHOTS = 6 };
#ifdef HUNTPS1_WOODS_ART
enum { FLY_OUT_TICKS = 20 };
#endif
typedef struct {
 W3Point p;
 int state, direction, lane, lift;
#ifdef HUNTPS1_WOODS_ART
 int flight_age, heading;
#endif
} Duck;
typedef struct {
 int signature, ticks, phase, phase_ticks, site, players, mode;
 int shots, hits, misses, p1, p2, ammo, stopped_camera_errors;
 int frame, overruns, max_polys, overflow, card, cal_x, cal_y;
 int center[2][2], eligible[2], present[2], edges[2], invalid[2];
 int rendered_site, active, landing_x, landing_z, phases, perfect, max_vblanks;
 int aim_x[2], aim_y[2], aim_pick[2], hits_site[5], shot_site[5];
 int max_cpu_vblanks, max_gpu_wait_vblanks, display_vblanks, claim_checks;
} WoodsProbe;
volatile WoodsProbe g_woods3d = {.signature = 0x57334431};
#ifdef HUNTPS1_WOODS_PROFILE
#include <hwregs_c.h>
/* Timer 1 is already configured by the SDK as the scanline clock. */
typedef struct {
 uint16_t forest, dog, actors_hud, logic_audio, gpu_wait, submit;
 uint8_t phase, site, gap, spare;
} WoodsTiming;
#if defined(HUNTPS1_WOODS_INTEGRATED)
enum { WOODS_TRACE_CAPACITY = 896 };
#elif defined(HUNTPS1_WOODS_PAN_CACHE)
enum { WOODS_TRACE_CAPACITY = 2048 };
#else
enum { WOODS_TRACE_CAPACITY = 4096 };
#endif
volatile WoodsTiming g_woods_trace[WOODS_TRACE_CAPACITY];
volatile unsigned g_woods_trace_count;
static WoodsTiming measured;
static uint16_t scanline_clock(void) { return TIMER_VALUE(1); }
#define STAMP(name) uint16_t name = scanline_clock()
#define SPAN(field, from) measured.field = (uint16_t)(scanline_clock() - (from))
#else
#define STAMP(name) ((void)0)
#define SPAN(field, from) ((void)0)
#endif
static W3Frame buffers[2];
#ifdef HUNTPS1_WOODS_FAST_SHOOT
static W3Point previous_ducks[2];
static int halfway_ducks;
#endif
static W3Camera camera, shoot_camera;
static W3Point dog, retrieval_from, retrieval_to;
#ifdef HUNTPS1_WOODS_MEADOW
enum { CAMERA_TRACK_TICKS=44, CAMERA_SETTLE_TICKS=5, MEADOW_YAW=50,
#ifdef HUNTPS1_WOODS_MARSH
 MEADOW_PITCH=22
#else
 MEADOW_PITCH=59
#endif
};
static int retrieval_out_ticks, retrieval_back_ticks, carrying;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
static int seated_finish;
enum { LAUGH_WALK=43, LAUGH_START=55, LAUGH_END=100 };
#endif
static W3Point retrieval_return;
/* Read once after the encounter. Keep the landing and pickup pairs for both birds. */
volatile int g_woods_meadow[12];
#ifdef HUNTPS1_WOODS_LAKE
volatile int g_woods_lake[4];
#endif
#endif
static Duck ducks[2];
static GunCal calibration, p2_cal, p2_pending;
static int p2_point, p2_status, cal_port;
volatile int g_woods_p2_profile[4];
static uint16_t p2_buttons = 0xffff;
static uint32_t text_packets[2][1024];
static uint8_t (*pads)[34];
static int leave_requested, solo_port;
static int phase, tick, site, mode, players, ammo, reload[2];
static int paused, pause_item;
static unsigned pause_buttons;
static int retrieval, caught, duck_count, priority;
static unsigned int was_present[2], held[2];
static uint16_t buttons_before = 0xffff, gun_buttons_before = 0xffff;

#ifdef HUNTPS1_WOODS_SCENE_ONE
enum { APPROACH_STEPS = 18, FADE_TICKS = 16 };
#else
enum { APPROACH_STEPS = 40 };
#endif
#ifdef HUNTPS1_WOODS_CINEMATIC
enum { WALK_TICKS = 84, STEP_TICKS = 14, WALK_START = 186, WALK_END = 416 };
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
enum { DOG_LEAD=180, DOG_SCENT_HOLD=34, DOG_SCENT_RAISED=41,
 DOG_ARRIVE=DOG_SCENT_RAISED+53, DOG_SNIFF_END=DOG_ARRIVE+48,
 DOG_RAISED=DOG_SNIFF_END+7, DOG_JUMP=DOG_RAISED+12 };
#endif

static int lerp(int a, int b, int t, int n);
static W3Point trail(int z);
#ifdef HUNTPS1_WOODS_PAN_CACHE
static W3Camera pan_camera(int frame);
#endif
static W3Camera walk_camera(int frame)
{
 int stride = frame%STEP_TICKS, half = STEP_TICKS/2;
 int rise = stride < half ? stride : STEP_TICKS-stride;
 int sway = (frame/STEP_TICKS)&1 ? -rise : rise;
 int z=lerp(WALK_START,WALK_END,frame,WALK_TICKS);
 return (W3Camera){w3_path(z)+sway,z,W3_STAGING_SIGN*19+sway/2,-12+rise/2,rise*2};
}
static void cinematic_walk(int frame)
{
 camera=walk_camera(frame);
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 dog = w3_dry_bank(trail(camera.z+DOG_LEAD));
#else
 dog = trail(camera.z+222);
#endif
}
#endif

static int hunt_target_count(void)
{
#ifdef HUNTPS1_WOODS_SCENE_ONE
 return 2;
#else
 return mode == 1 ? players : 5*players;
#endif
}
static int station(void) { return 760 + site * 1750; }
static int side(void) { return (site & 1 ? -1 : 1)*W3_STAGING_SIGN; }
static int lerp(int a, int b, int t, int n) { return a + (b-a)*t/n; }
static W3Point mix(W3Point a, W3Point b, int t, int n)
{
 return (W3Point){lerp(a.x,b.x,t,n),lerp(a.y,b.y,t,n),lerp(a.z,b.z,t,n)};
}
static W3Point trail(int z) { return (W3Point){w3_path(z),w3_ground(z),z}; }
static W3Point bush(void)
{
 W3Point p = trail(station());
 p.x += side()*150;
 return p;
}
#ifdef HUNTPS1_WOODS_LEFT_FLUSH
static int eased_progress(int t, int duration)
{
 if (t <= 0) return 0;
 if (t >= duration) return 256;
 int x=t*256/duration;
 return x*x*(768-2*x)/65536;
}

#ifdef HUNTPS1_WOODS_DOG_BATCH
static W3Camera sniff_camera(int frame)
{
 W3Camera p=walk_camera(WALK_TICKS);
 p.pitch=lerp(-12,-122,eased_progress(frame,14),256);
 return p;
}
#endif
#ifdef HUNTPS1_WOODS_PAN_CACHE
static W3Camera pan_camera(int frame)
{
 W3Camera p=walk_camera(WALK_TICKS);
 int amount=eased_progress(frame,CAMERA_TRACK_TICKS);
 p.yaw=lerp(-86,MEADOW_YAW,amount,256);
 p.pitch=lerp(-12,MEADOW_PITCH,amount,256);
 return p;
}
#endif

static W3Point investigate(void)
{
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 W3Point p=bush();
 p.z-=119;
 p.x-=25;
 return w3_dry_bank(p);
#else
 return mix(trail(camera.z+222),bush(),110,256);
#endif
}
#endif
static void set_phase(int next)
{
 phase = next;
 tick = 0;
 g_woods3d.phases |= 1 << next;
}
static void begin(int selection, int count)
{
 #ifdef HUNTPS1_WOODS_PROFILE
 g_woods_trace_count = 0;
 #endif
 mode = selection;
 players = count;
 site = 0;
 g_woods3d.shots = g_woods3d.hits = g_woods3d.misses = 0;
 g_woods3d.p1 = g_woods3d.p2 = g_woods3d.perfect = 0;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 seated_finish=0;
#endif
#ifdef HUNTPS1_WOODS_MEADOW
 for (int i=0;i<12;i++) g_woods_meadow[i]=0;
#endif
#ifdef HUNTPS1_WOODS_LAKE
 for (int i=0;i<4;i++) g_woods_lake[i]=0;
#endif
 for (int i = 0; i < 5; i++)
  g_woods3d.hits_site[i] = g_woods3d.shot_site[i] = 0;
 camera = (W3Camera){w3_path(0),0,0,0};
#ifdef HUNTPS1_WOODS_SCENE_ONE
 mode = 1;
 camera.z = 226;
 camera.x = w3_path(camera.z);
 camera.yaw = (w3_path(camera.z+245)-camera.x)*256/245;
 dog = trail(camera.z+245);
#ifdef HUNTPS1_WOODS_CINEMATIC
 cinematic_walk(0);
#endif
#ifdef HUNTPS1_WOODS_INTEGRATED
 set_phase(WALK);
#else
 set_phase(FADE_IN);
#endif
#else
 dog = trail(245);
 set_phase(WALK);
#endif
}
static void walking(int speed)
{
 camera.z += speed;
 camera.x = w3_path(camera.z);
 camera.yaw = (w3_path(camera.z+245)-camera.x)*256/245;
 dog = trail(camera.z+245);
}
static void launch(void)
{
#ifdef HUNTPS1_WOODS_SCENE_ONE
 duck_count = 2;
 ammo = 3;
#else
 duck_count = players;
 ammo = players == 1 ? 3 : MAX_SHOTS;
#endif
 caught = 0;
 for (int i = 0; i < duck_count; i++) {
  ducks[i].p = bush();
  ducks[i].p.y += 140;
  ducks[i].p.x += i * 32 * W3_STAGING_SIGN;
  ducks[i].direction = ducks[i].lane = i ? side() : -side();
  ducks[i].state = FLYING;
  ducks[i].lift = 0;
#ifdef HUNTPS1_WOODS_ART
  ducks[i].p.y -= 41+i*12;
  ducks[i].flight_age = 0;
  ducks[i].heading = (-370+i*190)*W3_STAGING_SIGN;
#endif
 }
 hunt_sfx_begin_flight();
 hunt_sfx_play(SFX_QUACK);
}
#ifdef HUNTPS1_WOODS_ART
static int track_angle(int angle, int goal)
{
 int error = goal-angle;
 if (abs(error) <= 2) return angle;
 int step = (abs(error)+5)/6;
 if (step > 5) step = 5;
 return angle+(error < 0 ? -step : step);
}

static void track_flight(void)
{
 int x=0, y=0, z=0, birds=0;
 int eye_y=w3_ground(camera.z)+166+camera.bob;
 for (int i=0;i<duck_count;i++) {
  const Duck *d=ducks+i;
  if (d->state != FLYING && d->state != FLYING_OUT) continue;
  x+=d->p.x-camera.x;
  y+=d->p.y-eye_y;
  z+=d->p.z-camera.z;
  birds++;
 }
 if (!birds || z <= 0) return;
 /* The escape stays within a narrow cone, where the bearing can use
    the lateral/forward ratio directly. Both axes ease toward it. */
 camera.yaw=track_angle(camera.yaw,x*245/z);
 camera.pitch=track_angle(camera.pitch,y*245/z);
}
#endif

static void flight(int aiming)
{
 for (int i = 0; i < duck_count; i++) {
  Duck *d = &ducks[i];
#ifdef HUNTPS1_WOODS_ART
  if (d->state == FLYING_OUT && ++d->lift == FLY_OUT_TICKS) d->state = ESCAPED;
  if (d->state == FLYING && aiming && tick > SHOOT_TICKS) {
   d->state = FLYING_OUT;
   d->lift = 0;
   g_woods3d.misses++;
  }
  if (d->state == FLYING || d->state == FLYING_OUT) {
   int age=d->flight_age++;
#ifdef HUNTPS1_WOODS_MEADOW
   int lateral=age<8 ? 1 : age<27 ? 4+i : 6-i*2;
   int forward=age<8 ? -2 : age<27 ? (age-8)/3 : 7+i;
   /* Flush together, then fan out on either side of the shared bearing. */
   int spread=age<8 ? 0 : age<20 ? 1 : 2;
   lateral += i ? -spread : spread;
#else
   int lateral=(-4+i*2)*W3_STAGING_SIGN;
   int forward=age < 22 ? 5+i : 7+i;
#endif
   d->p.x+=lateral;
   d->p.y+=age < 20 ? 7-i : age < 64 ? 6-i : 4-i;
   d->p.z+=forward;
#ifdef HUNTPS1_WOODS_MEADOW
   d->heading=age<27 ? lerp(1680,690-i*300,age,27) : 550-i*320;
#else
   d->heading=lerp(-370+i*190,-340+i*155,age < 64 ? age : 64,64)*W3_STAGING_SIGN;
#endif
   d->direction=-W3_STAGING_SIGN;
#else
  if (d->state == FLYING) {
   if (aiming && tick > SHOOT_TICKS) {
    d->state = ESCAPED;
    g_woods3d.misses++;
   } else {
    int age = aiming ? tick + 21 : tick;
    int velocity;
    if (age < 39) velocity = 6;
    else if (age < 55) velocity = (47-age)*3/4;
    else if (age < 85) velocity = -6;
    else if (age < 101) velocity = (age-93)*3/4;
    else velocity = 6;
    d->p.x += d->lane * velocity;
    if (velocity) d->direction = velocity > 0 ? d->lane : -d->lane;
    d->p.y += age < 28 ? 7 + i : age < 60 ? 2 : 0;
    d->p.z += 2 + i;
   }
#endif
  } else if (d->state == FALLING) {
#ifdef HUNTPS1_WOODS_MEADOW
   d->lift++;
   d->p.y+=2-d->lift;
#ifdef HUNTPS1_WOODS_SHOWCASE
   int angle=d->lift*118;
   d->p.x+=2+isin(angle)/1024;
   d->p.z+=(d->lift<13 ? 8 : 5)+icos(angle)/1365;
   d->heading+=118;
#else
   d->p.x+=d->lift<10 ? 5-i*2 : 2;
   d->p.z+=d->lift<12 ? 7+i : d->lift<25 ? 5 : 3;
#endif
#else
   if (++d->lift > 7) {
    d->p.y -= (d->lift-7)*2;
    d->p.x += d->direction*2;
    d->p.z += 2;
   }
#endif
   int landing_y=w3_ground(d->p.z)+15;
#ifdef HUNTPS1_WOODS_LAKE
   int water=w3_in_pond(d->p.x,d->p.z);
   if (water) landing_y=W3_WATER_Y+9;
#endif
   if (d->p.y <= landing_y) {
    d->state = LANDED;
    hunt_sfx_play(SFX_LAND);
    d->p.y = landing_y;
#ifdef HUNTPS1_WOODS_LAKE
    d->lift=0;
    g_woods_lake[i]=water;
#endif
    g_woods3d.landing_x = d->p.x;
    g_woods3d.landing_z = d->p.z;
#ifdef HUNTPS1_WOODS_MEADOW
   g_woods_meadow[i*6]=d->p.x;
   g_woods_meadow[i*6+1]=d->p.z;
#endif
   }
#ifdef HUNTPS1_WOODS_LAKE
  } else if (d->state==LANDED && d->lift<18) {
   d->lift++;
#endif
  }
 }
}
#ifdef HUNTPS1_WOODS_MEADOW
static int run_ticks(W3Point a, W3Point b)
{
 int dx=abs(a.x-b.x), dz=abs(a.z-b.z);
#ifdef HUNTPS1_WOODS_LAKE
 int speed=w3_in_pond(a.x,a.z) || w3_in_pond(b.x,b.z) ? 11 : 15;
 return 1+(dx+dz-(dx<dz ? dx : dz)/2)/speed;
#else
 return 1+(dx+dz-(dx<dz ? dx : dz)/2)/15;
#endif
}

static int run_heading(W3Point from, W3Point to)
{
 int dx=to.x-from.x, dz=to.z-from.z;
 int x=abs(dx), z=abs(dz);
 int angle=x>z ? 1024-z*512/x : z ? x*512/z : 0;
 if (dz<0) angle=2048-angle;
 return dx<0 ? -angle : angle;
}
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
static int camera_follow_step(int current,int goal,int tolerance)
{
 int error=goal-current, magnitude=abs(error);
 if (magnitude<=tolerance) return current;
 int step=(magnitude+7)/8;
 if (step>12) step=12;
 return current+(error<0 ? -step : step);
}

static void look_at_dog(void)
{
 W3Point eye={camera.x,w3_ground(camera.z)+166,camera.z};
 int swimming=w3_in_pond(dog.x,dog.z);
 W3Point aim={dog.x,dog.y+(swimming ? 64 : 36),dog.z};
 int dx=abs(aim.x-eye.x), dz=abs(aim.z-eye.z);
 int distance=dx+dz-(dx<dz ? dx : dz)/2;
 /* World bearings use GTE turn units. The camera stores 2/5 of that. */
 int yaw=run_heading(eye,aim)*2/5;
 int pitch=run_heading((W3Point){0,0,0},(W3Point){aim.y-eye.y,0,distance})*2/5;
 int tolerance=9;
#ifdef HUNTPS1_WOODS_SWIM_FRAMING
 /* Let the dog cross a small central area before nudging the view.
    Keep the original close framing when the dog comes back ashore. */
 if (swimming) tolerance=28;
#endif
 camera.yaw=camera_follow_step(camera.yaw,yaw,tolerance);
 camera.pitch=camera_follow_step(camera.pitch,pitch,tolerance);
}

static int final_pickup(void)
{
 return retrieval!=0 || ducks[1].state!=LANDED;
}

static int sit_tick(void)
{
 return tick-(retrieval_out_ticks+8+retrieval_back_ticks);
}

static int sit_heading(int heading,int settle)
{
 /* A three-quarter sit leaves the tail visible beside the haunch. */
 int toward=run_heading(dog,(W3Point){camera.x,0,camera.z})+410;
 int turn=(toward-heading+2048)&4095;
 return heading+(turn-2048)*settle/256;
}
#endif
#endif
static void next_retrieval(void)
{
 while (retrieval < duck_count && ducks[retrieval].state != LANDED)
  retrieval++;
 retrieval_from = dog;
 retrieval_to = retrieval < duck_count ? ducks[retrieval].p : trail(camera.z+285);
 retrieval_to.y = w3_ground(retrieval_to.z);
#ifdef HUNTPS1_WOODS_MEADOW
#ifdef HUNTPS1_WOODS_MARSH
 retrieval_return=trail(W3_POND_NEAR-49);
#else
 retrieval_return=trail(camera.z+255);
#endif
 retrieval_return.x+=65;
 retrieval_out_ticks=run_ticks(retrieval_from,retrieval_to);
 retrieval_back_ticks=run_ticks(retrieval_to,retrieval_return);
 carrying=0;
#endif
 set_phase(DOG_RETRIEVE);
}
static void step(void)
{
 tick++;
 switch (phase) {
#ifdef HUNTPS1_WOODS_SCENE_ONE
 case FADE_IN:
  if (tick == FADE_TICKS) set_phase(WALK);
  break;
 case FADE_OUT:
#ifdef HUNTPS1_WOODS_INTEGRATED
  if (tick > FADE_TICKS) leave_requested = 1;
#else
  if (tick == FADE_TICKS) set_phase(RESULTS);
#endif
  break;
#endif
 case WALK:
#ifdef HUNTPS1_WOODS_CINEMATIC
  cinematic_walk(tick);
#if defined(HUNTPS1_WOODS_SOUND) && !defined(HUNTPS1_WOODS_MUTE_STEPS)
  if (tick % STEP_TICKS == 0)
   hunt_sfx_play((tick / STEP_TICKS) & 1 ? SFX_WOODS_STEP_LEFT : SFX_WOODS_STEP_RIGHT);
#endif
  if (tick == WALK_TICKS)
#else
  walking(8);
#ifdef HUNTPS1_WOODS_SCENE_ONE
  if (tick == 8)
#else
  if (camera.z >= station()-630)
#endif
#endif
   set_phase(APPROACH);
  break;
 case APPROACH:
#ifdef HUNTPS1_WOODS_CINEMATIC
#ifdef HUNTPS1_WOODS_LEFT_FLUSH
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  int travel=tick-DOG_SCENT_RAISED;
  if (travel<0) travel=0;
  if (travel>DOG_ARRIVE-DOG_SCENT_RAISED) travel=DOG_ARRIVE-DOG_SCENT_RAISED;
  dog=w3_dry_bank(mix(trail(camera.z+DOG_LEAD),investigate(),
   travel,DOG_ARRIVE-DOG_SCENT_RAISED));
  camera.pitch=lerp(-12,-122,eased_progress(tick,14),256);
  camera.pitch=lerp(camera.pitch,-12,eased_progress(tick-DOG_SCENT_HOLD,23),256);
  camera.yaw=lerp(-19,-86,eased_progress(tick-DOG_SCENT_RAISED,32),256);
#else
  dog=mix(trail(camera.z+222),investigate(),eased_progress(tick-6,19),256);
  camera.yaw=lerp(-19,-86,eased_progress(tick-3,32),256);
#endif
#else
  dog = mix(trail(camera.z+222),bush(),tick < 48 ? tick/2 : 24,60);
  camera.yaw = lerp(19,61,tick < 42 ? tick : 42,42);
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  if (tick == DOG_JUMP) set_phase(DOG_FLUSH);
#else
  if (tick == 48) hunt_sfx_play(SFX_BARK);
  if (tick == 57) set_phase(DOG_FLUSH);
#endif
  break;
#else
  if (tick <= APPROACH_STEPS)
   walking(7);
  dog = mix(trail(camera.z+245),bush(),tick < 54 ? tick : 54,60);
#ifdef HUNTPS1_WOODS_SCENE_ONE
  if (tick == 57)
#else
  if (mode && tick == 74)
#endif
   set_phase(DOG_FLUSH);
  else if (!mode && tick == 74) {
   if (++site == 5) { site = 0; camera.z = 0; }
   set_phase(WALK);
  }
  break;
#endif
 case DOG_FLUSH:
#ifdef HUNTPS1_WOODS_LEFT_FLUSH
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  int air=tick>7 ? tick-7 : 0;
  W3Point cover=w3_flush_cover(station(),side());
  dog=mix(investigate(),cover,air,17);
  dog.y+=air*(17-air)*7/8;
  if (tick==8) hunt_sfx_play(SFX_BARK);
#else
  dog=mix(investigate(),bush(),eased_progress(tick-4,20),256);
  if (tick > 4) dog.y+=(tick-4)*(24-tick)*3/4;
#endif
#elif defined(HUNTPS1_WOODS_CINEMATIC)
  dog = mix(mix(trail(camera.z+222),bush(),24,60),bush(),tick,24);
#else
  dog = bush();
#endif
#ifndef HUNTPS1_WOODS_LEFT_FLUSH
  dog.y += tick*(24-tick)/2;
#endif
#ifndef HUNTPS1_WOODS_CINEMATIC
  if (tick == 8)
   hunt_sfx_play(SFX_BARK);
#endif
  if (tick == 24) {
   launch();
   shoot_camera = camera;
   set_phase(CAMERA_REFRAME);
  }
  break;
 case CAMERA_REFRAME:
  flight(0);
#ifdef HUNTPS1_WOODS_SOUND
  hunt_sfx_pair_flight(0);
#endif
#ifdef HUNTPS1_WOODS_MEADOW
  camera.yaw=lerp(shoot_camera.yaw,MEADOW_YAW,eased_progress(tick,CAMERA_TRACK_TICKS),256);
  camera.pitch=lerp(shoot_camera.pitch,MEADOW_PITCH,eased_progress(tick,CAMERA_TRACK_TICKS),256);
  if (tick==CAMERA_TRACK_TICKS) {
   shoot_camera=camera;
   set_phase(CAMERA_SETTLE);
  }
  break;
 case CAMERA_SETTLE:
  flight(0);
#ifdef HUNTPS1_WOODS_SOUND
  hunt_sfx_pair_flight(0);
#endif
  if (tick==CAMERA_SETTLE_TICKS) set_phase(SHOOT);
  break;
#elif defined(HUNTPS1_WOODS_ART)
  track_flight();
#elif defined(HUNTPS1_WOODS_CINEMATIC)
  camera.pitch = lerp(shoot_camera.pitch,55,tick,21);
#else
  camera.pitch = lerp(0,55,tick,21);
#endif
#ifndef HUNTPS1_WOODS_ART
  camera.yaw = lerp(shoot_camera.yaw,side()*41,tick,21);
#endif
#ifndef HUNTPS1_WOODS_MEADOW
  if (tick == 21) {
   shoot_camera = camera;
   set_phase(SHOOT);
  }
  break;
#endif
 case SHOOT: {
  if (camera.x != shoot_camera.x || camera.z != shoot_camera.z
#if !defined(HUNTPS1_WOODS_ART) || defined(HUNTPS1_WOODS_MEADOW)
      ||
      camera.yaw != shoot_camera.yaw || camera.pitch != shoot_camera.pitch)
#else
     )
#endif
   g_woods3d.stopped_camera_errors++;
  flight(1);
#if defined(HUNTPS1_WOODS_ART) && !defined(HUNTPS1_WOODS_MEADOW)
  track_flight();
  shoot_camera.yaw=camera.yaw;
  shoot_camera.pitch=camera.pitch;
#endif
  int flying = 0;
  for (int i = 0; i < duck_count; i++) {
   flying |= ducks[i].state == FLYING;
#ifdef HUNTPS1_WOODS_ART
   flying |= ducks[i].state == FLYING_OUT;
#endif
  }
  if (flying) hunt_sfx_pair_flight(!ammo);
  if (!flying)
   set_phase(DUCK_FALL);
  break;
 }
 case DUCK_FALL: {
  flight(0);
#ifdef HUNTPS1_WOODS_MEADOW
  camera.pitch=lerp(shoot_camera.pitch,3,eased_progress(tick,22),256);
#else
  camera.pitch = lerp(shoot_camera.pitch,-8,tick < 12 ? tick : 12,12);
#endif
  int falling = 0;
  for (int i = 0; i < duck_count; i++)
   falling |= ducks[i].state == FALLING;
  if (!falling && tick >=
#ifdef HUNTPS1_WOODS_MEADOW
      22
#else
      12
#endif
  ) {
   retrieval = 0;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   if (!caught) {
    dog=investigate();
   }
#endif
   next_retrieval();
#ifndef HUNTPS1_WOODS_APPROVED_ASSETS
   if (!caught) hunt_sfx_play(SFX_LAUGH);
#endif
  }
  break;
 }
 case DOG_RETRIEVE:
#ifdef HUNTPS1_WOODS_MEADOW
  if (retrieval<duck_count) {
   int return_start=retrieval_out_ticks+8;
   if (tick<=retrieval_out_ticks)
    dog=mix(retrieval_from,retrieval_to,tick,retrieval_out_ticks);
   if (tick==return_start) {
    g_woods_meadow[retrieval*6+2]=dog.x;
    g_woods_meadow[retrieval*6+3]=dog.z;
    ducks[retrieval].state=HIDDEN;
    carrying=1;
    hunt_sfx_play(SFX_RETRIEVE);
   }
   if (carrying && tick<=return_start+retrieval_back_ticks)
    dog=mix(retrieval_to,retrieval_return,tick-return_start,retrieval_back_ticks);
   if (tick==return_start+retrieval_back_ticks) {
    g_woods_meadow[retrieval*6+4]=dog.x;
    g_woods_meadow[retrieval*6+5]=dog.z;
   }
   dog.y=w3_ground(dog.z);
#ifdef HUNTPS1_WOODS_LAKE
   if (w3_in_pond(dog.x,dog.z)) {
    dog.y=W3_WATER_Y-51;
    g_woods_lake[2]++;
    if (carrying) g_woods_lake[3]++;
   }
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   else dog.y=w3_dry_bank(dog).y;
   look_at_dog();
#endif
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   if (sit_tick()<(final_pickup() ? 55 : 20)) break;
#else
   if (tick<return_start+retrieval_back_ticks+20) break;
#endif
  }
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  else if (tick<LAUGH_END) {
   dog=w3_dry_bank(mix(retrieval_from,retrieval_return,
    tick<LAUGH_WALK ? tick : LAUGH_WALK,LAUGH_WALK));
   look_at_dog();
   if (tick==LAUGH_START) hunt_sfx_play(SFX_LAUGH);
   break;
  }
#else
  else if (tick<52) {
   dog.y=w3_ground(dog.z)+(tick%12<6 ? tick%6 : 5-tick%6)*3;
   break;
  }
#endif
  if (retrieval==0 && ducks[1].state==LANDED) {
   retrieval++;
   next_retrieval();
  } else {
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   {
    seated_finish=carrying ? 1 : 2;
    g_woods3d.perfect=g_woods3d.hits==hunt_target_count();
    hunt_sfx_play(g_woods3d.perfect ? SFX_PERFECT : SFX_TALLY);
    set_phase(FADE_OUT);
    break;
   }
#else
   shoot_camera=camera;
   retrieval_from=dog;
   set_phase(CAMERA_RETURN);
#endif
  }
#else
  if (retrieval < duck_count && tick <= 20)
   dog = mix(retrieval_from,retrieval_to,tick,20);
  if (retrieval < duck_count && tick == 21) {
   ducks[retrieval].state = HIDDEN;
   hunt_sfx_play(SFX_RETRIEVE);
  }
  if (retrieval < duck_count && tick >= 21 && tick <= 45)
   dog = mix(retrieval_to,trail(camera.z+275),tick-21,24);
  if (!caught)
   dog.y = w3_ground(dog.z) + (tick % 12 < 6 ? tick % 6 : 5 - tick % 6) * 3;
  if (tick == (retrieval < duck_count ? 76 : 52)) {
   if (retrieval == 0 && duck_count == 2 && ducks[1].state == LANDED) {
    retrieval++;
    next_retrieval();
   } else {
    shoot_camera = camera;
    retrieval_from = dog;
    set_phase(CAMERA_RETURN);
   }
  }
#endif
  break;
 case CAMERA_RETURN:
  dog = mix(retrieval_from,trail(camera.z+245),tick,25);
  camera.pitch = lerp(shoot_camera.pitch,0,tick,25);
  camera.yaw = lerp(shoot_camera.yaw,(w3_path(camera.z+245)-camera.x)*256/245,tick,25);
  if (tick == 25) set_phase(RESUME);
  break;
 case RESUME:
  if (tick == 15) {
   if (mode == 1 || site == 4) {
    g_woods3d.perfect = g_woods3d.hits == hunt_target_count();
    hunt_sfx_play(g_woods3d.perfect ? SFX_PERFECT : SFX_TALLY);
#ifdef HUNTPS1_WOODS_SCENE_ONE
    set_phase(FADE_OUT);
#else
    set_phase(RESULTS);
#endif
   } else {
    site++;
    set_phase(WALK);
   }
  }
  break;
 }
}

typedef struct { int fired, awarded; } Claims;

static Claims resolve_claims(const int target[2], int fired, int available, int remaining, int first)
{
 Claims result = {0,0};
 for (int k = 0; k < 2 && remaining; k++) {
  int port = k ^ first;
  if (!(fired & (1 << port))) continue;
  result.fired |= 1 << port;
  remaining--;
  int duck = target[port];
  if (duck < 0 || !(available & (1 << duck))) continue;
  available &= ~(1 << duck);
  result.awarded |= 1 << port;
 }
 return result;
}

static int check_claims(void)
{
 const int same[] = {0,0}, split[] = {0,1};
 int passed = 0;
 Claims c = resolve_claims(same,3,3,2,0);
 if (c.fired == 3 && c.awarded == 1) passed |= 1;
 c = resolve_claims(same,3,3,2,1);
 if (c.fired == 3 && c.awarded == 2) passed |= 2;
 c = resolve_claims(split,3,3,2,0);
 if (c.fired == 3 && c.awarded == 3) passed |= 4;
 c = resolve_claims(split,3,3,1,1);
 if (c.fired == 2 && c.awarded == 2) passed |= 8;
 c = resolve_claims(split,3,0,2,0);
 if (c.fired == 3 && c.awarded == 0) passed |= 16;
 return passed;
}
static void publish_p2_profile(void)
{
 g_woods_p2_profile[0] = p2_cal.has_cal;
 g_woods_p2_profile[1] = p2_cal.ox;
 g_woods_p2_profile[2] = p2_cal.oy;
 g_woods_p2_profile[3] = p2_status;
}

static void calibration_input(int port, PADTYPE sample, unsigned down, int edge)
{
#ifdef HUNTPS1_WOODS_INTEGRATED
 if (port==1 && phase==SELECT && (down&GCON_B)) leave_requested=1;
#endif
 if (phase == SELECT && (down & GCON_A)) {
  guncal_reset(&p2_pending);
  cal_port = port;
  p2_point = 0;
  set_phase(CAL_P2);
  return;
 }
 if (phase != CAL_P2 || port != cal_port) return;
 if (down & GCON_B) {
  set_phase(SELECT);
  return;
 }
 if (p2_point == GUNCAL_POINTS) {
  if (port == 1 && (down & GCON_A) && p2_pending.has_cal) {
   p2_cal = p2_pending;
   p2_status = hunt_cal_save_p2(&p2_cal);
   publish_p2_profile();
   set_phase(SELECT);
  } else if (edge && p2_pending.has_cal) {
   if (port == 0) {
    calibration = p2_pending;
    g_woods3d.cal_x = calibration.ox;
    g_woods3d.cal_y = calibration.oy;
   } else {
    p2_cal = p2_pending;
    p2_status = CAL_SAVE_NONE;
    publish_p2_profile();
   }
   set_phase(SELECT);
  }
  return;
 }
 if (!edge || tick < 8 || !guncon_has_light(sample.gun_x,sample.gun_y))
  return;
 int x,y;
 map_gun_to_screen(sample.gun_x,sample.gun_y,&x,&y);
 if (guncal_add_shot(&p2_pending,p2_point,x,y,sample.gun_x,sample.gun_y) &&
     guncal_point_full(&p2_pending,p2_point)) {
  tick = 0;
  if (++p2_point == GUNCAL_POINTS) guncal_compute(&p2_pending);
 }
}
static int poll_pause(const PADTYPE samples[2])
{
 enum { BACK = 1, UP = 2, DOWN = 4, CONFIRM = 8, B_BUTTON = 16 };
 unsigned buttons = 0;
 for (int i = 0; i < 2; i++) {
  const PADTYPE *p = &samples[i];
  if (p->stat != 0) continue;
  if (p->type == PAD_ID_GUNCON) {
   if (!(p->btn & GCON_B)) buttons |= B_BUTTON;
   if (!(p->btn & GCON_A)) buttons |= UP;
   if (!(p->btn & GCON_TRIGGER)) buttons |= CONFIRM;
  } else if (p->type == PAD_ID_DIGITAL || p->type == PAD_ID_ANALOG) {
   if (!(p->btn & PAD_START) || (paused && !(p->btn & PAD_SELECT))) buttons |= BACK;
   if (!(p->btn & PAD_UP) || !(p->btn & PAD_TRIANGLE)) buttons |= UP;
   if (!(p->btn & PAD_DOWN)) buttons |= DOWN;
   if (!(p->btn & PAD_SQUARE)) buttons |= B_BUTTON;
   if (!(p->btn & PAD_CROSS)) buttons |= CONFIRM;
  }
 }
 unsigned down = buttons & ~pause_buttons;
 pause_buttons = buttons;
 if (!paused) {
  if (!(down & (BACK | B_BUTTON)) || phase == SELECT || phase == CAL_P2 || phase == RESULTS)
   return 0;
  paused = 1;
  pause_item = 0;
  hunt_sfx_pause(1);
 } else if (down & BACK) {
  paused = 0;
  hunt_sfx_pause(0);
 } else if (down & CONFIRM) {
  paused = 0;
  if (pause_item == 0) {
   hunt_sfx_pause(0);
  } else {
   hunt_sfx_reset();
   if (pause_item == 1) begin(mode,players);
   else {
#ifdef HUNTPS1_WOODS_INTEGRATED
    leave_requested = 1;
#else
    set_phase(SELECT);
#endif
   }
  }
 } else if (down & (UP | DOWN | B_BUTTON)) {
  pause_item = (pause_item + (down & UP ? 2 : 1)) % 3;
 }
 for (int i = 0; i < 2; i++) {
  was_present[i] = samples[i].stat == 0 && samples[i].type == PAD_ID_GUNCON;
  held[i] = was_present[i] && !(samples[i].btn & GCON_TRIGGER);
 }
 return 1;
}

static void poll(const W3Frame *visible)
{
 PADTYPE samples[2] = {*(const volatile PADTYPE *)pads[0], *(const volatile PADTYPE *)pads[1]};
 if (poll_pause(samples)) return;
#if defined(HUNTPS1_WOODS_INTEGRATED) && !defined(HUNTPS1_WOODS_SCENE_ONE)
 if (players == 1) {
#else
 if (players == 1 && phase == SELECT) {
#endif
  PADTYPE a = samples[0];
  PADTYPE b = samples[1];
  if (a.stat == 0 && a.type == PAD_ID_GUNCON) solo_port = 0;
  else if (b.stat == 0 && b.type == PAD_ID_GUNCON) solo_port = 1;
 }
 int target[2] = {-1,-1}, fired = 0;
 for (int i = 0; i < 2; i++) {
  PADTYPE sample = samples[i];
  int present = sample.stat == 0 && sample.type == PAD_ID_GUNCON;
  int pressed = present && !(sample.btn & GCON_TRIGGER);
  int edge = was_present[i] && pressed && !held[i];
  if (reload[i]) reload[i]--;
  g_woods3d.present[i] = present;
  was_present[i] = present;
  held[i] = pressed;
  if (!i && sample.stat == 0 && sample.type == PAD_ID_DIGITAL) {
   uint16_t down = buttons_before & ~sample.btn;
   buttons_before = sample.btn;
   if (phase == SELECT) {
#ifndef HUNTPS1_WOODS_SCENE_ONE
    if (down & PAD_UP) mode = (mode+2)%3;
    if (down & PAD_DOWN) mode = (mode+1)%3;
#endif
    if (down & PAD_LEFT) players = 1;
    if (down & PAD_RIGHT) players = 2;
    if (down & PAD_START) begin(mode,players);
#ifdef HUNTPS1_WOODS_INTEGRATED
    if (down & PAD_SELECT) leave_requested = 1;
#endif
   } else if (down & PAD_SELECT) {
    set_phase(SELECT);
   }
  } else if (!i) {
   buttons_before = 0xffff;
  }
  if (!i && present) {
   uint16_t down = gun_buttons_before & ~sample.btn;
   gun_buttons_before = sample.btn;
#if !defined(HUNTPS1_WOODS_INTEGRATED) || defined(HUNTPS1_WOODS_SCENE_ONE)
   if ((phase == SELECT && (down & GCON_A)) || (phase == CAL_P2 && cal_port == 0)) {
    calibration_input(0,sample,down,edge);
    continue;
   }
#endif
   if (phase == SELECT) {
#if defined(HUNTPS1_WOODS_INTEGRATED) && !defined(HUNTPS1_WOODS_SCENE_ONE)
    if (down & GCON_A) mode = (mode+1)%3;
#endif
#ifndef HUNTPS1_WOODS_INTEGRATED
    if (down & GCON_B) players = 3-players;
#endif
    if (edge) {
#if !defined(HUNTPS1_WOODS_INTEGRATED) || defined(HUNTPS1_WOODS_SCENE_ONE)
     if (players == 1) solo_port = 0;
#endif
     begin(mode,players);
    }
#ifdef HUNTPS1_WOODS_INTEGRATED
    if (down & GCON_B) {
     leave_requested = 1;
    }
#endif
   } else if (phase == RESULTS && (down & GCON_B)) {
    set_phase(SELECT);
   }
  } else if (!i) {
   gun_buttons_before = 0xffff;
  }
  if (i) {
   int was_select = phase == SELECT;
   unsigned down = present ? p2_buttons & ~sample.btn : 0;
   p2_buttons = present ? sample.btn : 0xffff;
   if (phase == RESULTS && (down & GCON_B)) set_phase(SELECT);
   else calibration_input(1,sample,down,edge);
#if !defined(HUNTPS1_WOODS_INTEGRATED) || defined(HUNTPS1_WOODS_SCENE_ONE)
   if (was_select && phase == SELECT && edge && players == 1) solo_port = 1;
#endif
   if (was_select && phase == SELECT && solo_port == 1 && edge)
    begin(mode,players);
  }
  if (edge) g_woods3d.edges[i]++;
  if (!edge || (players == 1 && i != solo_port) || phase != SHOOT || !visible->active || reload[i])
   continue;
  fired |= 1 << i;
  reload[i] = 6;
  if (guncon_has_light(sample.gun_x,sample.gun_y)) {
   int x,y;
   map_gun_to_screen(sample.gun_x,sample.gun_y,&x,&y);
   guncal_apply(i ? &p2_cal : &calibration,x,y,&x,&y);
   target[i] = w3_pick(visible,x,y);
   g_woods3d.aim_x[i] = x;
   g_woods3d.aim_y[i] = y;
   g_woods3d.aim_pick[i] = target[i];
  } else {
   g_woods3d.invalid[i]++;
  }
 }
#ifdef HUNTPS1_WOODS_REPLAY
 /* Two fixed opportunities per encounter. Use the actual displayed
    triangles and the same shot ownership path as controller input. */
#ifdef HUNTPS1_WOODS_SCENE_ONE
 if (
#ifdef HUNTPS1_WOODS_MEADOW
     players==2 &&
#endif
     phase == SHOOT && visible->active && (tick == 14 || tick == 26) && ammo == (tick == 14 ? 3 : 2)) {
  int duck = tick == 14 ? 0 : 1;
  int port = players == 2 ? duck : 0;
  target[port] = w3_pick(visible,visible->center[duck][0],visible->center[duck][1]);
  fired |= 1 << port;
 }
#else
 if (phase == SHOOT && visible->active && mode == 2) {
  for (int i = 0; i < players; i++) {
   if (tick != 14 + 12*i || reload[i]) continue;
   int x = visible->center[i][0], y = visible->center[i][1];
   target[i] = w3_pick(visible,x,y);
   fired |= 1 << i;
   reload[i] = 6;
  }
 }
#endif
#endif
 int available = 0;
 for (int i = 0; i < duck_count; i++)
  if (ducks[i].state == FLYING) available |= 1 << i;
 Claims c = resolve_claims(target,fired,available,ammo,priority);
 if (fired == 3) priority ^= 1;
 for (int i = 0; i < 2; i++) {
  if (!(c.fired & (1 << i))) continue;
  ammo--;
  g_woods3d.shots++;
  g_woods3d.shot_site[site]++;
  hunt_sfx_play(SFX_SHOT);
  int t = target[i];
  if (c.awarded & (1 << i)) {
   ducks[t].state = FALLING;
   ducks[t].lift = 0;
   caught++;
   hunt_sfx_play(SFX_HIT);
   g_woods3d.hits++;
   g_woods3d.hits_site[site]++;
   if (i) g_woods3d.p2++; else g_woods3d.p1++;
  }
 }
}

#ifdef HUNTPS1_WOODS_SCENE_ONE
static void scene_fade(W3Frame *f, int amount)
{
 static TILE shades[2];
 static DR_TPAGE modes[2];
 int index = f == &buffers[1];
 TILE *shade = &shades[index];
 DR_TPAGE *mode = &modes[index];
 setTile(shade);
 setXY0(shade,0,0);
 setWH(shade,320,240);
 setRGB0(shade,amount,amount,amount);
 setSemiTrans(shade,1);
 setDrawTPage(mode,1,0,getTPage(0,2,0,0));
 addPrim(&f->ot[0],shade);
 addPrim(&f->ot[0],mode);
}
#endif
static int render(W3Frame *f)
{
 static const char *sites[] = {"FOREST ENTRANCE","WINDING TRAIL","POND CLEARING",
  "DENSE WOODS","FINAL CLEARING"};
 static const char *modes[] = {"WALKTHROUGH","SINGLE ENCOUNTER","FIVE ENCOUNTERS"};
 char line[164];
 STAMP(forest_start);
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 w3_use_walk_visibility(phase==WALK ? tick : -1);
#endif
 w3_begin(f,camera,site);
 if (paused) {
  f->draw.isbg = 1;
  setRGB0(&f->draw,0,0,0);
  snprintf(line,sizeof(line),"PAUSE\n\n%c RESUME\n%c RESTART\n%c MAIN MENU",
   pause_item == 0 ? '>' : ' ',pause_item == 1 ? '>' : ' ',pause_item == 2 ? '>' : ' ');
  FntSort(f->ot,text_packets[f == &buffers[1]],112,80,line);
  return 0;
 }
#ifdef HUNTPS1_WOODS_EARLY_DOG_BACKGROUND
 f->early_backdrop=phase==APPROACH || phase==DOG_FLUSH || phase==DOG_RETRIEVE;
#ifdef HUNTPS1_WOODS_INTEGRATED
 if (phase==WALK) f->early_backdrop=2;
#endif
#endif
#ifdef HUNTPS1_WOODS_FAST_SHOOT
 w3_use_fast_shoot_view(phase==CAMERA_SETTLE || phase==SHOOT);
#endif
#ifdef HUNTPS1_WOODS_PAN_CACHE
 w3_use_pan_packets(phase==CAMERA_REFRAME ? tick : -1);
#endif
#ifdef HUNTPS1_WOODS_DOG_BATCH
 w3_use_sniff_packets(phase==APPROACH ? tick : -1);
#endif
 f->draw.isbg = phase == CAL_P2;
 if (phase == CAL_P2) {
  static TILE marks[2][2];
  int buf = f == &buffers[1];
  setRGB0(&f->draw,110,115,104);
  if (p2_point < GUNCAL_POINTS) {
   int x=guncal_tx[p2_point], y=guncal_ty[p2_point];
   for (int i=0;i<2;i++) {
    TILE *m=&marks[buf][i];
    setTile(m);
    setRGB0(m,255,255,255);
    setXY0(m,x-(i?1:9),y-(i?9:1));
    setWH(m,i?3:19,i?19:3);
    addPrim(&f->ot[0],m);
   }
   snprintf(line,sizeof(line),"PORT %d GUNCON CALIBRATION\nPOINT %d/5   SHOTS %d/8\nB: CANCEL",
    cal_port+1,p2_point+1,p2_pending.pt[p2_point].n);
  } else if (p2_pending.has_cal && cal_port == 0) {
   snprintf(line,sizeof(line),"P1 OFFSET %+d, %+d\nTRIGGER: USE IN RAM\nB: CANCEL",
    p2_pending.ox,p2_pending.oy);
  } else if (p2_pending.has_cal) {
   snprintf(line,sizeof(line),"P2 OFFSET %+d, %+d\nP2 A: SAVE TO CARD\nP2 TRIGGER: USE IN RAM\nP2 B: CANCEL",
    p2_pending.ox,p2_pending.oy);
  } else {
   snprintf(line,sizeof(line),"P2 CALIBRATION INVALID\nP2 B: RETURN AND RETRY");
  }
  FntSort(f->ot,text_packets[buf],24,26,line);
  return 0;
 }
#ifdef HUNTPS1_WOODS_SCENE_ONE
 if (phase == RESULTS) {
  f->draw.isbg = 1;
  setRGB0(&f->draw,0,0,0);
 } else {
#ifdef HUNTPS1_WOODS_LEFT_FLUSH
  int rustle=0;
  if (phase == DOG_FLUSH && tick > 12) {
   int amplitude=tick < 20 ? (tick-12)*3 : (24-tick)*5;
   rustle=(tick&2) ? amplitude : -amplitude;
  }
  w3_forest(station(),side(),rustle);
#else
  w3_forest(station(),side(),phase == DOG_FLUSH ? ((tick & 3)-2)*8 : 0);
#endif
 }
#else
 w3_forest(station(),side(),phase == DOG_FLUSH ? ((tick & 3)-2)*8 : 0);
#endif
 SPAN(forest,forest_start);
#ifdef HUNTPS1_WOODS_SPLIT_PAN
 int split=0;
 uint32_t split_link=0;
 if (!site && (phase==CAMERA_REFRAME
#ifdef HUNTPS1_WOODS_SPLIT_FALL
    || phase==DUCK_FALL
#endif
#ifdef HUNTPS1_WOODS_DOG_BATCH
    || f->early_backdrop
#endif
    )) {
  int max_depth=0;
#ifdef HUNTPS1_WOODS_DOG_BATCH
  if (f->early_backdrop) max_depth=w3_camera_depth(dog);
#endif
  for (int i=0;i<duck_count;i++) {
   int depth=w3_camera_depth(ducks[i].p);
   if (depth>max_depth) max_depth=depth;
  }
  /* Leave 192 world units beyond each duck anchor for wings and
     splash ripples. Actor packets go below the far chain. */
  split=2+(max_depth+192)/10;
  if (split>=W3_OT-1) split=0;
  if (split) {
   split_link=f->ot[split];
   termPrim(&f->ot[split]);
#ifdef HUNTPS1_WOODS_DOG_BATCH
   if (f->early_backdrop) DrawOTag(&f->ot[W3_OT-1]);
   else
#endif
    DrawOTagEnv(&f->ot[W3_OT-1],&f->draw);
  }
 }
#endif
 STAMP(dog_start);
 if (phase != SELECT
#ifdef HUNTPS1_WOODS_SCENE_ONE
     && phase != RESULTS
#endif
#ifdef HUNTPS1_WOODS_ART
     && (phase < CAMERA_REFRAME || phase > DUCK_FALL)
#endif
#ifdef HUNTPS1_WOODS_MEADOW
     && phase != CAMERA_SETTLE
#endif
 ) {
  int posture = 0, heading = (w3_path(dog.z+105)-w3_path(dog.z))*6;
#ifdef HUNTPS1_WOODS_LEFT_FLUSH
  if (phase == APPROACH) {
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   int moving=run_heading(trail(camera.z+DOG_LEAD),investigate());
   heading=lerp(heading,moving,eased_progress(tick-DOG_SCENT_RAISED,9),256);
   heading=lerp(heading,run_heading(investigate(),w3_flush_cover(station(),side())),
    eased_progress(tick-DOG_ARRIVE+9,12),256);
   posture=tick<DOG_SCENT_RAISED || (tick>=DOG_ARRIVE && tick<DOG_RAISED) ? 1 : 5;
#else
   heading=lerp(heading,-740,eased_progress(tick,25),256);
   posture=tick < 25 || tick >= 46 ? 5 : 1;
#endif
  } else if (phase == DOG_FLUSH) {
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   posture=9;
   heading=run_heading(investigate(),w3_flush_cover(station(),side()));
#else
   posture=tick <= 4 ? 6 : 2;
   heading=-740;
#endif
  } else
#else
  if (phase == APPROACH && tick > APPROACH_STEPS) {
   posture = 1;
   heading = side()*350;
  } else if (phase == DOG_FLUSH) {
   posture = 2;
   heading = side()*350;
  } else
#endif
  if (phase == DOG_RETRIEVE) {
#ifdef HUNTPS1_WOODS_MEADOW
   if (retrieval>=duck_count) {
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
    heading=sit_heading(run_heading(retrieval_from,retrieval_return),eased_progress(tick-LAUGH_WALK,9));
    if (tick>=LAUGH_WALK) posture=10;
#else
    posture=4;
    heading=2048;
#endif
   } else {
    heading=carrying ? run_heading(retrieval_to,retrieval_return) :
     run_heading(retrieval_from,retrieval_to);
    if (!carrying && tick>=retrieval_out_ticks) posture=1;
    if (carrying && tick>=retrieval_out_ticks+8+retrieval_back_ticks) posture=3;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
    if (carrying && final_pickup() && sit_tick()>=0) {
     heading=sit_heading(heading,eased_progress(sit_tick(),9));
     posture=8;
    }
#endif
#ifdef HUNTPS1_WOODS_LAKE
    if (w3_in_pond(dog.x,dog.z)) posture=7;
#endif
   }
#else
   if (!caught) posture = 4;
   else if (tick > 45) posture = 3;
   if (!caught || tick > 20) heading = 2048;
   else heading = (retrieval_to.x > retrieval_from.x ? 1 : -1)*350;
#endif
  }
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  if (phase==FADE_OUT && seated_finish) {
   posture=8;
   heading=sit_heading(run_heading(seated_finish==1 ? retrieval_to : retrieval_from,retrieval_return),256);
  }
#endif
  int gait = -1;
  if (phase == WALK || (phase == APPROACH &&
#ifdef HUNTPS1_WOODS_LEFT_FLUSH
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
    tick > DOG_SCENT_RAISED && tick < DOG_ARRIVE
#else
    tick > 6 && tick < 25
#endif
#else
    tick <= APPROACH_STEPS
#endif
  ))
   gait = g_woods3d.ticks;
  else if (phase == DOG_RETRIEVE && caught &&
#ifdef HUNTPS1_WOODS_MEADOW
    (tick<retrieval_out_ticks || (carrying && tick<retrieval_out_ticks+8+retrieval_back_ticks))
#else
    tick <= 45
#endif
  )
   gait = g_woods3d.ticks*3;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
  if (phase==DOG_RETRIEVE && !caught && tick<LAUGH_WALK) gait=g_woods3d.ticks;
  int pose_tick=phase==APPROACH && posture==1 ?
   (tick<DOG_SCENT_RAISED ? (tick<DOG_SCENT_HOLD ? tick : DOG_SCENT_RAISED-tick) :
    tick<DOG_SNIFF_END ? tick-DOG_ARRIVE : DOG_RAISED-tick) :
    posture==8 ? (seated_finish ? 55+tick : sit_tick()) :
    posture==10 ? tick-LAUGH_WALK : tick;
  w3_dog(dog,gait,posture,heading,pose_tick);
#else
  w3_dog(dog,gait,posture,heading,tick);
#endif
 }
 SPAN(dog,dog_start);
 STAMP(actors_start);
 if ((phase >= CAMERA_REFRAME && phase <= DOG_RETRIEVE)
#ifdef HUNTPS1_WOODS_MEADOW
     || phase == CAMERA_SETTLE
#endif
 ) {
  for (int i = 0; i < duck_count; i++) {
   Duck *d = &ducks[i];
   W3Point position=d->p;
#ifdef HUNTPS1_WOODS_FAST_SHOOT
   if (phase==SHOOT && halfway_ducks && d->state==FLYING)
    position=mix(previous_ducks[i],position,1,2);
#endif
#ifdef HUNTPS1_WOODS_LAKE
   if (d->state==LANDED && d->lift<18 && w3_in_pond(d->p.x,d->p.z))
    w3_pond_ripple(d->p,d->lift);
#endif
   if (d->state == FLYING || d->state == FALLING || d->state == LANDED
#ifdef HUNTPS1_WOODS_ART
       || d->state == FLYING_OUT
#endif
   )
    w3_duck(position,
#ifdef HUNTPS1_WOODS_ART
     d->state == FLYING || d->state == FLYING_OUT ? d->flight_age+i*5 :
#ifdef HUNTPS1_WOODS_DETAIL
#ifdef HUNTPS1_WOODS_SHOWCASE
     d->state == LANDED ? W3_DUCK_FOLDED :
#endif
     d->state == FALLING ? -1-d->lift : 2,
#else
     2,
#endif
     d->heading,
#else
     d->state == FLYING ? g_woods3d.ticks : 2,d->direction,
#endif
     phase == SHOOT && d->state == FLYING ? i : -1
#ifdef HUNTPS1_WOODS_ART
     ,d->state == FLYING_OUT ? d->lift*256/FLY_OUT_TICKS : 0
#endif
    );
  }
 #ifdef HUNTPS1_WOODS_MEADOW
  if ((phase==DOG_RETRIEVE
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
    || (phase==FADE_OUT && seated_finish)
#endif
   ) && carrying) {
   int heading=run_heading(retrieval_to,retrieval_return);
   int mouth_y=dog.y+55;
   int release=0;
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
   int mouth_reach=78;
   int seated_tick=seated_finish ? 55+tick : sit_tick();
   if (final_pickup() && seated_tick>=0) {
    int settle=eased_progress(seated_tick,9);
    heading=sit_heading(heading,settle);
    mouth_reach-=6*settle/256;
    release=eased_progress(seated_tick-8,9);
   }
#else
   int mouth_reach=68;
#endif
#ifdef HUNTPS1_WOODS_LAKE
   if (w3_in_pond(dog.x,dog.z)) mouth_y=W3_WATER_Y+14;
#endif
   W3Point bird={dog.x+isin(heading)*mouth_reach/4096,mouth_y,
    dog.z+icos(heading)*mouth_reach/4096};
   bird=mix(bird,(W3Point){dog.x-95,dog.y+2,dog.z-18},release,256);
   int bird_heading=heading+1024;
   int drop_turn=(-1024-bird_heading+2048)&4095;
   bird_heading+=(drop_turn-2048)*release/256;
   w3_duck(bird,
#ifdef HUNTPS1_WOODS_SHOWCASE
    W3_DUCK_FOLDED,
#else
    2,
#endif
    bird_heading,-1,0);
  }
 #else
  if (phase == DOG_RETRIEVE && retrieval < duck_count && tick >= 21)
   w3_duck((W3Point){dog.x,dog.y+46,dog.z-61},2,
#ifdef HUNTPS1_WOODS_ART
    side()*1024,
#else
    side(),
#endif
    -1
#ifdef HUNTPS1_WOODS_ART
    ,0
#endif
   );
 #endif
 }
 f->active = phase == SHOOT;
#ifdef HUNTPS1_WOODS_ART
 f->active &= f->center[0][0] >= 0 || f->center[1][0] >= 0;
#endif
 if (phase == SELECT) {
#if defined(HUNTPS1_WOODS_SCENE_ONE) && defined(HUNTPS1_WOODS_INTEGRATED)
  const char *controls = "LEFT/RIGHT: PLAYERS\nTRIGGER OR START: BEGIN\nB OR SELECT: DELUXE";
#elif defined(HUNTPS1_WOODS_SCENE_ONE)
  const char *controls = "B OR LEFT/RIGHT: PLAYERS\nTRIGGER OR START: BEGIN";
#elif defined(HUNTPS1_WOODS_INTEGRATED)
  const char *controls = "A OR UP/DOWN: ROUTE\nLEFT/RIGHT: PLAYERS\nTRIGGER OR START: BEGIN\nB OR SELECT: DELUXE";
#else
  const char *controls = "UP/DOWN: ROUTE\nB OR LEFT/RIGHT: PLAYERS\nTRIGGER OR START: BEGIN";
#endif
#if defined(HUNTPS1_WOODS_INTEGRATED) && !defined(HUNTPS1_WOODS_SCENE_ONE)
  const char *cal_hint = "P2 A: CALIBRATE";
#else
  const char *cal_hint = "GUNCON A: CALIBRATE";
#endif
#if !defined(HUNTPS1_WOODS_INTEGRATED) || defined(HUNTPS1_WOODS_SCENE_ONE)
  char aim_status[42];
  int port=solo_port;
  snprintf(aim_status,sizeof(aim_status),"%s%d: %s   %s%d: %s",
   players==1 && port==0 ? ">" : "",1,calibration.has_cal ? "CAL" : "RAW",
   players==1 && port==1 ? ">" : "",2,p2_cal.has_cal ? "CAL" : "RAW");
#else
  const char *aim_status=p2_status != CAL_SAVE_NONE ? hunt_cal_save_status(p2_status) :
   p2_cal.has_cal ? "P2 CAL ACTIVE IN RAM" : "P2 USES RAW AIM";
#endif
  snprintf(line,sizeof(line),"INTO THE WOODS\n\n%s  /  %dP\n%s\n\n%s\n%s",
#ifdef HUNTPS1_WOODS_SCENE_ONE
   sites[0],
#else
   modes[mode],
#endif
   players,controls,cal_hint,aim_status);
 }
 else if (phase == RESULTS)
  snprintf(line,sizeof(line),"%s\n\nDUCKS %d/%d   AIM %d%%\nSCORE %04d  ESCAPED %d\nP1 %d   P2 %d\n\nB OR SELECT: RETURN",
   g_woods3d.perfect ? "PERFECT HUNT!" : "HUNT COMPLETE",g_woods3d.hits,
   hunt_target_count(),g_woods3d.shots ? 100*g_woods3d.hits/g_woods3d.shots : 0,
   g_woods3d.hits*500,g_woods3d.misses,g_woods3d.p1,g_woods3d.p2);
 else
#ifdef HUNTPS1_WOODS_CINEMATIC
  line[0] = 0;
#else
  snprintf(line,sizeof(line),"%s\n%s",sites[site],phase == SHOOT ?
   (ammo ? "SHOOT" : "OUT OF SHOTS") : "");
#endif

 if (line[0])
  FntSort(f->ot,text_packets[f == &buffers[1]],24,26,line);
#ifdef HUNTPS1_WOODS_SCENE_ONE
 if (phase == FADE_IN || phase == FADE_OUT)
  scene_fade(f,255 * (phase == FADE_IN ? FADE_TICKS-tick : tick) / FADE_TICKS);
#ifdef HUNTPS1_WOODS_INTEGRATED
 else if (phase == WALK && tick < FADE_TICKS)
  scene_fade(f,255 * (FADE_TICKS-tick) / FADE_TICKS);
#endif
#endif
 SPAN(actors_hud,actors_start);
#ifdef HUNTPS1_WOODS_SPLIT_PAN
 if (split) {
  STAMP(split_submit);
  DrawSync(0);
  f->ot[split]=split_link;
  DrawOTag(&f->ot[split]);
  SPAN(submit,split_submit);
  return 1;
 }
#endif
#ifdef HUNTPS1_WOODS_EARLY_DOG_BACKGROUND
 if (f->early_backdrop) {
  STAMP(world_submit);
  /* The backdrop has its own DMA chain. The world list can be queued
     behind it without touching the draw environment or its packets. */
  DrawOTag(&f->ot[W3_OT-1]);
  SPAN(submit,world_submit);
  return 1;
 }
#endif
 return 0;
}

volatile int g_woods_startup_vblanks[5];

static void run_woods(int selected_players, int selected_mode)
{
 int startup = VSync(-1);
 DrawSync(0);
 InitGeom();
 for (int i = 0; i < 2; i++) {
  SetDefDrawEnv(&buffers[i].draw,0,i*240,320,240);
  SetDefDispEnv(&buffers[i].display,0,i*240,320,240);
  buffers[i].draw.isbg = 0;
  buffers[i].draw.dtd = 1;
  buffers[i].forest_valid = 0;
#ifdef HUNTPS1_WOODS_EARLY_DOG_BACKGROUND
  buffers[i].early_backdrop=0;
#endif
  buffers[i].active = buffers[i].count = 0;
 }
 w3_upload_foliage();
 g_woods_startup_vblanks[0] = VSync(-1) - startup;
 guncal_reset(&p2_cal);
 p2_status = hunt_cal_load_p2(&p2_cal);
 publish_p2_profile();
 g_woods_startup_vblanks[1] = VSync(-1) - startup;
 g_woods3d.cal_x = calibration.ox;
 g_woods3d.cal_y = calibration.oy;
 g_woods3d.claim_checks = check_claims();
 mode = selected_mode;
 players = selected_players;
 site = phase = tick = leave_requested = 0;
 paused = pause_item = 0;
 pause_buttons = 0;
 camera = (W3Camera){0};
#if defined(HUNTPS1_WOODS_INTEGRATED) && defined(HUNTPS1_WOODS_SCENE_ONE)
 /* The entrance camera path and site are fixed. Packet caches live in RAM. */
 static int entrance_prepared;
 if (!entrance_prepared) {
  entrance_prepared = 1;
#endif
#ifdef HUNTPS1_WOODS_APPROVED_ASSETS
 w3_prepare_walk_visibility(walk_camera,WALK_TICKS+1);
#endif
#ifdef HUNTPS1_WOODS_WALK_CACHE
 w3_prepare_walk_packets(&buffers[0],walk_camera,WALK_TICKS,station(),side());
#endif
#ifdef HUNTPS1_WOODS_PAN_CACHE
 w3_prepare_pan_packets(&buffers[0],pan_camera,CAMERA_TRACK_TICKS,station(),side());
#endif
#ifdef HUNTPS1_WOODS_DOG_BATCH
 w3_prepare_sniff_packets(&buffers[0],sniff_camera,15,station(),side());
#endif
#if defined(HUNTPS1_WOODS_INTEGRATED) && defined(HUNTPS1_WOODS_SCENE_ONE)
 }
#endif
 g_woods_startup_vblanks[2] = VSync(-1) - startup;
 buttons_before = gun_buttons_before = p2_buttons = 0xffff;
 memset(was_present,0,sizeof(was_present));
 memset(held,0,sizeof(held));
 memset(reload,0,sizeof(reload));
#if defined(HUNTPS1_WOODS_INTEGRATED) && defined(HUNTPS1_WOODS_SCENE_ONE)
 begin(selected_mode,selected_players);
 /* Consume a trigger held over from Deluxe player selection. */
 for (int i=0;i<2;i++) held[i]=1;
#endif
 int draw = 0, display = 1, frame = 0;
 if (!render(&buffers[draw]))
  DrawOTagEnv(&buffers[draw].ot[W3_OT-1],&buffers[draw].draw);
 int start = VSync(-1);
 g_woods_startup_vblanks[3] = start - startup;
 int rendered_phase = phase, rendered_site = site;
#ifdef HUNTPS1_WOODS_FAST_SHOOT
 int next_shoot_step=0, fast_present=0;
#endif
 for (;;) {
  int gpu_wait = VSync(-1);
  STAMP(wait_start);
  DrawSync(0);
  SPAN(gpu_wait,wait_start);
  gpu_wait = VSync(-1) - gpu_wait;
  if (gpu_wait > g_woods3d.max_gpu_wait_vblanks) g_woods3d.max_gpu_wait_vblanks = gpu_wait;
  VSync(0);
  poll(&buffers[display]);
  if (leave_requested) break;
  PutDispEnv(&buffers[draw].display);
  SetDispMask(1);
  if (!frame) g_woods_startup_vblanks[4] = VSync(-1) - startup;
  display = draw;
  draw ^= 1;
  int now = VSync(-1);
  int elapsed = now-start;
  start = now;
#ifdef HUNTPS1_WOODS_PROFILE
  if (rendered_phase > SELECT &&
#ifdef HUNTPS1_WOODS_CINEMATIC
      rendered_phase != CAL_P2 &&
#else
      rendered_phase <= RESULTS &&
#endif
      g_woods_trace_count < WOODS_TRACE_CAPACITY) {
   measured.phase = rendered_phase;
   measured.site = rendered_site;
   measured.gap = elapsed > 255 ? 255 : elapsed;
#ifdef HUNTPS1_WOODS_MEADOW
   measured.spare = buffers[display].active;
#endif
   g_woods_trace[g_woods_trace_count++] = measured;
  }
#endif
  if (frame) g_woods3d.display_vblanks += elapsed;
  if (frame && elapsed > 2) g_woods3d.overruns++;
  if (frame && elapsed > g_woods3d.max_vblanks) g_woods3d.max_vblanks = elapsed;
  W3Frame *f = &buffers[display];
  if (f->count > g_woods3d.max_polys) g_woods3d.max_polys = f->count;
  g_woods3d.overflow += f->overflow;
  for (int i = 0; i < 2; i++) {
   g_woods3d.center[i][0] = f->center[i][0];
   g_woods3d.center[i][1] = f->center[i][1];
   g_woods3d.eligible[i] = f->center[i][0] >= 0;
  }
  g_woods3d.active = f->active;
  g_woods3d.rendered_site = f->forest_site;
  g_woods3d.frame = ++frame;
  STAMP(logic_start);
#ifdef HUNTPS1_WOODS_FAST_SHOOT
  if (fast_present && elapsed>1) hunt_sfx_frame();
#endif
  hunt_sfx_frame();
#ifdef HUNTPS1_WOODS_FAST_SHOOT
  halfway_ducks=0;
  if (phase!=SHOOT || now>=next_shoot_step) {
   if (phase==SHOOT) {
    for (int i=0;i<duck_count;i++) previous_ducks[i]=ducks[i].p;
    next_shoot_step=now+2;
    halfway_ducks=1;
   } else {
    next_shoot_step=now+2;
   }
#endif
  if (!paused) {
   step();
   g_woods3d.ticks++;
  }
#ifdef HUNTPS1_WOODS_FAST_SHOOT
  }
#endif
  if (leave_requested) break;
  SPAN(logic_audio,logic_start);
  g_woods3d.phase = phase;
  g_woods3d.phase_ticks = tick;
  g_woods3d.site = site;
  g_woods3d.players = players;
  g_woods3d.mode = mode;
  g_woods3d.ammo = ammo;
  int cpu_start = VSync(-1);
  rendered_phase = phase;
  rendered_site = site;
  int submitted=render(&buffers[draw]);
  int cpu_elapsed = VSync(-1) - cpu_start;
  if (cpu_elapsed > g_woods3d.max_cpu_vblanks) g_woods3d.max_cpu_vblanks = cpu_elapsed;
  if (!submitted) {
   STAMP(submit_start);
   DrawOTagEnv(&buffers[draw].ot[W3_OT-1],&buffers[draw].draw);
   SPAN(submit,submit_start);
  }
#ifdef HUNTPS1_WOODS_FAST_SHOOT
  /* The next loop samples the still-displayed buffer at the next VBlank.
     Simulation remains on a two-VBlank deadline while rendering can flip. */
  fast_present=!paused && rendered_phase==SHOOT;
  if (fast_present) continue;
#endif
  /* If the renderer already crossed the midpoint VBlank, sample the
     current display now instead of spending an extra refresh waiting. */
  if (VSync(-1) == now) VSync(0);
  poll(f);
  hunt_sfx_frame();
  if (leave_requested) break;
 }
}
#ifdef HUNTPS1_WOODS_INTEGRATED
int woods3d_run_deluxe(uint8_t input[2][34], const GunCal *saved_calibration,
 int selected_players, int primary_port)
{
 pads = input;
 calibration = *saved_calibration;
 solo_port = primary_port;
 hunt_sfx_set_woods_mix(1);
 run_woods(selected_players,2);
 hunt_sfx_set_woods_mix(0);
 DrawSync(0);
 return phase == FADE_OUT && tick > FADE_TICKS;
}
#else
int main(void)
{
 static uint8_t standalone_pads[2][34];
 pads = standalone_pads;
 ResetGraph(0);
 SetVideoMode(MODE_NTSC);
 FntLoad(960,0);
 guncal_reset(&calibration);
 pads[0][0] = pads[1][0] = 0xff;
 InitPAD(pads[0],34,pads[1],34);
 StartPAD();
 ChangeClearPAD(0);
 hunt_cal_card_init();
 g_woods3d.card = hunt_cal_load(&calibration);
 hunt_sfx_init();
 hunt_sfx_set_woods_mix(1);
 run_woods(1,1);
 hunt_sfx_set_woods_mix(0);
 return 0;
}
#endif
