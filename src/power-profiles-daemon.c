/*
 * Copyright (c) 2014-2016, 2020-2021 Bastien Nocera <hadess@hadess.net>
 * Copyright (c) 2021 David Redondo <kde@david-redondo.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#define G_LOG_DOMAIN "Core"

#include "config.h"

#include <locale.h>
#include <polkit/polkit.h>
#include <stdio.h>

#include "power-profiles-daemon-resources.h"
#include "power-profiles-daemon.h"
#include "ppd-driver-cpu.h"
#include "ppd-driver-platform.h"
#include "ppd-action.h"
#include "ppd-enums.h"

#define POWER_PROFILES_DBUS_NAME          "org.freedesktop.UPower.PowerProfiles"
#define POWER_PROFILES_DBUS_PATH          "/org/freedesktop/UPower/PowerProfiles"
#define POWER_PROFILES_IFACE_NAME         POWER_PROFILES_DBUS_NAME

#define POWER_PROFILES_LEGACY_DBUS_NAME   "net.hadess.PowerProfiles"
#define POWER_PROFILES_LEGACY_DBUS_PATH   "/net/hadess/PowerProfiles"
#define POWER_PROFILES_LEGACY_IFACE_NAME  POWER_PROFILES_LEGACY_DBUS_NAME

#define POWER_PROFILES_POLICY_NAMESPACE "org.freedesktop.UPower.PowerProfiles"

#define POWER_PROFILES_RESOURCES_PATH "/org/freedesktop/UPower/PowerProfiles"

#ifndef POLKIT_HAS_AUTOPOINTERS
/* FIXME: Remove this once we're fine to depend on polkit 0.114 */
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitSubject, g_object_unref)
#endif

typedef struct {
  GMainLoop *main_loop;
  GDBusConnection *connection;
  guint name_id;
  guint legacy_name_id;
  gboolean was_started;
  int ret;

  GKeyFile *config;
  char *config_path;

  PolkitAuthority *auth;

  PpdProfile active_profile;
  PpdProfile selected_profile;
  GPtrArray *probed_drivers;
  PpdDriverCpu *cpu_driver;
  PpdDriverPlatform *platform_driver;
  GPtrArray *actions;
  GHashTable *profile_holds;
  GLogLevelFlags log_level;
} PpdApp;

typedef struct {
  PpdProfile profile;
  char *reason;
  char *application_id;
  char *requester;
  char *requester_iface;
} ProfileHold;

static void
profile_hold_free (ProfileHold *hold)
{
  if (hold == NULL)
    return;
  g_free (hold->reason);
  g_free (hold->application_id);
  g_free (hold->requester);
  g_free (hold->requester_iface);
  g_free (hold);
}

static PpdApp *ppd_app = NULL;

static void stop_profile_drivers (PpdApp *data);
static void start_profile_drivers (PpdApp *data);

/* profile drivers and actions */
#include "ppd-action-trickle-charge.h"
#include "ppd-action-amdgpu-panel-power.h"
#include "ppd-driver-placeholder.h"
#include "ppd-driver-platform-profile.h"
#include "ppd-driver-intel-pstate.h"
#include "ppd-driver-amd-pstate.h"
#include "ppd-driver-fake.h"

typedef GType (*GTypeGetFunc) (void);

static GTypeGetFunc objects[] = {
  /* Hardware specific profile drivers */
  ppd_driver_fake_get_type,
  ppd_driver_platform_profile_get_type,
  ppd_driver_intel_pstate_get_type,
  ppd_driver_amd_pstate_get_type,

  /* Generic profile driver */
  ppd_driver_placeholder_get_type,

  /* Actions */
  ppd_action_trickle_charge_get_type,
  ppd_action_amdgpu_panel_power_get_type,
};

typedef enum {
  PROP_ACTIVE_PROFILE             = 1 << 0,
  PROP_INHIBITED                  = 1 << 1,
  PROP_PROFILES                   = 1 << 2,
  PROP_ACTIONS                    = 1 << 3,
  PROP_DEGRADED                   = 1 << 4,
  PROP_ACTIVE_PROFILE_HOLDS       = 1 << 5,
  PROP_VERSION                    = 1 << 6,
} PropertiesMask;

#define PROP_ALL (PROP_ACTIVE_PROFILE       | \
                  PROP_INHIBITED            | \
                  PROP_PROFILES             | \
                  PROP_ACTIONS              | \
                  PROP_DEGRADED             | \
                  PROP_ACTIVE_PROFILE_HOLDS | \
                  PROP_VERSION)

static gboolean
driver_profile_support (PpdDriver *driver,
                       PpdProfile profile)
{
  if (!PPD_IS_DRIVER (driver))
    return FALSE;
  return (ppd_driver_get_profiles (driver) & profile) != 0;
}

static gboolean
get_profile_available (PpdApp     *data,
                       PpdProfile  profile)
{
  return driver_profile_support (PPD_DRIVER (data->cpu_driver), profile) ||
         driver_profile_support (PPD_DRIVER (data->platform_driver), profile);
}

static const char *
get_active_profile (PpdApp *data)
{
  return ppd_profile_to_str (data->active_profile);
}

static char *
get_performance_degraded (PpdApp *data)
{
  const gchar *cpu_degraded = NULL;
  const gchar *platform_degraded = NULL;

  if (driver_profile_support (PPD_DRIVER (data->platform_driver), PPD_PROFILE_PERFORMANCE))
    platform_degraded = ppd_driver_get_performance_degraded (PPD_DRIVER (data->platform_driver));

  if (driver_profile_support (PPD_DRIVER (data->cpu_driver), PPD_PROFILE_PERFORMANCE))
    cpu_degraded = ppd_driver_get_performance_degraded (PPD_DRIVER (data->cpu_driver));

  if (!cpu_degraded && !platform_degraded)
    return g_strdup ("");

  if (!cpu_degraded)
    return g_strdup (platform_degraded);

  if (!platform_degraded)
    return g_strdup (cpu_degraded);

  return g_strjoin (",", cpu_degraded, platform_degraded, NULL);
}

