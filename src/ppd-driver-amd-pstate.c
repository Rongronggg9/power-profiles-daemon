/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 * Copyright (c) 2022 Prajna Sariputra <putr4.s@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#define G_LOG_DOMAIN "CpuDriver"

#include <upower.h>

#include "ppd-utils.h"
#include "ppd-driver-amd-pstate.h"

#define CPUFREQ_POLICY_DIR "/sys/devices/system/cpu/cpufreq/"
#define PSTATE_STATUS_PATH "/sys/devices/system/cpu/amd_pstate/status"
#define ACPI_PM_PROFILE "/sys/firmware/acpi/pm_profile"

enum acpi_preferred_pm_profiles {
  PM_UNSPECIFIED = 0,
  PM_DESKTOP = 1,
  PM_MOBILE = 2,
  PM_WORKSTATION = 3,
  PM_ENTERPRISE_SERVER = 4,
  PM_SOHO_SERVER = 5,
  PM_APPLIANCE_PC = 6,
  PM_PERFORMANCE_SERVER = 7,
  PM_TABLET = 8,
  NR_PM_PROFILES = 9
};

struct _PpdDriverAmdPstate
{
  PpdDriverCpu  parent_instance;

  PpdProfile activated_profile;
  GList *epp_devices; /* GList of paths */
};

G_DEFINE_TYPE (PpdDriverAmdPstate, ppd_driver_amd_pstate, PPD_TYPE_DRIVER_CPU)

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
  g_autofree char *pm_profile_path = NULL;
  g_autofree char *pm_profile_str = NULL;
  guint64 pm_profile;
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

  /* only run on things that we know aren't servers */
  pm_profile_path = ppd_utils_get_sysfs_path (ACPI_PM_PROFILE);
  if (!g_file_get_contents (pm_profile_path, &pm_profile_str, NULL, NULL))
    return ret;
  pm_profile = g_ascii_strtoull (pm_profile_str, NULL, 10);
  switch (pm_profile) {
  case PM_UNSPECIFIED:
  case PM_ENTERPRISE_SERVER:
  case PM_SOHO_SERVER:
  case PM_PERFORMANCE_SERVER:
    g_debug ("AMD-P-State not supported on PM profile %" G_GUINT64_FORMAT, pm_profile);
    return ret;
  default:
    break;
  }

  while ((dirname = g_dir_read_name (dir)) != NULL) {
    g_autofree char *base = NULL;
    g_autofree char *path = NULL;
    g_autoptr(GError) error = NULL;

    base = g_build_filename (policy_dir,
                             dirname,
                             NULL);

    path = g_build_filename (base,
                             "energy_performance_preference",
                             NULL);
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
      continue;

    pstate->epp_devices = g_list_prepend (pstate->epp_devices, g_steal_pointer (&base));
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
profile_to_gov_pref (PpdProfile profile)
{
  switch (profile) {
  case PPD_PROFILE_POWER_SAVER:
    return "powersave";
  case PPD_PROFILE_BALANCED:
    return "powersave";
  case PPD_PROFILE_PERFORMANCE:
    return "performance";
  }

  g_assert_not_reached ();
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
                       PpdProfile   profile,
                       GError     **error)
{
  gboolean ret = TRUE;
  GList *l;

  for (l = devices; l != NULL; l = l->next) {
    const char *base = l->data;
    g_autofree char *epp = NULL;
    g_autofree char *gov = NULL;

    gov = g_build_filename (base,
                            "scaling_governor",
                            NULL);

    ret = ppd_utils_write (gov, profile_to_gov_pref (profile), error);
    if (!ret)
      break;

    epp = g_build_filename (base,
                            "energy_performance_preference",
                            NULL);

    ret = ppd_utils_write (epp, profile_to_epp_pref (profile), error);
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

  g_return_val_if_fail (pstate->epp_devices != NULL, FALSE);

  if (pstate->epp_devices) {
    ret = apply_pref_to_devices (pstate->epp_devices, profile, error);
    if (!ret && pstate->activated_profile != PPD_PROFILE_UNSET) {
      g_autoptr(GError) error_local = NULL;
      /* reset back to previous */
      if (!apply_pref_to_devices (pstate->epp_devices,
                                  pstate->activated_profile,
                                  &error_local))
        g_warning ("failed to restore previous profile: %s", error_local->message);
      return ret;
    }
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

  object_class = G_OBJECT_CLASS (klass);
  object_class->constructor = ppd_driver_amd_pstate_constructor;
  object_class->finalize = ppd_driver_amd_pstate_finalize;

  driver_class = PPD_DRIVER_CLASS (klass);
  driver_class->probe = ppd_driver_amd_pstate_probe;
  driver_class->activate_profile = ppd_driver_amd_pstate_activate_profile;
}

static void
ppd_driver_amd_pstate_init (PpdDriverAmdPstate *self)
{
}
