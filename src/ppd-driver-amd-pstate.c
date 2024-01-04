/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 * Copyright (c) 2022 Prajna Sariputra <putr4.s@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include <upower.h>

#include "ppd-utils.h"
#include "ppd-driver-amd-pstate.h"

#define CPUFREQ_POLICY_DIR "/sys/devices/system/cpu/cpufreq/"
#define DEFAULT_CPU_FREQ_SCALING_GOV "powersave"
#define PSTATE_STATUS_PATH "/sys/devices/system/cpu/amd_pstate/status"

struct _PpdDriverAmdPstate
{
  PpdDriverCpu  parent_instance;

  PpdProfile activated_profile;
  GList *epp_devices; /* GList of paths */
};

G_DEFINE_TYPE(PpdDriverAmdPstate, ppd_driver_amd_pstate, PPD_TYPE_DRIVER_CPU)

static gboolean ppd_driver_amd_pstate_activate_profile (PpdDriver                   *driver,
                                                        PpdProfile                   profile,
                                                        PpdProfileActivationReason   reason,
                                                        GError                     **error);

static GObject*
ppd_driver_amd_pstate_constructor (GType                  type,
                                   guint                  n_construct_params,
                                   GObjectConstructParam *construct_params)
{
  GObject *object;

  object = G_OBJECT_CLASS (ppd_driver_amd_pstate_parent_class)->constructor (type,
                                                                             n_construct_params,
                                                                             construct_params);
  g_object_set (object,
                "driver-name", "amd_pstate",
                "profiles", PPD_PROFILE_PERFORMANCE | PPD_PROFILE_BALANCED | PPD_PROFILE_POWER_SAVER,
                NULL);

  return object;
}

static PpdProbeResult
probe_epp (PpdDriverAmdPstate *pstate)
{
  g_autoptr(GDir) dir = NULL;
  g_autofree char *policy_dir = NULL;
  g_autofree char *pstate_status_path = NULL;
  g_autofree char *status = NULL;
  const char *dirname;
  PpdProbeResult ret = PPD_PROBE_RESULT_FAIL;

  /* Verify that AMD P-State is running in active mode */
  pstate_status_path = ppd_utils_get_sysfs_path (PSTATE_STATUS_PATH);
  if (!g_file_get_contents (pstate_status_path, &status, NULL, NULL))
    return ret;
  status = g_strchomp (status);
  if (g_strcmp0 (status, "active") != 0) {
    g_debug ("AMD P-State is not running in active mode");
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
ppd_driver_amd_pstate_probe (PpdDriver  *driver)
{
  PpdDriverAmdPstate *pstate = PPD_DRIVER_AMD_PSTATE (driver);
  PpdProbeResult ret = PPD_PROBE_RESULT_FAIL;

  ret = probe_epp (pstate);

  if (ret != PPD_PROBE_RESULT_SUCCESS)
    goto out;

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
ppd_driver_amd_pstate_activate_profile (PpdDriver                    *driver,
                                          PpdProfile                   profile,
                                          PpdProfileActivationReason   reason,
                                          GError                     **error)
{
  PpdDriverAmdPstate *pstate = PPD_DRIVER_AMD_PSTATE (driver);
  gboolean ret = FALSE;
  const char *pref;

  g_return_val_if_fail (pstate->epp_devices != NULL, FALSE);

  if (pstate->epp_devices) {
    pref = profile_to_epp_pref (profile);
    ret = apply_pref_to_devices (pstate->epp_devices, pref, error);
    if (!ret)
      return ret;
  }

  if (ret)
    pstate->activated_profile = profile;

  return ret;
}

static void
ppd_driver_amd_pstate_finalize (GObject *object)
{
  PpdDriverAmdPstate *driver;

  driver = PPD_DRIVER_AMD_PSTATE (object);
  g_clear_list (&driver->epp_devices, g_free);
  G_OBJECT_CLASS (ppd_driver_amd_pstate_parent_class)->finalize (object);
}

static void
ppd_driver_amd_pstate_class_init (PpdDriverAmdPstateClass *klass)
{
  GObjectClass *object_class;
  PpdDriverClass *driver_class;

  object_class = G_OBJECT_CLASS(klass);
  object_class->constructor = ppd_driver_amd_pstate_constructor;
  object_class->finalize = ppd_driver_amd_pstate_finalize;

  driver_class = PPD_DRIVER_CLASS(klass);
  driver_class->probe = ppd_driver_amd_pstate_probe;
  driver_class->activate_profile = ppd_driver_amd_pstate_activate_profile;
}

static void
ppd_driver_amd_pstate_init (PpdDriverAmdPstate *self)
{
}
