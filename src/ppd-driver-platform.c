/*
 * Copyright (c) 2023 Mario Limonciello <superm1@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#define G_LOG_DOMAIN "PlatformDriver"

#include "ppd-driver-platform.h"

G_DEFINE_TYPE (PpdDriverPlatform, ppd_driver_platform, PPD_TYPE_DRIVER)

/**
 * SECTION:ppd-driver-platform
 * @Short_description: Profile Drivers
 * @Title: Platform Profile Drivers
 *
 * Platform drivers are the implementation of the different profiles for
 * the whole system. A driver will need to implement support for `power-saver`
 * and `balanced` at a minimum.
 *
 * If no system-specific platform driver is available, a placeholder driver
 * will be put in place, and the `performance` profile will be unavailable.
 *
 * There should not be a need to implement system-specific drivers, as the
 * [`platform_profile`] (https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/ABI/testing/sysfs-platform_profile)
 * kernel API offers a way to implement system-specific profiles which
 * `power-profiles-daemon` can consume.
 *
 * When a driver implements the `performance` profile, it might set the
 * #PpdDriver:performance-degraded property if the profile isn't running to
 * its fullest performance for any reason, such as thermal limits being
 * reached, or because a part of the user's body is too close for safety,
 * for example.
 */

static void
ppd_driver_platform_finalize (GObject *object)
{
  G_OBJECT_CLASS (ppd_driver_platform_parent_class)->finalize (object);
}

static void
ppd_driver_platform_class_init (PpdDriverPlatformClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = ppd_driver_platform_finalize;
}

static void
ppd_driver_platform_init (PpdDriverPlatform *self)
{
}
