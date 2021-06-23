#ifndef _YM2612_PSG_DEBUG
#define _YM2612_PSG_DEBUG

void Update_YM2612_View();


#ifdef __cplusplus
extern "C" {
#endif

  extern int enabled_channels_ym[6];
  extern int enabled_channels_psg[4];

#ifdef __cplusplus
};
#endif

#endif // _YM2612_PSG_DEBUG
