/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifndef _TFF_APPLICATION_H
#define _TFF_APPLICATION_H

typedef enum {
    UI_HIDE,
    UI_SHOW
} CastEventE;

typedef void (*EventCallback)(int event);

typedef struct
{
    char deviceName[64];                    // Device name to be displayed on the receiver side
    int  resolution;                        // Screen resolution: 0 for 1280*720, 1 for 1920*1080

    void (*OnStart)();                      // User callback interface for starting cast
    void (*OnSetVolume)(int volumePercent); // User callback interface for setting volume
    void (*OnStop)();                       // User callback interface for stopping cast
} TFFCastInitPara;

#ifdef TFF_CAST_EXPORTS
#define TFF_CAST_API
#else
#define TFF_CAST_API __attribute__((visibility ("default")))
#endif

#ifdef  __cplusplus
extern "C" {
#endif

//mode: 0:720p; 1:1080p
TFF_CAST_API int TFFCast_startService(TFFCastInitPara *initpara);
TFF_CAST_API void TFFCast_setRotate();
TFF_CAST_API void TFFCast_setFullScreen();

#ifdef  __cplusplus
}
#endif

#endif //TFF_APPLICATION_H