static GVariant *
get_profiles_variant (PpdApp *data)
{
  GVariantBuilder builder;
  guint i;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (i = 0; i < NUM_PROFILES; i++) {
    PpdDriver *platform_driver = PPD_DRIVER (data->platform_driver);
    PpdDriver *cpu_driver = PPD_DRIVER (data->cpu_driver);
    PpdProfile profile = 1 << i;
    GVariantBuilder asv_builder;
    gboolean cpu, platform;
    const gchar *driver = NULL;

    /* check if any of the drivers support */
    if (!get_profile_available (data, profile))
      continue;

    g_variant_builder_init (&asv_builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&asv_builder, "{sv}", "Profile",
                           g_variant_new_string (ppd_profile_to_str (profile)));
    cpu = driver_profile_support (cpu_driver, profile);
    platform = driver_profile_support (platform_driver, profile);
    if (cpu)
      g_variant_builder_add (&asv_builder, "{sv}", "CpuDriver",
                             g_variant_new_string (ppd_driver_get_driver_name (cpu_driver)));
    if (platform)
      g_variant_builder_add (&asv_builder, "{sv}", "PlatformDriver",
                             g_variant_new_string (ppd_driver_get_driver_name (platform_driver)));

    /* compatibility with older API */
    if (cpu && platform)
      driver = "multiple";
    else if (cpu)
      driver = ppd_driver_get_driver_name (cpu_driver);
    else if (platform)
      driver = ppd_driver_get_driver_name (platform_driver);

    if (driver)
      g_variant_builder_add (&asv_builder, "{sv}", "Driver",
                             g_variant_new_string (driver));

    g_variant_builder_add (&builder, "a{sv}", &asv_builder);
  }

  return g_variant_builder_end (&builder);
}

static GVariant *
get_actions_variant (PpdApp *data)
{
  GVariantBuilder builder;
  guint i;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));

  for (i = 0; i < data->actions->len; i++) {
    PpdAction *action = g_ptr_array_index (data->actions, i);

    g_variant_builder_add (&builder, "s", ppd_action_get_action_name (action));
  }

  return g_variant_builder_end (&builder);
}

static GVariant *
get_profile_holds_variant (PpdApp *data)
{
  GVariantBuilder builder;
  GHashTableIter iter;
  gpointer value;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));
  g_hash_table_iter_init (&iter, data->profile_holds);

  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    GVariantBuilder asv_builder;
    ProfileHold *hold = value;

    g_variant_builder_init (&asv_builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&asv_builder, "{sv}", "ApplicationId",
                           g_variant_new_string (hold->application_id));
    g_variant_builder_add (&asv_builder, "{sv}", "Profile",
                           g_variant_new_string (ppd_profile_to_str (hold->profile)));
    g_variant_builder_add (&asv_builder, "{sv}", "Reason", g_variant_new_string (hold->reason));

    g_variant_builder_add (&builder, "a{sv}", &asv_builder);
  }

  return g_variant_builder_end (&builder);
}

static void
send_dbus_event_iface (PpdApp         *data,
                       PropertiesMask  mask,
                       const gchar    *iface,
                       const gchar    *path)
{
  GVariantBuilder props_builder;
  GVariant *props_changed = NULL;

  g_assert (data->connection);

  if (mask == 0)
    return;

  g_assert ((mask & PROP_ALL) != 0);

  g_variant_builder_init (&props_builder, G_VARIANT_TYPE ("a{sv}"));

  if (mask & PROP_ACTIVE_PROFILE) {
    g_variant_builder_add (&props_builder, "{sv}", "ActiveProfile",
                           g_variant_new_string (get_active_profile (data)));
  }
  if (mask & PROP_INHIBITED) {
    g_variant_builder_add (&props_builder, "{sv}", "PerformanceInhibited",
                           g_variant_new_string (""));
  }
  if (mask & PROP_DEGRADED) {
    gchar *degraded = get_performance_degraded (data);
    g_variant_builder_add (&props_builder, "{sv}", "PerformanceDegraded",
                           g_variant_new_take_string (g_steal_pointer (&degraded)));
  }
  if (mask & PROP_PROFILES) {
    g_variant_builder_add (&props_builder, "{sv}", "Profiles",
                           get_profiles_variant (data));
  }
  if (mask & PROP_ACTIONS) {
    g_variant_builder_add (&props_builder, "{sv}", "Actions",
                           get_actions_variant (data));
  }
  if (mask & PROP_ACTIVE_PROFILE_HOLDS) {
    g_variant_builder_add (&props_builder, "{sv}", "ActiveProfileHolds",
                           get_profile_holds_variant (data));
  }
  if (mask & PROP_VERSION) {
    g_variant_builder_add (&props_builder, "{sv}", "Version",
                           g_variant_new_string (VERSION));
  }

  props_changed = g_variant_new ("(s@a{sv}@as)", iface,
                                 g_variant_builder_end (&props_builder),
                                 g_variant_new_strv (NULL, 0));

  g_dbus_connection_emit_signal (data->connection,
                                 NULL,
                                 path,
                                 "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged",
                                 props_changed, NULL);
}

static void
send_dbus_event (PpdApp         *data,
                 PropertiesMask  mask)
{
  send_dbus_event_iface (data, mask,
                         POWER_PROFILES_IFACE_NAME,
                         POWER_PROFILES_DBUS_PATH);
  send_dbus_event_iface (data, mask,
                         POWER_PROFILES_LEGACY_IFACE_NAME,
                         POWER_PROFILES_LEGACY_DBUS_PATH);
}

