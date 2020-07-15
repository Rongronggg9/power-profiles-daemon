/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include <glib-object.h>
#include "ppd-profile.h"

#define PPD_TYPE_PROFILE_DRIVER (ppd_profile_driver_get_type())
G_DECLARE_DERIVABLE_TYPE(PpdProfileDriver, ppd_profile_driver, PPD, PROFILE_DRIVER, GObject)

struct _PpdProfileDriverClass
{
  GObjectClass   parent_class;

  gboolean       (* probe) (PpdProfileDriver *driver);
};

gboolean ppd_profile_driver_probe (PpdProfileDriver *driver);
const char *ppd_profile_driver_get_driver_name (PpdProfileDriver *driver);
PpdProfile ppd_profile_driver_get_profile (PpdProfileDriver *driver);
const char *ppd_profile_driver_get_inhibited (PpdProfileDriver *driver);
