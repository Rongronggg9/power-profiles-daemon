/*
 * Copyright (c) 2014-2016, 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include <locale.h>

#include "power-profiles-daemon-resources.h"
#include "power-profiles-daemon.h"
#include "ppd-driver.h"
#include "ppd-action.h"
#include "ppd-enums.h"

#define POWER_PROFILES_DBUS_NAME          "net.hadess.PowerProfiles"
#define POWER_PROFILES_DBUS_PATH          "/net/hadess/PowerProfiles"
#define POWER_PROFILES_IFACE_NAME         POWER_PROFILES_DBUS_NAME

typedef struct {
  GMainLoop *main_loop;
  GDBusNodeInfo *introspection_data;
  GDBusConnection *connection;
  guint name_id;
  gboolean was_started;
  int ret;

  PpdProfile active_profile;
  GPtrArray *drivers;
  GPtrArray *actions;
} PpdApp;

static PpdApp *ppd_app = NULL;

static PpdDriver *
get_driver_for_profile (PpdApp     *data,
                        PpdProfile  profile)
{
  guint i;

  g_return_val_if_fail (ppd_profile_has_single_flag (profile), NULL);

  for (i = 0; i < data->drivers->len; i++) {
    PpdDriver *driver = g_ptr_array_index (data->drivers, i);

    if (ppd_driver_get_profiles (driver) & profile)
      return driver;
  }

  return NULL;
}

#define GET_DRIVER(p) (get_driver_for_profile (data, p))
#define ACTIVE_DRIVER (get_driver_for_profile (data, data->active_profile))

/* profile drivers and actions */
#include "ppd-action-trickle-charge.h"
#include "ppd-driver-balanced.h"
#include "ppd-driver-power-saver.h"
#include "ppd-driver-platform-profile.h"
#include "ppd-driver-intel-pstate.h"
#include "ppd-driver-fake.h"

typedef GType (*GTypeGetFunc) (void);

static GTypeGetFunc objects[] = {
  /* Hardware specific profile drivers */
  ppd_driver_fake_get_type,
  ppd_driver_platform_profile_get_type,
  ppd_driver_intel_pstate_get_type,

  /* Generic profile drivers */
  ppd_driver_balanced_get_type,
  ppd_driver_power_saver_get_type,

  /* Actions */
  ppd_action_trickle_charge_get_type,
};

typedef enum {
  PROP_ACTIVE_PROFILE             = 1 << 0,
  PROP_INHIBITED                  = 1 << 1,
  PROP_PROFILES                   = 1 << 2,
  PROP_ACTIONS                    = 1 << 3,
} PropertiesMask;

#define PROP_ALL (PROP_ACTIVE_PROFILE | PROP_INHIBITED | PROP_PROFILES | PROP_ACTIONS)

static const char *
get_active_profile (PpdApp *data)
{
  return ppd_profile_to_str (data->active_profile);
}

static const char *
get_performance_inhibited (PpdApp *data)
{
  PpdDriver *driver;
  const char *ret;

  driver = GET_DRIVER(PPD_PROFILE_PERFORMANCE);
  if (!driver)
    return "";
  ret = ppd_driver_get_performance_inhibited (driver);
  g_assert (ret != NULL);
  return ret;
}

static GVariant *
get_profiles_variant (PpdApp *data)
{
  GVariantBuilder builder;
  guint i;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (i = 0; i < NUM_PROFILES; i++) {
    PpdDriver *driver = GET_DRIVER(1 << i);
    GVariantBuilder asv_builder;

    if (driver == NULL)
      continue;

    g_variant_builder_init (&asv_builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&asv_builder, "{sv}", "Profile",
                           g_variant_new_string (ppd_profile_to_str (1 << i)));
    g_variant_builder_add (&asv_builder, "{sv}", "Driver",
                           g_variant_new_string (ppd_driver_get_driver_name (driver)));

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

static void
send_dbus_event (PpdApp     *data,
                 PropertiesMask  mask)
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
                           g_variant_new_string (get_performance_inhibited (data)));
  }
  if (mask & PROP_PROFILES) {
    g_variant_builder_add (&props_builder, "{sv}", "Profiles",
                           get_profiles_variant (data));
  }
  if (mask & PROP_ACTIONS) {
    g_variant_builder_add (&props_builder, "{sv}", "Actions",
                           get_actions_variant (data));
  }

  props_changed = g_variant_new ("(s@a{sv}@as)", POWER_PROFILES_IFACE_NAME,
                                 g_variant_builder_end (&props_builder),
                                 g_variant_new_strv (NULL, 0));

  g_dbus_connection_emit_signal (data->connection,
                                 NULL,
                                 POWER_PROFILES_DBUS_PATH,
                                 "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged",
                                 props_changed, NULL);
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

