/* Standard BIOS file (slot 1, then slot 2 if absent), using SaveTest's chunk pattern.
 * No formatting, raw-sector writes, erase, truncation or unrelated filenames.
 * Two alternating CRC records preserve the previous calibration on torn update.
 */
#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <sys/fcntl.h>
#include <string.h>
#include "cal_save.h"
#include "cal_save_codec.h"
typedef struct {
 const char *path[2];
 const char *owner;
 const char *title;
} CalProfile;
static const CalProfile p1_profile={
 {"bu00:BASCUS-00000HUNTPS1","bu10:BASCUS-00000HUNTPS1"},
 "HUNTPS1-CAL-V1","HuntPS1 Calibration"
};
static const CalProfile p2_profile={
 {"bu00:BASCUS-00000HUNT2P","bu10:BASCUS-00000HUNT2P"},
 "HUNTPS1-CAL-P2-V1","HuntPS1 P2 Calibration"
};
static int active_slot, p2_slot;
int g_cal_card_slot;
#define FILE_BYTES 8192
#define OWNER_OFFSET 256
#define SLOT_OFFSET 384
#define HEADER_BYTES 640
#define WAIT_SPINS 400000

static int sw[4],hw[4],events_ready;
static uint8_t buffer[FILE_BYTES] __attribute__((aligned(4)));
static int valid_event(int handle)
{
 /* BIOS handles are F1000000+n, negative when represented as signed int. */
 return ((uint32_t)handle & 0xff000000u)==0xf1000000u;
}
static void drain(void)
{
 int i;for(i=0;i<4;i++){TestEvent(sw[i]);TestEvent(hw[i]);}
}
static int wait_event(int *events)
{
 int i,j;for(i=0;i<WAIT_SPINS;i++)for(j=0;j<4;j++)if(TestEvent(events[j]))return j;
 return 4;
}
void hunt_cal_card_init(void)
{
 static const int spec[4]={EvSpIOE,EvSpERROR,EvSpTIMOUT,EvSpNEW};int i;
 events_ready=1;
 EnterCriticalSection();
 for(i=0;i<4;i++){
  sw[i]=OpenEvent(SwCARD,spec[i],EvMdNOINTR,0);hw[i]=OpenEvent(HwCARD,spec[i],EvMdNOINTR,0);
  if(!valid_event(sw[i]) || !valid_event(hw[i]))events_ready=0;
  if(valid_event(sw[i]))EnableEvent(sw[i]);if(valid_event(hw[i]))EnableEvent(hw[i]);
 }
 ExitCriticalSection();InitCARD(1);
}
static int ready(const CalProfile *profile)
{
 int ev;if(!events_ready)return CAL_SAVE_IO_ERROR;
 drain();if(_card_info(active_slot*16)!=1)return CAL_SAVE_IO_ERROR;
 ev=wait_event(sw);
 if(ev==2)return CAL_SAVE_NO_CARD;if(ev==1 || ev==4)return CAL_SAVE_IO_ERROR;
 if(ev==3){
  if(profile==&p2_profile)return CAL_SAVE_UNCONFIRMED;
#ifdef HUNTPS1_CAL_NO_ACK
  return CAL_SAVE_UNCONFIRMED;
#else
  /* Accept this card once using the BIOS reserved-sector write. Never reuse
   * records from a replaced card or mask NEW during subsequent operations.
   */
  memset(buffer,0,sizeof(buffer));drain();
  if(_card_clear(active_slot*16)!=1)return CAL_SAVE_IO_ERROR;
  ev=wait_event(hw);
  if(ev!=0)return ev==2?CAL_SAVE_NO_CARD:
   (ev==3?CAL_SAVE_UNCONFIRMED:CAL_SAVE_IO_ERROR);
  drain();if(_card_info(active_slot*16)!=1)return CAL_SAVE_IO_ERROR;
  ev=wait_event(sw);
  if(ev!=0)return ev==2?CAL_SAVE_NO_CARD:
   (ev==3?CAL_SAVE_UNCONFIRMED:CAL_SAVE_IO_ERROR);
#endif
 }
 drain();if(_card_load(active_slot*16)!=1)return CAL_SAVE_IO_ERROR;
 ev=wait_event(sw);
 return ev==0?0:(ev==2?CAL_SAVE_NO_CARD:CAL_SAVE_IO_ERROR);
}
static int transfer(int fd,uint8_t *data,int count,int writing)
{
 int offset=0;while(offset<count){int n=writing?write(fd,data+offset,128):read(fd,data+offset,128);if(n!=128)return 0;offset+=128;}
 return 1;
}
static int owned(const CalProfile *profile)
{
 return buffer[0]=='S' && buffer[1]=='C' && buffer[3]==1
  && !memcmp(buffer+OWNER_OFFSET,profile->owner,strlen(profile->owner)+1);
}
static int read_header(const CalProfile *profile)
{
 int fd=open(profile->path[active_slot],FREAD),ok;if(fd<0)return CAL_SAVE_NONE;
 ok=transfer(fd,buffer,HEADER_BYTES,0);close(fd);
 if(!ok)return CAL_SAVE_IO_ERROR;
 return owned(profile)?CAL_SAVE_LOADED:CAL_SAVE_CONFLICT;
}
static int flash_preference;

