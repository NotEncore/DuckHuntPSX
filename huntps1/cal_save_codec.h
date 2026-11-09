#ifndef HUNT_CAL_SAVE_CODEC_H
#define HUNT_CAL_SAVE_CODEC_H
#include <stdint.h>
#include <string.h>
#include "../guncal/guncal.h"
/* Explicit 128-byte records, CRC including all reserved bytes. No shot arrays. */
typedef struct { uint32_t word[32]; } HuntCalRecord;
static uint32_t hunt_cal_crc(const void *buffer, unsigned int count)
{
 const uint8_t *p=(const uint8_t *)buffer;
 uint32_t crc=0xffffffffu; unsigned int i,j;
 for(i=0;i<count;i++){crc^=p[i];for(j=0;j<8;j++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
 return crc^0xffffffffu;
}
static int hunt_cal_parameters_ok(const GunCal *c)
{
 return c->has_cal==1 && (c->use_scale==0 || c->use_scale==1)
  && c->sx>=16384 && c->sx<=262144 && c->sy>=16384 && c->sy<=262144
  && c->ox>=-320 && c->ox<=320 && c->oy>=-240 && c->oy<=240;
}
static void hunt_cal_encode(HuntCalRecord *r,const GunCal *c,uint32_t sequence)
{
 memset(r,0,sizeof(*r));r->word[0]=0x4c414348u;r->word[1]=1;
 r->word[2]=0x014000f0u; /* mapping/version signature: 320x240 */
 r->word[3]=sequence;r->word[4]=c->use_scale;r->word[5]=c->sx;r->word[6]=c->sy;
 r->word[7]=c->ox;r->word[8]=c->oy;r->word[31]=hunt_cal_crc(r,124);
}
static int hunt_cal_decode(const HuntCalRecord *r,GunCal *c)
{
 GunCal candidate;
 if(r->word[0]!=0x4c414348u || r->word[1]!=1 || r->word[2]!=0x014000f0u
    || r->word[31]!=hunt_cal_crc(r,124))return 0;
 memset(&candidate,0,sizeof(candidate));candidate.has_cal=1;
 candidate.use_scale=(int32_t)r->word[4];candidate.sx=(int32_t)r->word[5];candidate.sy=(int32_t)r->word[6];
 candidate.ox=(int32_t)r->word[7];candidate.oy=(int32_t)r->word[8];
 if(!hunt_cal_parameters_ok(&candidate))return 0;
 *c=candidate;return 1;
}
static int hunt_cal_latest(const HuntCalRecord *a,const HuntCalRecord *b)
{
 GunCal c;int va=hunt_cal_decode(a,&c),vb=hunt_cal_decode(b,&c);
 if(!va)return vb?1:-1;if(!vb)return 0;
 return (int32_t)(b->word[3]-a->word[3])>0?1:0;
}
#endif