static void
activate_target_profile (PpdApp     *data,
                         PpdProfile  target_profile)
{
  guint i;

  g_debug ("Setting active profile '%s' (current: '%s')",
           ppd_profile_to_str (target_profile),
           ppd_profile_to_str (data->active_profile));

  for (i = 0; i < data->drivers->len; i++) {
    PpdDriver *driver = g_ptr_array_index (data->drivers, i);
    g_autoptr(GError) error = NULL;

    if (!ppd_driver_activate_profile (driver, target_profile, &error)) {
      g_warning ("Failed to activate driver '%s': %s",
                 ppd_driver_get_driver_name (driver),
                 error->message);
    }
  }

  actions_activate_profile (data->actions, target_profile);

  data->active_profile = target_profile;
}

static gboolean
set_active_profile (PpdApp      *data,
                    const char  *profile,
                    GError     **error)
{
  PpdProfile target_profile;

  target_profile = ppd_profile_from_str (profile);
  if (target_profile == PPD_PROFILE_UNSET) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "Invalid profile name '%s'", profile);
    return FALSE;
  }

  if (target_profile == data->active_profile) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "Profile '%s' already active", profile);
    return FALSE;
  }

  if (target_profile == PPD_PROFILE_PERFORMANCE &&
      ppd_driver_is_performance_inhibited (GET_DRIVER (PPD_PROFILE_PERFORMANCE))) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "Profile '%s' is inhibited", profile);
    return FALSE;
  }

  g_debug ("Transitioning active profile from '%s' to '%s'",
           ppd_profile_to_str (data->active_profile), profile);
  data->active_profile = target_profile;

  activate_target_profile (data, target_profile);
  send_dbus_event (data, PROP_ACTIVE_PROFILE);

  return TRUE;
}

static void
driver_performance_inhibited_changed_cb (GObject    *gobject,
                                         GParamSpec *pspec,
                                         gpointer    user_data)
{
  PpdApp *data = user_data;
  PpdDriver *driver = PPD_DRIVER (gobject);
  const char *prop_str = pspec->name;

  if (g_strcmp0 (prop_str, "performance-inhibited") != 0) {
    g_warning ("Ignoring '%s' property change on profile driver '%s'",
               prop_str, ppd_driver_get_driver_name (driver));
    return;
  }

  if (!(ppd_driver_get_profiles (driver) & PPD_PROFILE_PERFORMANCE)) {
    g_warning ("Ignored 'performance-inhibited' change on non-performance driver '%s'",
               ppd_driver_get_driver_name (driver));
    return;
  }

  send_dbus_event (data, PROP_INHIBITED);
  if (!ppd_driver_is_performance_inhibited (driver))
    return;

  activate_target_profile (data, PPD_PROFILE_BALANCED);
  send_dbus_event (data, PROP_ACTIVE_PROFILE);
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

  activate_target_profile (data, new_profile);
  send_dbus_event (data, PROP_ACTIVE_PROFILE);
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
    return g_variant_new_string (get_performance_inhibited (data));
  if (g_strcmp0 (property_name, "Profiles") == 0)
    return get_profiles_variant (data);
  if (g_strcmp0 (property_name, "Actions") == 0)
    return get_actions_variant (data);
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

  g_variant_get (value, "&s", &profile);
  return set_active_profile (data, profile, error);
}

static const GDBusInterfaceVTable interface_vtable =
{
  NULL,
  handle_get_property,
  handle_set_property
};

static void
name_lost_handler (GDBusConnection *connection,
                   const gchar     *name,
                   gpointer         user_data)
{
  PpdApp *data = user_data;
  g_debug ("power-profiles-daemon is already running, or it cannot own its D-Bus name. Verify installation.");
  if (!data->was_started)
    data->ret = 1;
  g_main_loop_quit (data->main_loop);
}

static void
bus_acquired_handler (GDBusConnection *connection,
                      const gchar     *name,
                      gpointer         user_data)
{
  PpdApp *data = user_data;

  g_dbus_connection_register_object (connection,
                                     POWER_PROFILES_DBUS_PATH,
                                     data->introspection_data->interfaces[0],
                                     &interface_vtable,
                                     data,
                                     NULL,
                                     NULL);

  data->connection = g_object_ref (connection);
}

static gboolean
has_required_drivers (PpdApp *data)
{
  PpdDriver *driver;

  driver = GET_DRIVER (PPD_PROFILE_BALANCED);
  if (!driver || !G_IS_OBJECT (driver))
    return FALSE;
  driver = GET_DRIVER (PPD_PROFILE_POWER_SAVER);
  if (!driver || !G_IS_OBJECT (driver))
    return FALSE;

  return TRUE;
}