static int load_inner(const CalProfile *profile,GunCal *cal,int *card_slot,int *flash)
{
 int st,slot;HuntCalRecord *r;
 active_slot=0;*card_slot=1;st=ready(profile);
 if(st==CAL_SAVE_NO_CARD){active_slot=1;*card_slot=2;st=ready(profile);}
 if(st)return st;st=read_header(profile);if(st!=CAL_SAVE_LOADED)return st;
 r=(HuntCalRecord *)(buffer+SLOT_OFFSET);slot=hunt_cal_latest(r,r+1);
 if(slot<0)return CAL_SAVE_CORRUPT;
 if(!hunt_cal_decode(r+slot,cal))return CAL_SAVE_CORRUPT;
 if(flash)*flash=r[slot].word[9]==1;
 return CAL_SAVE_LOADED;
}
static int save_inner(const CalProfile *profile,const GunCal *cal,int flash)
{
 int st,fd,slot,new_file,ok;uint32_t sequence=1;HuntCalRecord record,verify,*records;
 GunCal saved;
 if(cal && !hunt_cal_parameters_ok(cal))return CAL_SAVE_CORRUPT;
 st=ready(profile);if(st)return st;
 st=read_header(profile);new_file=st==CAL_SAVE_NONE;
 if(st!=CAL_SAVE_NONE && st!=CAL_SAVE_LOADED)return st;
 if(new_file && !cal)return CAL_SAVE_NONE;
 if(new_file){
  /* Confirm absence in the directory before creating; never truncate an existing file. */
  struct DIRENTRY entry;memset(&entry,0,sizeof(entry));
  if(firstfile(profile->path[active_slot],&entry))return CAL_SAVE_CONFLICT;
  memset(buffer,0,sizeof(buffer));buffer[0]='S';buffer[1]='C';buffer[2]=0x11;buffer[3]=1;
  memcpy(buffer+4,profile->title,strlen(profile->title));memcpy(buffer+OWNER_OFFSET,profile->owner,strlen(profile->owner)+1);
  ((uint16_t *)(buffer+0x60))[1]=0x83ff;memset(buffer+0x80,0x11,128);
  slot=0;
 }else{
  records=(HuntCalRecord *)(buffer+SLOT_OFFSET);slot=hunt_cal_latest(records,records+1);
  if(!cal){
   if(slot<0 || !hunt_cal_decode(records+slot,&saved))return CAL_SAVE_CORRUPT;
   cal=&saved;
  }
  if(slot>=0)sequence=records[slot].word[3]+1;
  slot=slot==0?1:0;
 }
 hunt_cal_encode(&record,cal,sequence);
 record.word[9]=flash!=0; /* Reserved in older version-1 records. */
 record.word[31]=hunt_cal_crc(&record,124);
 if(new_file){
  memcpy(buffer+SLOT_OFFSET,&record,128);
  fd=open(profile->path[active_slot],FCREATE|FWRITE|FNBLOCKS(1));if(fd<0)return CAL_SAVE_IO_ERROR;
  ok=transfer(fd,buffer,FILE_BYTES,1);close(fd);if(!ok)return CAL_SAVE_IO_ERROR;
 }else{
  fd=open(profile->path[active_slot],FWRITE);if(fd<0)return CAL_SAVE_IO_ERROR;
  ok=lseek(fd,SLOT_OFFSET+slot*128,0)==SLOT_OFFSET+slot*128;
  if(ok)ok=transfer(fd,(uint8_t *)&record,128,1);close(fd);if(!ok)return CAL_SAVE_IO_ERROR;
 }
 fd=open(profile->path[active_slot],FREAD);if(fd<0)return CAL_SAVE_IO_ERROR;
 ok=lseek(fd,SLOT_OFFSET+slot*128,0)==SLOT_OFFSET+slot*128;
 if(ok)ok=transfer(fd,(uint8_t *)&verify,128,0);close(fd);
 return ok && !memcmp(&record,&verify,128)?CAL_SAVE_WRITTEN:CAL_SAVE_IO_ERROR;
}
/* BIOS card byte IRQs require the BIOS dispatcher, not the SDK's final ISR.
 * Operations occur only at boot/calibration completion, after uploads finish.
 * Drain GPU DMA before suspending callbacks; restore all SDK DMA/IRQ state.
 */
