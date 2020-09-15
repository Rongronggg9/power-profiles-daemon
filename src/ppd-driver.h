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

#define PPD_TYPE_DRIVER (ppd_driver_get_type())
G_DECLARE_DERIVABLE_TYPE(PpdDriver, ppd_driver, PPD, DRIVER, GObject)

/**
 * PpdDriverClass:
 * @parent_class: The parent class.
 * @probe: Called by the daemon on startup.
 * @activate_profile: Called by the daemon for every profile change.
 *
 * New profile drivers should derive from #PpdDriver and implement
 * at least one of probe() and @activate_profile.
 */
struct _PpdDriverClass
{
  GObjectClass   parent_class;

  gboolean       (* probe)            (PpdDriver   *driver);
  gboolean       (* activate_profile) (PpdDriver   *driver,
                                       PpdProfile   profile,
                                       GError     **error);
};

#ifndef __GTK_DOC_IGNORE__
gboolean ppd_driver_probe (PpdDriver *driver);
gboolean ppd_driver_activate_profile (PpdDriver *driver, PpdProfile profile, GError **error);
const char *ppd_driver_get_driver_name (PpdDriver *driver);
PpdProfile ppd_driver_get_profiles (PpdDriver *driver);
const char *ppd_driver_get_performance_inhibited (PpdDriver *driver);
gboolean ppd_driver_is_performance_inhibited (PpdDriver *driver);
void ppd_driver_emit_profile_changed (PpdDriver *driver, PpdProfile profile);
#endif