static void
save_configuration (PpdApp *data)
{
  g_autoptr(GError) error = NULL;

  if (PPD_IS_DRIVER_CPU (data->cpu_driver)) {
    g_key_file_set_string (data->config, "State", "CpuDriver",
                           ppd_driver_get_driver_name (PPD_DRIVER (data->cpu_driver)));
  }

  if (PPD_IS_DRIVER_PLATFORM (data->platform_driver)) {
    g_key_file_set_string (data->config, "State", "PlatformDriver",
                           ppd_driver_get_driver_name (PPD_DRIVER (data->platform_driver)));
  }

  g_key_file_set_string (data->config, "State", "Profile",
                         ppd_profile_to_str (data->active_profile));

  if (!g_key_file_save_to_file (data->config, data->config_path, &error))
    g_warning ("Could not save configuration file '%s': %s", data->config_path, error->message);
}

static gboolean
apply_configuration (PpdApp *data)
{
  g_autofree char *platform_driver = NULL;
  g_autofree char *profile_str = NULL;
  g_autofree char *cpu_driver = NULL;
  PpdProfile profile;

  cpu_driver = g_key_file_get_string (data->config, "State", "CpuDriver", NULL);
  if (PPD_IS_DRIVER_CPU (data->cpu_driver) &&
      g_strcmp0 (ppd_driver_get_driver_name (PPD_DRIVER (data->cpu_driver)), cpu_driver) != 0)
    return FALSE;

  platform_driver = g_key_file_get_string (data->config, "State", "PlatformDriver", NULL);

  if (PPD_IS_DRIVER_PLATFORM (data->platform_driver) &&
      g_strcmp0 (ppd_driver_get_driver_name (PPD_DRIVER (data->platform_driver)), platform_driver) != 0)
    return FALSE;

  profile_str = g_key_file_get_string (data->config, "State", "Profile", NULL);
  if (profile_str == NULL)
    return FALSE;

  profile = ppd_profile_from_str (profile_str);
  if (profile == PPD_PROFILE_UNSET) {
    g_debug ("Resetting invalid configuration profile '%s'", profile_str);
    g_key_file_remove_key (data->config, "State", "Profile", NULL);
    return FALSE;
  }

  g_debug ("Applying profile '%s' from configuration file", profile_str);
  data->active_profile = profile;
  return TRUE;
}

static void
load_configuration (PpdApp *data)
{
  g_autoptr(GError) error = NULL;

  if (g_getenv ("UMOCKDEV_DIR") != NULL)
    data->config_path = g_build_filename (g_getenv ("UMOCKDEV_DIR"), "ppd_test_conf.ini", NULL);
  else
    data->config_path = g_strdup ("/var/lib/power-profiles-daemon/state.ini");
  data->config = g_key_file_new ();
  if (!g_key_file_load_from_file (data->config, data->config_path, G_KEY_FILE_KEEP_COMMENTS, &error))
    g_debug ("Could not load configuration file '%s': %s", data->config_path, error->message);
}

static void
actions_activate_profile (GPtrArray *actions,
                          PpdProfile profile)
{
  guint i;

  g_return_if_fail (actions != NULL);

  for (i = 0; i < actions->len; i++) {
    g_autoptr(GError) error = NULL;
    PpdAction *action;
    gboolean ret;

    action = g_ptr_array_index (actions, i);

    ret = ppd_action_activate_profile (action, profile, &error);
    if (!ret)
      g_warning ("Failed to activate action '%s' to profile %s: %s",
                 ppd_profile_to_str (profile),
                 ppd_action_get_action_name (action),
                 error->message);
  }
}

static gboolean
activate_target_profile (PpdApp                      *data,
                         PpdProfile                   target_profile,
                         PpdProfileActivationReason   reason,
                         GError                     **error)
{
  PpdProfile current_profile = data->active_profile;
  gboolean cpu_set = TRUE;
  gboolean platform_set = TRUE;

  g_debug ("Setting active profile '%s' for reason '%s' (current: '%s')",
           ppd_profile_to_str (target_profile),
           ppd_profile_activation_reason_to_str (reason),
           ppd_profile_to_str (current_profile));

  /* Try CPU first */
  if (driver_profile_support (PPD_DRIVER (data->cpu_driver), target_profile))
    cpu_set = ppd_driver_activate_profile (PPD_DRIVER (data->cpu_driver),
                                           target_profile, reason, error);

  if (!cpu_set) {
    g_prefix_error (error, "Failed to activate CPU driver '%s': ",
                    ppd_driver_get_driver_name (PPD_DRIVER (data->cpu_driver)));
    return FALSE;
  }

  /* Then try platform */
  if (driver_profile_support (PPD_DRIVER (data->platform_driver), target_profile)) {
    platform_set = ppd_driver_activate_profile (PPD_DRIVER (data->platform_driver),
                                                target_profile, reason, error);
  }

  if (!platform_set) {
    g_prefix_error (error, "Failed to activate platform driver '%s': ",
                    ppd_driver_get_driver_name (PPD_DRIVER (data->platform_driver)));

    /* Try to recover */
    if (cpu_set) {
      g_autoptr(GError) recovery_error = NULL;

      g_debug ("Reverting CPU driver '%s' to profile '%s'",
               ppd_driver_get_driver_name (PPD_DRIVER (data->cpu_driver)),
               ppd_profile_to_str (current_profile));

      if (!ppd_driver_activate_profile (PPD_DRIVER (data->cpu_driver),
                                        current_profile, PPD_PROFILE_ACTIVATION_REASON_INTERNAL,
                                        &recovery_error)) {
        g_prefix_error (error, "Failed to revert CPU driver '%s': ",
                        ppd_driver_get_driver_name (PPD_DRIVER (data->cpu_driver)));
        g_warning ("Failed to revert CPU driver '%s': %s",
                   ppd_driver_get_driver_name (PPD_DRIVER (data->cpu_driver)),
                   recovery_error->message);
      }
    }

    return FALSE;
  }

  actions_activate_profile (data->actions, target_profile);

  data->active_profile = target_profile;

  if (reason == PPD_PROFILE_ACTIVATION_REASON_USER ||
      reason == PPD_PROFILE_ACTIVATION_REASON_INTERNAL)
    save_configuration (data);

  return TRUE;
}