static void bios_begin(void)
{
 DrawSync(0);StopPAD();StopCARD();StopCallback();
 StartPAD();StartCARD();ChangeClearPAD(1);
 *(volatile uint16_t *)0x1f801074 |= 0x81;
 ExitCriticalSection();
}
static void bios_end(void)
{
 StopPAD();StopCARD();EnterCriticalSection();RestartCallback();
 StartPAD();StartCARD();ChangeClearPAD(0);
}
int hunt_cal_load(GunCal *cal)
{
 int status;flash_preference=0;bios_begin();status=load_inner(&p1_profile,cal,&g_cal_card_slot,&flash_preference);bios_end();return status;
}
int hunt_cal_save(const GunCal *cal)
{
 int status;bios_begin();active_slot=g_cal_card_slot==2;status=save_inner(&p1_profile,cal,flash_preference);bios_end();return status;
}
int hunt_cal_flash_enabled(void)
{
 return flash_preference;
}
int hunt_cal_save_flash(int enabled)
{
 int status;flash_preference=enabled!=0;
 bios_begin();active_slot=g_cal_card_slot==2;
 status=save_inner(&p1_profile,0,flash_preference);bios_end();return status;
}
int hunt_cal_load_p2(GunCal *cal)
{
 int status;bios_begin();status=load_inner(&p2_profile,cal,&p2_slot,0);bios_end();return status;
}
int hunt_cal_save_p2(const GunCal *cal)
{
 int status;bios_begin();active_slot=p2_slot==2;
 status=save_inner(&p2_profile,cal,0);bios_end();return status;
}
const char *hunt_cal_save_status(int status)
{
 switch(status){case CAL_SAVE_LOADED:return "CAL LOADED FROM CARD";case CAL_SAVE_WRITTEN:return "CAL SAVED AND VERIFIED";
 case CAL_SAVE_NO_CARD:return "CAL RAM ONLY - NO CARD";case CAL_SAVE_CORRUPT:return "CAL SAVE INVALID";
 case CAL_SAVE_IO_ERROR:return "CAL RAM ONLY - CARD ERROR";case CAL_SAVE_CONFLICT:return "CAL FILE CONFLICT";
 case CAL_SAVE_UNCONFIRMED:return "CAL CARD UNCONFIRMED";
 default:return "CAL NO SAVE ON CARD";}
}
