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

#define PPD_TYPE_ACTION (ppd_action_get_type())
G_DECLARE_DERIVABLE_TYPE(PpdAction, ppd_action, PPD, ACTION, GObject)

struct _PpdActionClass
{
  GObjectClass   parent_class;

  gboolean       (* probe)            (PpdAction  *action);
  gboolean       (* activate_profile) (PpdAction  *action,
                                       PpdProfile  profile,
                                       GError    **error);
};

gboolean ppd_action_probe (PpdAction *action);
gboolean ppd_action_activate_profile (PpdAction *action, PpdProfile profile, GError **error);
const char *ppd_action_get_action_name (PpdAction *action);
PpdProfile ppd_action_get_profile (PpdAction *action);
