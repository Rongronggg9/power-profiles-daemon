/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#define G_LOG_DOMAIN "TrickleChargeAction"

#include <gudev/gudev.h>

#include "ppd-action-trickle-charge.h"
#include "ppd-profile.h"
#include "ppd-utils.h"

#define CHARGE_TYPE_SYSFS_NAME "charge_type"

struct _PpdActionTrickleCharge
{
  PpdAction  parent_instance;

  GUdevClient *client;
  gboolean active;
};

G_DEFINE_TYPE (PpdActionTrickleCharge, ppd_action_trickle_charge, PPD_TYPE_ACTION)

static GObject*
ppd_action_trickle_charge_constructor (GType                  type,
                                            guint                  n_construct_params,
                                            GObjectConstructParam *construct_params)
{
  GObject *object;

  object = G_OBJECT_CLASS (ppd_action_trickle_charge_parent_class)->constructor (type,
                                                                                n_construct_params,
                                                                                construct_params);
  g_object_set (object,
                "action-name", "trickle_charge",
                NULL);

  return object;
}

static void
set_charge_type (PpdActionTrickleCharge *action,
                 const char             *charge_type)
{
  GList *devices, *l;

  devices = g_udev_client_query_by_subsystem (action->client, "power_supply");
  if (devices == NULL)
    return;

  for (l = devices; l != NULL; l = l->next) {
    GUdevDevice *dev = l->data;
    const char *value;

    if (g_strcmp0 (g_udev_device_get_sysfs_attr (dev, "scope"), "Device") != 0)
      continue;

    value = g_udev_device_get_sysfs_attr_uncached (dev, CHARGE_TYPE_SYSFS_NAME);
    if (!value)
      continue;

    if (g_strcmp0 (charge_type, value) == 0)
      continue;

    ppd_utils_write_sysfs (dev, CHARGE_TYPE_SYSFS_NAME, charge_type, NULL);

    break;
  }

  g_list_free_full (devices, g_object_unref);
}

static gboolean
ppd_action_trickle_charge_activate_profile (PpdAction   *action,
                                            PpdProfile   profile,
                                            GError     **error)
{
  PpdActionTrickleCharge *self = PPD_ACTION_TRICKLE_CHARGE (action);

  if (profile == PPD_PROFILE_POWER_SAVER) {
    set_charge_type (self, "Trickle");
    self->active = TRUE;
  } else {
    set_charge_type (self, "Fast");
    self->active = FALSE;
  }

  return TRUE;
}

static void
uevent_cb (GUdevClient *client,
           gchar       *action,
           GUdevDevice *device,
           gpointer     user_data)
{
  PpdActionTrickleCharge *self = user_data;
  const char *charge_type;

  if (g_strcmp0 (action, "add") != 0)
    return;

  if (!g_udev_device_has_sysfs_attr (device, CHARGE_TYPE_SYSFS_NAME))
    return;

  charge_type = self->active ? "Trickle" : "Fast";
  g_debug ("Updating charge type for '%s' to '%s'",
           g_udev_device_get_sysfs_path (device),
           charge_type);
  ppd_utils_write_sysfs (device, CHARGE_TYPE_SYSFS_NAME, charge_type, NULL);
}

static void
ppd_action_trickle_charge_finalize (GObject *object)
{
  PpdActionTrickleCharge *driver;

  driver = PPD_ACTION_TRICKLE_CHARGE (object);
  g_clear_object (&driver->client);
  G_OBJECT_CLASS (ppd_action_trickle_charge_parent_class)->finalize (object);
}

static void
ppd_action_trickle_charge_class_init (PpdActionTrickleChargeClass *klass)
{
  GObjectClass *object_class;
  PpdActionClass *driver_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->constructor = ppd_action_trickle_charge_constructor;
  object_class->finalize = ppd_action_trickle_charge_finalize;

  driver_class = PPD_ACTION_CLASS (klass);
  driver_class->activate_profile = ppd_action_trickle_charge_activate_profile;
}

static void
ppd_action_trickle_charge_init (PpdActionTrickleCharge *self)
{
  const gchar * const subsystem[] = { "power_supply", NULL };

  self->client = g_udev_client_new (subsystem);
  g_signal_connect (G_OBJECT (self->client), "uevent",
                    G_CALLBACK (uevent_cb), self);
}