static void
release_hold_notify (PpdApp      *data,
                     ProfileHold *hold,
                     guint        cookie)
{
  const char *req_path = POWER_PROFILES_DBUS_PATH;

  if (g_strcmp0 (hold->requester_iface, POWER_PROFILES_LEGACY_IFACE_NAME) == 0)
    req_path = POWER_PROFILES_LEGACY_DBUS_PATH;

  g_dbus_connection_emit_signal (data->connection, hold->requester, req_path,
                                 hold->requester_iface, "ProfileReleased",
                                 g_variant_new ("(u)", cookie), NULL);
}

static void
release_all_profile_holds (PpdApp *data)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, data->profile_holds);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    ProfileHold *hold = value;
    guint cookie = GPOINTER_TO_UINT (key);

    release_hold_notify (data, hold, cookie);
    g_bus_unwatch_name (cookie);
  }
  g_hash_table_remove_all (data->profile_holds);
}

static gboolean
set_active_profile (PpdApp      *data,
                    const char  *profile,
                    GError     **error)
{
  PpdProfile target_profile;
  guint mask = PROP_ACTIVE_PROFILE;

  target_profile = ppd_profile_from_str (profile);
  if (target_profile == PPD_PROFILE_UNSET) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "Invalid profile name '%s'", profile);
    return FALSE;
  }
  if (!get_profile_available (data, target_profile)) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "Cannot switch to unavailable profile '%s'", profile);
    return FALSE;
  }

  if (target_profile == data->active_profile)
    return TRUE;

  g_debug ("Transitioning active profile from '%s' to '%s' by user request",
           ppd_profile_to_str (data->active_profile), profile);

  if (g_hash_table_size (data->profile_holds) != 0 ) {
    g_debug ("Releasing active profile holds");
    release_all_profile_holds (data);
    mask |= PROP_ACTIVE_PROFILE_HOLDS;
  }

  if (!activate_target_profile (data, target_profile, PPD_PROFILE_ACTIVATION_REASON_USER, error))
    return FALSE;
  data->selected_profile = target_profile;
  send_dbus_event (data, mask);

  return TRUE;
}

static PpdProfile
effective_hold_profile (PpdApp *data)
{
  GHashTableIter iter;
  gpointer value;
  PpdProfile profile = PPD_PROFILE_UNSET;

  g_hash_table_iter_init (&iter, data->profile_holds);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    ProfileHold *hold = value;

    if (hold->profile == PPD_PROFILE_POWER_SAVER) {
      profile = PPD_PROFILE_POWER_SAVER;
      break;
    }
    profile = hold->profile;
  }
  return profile;
}

static void
driver_performance_degraded_changed_cb (GObject    *gobject,
                                        GParamSpec *pspec,
                                        gpointer    user_data)
{
  PpdApp *data = user_data;
  PpdDriver *driver = PPD_DRIVER (gobject);
  const char *prop_str = pspec->name;

  if (g_strcmp0 (prop_str, "performance-degraded") != 0) {
    g_warning ("Ignoring '%s' property change on profile driver '%s'",
               prop_str, ppd_driver_get_driver_name (driver));
    return;
  }

  if (!(ppd_driver_get_profiles (driver) & PPD_PROFILE_PERFORMANCE)) {
    g_warning ("Ignored 'performance-degraded' change on non-performance driver '%s'",
               ppd_driver_get_driver_name (driver));
    return;
  }

  send_dbus_event (data, PROP_DEGRADED);
}

static void
driver_profile_changed_cb (PpdDriver *driver,
                           PpdProfile new_profile,
                           gpointer   user_data)
{
  PpdApp *data = user_data;

  g_debug ("Driver '%s' switched internally to profile '%s' (current: '%s')",
           ppd_driver_get_driver_name (driver),
           ppd_profile_to_str (new_profile),
           ppd_profile_to_str (data->active_profile));
  if (new_profile == data->active_profile)
    return;

  activate_target_profile (data, new_profile, PPD_PROFILE_ACTIVATION_REASON_INTERNAL, NULL);
  send_dbus_event (data, PROP_ACTIVE_PROFILE);
}

static void
release_profile_hold (PpdApp *data,
                      guint   cookie)
{
  guint mask = PROP_ACTIVE_PROFILE_HOLDS;
  ProfileHold *hold;
  PpdProfile hold_profile, next_profile;

  hold = g_hash_table_lookup (data->profile_holds, GUINT_TO_POINTER (cookie));
  if (!hold) {
    g_debug ("No hold with cookie %d", cookie);
    return;
  }

  g_bus_unwatch_name (cookie);
  hold_profile = hold->profile;
  release_hold_notify (data, hold, cookie);
  g_hash_table_remove (data->profile_holds, GUINT_TO_POINTER (cookie));

  if (g_hash_table_size (data->profile_holds) == 0 &&
      hold_profile != data->selected_profile) {
    g_debug ("No profile holds anymore going back to last manually activated profile");
    activate_target_profile (data, data->selected_profile, PPD_PROFILE_ACTIVATION_REASON_PROGRAM_HOLD, NULL);
    mask |= PROP_ACTIVE_PROFILE;
  } else if (hold_profile == data->active_profile) {
    next_profile = effective_hold_profile (data);
    if (next_profile != PPD_PROFILE_UNSET &&
        next_profile != data->active_profile) {
      g_debug ("Next profile is %s", ppd_profile_to_str (next_profile));
      activate_target_profile (data, next_profile, PPD_PROFILE_ACTIVATION_REASON_PROGRAM_HOLD, NULL);
      mask |= PROP_ACTIVE_PROFILE;
    }
  }

  send_dbus_event (data, mask);
}

