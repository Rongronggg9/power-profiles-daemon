/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include <upower.h>

#include "ppd-utils.h"
#include "ppd-driver-intel-pstate.h"

#define CPUFREQ_POLICY_DIR "/sys/devices/system/cpu/cpufreq/"
#define NO_TURBO_PATH "/sys/devices/system/cpu/intel_pstate/no_turbo"

struct _PpdDriverIntelPstate
{
  PpdDriver  parent_instance;

  UpClient *client;
  PpdProfile activated_profile;
  gboolean on_battery;
  GList *devices; /* GList of paths */
  GFileMonitor *no_turbo_mon;
  char *no_turbo_path;
};

G_DEFINE_TYPE (PpdDriverIntelPstate, ppd_driver_intel_pstate, PPD_TYPE_DRIVER)

static gboolean ppd_driver_intel_pstate_activate_profile (PpdDriver   *driver,
                                                          PpdProfile   profile,
                                                          GError     **error);

static GObject*
ppd_driver_intel_pstate_constructor (GType                  type,
                                    guint                  n_construct_params,
                                    GObjectConstructParam *construct_params)
{
  GObject *object;

  object = G_OBJECT_CLASS (ppd_driver_intel_pstate_parent_class)->constructor (type,
                                                                              n_construct_params,
                                                                              construct_params);
  g_object_set (object,
                "driver-name", "intel_pstate",
                "profiles", PPD_PROFILE_PERFORMANCE | PPD_PROFILE_BALANCED | PPD_PROFILE_POWER_SAVER,
                NULL);

  return object;
}

static void
on_battery_changed (GObject    *gobject,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
  PpdDriverIntelPstate *pstate = user_data;
  gboolean old_on_battery;

  old_on_battery = pstate->on_battery;
  pstate->on_battery = up_client_get_on_battery (pstate->client);

  if (pstate->activated_profile == PPD_PROFILE_BALANCED) {
    ppd_driver_intel_pstate_activate_profile (PPD_DRIVER (pstate),
                                              pstate->activated_profile,
                                              NULL);
  }

  g_debug ("Battery status changed from %s to %s",
           old_on_battery ? "on battery" : "on mains",
           pstate->on_battery ? "on battery" : "on mains");
}

static void
update_no_turbo (PpdDriverIntelPstate *pstate)
{
  g_autofree char *contents = NULL;
  gboolean turbo_disabled = FALSE;

  if (g_file_get_contents (pstate->no_turbo_path, &contents, NULL, NULL)) {
    contents = g_strchomp (contents);
    if (g_strcmp0 (contents, "1") == 0)
      turbo_disabled = TRUE;
  }

  g_object_set (G_OBJECT (pstate), "performance-inhibited",
                turbo_disabled ? "high-operating-temperature" : NULL,
                NULL);
}

static void
no_turbo_changed (GFileMonitor     *monitor,
                  GFile            *file,
                  GFile            *other_file,
                  GFileMonitorEvent event_type,
                  gpointer          user_data)
{
  PpdDriverIntelPstate *pstate = user_data;
  g_autofree char *path = NULL;

  path = g_file_get_path (file);
  g_debug ("File monitor change happened for '%s'", path);
  update_no_turbo (pstate);
}

static GFileMonitor *
monitor_no_turbo_prop (const char *path)
{
  g_autoptr(GFile) no_turbo = NULL;

  if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
    g_debug ("Not monitoring '%s' as it does not exist", path);
    return NULL;
  }

  g_debug ("About to start monitoring '%s'", path);
  no_turbo = g_file_new_for_path (path);
  return g_file_monitor (no_turbo, G_FILE_MONITOR_NONE, NULL, NULL);
}

static GDir *
open_policy_dir (void)
{
  g_autofree char *dir = NULL;
  dir = ppd_utils_get_sysfs_path (CPUFREQ_POLICY_DIR);
  g_debug ("Opening policy dir '%s'", dir);
  return g_dir_open (dir, 0, NULL);
}

