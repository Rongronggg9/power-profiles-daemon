/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

/**
 * PpdProfile:
 * @PPD_PROFILE_UNSET: unset profile, when bugs occur
 * @PPD_PROFILE_BALANCED: balanced, the default profile
 * @PPD_PROFILE_POWER_SAVER: "power-saver", the battery saving profile
 * @PPD_PROFILE_PERFORMANCE: as fast as possible, a profile that does
 *   not care about noise or battery consumption, only available
 *   on some systems.
 *
 * The different profiles available for users to select.
 */
typedef enum {
  PPD_PROFILE_UNSET        = -1,
  PPD_PROFILE_BALANCED     = 0,
  PPD_PROFILE_POWER_SAVER,
  PPD_PROFILE_PERFORMANCE
} PpdProfile;
