/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include "ppd-profile-driver.h"

#define PPD_TYPE_PROFILE_DRIVER_POWER_SAVER (ppd_profile_driver_power_saver_get_type())
G_DECLARE_FINAL_TYPE(PpdProfileDriverPowerSaver, ppd_profile_driver_power_saver, PPD, PROFILE_DRIVER_POWER_SAVER, PpdProfileDriver)
