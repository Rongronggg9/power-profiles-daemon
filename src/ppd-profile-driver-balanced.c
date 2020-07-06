/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include "ppd-profile-driver-balanced.h"

struct _PpdProfileDriverBalanced
{
  PpdProfileDriver  parent_instance;
};

G_DEFINE_TYPE (PpdProfileDriverBalanced, ppd_profile_driver_balanced, PPD_TYPE_PROFILE_DRIVER)

static GObject*
ppd_profile_driver_balanced_constructor (GType                  type,
                                      guint                  n_construct_params,
                                      GObjectConstructParam *construct_params)
{
  GObject *object;

  object = G_OBJECT_CLASS (ppd_profile_driver_balanced_parent_class)->constructor (type,
                                                                                n_construct_params,
                                                                                construct_params);
  g_object_set (object,
                "driver-name", "balanced",
                "profile", PPD_PROFILE_BALANCED,
                NULL);

  return object;
}

static void
ppd_profile_driver_balanced_class_init (PpdProfileDriverBalancedClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS(klass);
  object_class->constructor = ppd_profile_driver_balanced_constructor;
}

static void
ppd_profile_driver_balanced_init (PpdProfileDriverBalanced *self)
{
}
