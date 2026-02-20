#ifndef GENEC_H
#define GENEC_H

#if defined(GECND_NEC_PHILCO)
#define NEC_KEY_D             0xb34cbf00 
#define NEC_KEY_C             0xb54abf00 
#define NEC_KEY_B             0xb649bf00 
#define NEC_KEY_A             0xb748bf00 
#define NEC_KEY_DOWN          0xba45bf00 
#define NEC_KEY_RIGHT         0xbd42bf00 
#define NEC_KEY_OK            0xbe41bf00 
#define NEC_KEY_LEFT          0xbf40bf00 
#define NEC_KEY_UP            0xe21dbf00 
#define NEC_KEY_VOL_DOWN      0xe51abf00 
#define NEC_KEY_CH_DOWN       0xe718bf00 
#define NEC_KEY_VOL_UP        0xe916bf00 
#define NEC_KEY_CH_UP         0xeb14bf00 
#define NEC_KEY_MENU          0xf10ebf00 
#else
#define NEC_KEY_D             0xd8271dcc 
#define NEC_KEY_C             0xd9261dcc 
#define NEC_KEY_B             0xda251dcc 
#define NEC_KEY_A             0xdb241dcc 
#define NEC_KEY_RIGHT         0xf7081dcc 
#define NEC_KEY_LEFT          0xf8071dcc 
#define NEC_KEY_DOWN          0xf9061dcc 
#define NEC_KEY_UP            0xfa051dcc 
#define NEC_KEY_OK            0xfb041dcc 
#define NEC_KEY_VOL_DOWN      0x7e811dcc 
#define NEC_KEY_CH_DOWN       0x79861dcc 
#define NEC_KEY_VOL_UP        0x7f801dcc 
#define NEC_KEY_CH_UP         0x7a851dcc 
#define NEC_KEY_MENU          0xfc031dcc 
#define NEC_KEY_POWER         0xff001dcc 
#endif

#endif