static gboolean
ppd_driver_intel_pstate_probe (PpdDriver *driver)
{
  PpdDriverIntelPstate *pstate = PPD_DRIVER_INTEL_PSTATE (driver);
  g_autoptr(GDir) dir = NULL;
  g_autofree char *policy_dir = NULL;
  const char *dirname;
  PpdProbeResult ret = PPD_PROBE_RESULT_FAIL;

  dir = open_policy_dir ();
  if (!dir)
    goto out;

  policy_dir = ppd_utils_get_sysfs_path (CPUFREQ_POLICY_DIR);
  while ((dirname = g_dir_read_name (dir)) != NULL) {
    g_autofree char *path = NULL;

    path = g_build_filename (policy_dir,
                             dirname,
                             "energy_performance_preference",
                             NULL);
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
      continue;

    pstate->devices = g_list_prepend (pstate->devices, g_steal_pointer (&path));
    ret = PPD_PROBE_RESULT_SUCCESS;
  }

  if (ret != PPD_PROBE_RESULT_SUCCESS)
    goto out;

  pstate->client = up_client_new ();
  if (pstate->client) {
    g_signal_connect (G_OBJECT (pstate->client), "notify::on-battery",
                      G_CALLBACK (on_battery_changed), pstate);
    pstate->on_battery = up_client_get_on_battery (pstate->client);
  }

  /* Monitor the first "no_turbo" */
  pstate->no_turbo_path = ppd_utils_get_sysfs_path (NO_TURBO_PATH);
  pstate->no_turbo_mon = monitor_no_turbo_prop (pstate->no_turbo_path);
  if (pstate->no_turbo_mon) {
    g_signal_connect (G_OBJECT (pstate->no_turbo_mon), "changed",
                      G_CALLBACK (no_turbo_changed), pstate);
  }
  update_no_turbo (pstate);

out:
  g_debug ("%s p-state settings",
           ret == PPD_PROBE_RESULT_SUCCESS ? "Found" : "Didn't find");
  return ret;
}

static const char *
profile_to_pref (PpdProfile profile,
                 gboolean   on_battery)
{
  /* Note that we don't check "energy_performance_available_preferences"
   * as all the values are always available */
  switch (profile) {
  case PPD_PROFILE_POWER_SAVER:
    return "power";
  case PPD_PROFILE_BALANCED:
    if (on_battery)
      return "balance_power";
    return "balance_performance";
  case PPD_PROFILE_PERFORMANCE:
    return "performance";
  }

  g_assert_not_reached ();
}

static gboolean
ppd_driver_intel_pstate_activate_profile (PpdDriver   *driver,
                                          PpdProfile   profile,
                                          GError     **error)
{
  PpdDriverIntelPstate *pstate = PPD_DRIVER_INTEL_PSTATE (driver);
  gboolean ret = TRUE;
  const char *pref;
  GList *l;

  g_return_val_if_fail (pstate->devices != NULL, FALSE);

  pref = profile_to_pref (profile, pstate->on_battery);

  for (l = pstate->devices; l != NULL; l = l->next) {
    const char *path = l->data;

    ret = ppd_utils_write (path, pref, error);
    if (!ret)
      break;
  }

  if (ret)
    pstate->activated_profile = profile;

  return ret;
}

static void
ppd_driver_intel_pstate_finalize (GObject *object)
{
  PpdDriverIntelPstate *driver;

  driver = PPD_DRIVER_INTEL_PSTATE (object);
  g_clear_list (&driver->devices, g_free);
  g_clear_object (&driver->client);
  g_clear_pointer (&driver->no_turbo_path, g_free);
  g_clear_object (&driver->no_turbo_mon);
  G_OBJECT_CLASS (ppd_driver_intel_pstate_parent_class)->finalize (object);
}

static void
ppd_driver_intel_pstate_class_init (PpdDriverIntelPstateClass *klass)
{
  GObjectClass *object_class;
  PpdDriverClass *driver_class;

  object_class = G_OBJECT_CLASS(klass);
  object_class->constructor = ppd_driver_intel_pstate_constructor;
  object_class->finalize = ppd_driver_intel_pstate_finalize;

  driver_class = PPD_DRIVER_CLASS(klass);
  driver_class->probe = ppd_driver_intel_pstate_probe;
  driver_class->activate_profile = ppd_driver_intel_pstate_activate_profile;
}

static void
ppd_driver_intel_pstate_init (PpdDriverIntelPstate *self)
{
}
