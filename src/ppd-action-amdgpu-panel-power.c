/*
 * Copyright (c) 2024 Advanced Micro Devices
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#define G_LOG_DOMAIN "AmdgpuAction"

#include "config.h"

#include <gudev/gudev.h>

#include "ppd-action-amdgpu-panel-power.h"
#include "ppd-profile.h"
#include "ppd-utils.h"

#define PROC_CPUINFO_PATH      "/proc/cpuinfo"

#define PANEL_POWER_SYSFS_NAME "amdgpu/panel_power_savings"

#define UPOWER_DBUS_NAME       "org.freedesktop.UPower"
#define UPOWER_DBUS_PATH       "/org/freedesktop/UPower"
#define UPOWER_DBUS_INTERFACE  "org.freedesktop.UPower"

/**
 * SECTION:ppd-action-amdgpu-panel-power
 * @Short_description: Power savings for eDP connected displays
 * @Title: AMDGPU Panel power action
 *
 * The AMDGPU panel power action utilizes the sysfs attribute present on some DRM
 * connectors for amdgpu called "panel_power_savings".  This will use an AMD specific
 * hardware feature for a power savings profile for the panel.
 *
 */

struct _PpdActionAmdgpuPanelPower
{
  PpdAction  parent_instance;
  PpdProfile last_profile;

  GUdevClient *client;
  GDBusProxy *proxy;
  guint watcher_id;

  gint panel_power_saving;
  gboolean on_battery;
};

G_DEFINE_TYPE (PpdActionAmdgpuPanelPower, ppd_action_amdgpu_panel_power, PPD_TYPE_ACTION)

static GObject*
ppd_action_amdgpu_panel_power_constructor (GType                  type,
                                           guint                  n_construct_params,
                                           GObjectConstructParam *construct_params)
{
  GObject *object;

  object = G_OBJECT_CLASS (ppd_action_amdgpu_panel_power_parent_class)->constructor (type,
                                                                                     n_construct_params,
                                                                                     construct_params);
  g_object_set (object,
                "action-name", "amdgpu_panel_power",
                NULL);

  return object;
}

static gboolean
set_panel_power (PpdActionAmdgpuPanelPower *self, gint power, GError **error)
{
  GList *devices, *l;

  devices = g_udev_client_query_by_subsystem (self->client, "drm");
  if (devices == NULL) {
    g_set_error_literal (error,
                         G_IO_ERROR,
                         G_IO_ERROR_NOT_FOUND,
                         "no drm devices found");
    return FALSE;
  }

  for (l = devices; l != NULL; l = l->next) {
    GUdevDevice *dev = l->data;
    const char *value;
    guint64 parsed;

    value = g_udev_device_get_devtype (dev);
    if (g_strcmp0 (value, "drm_connector") != 0)
      continue;

    value = g_udev_device_get_sysfs_attr_uncached (dev, PANEL_POWER_SYSFS_NAME);
    if (!value)
      continue;

    parsed = g_ascii_strtoull (value, NULL, 10);

    /* overflow check */
    if (parsed == G_MAXUINT64) {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "cannot parse %s as caused overflow",
                   value);
      return FALSE;
    }

    if (parsed == power)
      continue;

    if (!ppd_utils_write_sysfs_int (dev, PANEL_POWER_SYSFS_NAME, power, error))
      return FALSE;

    break;
  }

  g_list_free_full (devices, g_object_unref);

  return TRUE;
}

static gboolean
ppd_action_amdgpu_panel_update_target (PpdActionAmdgpuPanelPower  *self,
                                       GError                    **error)
{
  gint target = 0;

  /* only activate if we know that we're on battery */
  if (self->on_battery) {
    switch (self->last_profile) {
    case PPD_PROFILE_POWER_SAVER:
      target = 4;
      break;
    case PPD_PROFILE_BALANCED:
      target = 3;
      break;
    case PPD_PROFILE_PERFORMANCE:
      target = 0;
      break;
    }
  }

  if (!set_panel_power (self, target, error))
    return FALSE;
  self->panel_power_saving = target;

  return TRUE;
}

static gboolean
ppd_action_amdgpu_panel_power_activate_profile (PpdAction   *action,
                                                PpdProfile   profile,
                                                GError     **error)
{
  PpdActionAmdgpuPanelPower *self = PPD_ACTION_AMDGPU_PANEL_POWER (action);
  self->last_profile = profile;

  if (self->proxy == NULL) {
    g_debug ("upower not available; battery data might be stale");
    return TRUE;
  }

  return ppd_action_amdgpu_panel_update_target (self, error);
}


static void
upower_properties_changed (GDBusProxy *proxy,
                           GVariant *changed_properties,
                           GStrv invalidated_properties,
                           PpdActionAmdgpuPanelPower *self)
{
  g_autoptr (GVariant) battery_val = NULL;
  g_autoptr (GError) error = NULL;
  gboolean new_on_battery;

  if (proxy != NULL)
    battery_val = g_dbus_proxy_get_cached_property (proxy, "OnBattery");
  new_on_battery = battery_val ? g_variant_get_boolean (battery_val) : FALSE;

  if (self->on_battery == new_on_battery)
    return;

  g_debug ("OnBattery: %d -> %d", self->on_battery, new_on_battery);
  self->on_battery = new_on_battery;
  if (!ppd_action_amdgpu_panel_update_target (self, &error))
    g_warning ("failed to update target: %s", error->message);
}

