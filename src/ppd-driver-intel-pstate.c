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

#define CPU_DIR "/sys/devices/system/cpu/"
#define CPUFREQ_POLICY_DIR "/sys/devices/system/cpu/cpufreq/"
#define DEFAULT_CPU_FREQ_SCALING_GOV "powersave"
#define PSTATE_STATUS_PATH "/sys/devices/system/cpu/intel_pstate/status"
#define NO_TURBO_PATH "/sys/devices/system/cpu/intel_pstate/no_turbo"
#define TURBO_PCT_PATH "/sys/devices/system/cpu/intel_pstate/turbo_pct"

struct _PpdDriverIntelPstate
{
  PpdDriver  parent_instance;

  PpdProfile activated_profile;
  GList *epp_devices; /* GList of paths */
  GList *epb_devices; /* GList of paths */
  GFileMonitor *no_turbo_mon;
  char *no_turbo_path;
};

G_DEFINE_TYPE (PpdDriverIntelPstate, ppd_driver_intel_pstate, PPD_TYPE_DRIVER)

static gboolean ppd_driver_intel_pstate_activate_profile (PpdDriver                   *driver,
                                                          PpdProfile                   profile,
                                                          PpdProfileActivationReason   reason,
                                                          GError                     **error);

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
update_no_turbo (PpdDriverIntelPstate *pstate)
{
  g_autofree char *contents = NULL;
  gboolean turbo_disabled = FALSE;

  if (g_file_get_contents (pstate->no_turbo_path, &contents, NULL, NULL)) {
    contents = g_strchomp (contents);
    if (g_strcmp0 (contents, "1") == 0)
      turbo_disabled = TRUE;
  }

  g_object_set (G_OBJECT (pstate), "performance-degraded",
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

static gboolean
has_turbo (void)
{
  g_autofree char *turbo_pct_path = NULL;
  g_autofree char *contents = NULL;
  gboolean has_turbo = TRUE;

  turbo_pct_path = ppd_utils_get_sysfs_path (TURBO_PCT_PATH);
  if (g_file_get_contents (turbo_pct_path, &contents, NULL, NULL)) {
    contents = g_strchomp (contents);
    if (g_strcmp0 (contents, "0") == 0)
      has_turbo = FALSE;
  }

  return has_turbo;
}

static PpdProbeResult
probe_epb (PpdDriverIntelPstate *pstate)
{
  g_autoptr(GDir) dir = NULL;
  g_autofree char *policy_dir = NULL;
  const char *dirname;
  PpdProbeResult ret = PPD_PROBE_RESULT_FAIL;

  policy_dir = ppd_utils_get_sysfs_path (CPU_DIR);
  dir = g_dir_open (policy_dir, 0, NULL);
  if (!dir) {
    g_debug ("Could not open %s", CPU_DIR);
    return ret;
  }

  while ((dirname = g_dir_read_name (dir)) != NULL) {
    g_autofree char *path = NULL;
    g_autofree char *gov_path = NULL;

    path = g_build_filename (policy_dir,
                             dirname,
                             "power",
                             "energy_perf_bias",
                             NULL);
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
      continue;

    pstate->epb_devices = g_list_prepend (pstate->epb_devices, g_steal_pointer (&path));
    ret = PPD_PROBE_RESULT_SUCCESS;
  }

  return ret;
}

static PpdProbeResult
probe_epp (PpdDriverIntelPstate *pstate)
{
  g_autoptr(GDir) dir = NULL;
  g_autofree char *policy_dir = NULL;
  g_autofree char *pstate_status_path = NULL;
  g_autofree char *status = NULL;
  const char *dirname;
  PpdProbeResult ret = PPD_PROBE_RESULT_FAIL;

  /* Verify that Intel P-State is running in active mode */
  pstate_status_path = ppd_utils_get_sysfs_path (PSTATE_STATUS_PATH);
  if (!g_file_get_contents (pstate_status_path, &status, NULL, NULL))
    return ret;
  status = g_strchomp (status);
  if (g_strcmp0 (status, "active") != 0) {
    g_debug ("Intel P-State is running in passive mode");
    return ret;
  }

  policy_dir = ppd_utils_get_sysfs_path (CPUFREQ_POLICY_DIR);
  dir = g_dir_open (policy_dir, 0, NULL);
  if (!dir) {
    g_debug ("Could not open %s", policy_dir);
    return ret;
  }

  while ((dirname = g_dir_read_name (dir)) != NULL) {
    g_autofree char *path = NULL;
    g_autofree char *gov_path = NULL;
    g_autoptr(GError) error = NULL;

    path = g_build_filename (policy_dir,
                             dirname,
                             "energy_performance_preference",
                             NULL);
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
      continue;

    /* Force a scaling_governor where the preference can be written */
    gov_path = g_build_filename (policy_dir,
                                 dirname,
                                 "scaling_governor",
                                 NULL);
    if (!ppd_utils_write (gov_path, DEFAULT_CPU_FREQ_SCALING_GOV, &error)) {
      g_warning ("Could not change scaling governor %s to '%s'", dirname, DEFAULT_CPU_FREQ_SCALING_GOV);
      continue;
    }

    pstate->epp_devices = g_list_prepend (pstate->epp_devices, g_steal_pointer (&path));
    ret = PPD_PROBE_RESULT_SUCCESS;
  }

  return ret;
}

static PpdProbeResult
ppd_driver_intel_pstate_probe (PpdDriver  *driver)
{
  PpdDriverIntelPstate *pstate = PPD_DRIVER_INTEL_PSTATE (driver);
  PpdProbeResult ret = PPD_PROBE_RESULT_FAIL;

  ret = probe_epp (pstate);
  if (ret == PPD_PROBE_RESULT_SUCCESS)
    probe_epb (pstate);
  else
    ret = probe_epb (pstate);

  if (ret != PPD_PROBE_RESULT_SUCCESS)
    goto out;

  if (has_turbo ()) {
    /* Monitor the first "no_turbo" */
    pstate->no_turbo_path = ppd_utils_get_sysfs_path (NO_TURBO_PATH);
    pstate->no_turbo_mon = monitor_no_turbo_prop (pstate->no_turbo_path);
    if (pstate->no_turbo_mon) {
      g_signal_connect (G_OBJECT (pstate->no_turbo_mon), "changed",
                        G_CALLBACK (no_turbo_changed), pstate);
    }
    update_no_turbo (pstate);
  }

out:
  g_debug ("%s p-state settings",
           ret == PPD_PROBE_RESULT_SUCCESS ? "Found" : "Didn't find");
  return ret;
}

static const char *
profile_to_epp_pref (PpdProfile profile)
{
  /* Note that we don't check "energy_performance_available_preferences"
   * as all the values are always available */
  switch (profile) {
  case PPD_PROFILE_POWER_SAVER:
    return "power";
  case PPD_PROFILE_BALANCED:
    return "balance_performance";
  case PPD_PROFILE_PERFORMANCE:
    return "performance";
  }

  g_assert_not_reached ();
}

static const char *
profile_to_epb_pref (PpdProfile profile)
{
  /* From arch/x86/include/asm/msr-index.h
   * See ENERGY_PERF_BIAS_* */
  switch (profile) {
  case PPD_PROFILE_POWER_SAVER:
    return "15";
  case PPD_PROFILE_BALANCED:
    return "6";
  case PPD_PROFILE_PERFORMANCE:
    return "0";
  }

  g_assert_not_reached ();
}

static gboolean
apply_pref_to_devices (GList       *devices,
                       const char  *pref,
                       GError     **error)
{
  gboolean ret = TRUE;
  GList *l;

  for (l = devices; l != NULL; l = l->next) {
    const char *path = l->data;

    ret = ppd_utils_write (path, pref, error);
    if (!ret)
      break;
  }

  return ret;
}

static gboolean
ppd_driver_intel_pstate_activate_profile (PpdDriver                    *driver,
                                          PpdProfile                   profile,
                                          PpdProfileActivationReason   reason,
                                          GError                     **error)
{
  PpdDriverIntelPstate *pstate = PPD_DRIVER_INTEL_PSTATE (driver);
  gboolean ret = FALSE;
  const char *pref;

  g_return_val_if_fail (pstate->epp_devices != NULL ||
                        pstate->epb_devices, FALSE);

  if (pstate->epp_devices) {
    pref = profile_to_epp_pref (profile);
    ret = apply_pref_to_devices (pstate->epp_devices, pref, error);
    if (!ret)
      return ret;
  }
  if (pstate->epb_devices) {
    pref = profile_to_epb_pref (profile);
    ret = apply_pref_to_devices (pstate->epb_devices, pref, error);
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
  g_clear_list (&driver->epp_devices, g_free);
  g_clear_list (&driver->epb_devices, g_free);
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