static void
holder_disappeared (GDBusConnection *connection,
                    const gchar     *name,
                    gpointer         user_data)
{
  PpdApp *data = user_data;
  GHashTableIter iter;
  gpointer key, value;
  GPtrArray *cookies;
  guint i;

  cookies = g_ptr_array_new ();
  g_hash_table_iter_init (&iter, data->profile_holds);
  while (g_hash_table_iter_next (&iter, &key, (gpointer *) &value)) {
    guint cookie = GPOINTER_TO_UINT (key);
    ProfileHold *hold = value;

    if (g_strcmp0 (hold->requester, name) != 0)
      continue;

    g_debug ("Holder %s with cookie %u disappeared, adding to list", name, cookie);
    g_ptr_array_add (cookies, GUINT_TO_POINTER (cookie));
  }

  for (i = 0; i < cookies->len; i++) {
    guint cookie = GPOINTER_TO_UINT (cookies->pdata[i]);
    g_debug ("Removing profile hold for cookie %u", cookie);
    release_profile_hold (data, cookie);
  }
  g_ptr_array_free (cookies, TRUE);
}

static void
hold_profile (PpdApp                *data,
              GVariant              *parameters,
              GDBusMethodInvocation *invocation)
{
  const char *profile_name;
  const char *reason;
  const char *application_id;
  PpdProfile profile;
  ProfileHold *hold;
  guint watch_id;
  guint mask;

  g_variant_get (parameters, "(&s&s&s)", &profile_name, &reason, &application_id);
  profile = ppd_profile_from_str (profile_name);
  if (profile != PPD_PROFILE_PERFORMANCE &&
      profile != PPD_PROFILE_POWER_SAVER) {
    g_dbus_method_invocation_return_error_literal (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                                   "Only profiles 'performance' and 'power-saver' can be a hold profile");
    return;
  }
  if (!get_profile_available (data, profile)) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                           "Cannot hold profile '%s' as it is not available",
                                           profile_name);
    return;
  }

  hold = g_new0 (ProfileHold, 1);
  hold->profile = profile;
  hold->reason = g_strdup (reason);
  hold->application_id = g_strdup (application_id);
  hold->requester = g_strdup (g_dbus_method_invocation_get_sender (invocation));
  hold->requester_iface = g_strdup (g_dbus_method_invocation_get_interface_name (invocation));

  g_debug ("%s (%s) requesting to hold profile '%s', reason: '%s'", application_id,
           hold->requester, profile_name, reason);
  watch_id = g_bus_watch_name_on_connection (data->connection, hold->requester,
                                             G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
                                             holder_disappeared, data, NULL);
  g_hash_table_insert (data->profile_holds, GUINT_TO_POINTER (watch_id), hold);
  g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", watch_id));
  mask = PROP_ACTIVE_PROFILE_HOLDS;

  if (profile != data->active_profile) {
    PpdProfile target_profile = effective_hold_profile (data);
    if (target_profile != PPD_PROFILE_UNSET &&
        target_profile != data->active_profile) {
      activate_target_profile (data, target_profile, PPD_PROFILE_ACTIVATION_REASON_PROGRAM_HOLD, NULL);
      mask |= PROP_ACTIVE_PROFILE;
    }
  }

  send_dbus_event (data, mask);
}

static void
release_profile (PpdApp                *data,
                 GVariant              *parameters,
                 GDBusMethodInvocation *invocation)
{
  guint cookie;
  g_variant_get (parameters, "(u)", &cookie);
  if (!g_hash_table_contains (data->profile_holds, GUINT_TO_POINTER (cookie))) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                           "No hold with cookie  %d", cookie);
    return;
  }
  release_profile_hold (data, cookie);
  g_dbus_method_invocation_return_value (invocation, NULL);
}

static gboolean
check_action_permission (PpdApp                *data,
                         const char            *sender,
                         const char            *action,
                         GError               **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(PolkitAuthorizationResult) result = NULL;
  g_autoptr(PolkitSubject) subject = NULL;

  subject = polkit_system_bus_name_new (sender);
  result = polkit_authority_check_authorization_sync (data->auth,
                                                      subject,
                                                      action,
                                                      NULL,
                                                      POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE,
                                                      NULL, &local_error);
  if (result == NULL ||
      !polkit_authorization_result_get_is_authorized (result))
    {
      g_set_error (error, G_DBUS_ERROR,
                   G_DBUS_ERROR_ACCESS_DENIED,
                   "Not Authorized: %s", local_error ? local_error->message : action);
      return FALSE;
    }

  return TRUE;

}

