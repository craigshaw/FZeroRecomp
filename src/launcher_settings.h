#pragma once

#include "config.h"
#include "recomp_launcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Extra settings absent from the pinned launcher's shared ABI. One launcher
 * runs at a time. Edits stay staged until the user selects Play. */
typedef struct FZeroLauncherExtras {
  int visual_style;
  int show_fps;
} FZeroLauncherExtras;

void FZeroLauncherSettingsBegin(const FZeroSettings *settings,
                                RecompLauncherCSettings *launcher);
void FZeroLauncherSettingsAccept(FZeroSettings *settings,
                                 const RecompLauncherCSettings *launcher);
FZeroLauncherExtras *FZeroLauncherExtraSettings(void);

#ifdef __cplusplus
}
#endif
