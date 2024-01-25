/*
 * Copyright (c) 2023 Mario Limonciello <superm1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include "ppd-driver.h"

#define PPD_TYPE_DRIVER_PLATFORM (ppd_driver_platform_get_type ())
G_DECLARE_DERIVABLE_TYPE (PpdDriverPlatform, ppd_driver_platform, PPD, DRIVER_PLATFORM, PpdDriver)

/**
 * PpdDriverPlatformClass:
 * @parent_class: The parent class.
 *
 * New Platform drivers should derive from #PpdDriverPlatform and implement
 * at least one of @probe () and @activate_profile.
 */
struct _PpdDriverPlatformClass
{
  PpdDriverClass   parent_class;
};
