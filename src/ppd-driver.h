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

#define PPD_TYPE_DRIVER (ppd_driver_get_type ())
G_DECLARE_DERIVABLE_TYPE (PpdDriver, ppd_driver, PPD, DRIVER, GObject)

/**
 * PpdProfileActivationReason:
 * @PPD_PROFILE_ACTIVATION_REASON_INTERNAL: the driver profile changed
 *   internally, usually because of a key combination.
 * @PPD_PROFILE_ACTIVATION_REASON_RESET: setting profile on startup, or
 *   because drivers are getting reprobed.
 * @PPD_PROFILE_ACTIVATION_REASON_USER: setting profile because the user
 *   requested it.
 * @PPD_PROFILE_ACTIVATION_REASON_RESUME: setting profile because preference
 *   is lost during suspend.
 * @PPD_PROFILE_ACTIVATION_REASON_PROGRAM_HOLD: setting profile because a program
 *   requested it through the `HoldProfile` method.
 *
 * Those are possible reasons for a profile being activated. Based on those
 * reasons, drivers can choose whether or not that changes the effective
 * profile internally.
 */
typedef enum{
  PPD_PROFILE_ACTIVATION_REASON_INTERNAL = 0,
  PPD_PROFILE_ACTIVATION_REASON_RESET,
  PPD_PROFILE_ACTIVATION_REASON_USER,
  PPD_PROFILE_ACTIVATION_REASON_RESUME,
  PPD_PROFILE_ACTIVATION_REASON_PROGRAM_HOLD
} PpdProfileActivationReason;

/**
 * PpdDriverClass:
 * @parent_class: The parent class.
 * @probe: Called by the daemon on startup.
 * @activate_profile: Called by the daemon for every profile change.
 *
 * New profile drivers should not derive from #PpdDriver.  They should
 * derive from the child from #PpdDriverCpu or #PpdDriverPlatform drivers
 * and implement at least one of probe () and @activate_profile.
 */
struct _PpdDriverClass
{
  GObjectClass   parent_class;

  PpdProbeResult (* probe)            (PpdDriver                   *driver);
  gboolean       (* activate_profile) (PpdDriver                   *driver,
                                       PpdProfile                   profile,
                                       PpdProfileActivationReason   reason,
                                       GError                     **error);
};

#ifndef __GTK_DOC_IGNORE__
PpdProbeResult ppd_driver_probe (PpdDriver *driver);
gboolean ppd_driver_activate_profile (PpdDriver *driver,
  PpdProfile profile, PpdProfileActivationReason reason, GError **error);
const char *ppd_driver_get_driver_name (PpdDriver *driver);
PpdProfile ppd_driver_get_profiles (PpdDriver *driver);
const char *ppd_driver_get_performance_degraded (PpdDriver *driver);
gboolean ppd_driver_is_performance_degraded (PpdDriver *driver);
void ppd_driver_emit_profile_changed (PpdDriver *driver, PpdProfile profile);
const char *ppd_profile_activation_reason_to_str (PpdProfileActivationReason reason);
#endif