static GVariant *
handle_get_property (GDBusConnection *connection,
                     const gchar     *sender,
                     const gchar     *object_path,
                     const gchar     *interface_name,
                     const gchar     *property_name,
                     GError         **error,
                     gpointer         user_data)
{
  PpdApp *data = user_data;

  g_assert (data->connection);

  if (g_strcmp0 (property_name, "ActiveProfile") == 0)
    return g_variant_new_string (get_active_profile (data));
  if (g_strcmp0 (property_name, "PerformanceInhibited") == 0)
    return g_variant_new_string ("");
  if (g_strcmp0 (property_name, "Profiles") == 0)
    return get_profiles_variant (data);
  if (g_strcmp0 (property_name, "Actions") == 0)
    return get_actions_variant (data);
  if (g_strcmp0 (property_name, "PerformanceDegraded") == 0) {
    gchar *degraded = get_performance_degraded (data);
    return g_variant_new_take_string (g_steal_pointer (&degraded));
  }
  if (g_strcmp0 (property_name, "ActiveProfileHolds") == 0)
    return get_profile_holds_variant (data);
  if (g_strcmp0 (property_name, "Version") == 0)
    return g_variant_new_string (VERSION);
  return NULL;
}

static gboolean
handle_set_property (GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GVariant         *value,
                     GError          **error,
                     gpointer          user_data)
{
  PpdApp *data = user_data;
  const char *profile;

  g_assert (data->connection);

  if (g_strcmp0 (property_name, "ActiveProfile") != 0) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "No such property: %s", property_name);
    return FALSE;
  }
  if (!check_action_permission (data, sender,
                                POWER_PROFILES_POLICY_NAMESPACE ".switch-profile",
                                error))
    return FALSE;

  g_variant_get (value, "&s", &profile);
  return set_active_profile (data, profile, error);
}

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
  PpdApp *data = user_data;
  g_assert (data->connection);

  if (!g_str_equal (interface_name, POWER_PROFILES_IFACE_NAME) &&
      !g_str_equal (interface_name, POWER_PROFILES_LEGACY_IFACE_NAME)) {
    g_dbus_method_invocation_return_error (invocation,G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                           "Unknown interface %s", interface_name);
    return;
  }

  if (g_strcmp0 (method_name, "HoldProfile") == 0) {
    g_autoptr(GError) local_error = NULL;
    if (!check_action_permission (data,
                                  g_dbus_method_invocation_get_sender (invocation),
                                  POWER_PROFILES_POLICY_NAMESPACE ".hold-profile",
                                  &local_error)) {
      g_dbus_method_invocation_return_gerror (invocation, local_error);
      return;
    }
    hold_profile (data, parameters, invocation);
  } else if (g_strcmp0 (method_name, "ReleaseProfile") == 0) {
    release_profile (data, parameters, invocation);
  } else {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                             "No such method %s in interface %s", interface_name,
                                              method_name);
  }
}


static const GDBusInterfaceVTable interface_vtable =
{
  handle_method_call,
  handle_get_property,
  handle_set_property
};

typedef struct {
  PpdApp *app;
  GBusNameOwnerFlags flags;
  GDBusInterfaceInfo *interface;
  GDBusInterfaceInfo *legacy_interface;
} PpdBusOwnData;

static void
ppd_bus_own_data_free (PpdBusOwnData *data)
{
  g_clear_pointer (&data->interface, g_dbus_interface_info_unref);
  g_clear_pointer (&data->legacy_interface, g_dbus_interface_info_unref);
  g_free (data);
}

