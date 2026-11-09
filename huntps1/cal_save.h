#ifndef HUNT_CAL_SAVE_H
#define HUNT_CAL_SAVE_H
#include "../guncal/guncal.h"
enum { CAL_SAVE_NONE=0, CAL_SAVE_LOADED=1, CAL_SAVE_WRITTEN=2,
 CAL_SAVE_NO_CARD=3, CAL_SAVE_CORRUPT=4, CAL_SAVE_IO_ERROR=5, CAL_SAVE_CONFLICT=6,
 CAL_SAVE_UNCONFIRMED=7 };
void hunt_cal_card_init(void);
int hunt_cal_load(GunCal *cal);
int hunt_cal_save(const GunCal *cal);
int hunt_cal_flash_enabled(void);
int hunt_cal_save_flash(int enabled);
int hunt_cal_load_p2(GunCal *cal);
int hunt_cal_save_p2(const GunCal *cal);
const char *hunt_cal_save_status(int status);
#endif
