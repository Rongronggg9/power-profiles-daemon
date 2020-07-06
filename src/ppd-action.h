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

#define PPD_TYPE_ACTION (ppd_action_get_type())
G_DECLARE_DERIVABLE_TYPE(PpdAction, ppd_action, PPD, ACTION, GObject)

struct _PpdActionClass
{
  GObjectClass   parent_class;
};
