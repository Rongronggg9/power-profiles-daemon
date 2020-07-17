/*
 * Copyright (c) 2014-2016, 2020 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include "power-profiles-daemon-resources.h"
#include "power-profiles-daemon.h"
#include "ppd-profile-driver.h"
#include "ppd-action.h"
#include "ppd-enums.h"

#define POWER_PROFILES_DBUS_NAME          "net.hadess.PowerProfiles"
#define POWER_PROFILES_DBUS_PATH          "/net/hadess/PowerProfiles"
#define POWER_PROFILES_IFACE_NAME         POWER_PROFILES_DBUS_NAME

typedef struct {
  PpdProfileDriver *driver;
  GList *actions; /* list of PpdActions for the profile */
} ProfileData;

typedef struct {
  GMainLoop *loop;
  GDBusNodeInfo *introspection_data;
  GDBusConnection *connection;
  guint name_id;
  int ret;

  PpdProfile active_profile;
  PpdProfile selected_profile;
  ProfileData profile_data[NUM_PROFILES];
  GPtrArray *actions[NUM_PROFILES];
} PpdApp;

#define GET_DRIVER(p) (data->profile_data[p].driver)
#define ACTIVE_DRIVER (data->profile_data[data->active_profile].driver)
#define SELECTED_DRIVER (data->profile_data[data->selected_profile].driver)

/* profile drivers and actions */
// #include "ppd-action-mfi-fastcharge.h"
#include "ppd-profile-driver-balanced.h"
#include "ppd-profile-driver-power-saver.h"
#include "ppd-profile-driver-lenovo-dytc.h"

typedef GType (*GTypeGetFunc) (void);

static GTypeGetFunc objects[] = {
  /* Hardware specific profile drivers */
  ppd_profile_driver_lenovo_dytc_get_type,

  /* Generic profile drivers */
  ppd_profile_driver_balanced_get_type,
  ppd_profile_driver_power_saver_get_type,

  /* Actions */
};

typedef enum {
  PROP_ACTIVE_PROFILE             = 1 << 0,
  PROP_SELECTED_PROFILE           = 1 << 1,
  PROP_INHIBITED                  = 1 << 2,
  PROP_PROFILES                   = 1 << 3,
} PropertiesMask;

#define PROP_ALL (PROP_ACTIVE_PROFILE | PROP_SELECTED_PROFILE | PROP_INHIBITED | PROP_PROFILES)

static const char *
profile_to_str (PpdProfile profile)
{
  GEnumClass *klass = g_type_class_ref (PPD_TYPE_PROFILE);
  GEnumValue *value = g_enum_get_value (klass, profile);
  const gchar *name = value ? value->value_nick : "";
  g_type_class_unref (klass);
  return name;
}

static PpdProfile
profile_from_str (const char *str)
{
  GEnumClass *klass = g_type_class_ref (PPD_TYPE_PROFILE);
  GEnumValue *value = g_enum_get_value_by_nick (klass, str);
  PpdProfile profile = value ? value->value : PPD_PROFILE_UNSET;
  g_type_class_unref (klass);
  return profile;
}

static const char *
get_active_profile (PpdApp *data)
{
  return profile_to_str (data->active_profile);
}

static const char *
get_selected_profile (PpdApp *data)
{
  return profile_to_str (data->selected_profile);
}

