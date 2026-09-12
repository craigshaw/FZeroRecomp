#include "launcher_settings.h"
#include <string.h>

static FZeroLauncherExtras extras;

FZeroLauncherExtras *FZeroLauncherExtraSettings(void) { return &extras; }

void FZeroLauncherSettingsBegin(const FZeroSettings *s,
                                RecompLauncherCSettings *l) {
  memset(l, 0, sizeof(*l));
  l->output_method = s->output_method;
  l->window_scale = s->window_scale;
  l->fullscreen = s->fullscreen;
  l->ignore_aspect = s->ignore_aspect;
  l->linear_filter = s->linear_filter;
  l->widescreen = s->widescreen;
  l->enable_audio = s->enable_audio;
  l->audio_freq = s->audio_freq;
  l->volume = s->volume;
  for (int p = 0; p < 2; ++p) {
    l->player_src[p] = s->player_src[p];
    l->deadzone[p] = s->deadzone[p];
  }
  l->skip_launcher = s->skip_launcher;
  extras.visual_style = s->visual_style;
  extras.show_fps = s->show_fps;
}

void FZeroLauncherSettingsAccept(FZeroSettings *s,
                                 const RecompLauncherCSettings *l) {
  s->output_method = l->output_method;
  s->window_scale = l->window_scale;
  s->fullscreen = l->fullscreen != 0;
  s->ignore_aspect = l->ignore_aspect != 0;
  s->linear_filter = l->linear_filter != 0;
  s->widescreen = l->widescreen != 0;
  s->visual_style = extras.visual_style;
  s->show_fps = extras.show_fps != 0;
  s->enable_audio = l->enable_audio != 0;
  s->audio_freq = l->audio_freq;
  s->volume = l->volume;
  for (int p = 0; p < 2; ++p) {
    s->player_src[p] = l->player_src[p];
    s->deadzone[p] = l->deadzone[p];
  }
  s->skip_launcher = l->skip_launcher != 0;
  FZeroSettingsSanitize(s);
}