static void
name_lost_handler (GDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
{
  PpdBusOwnData *data = user_data;
  PpdApp *app = data->app;

  g_debug ("power-profiles-daemon is already running, or it cannot own its D-Bus name. Verify installation.");
  if (!app->was_started)
    app->ret = 1;

  g_main_loop_quit (app->main_loop);
}

static void
legacy_name_acquired_handler (GDBusConnection *connection,
                              const gchar     *name,
                              gpointer         user_data)
{
  g_debug ("Name '%s' acquired", name);
}

static void
bus_acquired_handler (GDBusConnection *connection,
                      const gchar     *name,
                      gpointer         user_data)
{
  PpdBusOwnData *data = user_data;

  g_dbus_connection_register_object (connection,
                                     POWER_PROFILES_DBUS_PATH,
                                     data->interface,
                                     &interface_vtable,
                                     data->app,
                                     NULL,
                                     NULL);

  g_dbus_connection_register_object (connection,
                                     POWER_PROFILES_LEGACY_DBUS_PATH,
                                     data->legacy_interface,
                                     &interface_vtable,
                                     data->app,
                                     NULL,
                                     NULL);

  data->app->legacy_name_id = g_bus_own_name_on_connection (connection,
                                                            POWER_PROFILES_LEGACY_DBUS_NAME,
                                                            data->flags,
                                                            legacy_name_acquired_handler,
                                                            name_lost_handler,
                                                            data,
                                                            NULL);

  data->app->connection = g_object_ref (connection);
}

static gboolean
has_required_drivers (PpdApp *data)
{
  if (!PPD_IS_DRIVER_CPU (data->cpu_driver) &&
      !PPD_IS_DRIVER_PLATFORM (data->platform_driver))
    return FALSE;

  if (!get_profile_available (data, PPD_PROFILE_BALANCED | PPD_PROFILE_POWER_SAVER))
    return FALSE;

  return TRUE;
}

static void
driver_probe_request_cb (PpdDriver *driver,
                         gpointer   user_data)
{
  PpdApp *data = user_data;

  stop_profile_drivers (data);
  start_profile_drivers (data);
}

static void
stop_profile_drivers (PpdApp *data)
{
  release_all_profile_holds (data);
  g_ptr_array_set_size (data->probed_drivers, 0);
  g_ptr_array_set_size (data->actions, 0);
  g_clear_object (&data->cpu_driver);
  g_clear_object (&data->platform_driver);
}

static gboolean
action_blocked (PpdAction *action)
{
  const gchar *action_name = ppd_action_get_action_name (action);
  const gchar *env = g_getenv ("POWER_PROFILE_DAEMON_ACTION_BLOCK");

  if (env == NULL)
    return FALSE;

  g_auto(GStrv) actions = NULL;

  actions = g_strsplit (env, ",", -1);
  return g_strv_contains ((const gchar *const *)actions, action_name);
}

static gboolean
driver_blocked (PpdDriver *driver)
{
  const gchar *driver_name = ppd_driver_get_driver_name (driver);
  const gchar *env = g_getenv ("POWER_PROFILE_DAEMON_DRIVER_BLOCK");

  if (env == NULL)
    return FALSE;

  g_auto(GStrv) drivers = NULL;
  drivers = g_strsplit (env, ",", -1);
  return g_strv_contains ((const char **) drivers, driver_name);
}

static void
start_profile_drivers (PpdApp *data)
{
  guint i;
  g_autoptr(GError) initial_error = NULL;

  for (i = 0; i < G_N_ELEMENTS (objects); i++) {
    GObject *object;

    object = g_object_new (objects[i] (), NULL);
    if (PPD_IS_DRIVER (object)) {
      PpdDriver *driver = PPD_DRIVER (object);
      PpdProfile profiles;
      PpdProbeResult result;

      g_debug ("Handling driver '%s'", ppd_driver_get_driver_name (driver));
      if (driver_blocked (driver)) {
        g_debug ("Driver '%s' is blocked, skipping", ppd_driver_get_driver_name (driver));
        continue;
      }

      if (PPD_IS_DRIVER_CPU (data->cpu_driver) && PPD_IS_DRIVER_CPU (driver)) {
        g_debug ("CPU driver '%s' already probed, skipping driver '%s'",
                 ppd_driver_get_driver_name (PPD_DRIVER (data->cpu_driver)),
                 ppd_driver_get_driver_name (driver));
        continue;
      }

      if (PPD_IS_DRIVER_PLATFORM (data->platform_driver) && PPD_IS_DRIVER_PLATFORM (driver)) {
        g_debug ("Platform driver '%s' already probed, skipping driver '%s'",
                 ppd_driver_get_driver_name (PPD_DRIVER (data->platform_driver)),
                 ppd_driver_get_driver_name (driver));
        continue;
      }

      profiles = ppd_driver_get_profiles (driver);
      if (!(profiles & PPD_PROFILE_ALL)) {
        g_warning ("Profile Driver '%s' implements invalid profiles '0x%X'",
                   ppd_driver_get_driver_name (driver),
                   profiles);
        g_object_unref (object);
        continue;
      }

      result = ppd_driver_probe (driver);
      if (result == PPD_PROBE_RESULT_FAIL) {
        g_debug ("probe () failed for driver %s, skipping",
                 ppd_driver_get_driver_name (driver));
        g_object_unref (object);
        continue;
      } else if (result == PPD_PROBE_RESULT_DEFER) {
        g_signal_connect (G_OBJECT (driver), "probe-request",
                          G_CALLBACK (driver_probe_request_cb), data);
        g_ptr_array_add (data->probed_drivers, driver);
        continue;
      }

      if (PPD_IS_DRIVER_CPU (driver))
          data->cpu_driver = PPD_DRIVER_CPU (driver);
      else if (PPD_IS_DRIVER_PLATFORM (driver))
          data->platform_driver = PPD_DRIVER_PLATFORM (driver);
      else
          g_assert_not_reached ();

      g_signal_connect (G_OBJECT (driver), "notify::performance-degraded",
                        G_CALLBACK (driver_performance_degraded_changed_cb), data);
      g_signal_connect (G_OBJECT (driver), "profile-changed",
                        G_CALLBACK (driver_profile_changed_cb), data);
    } else if (PPD_IS_ACTION (object)) {
      PpdAction *action = PPD_ACTION (object);

      g_debug ("Handling action '%s'", ppd_action_get_action_name (action));

      if (action_blocked (action)) {
        g_debug ("Action '%s' is blocked, skipping", ppd_action_get_action_name (action));
        continue;
      }

      if (ppd_action_probe (action) == PPD_PROBE_RESULT_FAIL) {
        g_debug ("probe () failed for action '%s', skipping",
                 ppd_action_get_action_name (action));
        g_object_unref (object);
        continue;
      }

      g_ptr_array_add (data->actions, action);
    } else {
      g_assert_not_reached ();
    }
  }

  if (!has_required_drivers (data)) {
    g_warning ("Some non-optional profile drivers are missing, programmer error");
    goto bail;
  }

  /* Set initial state either from configuration, or using the currently selected profile */
  apply_configuration (data);
  if (!activate_target_profile (data, data->active_profile, PPD_PROFILE_ACTIVATION_REASON_RESET, &initial_error))
    g_warning ("Failed to activate initial profile: %s", initial_error->message);

  send_dbus_event (data, PROP_ALL);

  data->was_started = TRUE;

  return;

bail:
  data->ret = 1;
  g_debug ("Exiting because some non recoverable error occurred during startup");
  g_main_loop_quit (data->main_loop);
}

void
restart_profile_drivers (void)
{
  stop_profile_drivers (ppd_app);
  start_profile_drivers (ppd_app);
}

static void
name_acquired_handler (GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data)
{
  PpdBusOwnData *data = user_data;

  g_debug ("Name '%s' acquired", name);

  start_profile_drivers (data->app);
}

static gboolean
setup_dbus (PpdApp    *data,
            gboolean   replace,
            GError   **error)
{
  g_autoptr(GBytes) iface_data = NULL;
  g_autoptr(GBytes) legacy_iface_data = NULL;
  g_autoptr(GDBusNodeInfo) introspection_data = NULL;
  g_autoptr(GDBusNodeInfo) legacy_introspection_data = NULL;
  PpdBusOwnData *own_data;

  iface_data = g_resources_lookup_data (POWER_PROFILES_RESOURCES_PATH "/"
                                        POWER_PROFILES_DBUS_NAME ".xml",
                                        G_RESOURCE_LOOKUP_FLAGS_NONE,
                                        error);
  if (!iface_data)
    return FALSE;

  legacy_iface_data = g_resources_lookup_data (POWER_PROFILES_RESOURCES_PATH "/"
                                               POWER_PROFILES_LEGACY_DBUS_NAME ".xml",
                                               G_RESOURCE_LOOKUP_FLAGS_NONE,
                                               error);
  if (!legacy_iface_data)
    return FALSE;

  introspection_data =  g_dbus_node_info_new_for_xml (g_bytes_get_data (iface_data, NULL),
                                                      error);
  if (!introspection_data)
    return FALSE;

  legacy_introspection_data =  g_dbus_node_info_new_for_xml (g_bytes_get_data (legacy_iface_data, NULL),
                                                             error);
  if (!legacy_introspection_data)
    return FALSE;

  own_data = g_new0 (PpdBusOwnData, 1);
  own_data->app = data;
  own_data->interface = g_dbus_interface_info_ref (introspection_data->interfaces[0]);
  own_data->legacy_interface = g_dbus_interface_info_ref (legacy_introspection_data->interfaces[0]);

  own_data->flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;
  if (replace)
    own_data->flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

  data->name_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                                  POWER_PROFILES_DBUS_NAME,
                                  own_data->flags,
                                  bus_acquired_handler,
                                  name_acquired_handler,
                                  name_lost_handler,
                                  own_data,
                                  (GDestroyNotify) ppd_bus_own_data_free);

  return TRUE;
}

