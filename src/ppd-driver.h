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
 * PpdProfileActivationReason:
 * PPD_PROFILE_ACTIVATION_REASON_INHIBITION: switching profiles because
 *   of performance profile inhibition.
 * PPD_PROFILE_ACTIVATION_REASON_INTERNAL: the driver profile changed
 *   internally, usually because of a key combination.
 * PPD_PROFILE_ACTIVATION_REASON_RESET: setting profile on startup, or
 *   because drivers are getting reprobed.
 * PPD_PROFILE_ACTIVATION_REASON_USER: setting profile because the user
 *   requested it.
 *
 * Those are possible reasons for a profile being activated. Based on those
 * reasons, drivers can choose whether or not that changes the effective
 * profile internally.
 */
typedef enum{
  PPD_PROFILE_ACTIVATION_REASON_INHIBITION = 0,
  PPD_PROFILE_ACTIVATION_REASON_INTERNAL,
  PPD_PROFILE_ACTIVATION_REASON_RESET,
  PPD_PROFILE_ACTIVATION_REASON_USER
} PpdProfileActivationReason;

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

  PpdProbeResult (* probe)            (PpdDriver   *driver,
                                       PpdProfile  *previous_profile);
  gboolean       (* activate_profile) (PpdDriver   *driver,
                                       PpdProfile   profile,
                                       GError     **error);
};

#ifndef __GTK_DOC_IGNORE__
PpdProbeResult ppd_driver_probe (PpdDriver *driver, PpdProfile *previous_profile);
gboolean ppd_driver_activate_profile (PpdDriver *driver, PpdProfile profile, GError **error);
const char *ppd_driver_get_driver_name (PpdDriver *driver);
PpdProfile ppd_driver_get_profiles (PpdDriver *driver);
const char *ppd_driver_get_performance_inhibited (PpdDriver *driver);
gboolean ppd_driver_is_performance_inhibited (PpdDriver *driver);
void ppd_driver_emit_profile_changed (PpdDriver *driver, PpdProfile profile);
#endif