static const char *
get_inhibited (PpdApp *data)
{
  PpdProfileDriver *driver;
  const char *ret;

  driver = data->profile_data[data->selected_profile].driver;
  ret = ppd_profile_driver_get_inhibited (driver);
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
    PpdProfileDriver *driver = data->profile_data[i].driver;
    GVariantBuilder asv_builder;

    if (driver == NULL)
      continue;

    g_variant_builder_init (&asv_builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&asv_builder, "{sv}", "Driver",
                           g_variant_new_string (ppd_profile_driver_get_driver_name (driver)));
    g_variant_builder_add (&asv_builder, "{sv}", "Profile",
                           g_variant_new_string (profile_to_str (ppd_profile_driver_get_profile (driver))));

    g_variant_builder_add (&builder, "a{sv}", &asv_builder);
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
  if (mask & PROP_SELECTED_PROFILE) {
    g_variant_builder_add (&props_builder, "{sv}", "SelectedProfile",
                           g_variant_new_string (get_selected_profile (data)));
  }
  if (mask & PROP_INHIBITED) {
    g_variant_builder_add (&props_builder, "{sv}", "Inhibited",
                           g_variant_new_string (get_inhibited (data)));
  }
  if (mask & PROP_PROFILES) {
    g_variant_builder_add (&props_builder, "{sv}", "Profiles",
                           get_profiles_variant (data));
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
set_all_actions_active (GPtrArray *actions,
                        gboolean   active)
{
  guint i;

  g_return_if_fail (actions != NULL);

  for (i = 0; i < actions->len; i++) {
    g_autoptr(GError) error = NULL;
    PpdAction *action;
    gboolean ret;

    action = g_ptr_array_index (actions, i);

    if (active)
      ret = ppd_action_activate (action, &error);
    else
      ret = ppd_action_deactivate (action, &error);

    if (!ret)
      g_warning ("Failed to %s action '%s': %s",
                 active ? "activate" : "deactivate",
                 ppd_action_get_action_name (action),
                 error->message);
  }
}

static void
set_active_profile (PpdApp     *data,
                    PpdProfile  target_profile)
{
  PpdProfileDriver *driver;
  g_autoptr(GError) error = NULL;

  g_debug ("Setting active profile '%s' (current: '%s')",
           profile_to_str (target_profile),
           profile_to_str (data->active_profile));

  driver = ACTIVE_DRIVER;
  if (!ppd_profile_driver_deactivate (driver, &error)) {
    g_warning ("Failed to deactivate driver '%s': %s",
               ppd_profile_driver_get_driver_name (driver),
               error->message);
    g_clear_error (&error);
  }
  set_all_actions_active (data->actions[data->active_profile], FALSE);

  driver = GET_DRIVER(target_profile);
  if (!ppd_profile_driver_activate (driver, &error)) {
    g_warning ("Failed to activate driver '%s': %s",
               ppd_profile_driver_get_driver_name (driver),
               error->message);
    g_clear_error (&error);
  }
  set_all_actions_active (data->actions[target_profile], TRUE);

  data->active_profile = target_profile;
}

static gboolean
set_selected_profile (PpdApp      *data,
                      const char  *profile,
                      GError     **error)
{
  PpdProfile target_profile;

  target_profile = profile_from_str (profile);
  if (target_profile == PPD_PROFILE_UNSET) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "Invalid profile name '%s'", profile);
    return FALSE;
  }

  if (target_profile == data->selected_profile) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "Profile '%s' already selected", profile);
    return FALSE;
  }

  g_debug ("Transitioning from '%s' to '%s'",
           profile_to_str (data->selected_profile), profile);
  data->selected_profile = target_profile;

  if (ppd_profile_driver_is_inhibited (SELECTED_DRIVER)) {
    send_dbus_event (data, PROP_SELECTED_PROFILE);
    g_debug ("Not transitioning to '%s' as inhibited", profile);
    return TRUE;
  }

  set_active_profile (data, target_profile);

  send_dbus_event (data, PROP_ACTIVE_PROFILE | PROP_SELECTED_PROFILE);

  return TRUE;
}

static void
driver_inhibited_changed_cb (GObject    *gobject,
                             GParamSpec *pspec,
                             gpointer    user_data)
{
  PpdApp *data = user_data;
  PpdProfileDriver *driver = PPD_PROFILE_DRIVER (gobject);
  const char *prop_str = pspec->name;

  if (g_strcmp0 (prop_str, "inhibited") != 0) {
    g_debug ("Ignoring '%s' property change on profile driver '%s'",
             prop_str, ppd_profile_driver_get_driver_name (driver));
    return;
  }

  if (ppd_profile_driver_get_profile (driver) != PPD_PROFILE_PERFORMANCE) {
    g_warning ("Ignored 'inhibited' change on non-performance driver '%s'",
               ppd_profile_driver_get_driver_name (driver));
    return;
  }

  if (data->selected_profile != PPD_PROFILE_PERFORMANCE) {
    g_debug ("User didn't want the performance profile, so ignoring 'inhibited' change on '%s'",
             ppd_profile_driver_get_driver_name (driver));
    return;
  }

  set_active_profile (data, ppd_profile_driver_is_inhibited (driver) ?
                      PPD_PROFILE_BALANCED : PPD_PROFILE_PERFORMANCE);
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
  if (g_strcmp0 (property_name, "SelectedProfile") == 0)
    return g_variant_new_string (get_selected_profile (data));
  if (g_strcmp0 (property_name, "Inhibited") == 0)
    return g_variant_new_string (get_inhibited (data));
  if (g_strcmp0 (property_name, "Profiles") == 0)
    return get_profiles_variant (data);
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

  if (g_strcmp0 (property_name, "SelectedProfile") != 0) {
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                 "No such property: %s", property_name);
    return FALSE;
  }

  g_variant_get (value, "&s", &profile);
  return set_selected_profile (data, profile, error);
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
  g_debug ("power-profiles-daemon is already running, or it cannot own its D-Bus name. Verify installation.");
  exit (0);
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
  if (!data->profile_data[PPD_PROFILE_BALANCED].driver ||
      !G_IS_OBJECT (data->profile_data[PPD_PROFILE_BALANCED].driver) ||
      !data->profile_data[PPD_PROFILE_POWER_SAVER].driver ||
      !G_IS_OBJECT (data->profile_data[PPD_PROFILE_POWER_SAVER].driver)) {
    return FALSE;
  }
  return TRUE;
}