static gboolean
profile_already_handled (PpdApp     *data,
                         PpdDriver  *driver,
                         PpdProfile  profiles)
{
  guint i;

  for (i = 0; i < NUM_PROFILES; i++) {
    PpdDriver *existing_driver;

    if (!(profiles & (1 << i)))
      continue;

    existing_driver = GET_DRIVER(1 << i);
    if (existing_driver) {
      g_debug ("Driver '%s' conflicts with already probed driver '%s' for profile %s",
               ppd_driver_get_driver_name (driver),
               ppd_driver_get_driver_name (existing_driver),
               ppd_profile_to_str (1 << i));
      return TRUE;
    }
  }

  return FALSE;
}

static void
stop_profile_drivers (PpdApp *data)
{
  g_ptr_array_set_size (data->actions, 0);
  g_ptr_array_set_size (data->drivers, 0);
}

static void
start_profile_drivers (PpdApp *data)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (objects); i++) {
    GObject *object;

    object = g_object_new (objects[i](), NULL);
    if (PPD_IS_DRIVER (object)) {
      PpdDriver *driver = PPD_DRIVER (object);
      PpdProfile profiles;

      g_debug ("Handling driver '%s'", ppd_driver_get_driver_name (driver));

      profiles = ppd_driver_get_profiles (driver);
      if (!(profiles & PPD_PROFILE_ALL)) {
        g_warning ("Profile Driver '%s' implements invalid profiles '0x%X'",
                   ppd_driver_get_driver_name (driver),
                   profiles);
        g_object_unref (object);
        continue;
      }

      if (profile_already_handled (data, driver, profiles)) {
        g_object_unref (object);
        continue;
      }

      if (ppd_driver_probe (driver) != PROBE_RESULT_SUCCESS) {
        g_debug ("probe() failed for driver %s, skipping",
                 ppd_driver_get_driver_name (driver));
        g_object_unref (object);
        continue;
      }

      g_ptr_array_add (data->drivers, driver);

      g_signal_connect (G_OBJECT (driver), "notify::performance-inhibited",
                        G_CALLBACK (driver_performance_inhibited_changed_cb), data);
      g_signal_connect (G_OBJECT (driver), "profile-changed",
                        G_CALLBACK (driver_profile_changed_cb), data);
    } else if (PPD_IS_ACTION (object)) {
      PpdAction *action = PPD_ACTION (object);

      g_debug ("Handling action '%s'", ppd_action_get_action_name (action));

      if (!ppd_action_probe (action)) {
        g_debug ("probe() failed for action '%s', skipping",
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

  /* Set initial state */
  activate_target_profile (data, data->active_profile);

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
  PpdApp *data = user_data;

  start_profile_drivers (data);
}

static gboolean
setup_dbus (PpdApp   *data,
            gboolean  replace)
{
  GBytes *bytes;
  GBusNameOwnerFlags flags;

  bytes = g_resources_lookup_data ("/net/hadess/PowerProfiles/net.hadess.PowerProfiles.xml",
                                   G_RESOURCE_LOOKUP_FLAGS_NONE,
                                   NULL);
  data->introspection_data = g_dbus_node_info_new_for_xml (g_bytes_get_data (bytes, NULL), NULL);
  g_bytes_unref (bytes);
  g_assert (data->introspection_data != NULL);

  flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;
  if (replace)
    flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

  data->name_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                                  POWER_PROFILES_DBUS_NAME,
                                  flags,
                                  bus_acquired_handler,
                                  name_acquired_handler,
                                  name_lost_handler,
                                  data,
                                  NULL);

  return TRUE;
}

static void
free_app_data (PpdApp *data)
{
  if (data == NULL)
    return;

  if (data->name_id != 0) {
    g_bus_unown_name (data->name_id);
    data->name_id = 0;
  }

  g_ptr_array_free (data->actions, TRUE);
  g_ptr_array_free (data->drivers, TRUE);

  g_clear_pointer (&data->main_loop, g_main_loop_unref);
  g_clear_pointer (&data->introspection_data, g_dbus_node_info_unref);
  g_clear_object (&data->connection);
  g_free (data);
  ppd_app = NULL;
}

void
main_loop_quit (void)
{
  g_main_loop_quit (ppd_app->main_loop);
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

  if (verbose)
    g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

  data = g_new0 (PpdApp, 1);
  data->main_loop = g_main_loop_new (NULL, TRUE);
  data->actions = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
  data->drivers = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
  data->active_profile = PPD_PROFILE_BALANCED;
  ppd_app = data;

  /* Set up D-Bus */
  setup_dbus (data, replace);

  g_main_loop_run (data->main_loop);
  ret = data->ret;
  free_app_data (data);

  return ret;
}
