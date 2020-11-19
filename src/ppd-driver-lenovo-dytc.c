/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include <gudev/gudev.h>
#include <gio/gio.h>

#include "ppd-driver-lenovo-dytc.h"
#include "ppd-utils.h"

#define LAPMODE_SYSFS_NAME "dytc_lapmode"
#define ACPI_PLATFORM_PROFILE_PATH "/sys/firmware/acpi/platform_profile"
#define ACPI_PLATFORM_PROFILE_CHOICES_PATH "/sys/firmware/acpi/platform_profile_choices"

struct _PpdDriverLenovoDytc
{
  PpdDriver  parent_instance;

  GUdevDevice *device;
  gboolean lapmode;
  PpdProfile acpi_platform_profile;
  GFileMonitor *lapmode_mon;
  GFileMonitor *acpi_platform_profile_mon;
  guint acpi_platform_profile_changed_id;
};

G_DEFINE_TYPE (PpdDriverLenovoDytc, ppd_driver_lenovo_dytc, PPD_TYPE_DRIVER)

static GObject*
ppd_driver_lenovo_dytc_constructor (GType                  type,
                                    guint                  n_construct_params,
                                    GObjectConstructParam *construct_params)
{
  GObject *object;

  object = G_OBJECT_CLASS (ppd_driver_lenovo_dytc_parent_class)->constructor (type,
                                                                              n_construct_params,
                                                                              construct_params);
  g_object_set (object,
                "driver-name", "lenovo_dytc",
                "profiles", PPD_PROFILE_PERFORMANCE | PPD_PROFILE_BALANCED | PPD_PROFILE_POWER_SAVER,
                NULL);

  return object;
}

static const char *
profile_to_acpi_platform_profile_value (PpdProfile profile)
{
  switch (profile) {
  case PPD_PROFILE_POWER_SAVER:
    return "low-power";
  case PPD_PROFILE_BALANCED:
    return "balanced";
  case PPD_PROFILE_PERFORMANCE:
    return "performance";
  }

  g_assert_not_reached ();
}

static PpdProfile
acpi_platform_profile_value_to_profile (const char *str)
{
  if (str == NULL)
    return PPD_PROFILE_UNSET;

  switch (str[0]) {
  case 'l': /* low-power */
  case 'c': /* cool */
  case 'q': /* quiet */
    return PPD_PROFILE_POWER_SAVER;
  case 'b':
    return PPD_PROFILE_BALANCED;
  case 'p':
    return PPD_PROFILE_PERFORMANCE;
  default:
    g_debug ("Got unsupported performance_profile value '%s'", str);
  }

  return PPD_PROFILE_UNSET;
}

static gboolean
verify_acpi_platform_profile_choices (void)
{
  g_autofree char *choices_str = NULL;
  g_autofree char *platform_profile_choices_path = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(GStrv) choices = NULL;

  platform_profile_choices_path = ppd_utils_get_sysfs_path (ACPI_PLATFORM_PROFILE_CHOICES_PATH);
  if (!g_file_get_contents (platform_profile_choices_path,
                            &choices_str, NULL, NULL)) {
    return FALSE;
  }

  choices = g_strsplit (choices_str, "\n", -1);
  return (g_strv_contains ((const char * const*) choices, "low-power") &&
          g_strv_contains ((const char * const*) choices, "balanced") &&
          g_strv_contains ((const char * const*) choices, "performance"));
}

static void
update_dytc_lapmode_state (PpdDriverLenovoDytc *dytc)
{
  gboolean new_lapmode;

  new_lapmode = g_udev_device_get_sysfs_attr_as_boolean_uncached (dytc->device, LAPMODE_SYSFS_NAME);
  if (new_lapmode == dytc->lapmode)
    return;

  dytc->lapmode = new_lapmode;
  g_debug ("dytc_lapmode is now %s, so profile is %s",
           dytc->lapmode ? "on" : "off",
           dytc->lapmode ? "inhibited" : "uninhibited");
  g_object_set (G_OBJECT (dytc),
                "performance-inhibited", dytc->lapmode ? "lap-detected" : NULL,
                NULL);
}

static void
update_acpi_platform_profile_state (PpdDriverLenovoDytc *dytc)
{
  g_autofree char *platform_profile_path = NULL;
  g_autofree char *new_profile_str = NULL;
  g_autoptr(GError) error = NULL;
  PpdProfile new_profile;

  platform_profile_path = ppd_utils_get_sysfs_path (ACPI_PLATFORM_PROFILE_PATH);
  if (!g_file_get_contents (platform_profile_path,
                            &new_profile_str, NULL, NULL)) {
    g_debug ("Failed to get contents for '%s': %s",
             platform_profile_path,
             error->message);
    return;
  }

  new_profile = acpi_platform_profile_value_to_profile (new_profile_str);
  if (new_profile == PPD_PROFILE_UNSET ||
      new_profile == dytc->acpi_platform_profile)
    return;

  g_debug ("ACPI performance_profile is now %c, so profile is %s",
           new_profile_str[0],
           ppd_profile_to_str (new_profile));
  dytc->acpi_platform_profile = new_profile;
  ppd_driver_emit_profile_changed (PPD_DRIVER (dytc), new_profile);
}

static void
lapmode_changed (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *other_file,
                 GFileMonitorEvent  event_type,
                 gpointer           user_data)
{
  PpdDriverLenovoDytc *dytc = user_data;
  g_debug (LAPMODE_SYSFS_NAME " attribute changed");
  update_dytc_lapmode_state (dytc);
}