static void
name_acquired_handler (GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data)
{
  PpdApp *data = user_data;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (objects); i++) {
    GObject *object;

    object = g_object_new (objects[i](), NULL);
    if (PPD_IS_PROFILE_DRIVER (object)) {
      PpdProfileDriver *driver = PPD_PROFILE_DRIVER (object);
      PpdProfile profile;

      g_debug ("Handling driver '%s'", ppd_profile_driver_get_driver_name (driver));

      profile = ppd_profile_driver_get_profile (driver);
      if (profile == PPD_PROFILE_UNSET) {
        g_warning ("Profile Driver '%s' implements invalid profile '%d'",
                   ppd_profile_driver_get_driver_name (driver),
                   profile);
        g_object_unref (object);
        continue;
      }

      if (!ppd_profile_driver_probe (driver)) {
        g_debug ("probe() failed for driver %s, skipping",
                 ppd_profile_driver_get_driver_name (driver));
        g_object_unref (object);
        continue;
      }

      data->profile_data[profile].driver = driver;

      g_signal_connect (G_OBJECT (driver), "notify::inhibited",
                        G_CALLBACK (driver_inhibited_changed_cb), data);
    } else if (PPD_IS_ACTION (object)) {
      PpdAction *action = PPD_ACTION (object);
      PpdProfile profile;

      g_debug ("Handling action '%s'", ppd_action_get_action_name (action));

      profile = ppd_action_get_profile (action);
      if (profile == PPD_PROFILE_UNSET) {
        g_warning ("Action '%s' implements invalid profile '%d'",
                   ppd_action_get_action_name (action),
                   profile);
        g_object_unref (object);
        continue;
      }

      if (!ppd_action_probe (action)) {
        g_debug ("probe() failed for action '%s', skipping",
                 ppd_action_get_action_name (action));
        g_object_unref (object);
        continue;
      }

      g_ptr_array_add (data->actions[profile], action);
    } else {
      g_assert_not_reached ();
    }
  }

  if (!has_required_drivers (data)) {
    g_warning ("Some non-optional profile drivers are missing, programmer error");
    goto bail;
  }

  send_dbus_event (data, PROP_ALL);

  return;

bail:
  data->ret = 0;
  g_debug ("Exiting because some non recoverable error occurred during startup");
  g_main_loop_quit (data->loop);
}

static gboolean
setup_dbus (PpdApp *data)
{
  GBytes *bytes;

  bytes = g_resources_lookup_data ("/net/hadess/PowerProfiles/net.hadess.PowerProfiles.xml",
                                   G_RESOURCE_LOOKUP_FLAGS_NONE,
                                   NULL);
  data->introspection_data = g_dbus_node_info_new_for_xml (g_bytes_get_data (bytes, NULL), NULL);
  g_bytes_unref (bytes);
  g_assert (data->introspection_data != NULL);

  data->name_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                                  POWER_PROFILES_DBUS_NAME,
                                  G_BUS_NAME_OWNER_FLAGS_NONE,
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
  guint i;

  if (data == NULL)
    return;

  if (data->name_id != 0) {
    g_bus_unown_name (data->name_id);
    data->name_id = 0;
  }

  for (i = 0; i < NUM_PROFILES; i++)
    g_ptr_array_free (data->actions[i], TRUE);

  g_clear_pointer (&data->introspection_data, g_dbus_node_info_unref);
  g_clear_object (&data->connection);
  g_clear_pointer (&data->loop, g_main_loop_unref);
  g_free (data);
}

int main (int argc, char **argv)
{
  PpdApp *data;
  int ret = 0;
  guint i;

  data = g_new0 (PpdApp, 1);
  for (i = 0; i < NUM_PROFILES; i++)
    data->actions[i] = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

  /* Set up D-Bus */
  setup_dbus (data);

  data->loop = g_main_loop_new (NULL, TRUE);
  g_main_loop_run (data->loop);
  ret = data->ret;
  free_app_data (data);

  return ret;
}