static void
free_app_data (PpdApp *data)
{
  if (data == NULL)
    return;

  g_clear_handle_id (&data->name_id, g_bus_unown_name);
  g_clear_handle_id (&data->legacy_name_id, g_bus_unown_name);

  g_clear_pointer (&data->config_path, g_free);
  g_clear_pointer (&data->config, g_key_file_unref);
  g_ptr_array_free (data->probed_drivers, TRUE);
  g_ptr_array_free (data->actions, TRUE);
  g_clear_object (&data->cpu_driver);
  g_clear_object (&data->platform_driver);
  g_hash_table_destroy (data->profile_holds);

  g_clear_object (&data->auth);

  g_clear_pointer (&data->main_loop, g_main_loop_unref);
  g_clear_object (&data->connection);
  g_free (data);
  ppd_app = NULL;
}

void
main_loop_quit (void)
{
  g_main_loop_quit (ppd_app->main_loop);
}

static inline gboolean
use_colored_ouput (void)
{
  if (g_getenv ("NO_COLOR"))
    return FALSE;
  return isatty (fileno (stdout));
}

static void
debug_handler_cb (const gchar *log_domain,
                  GLogLevelFlags log_level,
                  const gchar *message,
                  gpointer user_data)
{
  PpdApp *data = user_data;
  g_autoptr(GString) domain = NULL;
  gboolean use_color;
  gint color;

  /* not in verbose mode */
  if (log_level > data->log_level)
   return;

  domain = g_string_new (log_domain);
  use_color = use_colored_ouput ();

  for (gsize i = domain->len; i < 15; i++)
    g_string_append_c (domain, ' ');
  g_print ("%s", domain->str);

  switch (log_level) {
  case G_LOG_LEVEL_ERROR:
  case G_LOG_LEVEL_CRITICAL:
  case G_LOG_LEVEL_WARNING:
    color = 31; /* red */
    break;
  default:
    color = 34; /* blue */
    break;
  }

  if (use_color)
    g_print ("%c[%dm%s\n%c[%dm", 0x1B, color, message, 0x1B, 0);
  else
    g_print ("%s\n", message);
}

int main (int argc, char **argv)
{
  PpdApp *data;
  int ret = 0;
  g_autoptr(GOptionContext) option_context = NULL;
  g_autoptr(GError) error = NULL;
  gboolean verbose = FALSE;
  gboolean replace = FALSE;
  const GOptionEntry options[] = {
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Show extra debugging information", NULL },
    { "replace", 'r', 0, G_OPTION_ARG_NONE, &replace, "Replace the running instance of power-profiles-daemon", NULL },
    { NULL}
  };

  setlocale (LC_ALL, "");
  option_context = g_option_context_new ("");
  g_option_context_add_main_entries (option_context, options, NULL);

  ret = g_option_context_parse (option_context, &argc, &argv, &error);
  if (!ret) {
    g_print ("Failed to parse arguments: %s\n", error->message);
    return EXIT_FAILURE;
  }

  data = g_new0 (PpdApp, 1);
  data->main_loop = g_main_loop_new (NULL, TRUE);
  data->auth = polkit_authority_get_sync (NULL, NULL);
  data->probed_drivers = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
  data->actions = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
  data->profile_holds = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) profile_hold_free);
  data->active_profile = PPD_PROFILE_BALANCED;
  data->selected_profile = PPD_PROFILE_BALANCED;
  data->log_level = verbose ? G_LOG_LEVEL_DEBUG : G_LOG_LEVEL_MESSAGE;

  /* redirect all domains */
  if (verbose && !g_log_writer_is_journald (fileno (stdout)))
    g_log_set_default_handler (debug_handler_cb, data);

  g_debug ("Starting power-profiles-daemon version "VERSION);

  load_configuration (data);
  ppd_app = data;

  /* Set up D-Bus */
  if (!setup_dbus (data, replace, &error)) {
    g_error ("Failed to start dbus: %s", error->message);
    return 1;
  }

  g_main_loop_run (data->main_loop);
  ret = data->ret;
  free_app_data (data);

  return ret;
}