static void
acpi_platform_profile_changed (GFileMonitor      *monitor,
                               GFile             *file,
                               GFile             *other_file,
                               GFileMonitorEvent  event_type,
                               gpointer           user_data)
{
  PpdDriverLenovoDytc *dytc = user_data;
  g_debug (ACPI_PLATFORM_PROFILE_PATH " changed");
  update_acpi_platform_profile_state (dytc);
}

static gboolean
ppd_driver_lenovo_dytc_activate_profile (PpdDriver   *driver,
                                         PpdProfile   profile,
                                         GError     **error)
{
  PpdDriverLenovoDytc *dytc = PPD_DRIVER_LENOVO_DYTC (driver);
  g_autofree char *platform_profile_path = NULL;

  g_return_val_if_fail (dytc->device, FALSE);

  if (dytc->acpi_platform_profile == profile) {
    g_debug ("Can't switch to %s mode, already there",
             ppd_profile_to_str (profile));
    return TRUE;
  }

  if (profile == PPD_PROFILE_PERFORMANCE &&
      dytc->lapmode) {
    g_debug ("Can't switch to performance mode, lapmode is detected");
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Mode is inhibited");
    return FALSE;
  }

  g_signal_handler_block (G_OBJECT (dytc->acpi_platform_profile_mon), dytc->acpi_platform_profile_changed_id);
  platform_profile_path = ppd_utils_get_sysfs_path (ACPI_PLATFORM_PROFILE_PATH);
  if (!ppd_utils_write (platform_profile_path, profile_to_acpi_platform_profile_value (profile), error)) {
    g_debug ("Failed to write to acpi_platform_profile: %s", (* error)->message);
    g_signal_handler_unblock (G_OBJECT (dytc->acpi_platform_profile_mon), dytc->acpi_platform_profile_changed_id);
    return FALSE;
  }
  g_signal_handler_unblock (G_OBJECT (dytc->acpi_platform_profile_mon), dytc->acpi_platform_profile_changed_id);

  g_debug ("Successfully switched to profile %s", ppd_profile_to_str (profile));
  dytc->acpi_platform_profile = profile;
  return TRUE;
}

static int
find_dytc (GUdevDevice *dev,
           gpointer     user_data)
{
  if (g_strcmp0 (g_udev_device_get_name (dev), "thinkpad_acpi") != 0)
    return 1;

  if (!g_udev_device_get_sysfs_attr (dev, LAPMODE_SYSFS_NAME))
    return 1;

  return 0;
}

static gboolean
ppd_driver_lenovo_dytc_probe (PpdDriver *driver)
{
  PpdDriverLenovoDytc *dytc = PPD_DRIVER_LENOVO_DYTC (driver);
  g_autoptr(GFile) acpi_platform_profile = NULL;
  g_autofree char *platform_profile_path = NULL;

  g_return_val_if_fail (!dytc->device, FALSE);

  platform_profile_path = ppd_utils_get_sysfs_path (ACPI_PLATFORM_PROFILE_PATH);
  if (!g_file_test (platform_profile_path, G_FILE_TEST_EXISTS))
    goto out;
  if (!verify_acpi_platform_profile_choices ())
    goto out;

  dytc->device = ppd_utils_find_device ("platform",
                                        (GCompareFunc) find_dytc,
                                        NULL);
  if (!dytc->device)
    goto out;

  dytc->lapmode_mon = ppd_utils_monitor_sysfs_attr (dytc->device,
                                                    LAPMODE_SYSFS_NAME,
                                                    NULL);
  g_signal_connect (G_OBJECT (dytc->lapmode_mon), "changed",
                    G_CALLBACK (lapmode_changed), dytc);

  acpi_platform_profile = g_file_new_for_path (platform_profile_path);
  dytc->acpi_platform_profile_mon = g_file_monitor (acpi_platform_profile,
                                                G_FILE_MONITOR_NONE,
                                                NULL,
                                                NULL);
  dytc->acpi_platform_profile_changed_id =
    g_signal_connect (G_OBJECT (dytc->acpi_platform_profile_mon), "changed",
                      G_CALLBACK (acpi_platform_profile_changed), dytc);
  update_dytc_lapmode_state (dytc);
  update_acpi_platform_profile_state (dytc);

out:
  g_debug ("%s a dytc_lapmode sysfs attribute to thinkpad_acpi",
           dytc->device ? "Found" : "Didn't find");
  return (dytc->device != NULL);
}

static void
ppd_driver_lenovo_dytc_finalize (GObject *object)
{
  PpdDriverLenovoDytc *driver;

  driver = PPD_DRIVER_LENOVO_DYTC (object);
  g_clear_object (&driver->device);
  g_clear_object (&driver->lapmode_mon);
  g_clear_object (&driver->acpi_platform_profile_mon);
  G_OBJECT_CLASS (ppd_driver_lenovo_dytc_parent_class)->finalize (object);
}

static void
ppd_driver_lenovo_dytc_class_init (PpdDriverLenovoDytcClass *klass)
{
  GObjectClass *object_class;
  PpdDriverClass *driver_class;

  object_class = G_OBJECT_CLASS(klass);
  object_class->constructor = ppd_driver_lenovo_dytc_constructor;
  object_class->finalize = ppd_driver_lenovo_dytc_finalize;

  driver_class = PPD_DRIVER_CLASS(klass);
  driver_class->probe = ppd_driver_lenovo_dytc_probe;
  driver_class->activate_profile = ppd_driver_lenovo_dytc_activate_profile;
}

static void
ppd_driver_lenovo_dytc_init (PpdDriverLenovoDytc *self)
{
}