static void
udev_uevent_cb (GUdevClient *client,
                gchar       *action,
                GUdevDevice *device,
                gpointer     user_data)
{
  PpdActionAmdgpuPanelPower *self = user_data;

  if (!g_str_equal (action, "add"))
    return;

  if (!g_udev_device_has_sysfs_attr (device, PANEL_POWER_SYSFS_NAME))
    return;

  g_debug ("Updating panel power saving for '%s' to '%d'",
           g_udev_device_get_sysfs_path (device),
           self->panel_power_saving);
  ppd_utils_write_sysfs_int (device, PANEL_POWER_SYSFS_NAME,
                             self->panel_power_saving, NULL);
}

static PpdProbeResult
ppd_action_amdgpu_panel_power_probe (PpdAction *action)
{
  g_autofree gchar *cpuinfo_path = NULL;
  g_autofree gchar *cpuinfo = NULL;
  g_auto(GStrv) lines = NULL;

  cpuinfo_path = ppd_utils_get_sysfs_path (PROC_CPUINFO_PATH);
  if (!g_file_get_contents (cpuinfo_path, &cpuinfo, NULL, NULL))
    return PPD_PROBE_RESULT_FAIL;

  lines = g_strsplit (cpuinfo, "\n", -1);

  for (gchar **line = lines; *line != NULL; line++) {
      if (g_str_has_prefix (*line, "vendor_id") &&
          strchr (*line, ':')) {
          g_auto(GStrv) sections = g_strsplit (*line, ":", 2);

          if (g_strv_length (sections) < 2)
            continue;
          if (g_strcmp0 (g_strstrip (sections[1]), "AuthenticAMD") == 0)
            return PPD_PROBE_RESULT_SUCCESS;
      }
  }


  return PPD_PROBE_RESULT_FAIL;
}

static void
upower_name_vanished (GDBusConnection *connection,
                      const gchar     *name,
                      gpointer         user_data)
{
  PpdActionAmdgpuPanelPower *self = user_data;

  g_debug ("%s vanished", UPOWER_DBUS_NAME);

  /* reset */
  g_clear_pointer (&self->proxy, g_object_unref);
  upower_properties_changed (NULL, NULL, NULL, self);
}

static void
upower_name_appeared (GDBusConnection *connection,
                      const gchar     *name,
                      const gchar     *name_owner,
                      gpointer         user_data)
{
  PpdActionAmdgpuPanelPower *self = user_data;
  g_autoptr (GError) error = NULL;

  g_debug ("%s appeared", UPOWER_DBUS_NAME);
  self->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                               G_DBUS_PROXY_FLAGS_NONE,
                                               NULL,
                                               UPOWER_DBUS_NAME,
                                               UPOWER_DBUS_PATH,
                                               UPOWER_DBUS_INTERFACE,
                                               NULL,
                                               &error);
  if (self->proxy == NULL) {
    g_debug ("failed to connect to upower: %s", error->message);
    return;
  }

  g_signal_connect (self->proxy,
                    "g-properties-changed",
                    G_CALLBACK(upower_properties_changed),
                    self);
  upower_properties_changed (self->proxy, NULL, NULL, self);
}

static void
ppd_action_amdgpu_panel_power_finalize (GObject *object)
{
  PpdActionAmdgpuPanelPower *action;

  action = PPD_ACTION_AMDGPU_PANEL_POWER (object);
  g_clear_handle_id (&action->watcher_id, g_bus_unwatch_name);
  g_clear_object (&action->client);
  g_clear_pointer (&action->proxy, g_object_unref);
  G_OBJECT_CLASS (ppd_action_amdgpu_panel_power_parent_class)->finalize (object);
}

static void
ppd_action_amdgpu_panel_power_class_init (PpdActionAmdgpuPanelPowerClass *klass)
{
  GObjectClass *object_class;
  PpdActionClass *driver_class;

  object_class = G_OBJECT_CLASS(klass);
  object_class->constructor = ppd_action_amdgpu_panel_power_constructor;
  object_class->finalize = ppd_action_amdgpu_panel_power_finalize;

  driver_class = PPD_ACTION_CLASS(klass);
  driver_class->probe = ppd_action_amdgpu_panel_power_probe;
  driver_class->activate_profile = ppd_action_amdgpu_panel_power_activate_profile;
}

static void
ppd_action_amdgpu_panel_power_init (PpdActionAmdgpuPanelPower *self)
{
  const gchar * const subsystem[] = { "drm", NULL };

  self->client = g_udev_client_new (subsystem);
  g_signal_connect (G_OBJECT (self->client), "uevent",
                    G_CALLBACK (udev_uevent_cb), self);

  self->watcher_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                       UPOWER_DBUS_NAME,
                                       G_BUS_NAME_WATCHER_FLAGS_NONE,
                                       upower_name_appeared,
                                       upower_name_vanished,
                                       self,
                                       NULL);
}
