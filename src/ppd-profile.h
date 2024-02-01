/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include <glib.h>

/**
 * PpdProbeResult:
 * @PPD_PROBE_RESULT_UNSET: unset
 * @PPD_PROBE_RESULT_DEFER: driver should be kept alive, as kernel
 *   support might appear.
 * @PPD_PROBE_RESULT_FAIL: driver failed to load.
 * @PPD_PROBE_RESULT_SUCCESS: driver successfully loaded.
 *
 * Those are the three possible values returned by a driver probe,
 * along with an unset value for convenience.
 */
typedef enum {
  PPD_PROBE_RESULT_UNSET = -2,
  PPD_PROBE_RESULT_DEFER = -1,
  PPD_PROBE_RESULT_FAIL = 0,
  PPD_PROBE_RESULT_SUCCESS = 1
} PpdProbeResult;

/**
 * PpdProfile:
 * @PPD_PROFILE_POWER_SAVER: "power-saver", the battery saving profile
 * @PPD_PROFILE_BALANCED: balanced, the default profile
 * @PPD_PROFILE_PERFORMANCE: as fast as possible, a profile that does
 *   not care about noise or battery consumption, only available
 *   on some systems.
 *
 * The different profiles available for users to select.
 */
typedef enum {
  PPD_PROFILE_POWER_SAVER  = 1 << 0,
  PPD_PROFILE_BALANCED     = 1 << 1,
  PPD_PROFILE_PERFORMANCE  = 1 << 2
} PpdProfile;

#define PPD_PROFILE_ALL   (PPD_PROFILE_BALANCED | PPD_PROFILE_POWER_SAVER | PPD_PROFILE_PERFORMANCE)
#define PPD_PROFILE_UNSET (0)

const char *ppd_profile_to_str (PpdProfile profile);
PpdProfile ppd_profile_from_str (const char *str);
gboolean ppd_profile_has_single_flag (PpdProfile profile);
