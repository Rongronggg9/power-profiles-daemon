/*
 * Copyright (c) 2020 Bastien Nocera <hadess@hadess.net>
 * Copyright (c) 2023 Rong Zhang <i@rong.moe>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include "ppd-utils.h"
#include "ppd-driver-tlp.h"

#define TLP_PATH "/usr/sbin/tlp"
#define TLP_RUN_DIR "/run/tlp/"
#define TLP_PWR_MODE_PATH TLP_RUN_DIR "last_pwr"
#define TLP_MANUAL_MODE_PATH TLP_RUN_DIR "manual_mode"

struct _PpdDriverTlp
{
    PpdDriver  parent_instance;

    PpdProfile activated_profile;
    gboolean initialized;
};

G_DEFINE_TYPE (PpdDriverTlp, ppd_driver_tlp, PPD_TYPE_DRIVER)

static gboolean ppd_driver_tlp_activate_profile (PpdDriver                   *driver,
                                                 PpdProfile                   profile,
                                                 PpdProfileActivationReason   reason,
                                                 GError                     **error);

static GObject*
ppd_driver_tlp_constructor (GType                  type,
                            guint                  n_construct_params,
                            GObjectConstructParam *construct_params)
{
    GObject *object;

    object = G_OBJECT_CLASS (ppd_driver_tlp_parent_class)->constructor (type,
                                                                        n_construct_params,
                                                                        construct_params);
    g_object_set (object,
                  "driver-name", "tlp",
                  "profiles", PPD_PROFILE_PERFORMANCE | PPD_PROFILE_BALANCED | PPD_PROFILE_POWER_SAVER,
                  NULL);

    return object;
}

static PpdProfile
read_tlp_profile (void)
{
    g_autofree char *pwr_mode_path = NULL;
    g_autofree char *manual_mode_path = NULL;
    g_autofree char *pwr_mode_str = NULL;
    g_autofree char *manual_mode_str = NULL;
    g_autoptr(GError) error = NULL;
    PpdProfile new_profile = PPD_PROFILE_UNSET;

    pwr_mode_path = ppd_utils_get_sysfs_path (TLP_PWR_MODE_PATH);
    manual_mode_path = ppd_utils_get_sysfs_path (TLP_MANUAL_MODE_PATH);
    if (!g_file_get_contents (pwr_mode_path,
                              &pwr_mode_str, NULL, &error)) {
        g_debug ("Failed to get contents for '%s': %s",
                 pwr_mode_path,
                 error->message);
        return PPD_PROFILE_UNSET;
    }
    if (!g_file_get_contents (manual_mode_path,
                              &manual_mode_str, NULL, &error)) {
        manual_mode_str = g_strdup("0");
    }

    switch (manual_mode_str[0]) {
    case '0': /* auto */
        new_profile = PPD_PROFILE_BALANCED;
        break;
    case '1': /* manual */
        switch (pwr_mode_str[0]) {
        case '0': /* AC */
            new_profile = PPD_PROFILE_PERFORMANCE;
            break;
        case '1': /* BAT */
            new_profile = PPD_PROFILE_POWER_SAVER;
            break;
        }
    }

    g_debug ("TLP (manual_mode,pwr_mode) is now (%c,%c), so profile is detected as %s",
             manual_mode_str[0],
             pwr_mode_str[0],
             ppd_profile_to_str (new_profile));
    return new_profile;
}

static const char *
profile_to_tlp_subcommand (PpdProfile profile)
{
    switch (profile) {
        case PPD_PROFILE_POWER_SAVER:
            return "bat";
        case PPD_PROFILE_BALANCED:
            return "start";
        case PPD_PROFILE_PERFORMANCE:
            return "ac";
    }

    g_assert_not_reached ();
}

static gboolean
call_tlp (const char  *subcommand,
          GError     **error)
{
    gboolean ret = TRUE;
    g_autofree char *cmd = NULL;
    g_autoptr(GError) internal_error = NULL;

    cmd = g_strdup_printf ("%s %s", TLP_PATH, subcommand);
    g_debug ("Executing '%s'", cmd);
    if (!g_spawn_command_line_sync (cmd,
                                    NULL,
                                    NULL,
                                    NULL,
                                    &internal_error)) {
        g_warning ("Failed to execute '%s': %s",
                   cmd,
                   internal_error->message);
        ret = FALSE;
        g_propagate_error (error, internal_error);
    }

    return ret;
}

static PpdProbeResult
probe_tlp (PpdDriverTlp *tlp)
{
    if (!g_file_test (TLP_PATH, G_FILE_TEST_EXISTS)) {
        g_debug ("TLP is not installed");
        return PPD_PROBE_RESULT_FAIL;
    }

    return PPD_PROBE_RESULT_SUCCESS;
}

static PpdProbeResult
ppd_driver_tlp_probe (PpdDriver  *driver)
{
    PpdDriverTlp *tlp = PPD_DRIVER_TLP (driver);
    PpdProbeResult ret = PPD_PROBE_RESULT_FAIL;
    PpdProfile new_profile;

    ret = probe_tlp (tlp);

    if (ret != PPD_PROBE_RESULT_SUCCESS)
        goto out;

    new_profile = read_tlp_profile ();
    tlp->activated_profile = new_profile;
    tlp->initialized = new_profile != PPD_PROFILE_UNSET;

    if (!tlp->initialized) {
        /*
        call_tlp ("init start", NULL);
        new_profile = read_tlp_profile ();
        tlp->activated_profile = new_profile;
        tlp->initialized = TRUE;
         */
        g_warning ("TLP not initialized. Initialize it to use the TLP-based driver");
        ret = PPD_PROBE_RESULT_FAIL;
    }

    out:
    g_debug ("%s TLP",
             ret == PPD_PROBE_RESULT_SUCCESS ? "Found" : "Didn't find");
    return ret;
}

static gboolean
ppd_driver_tlp_activate_profile (PpdDriver                    *driver,
                                 PpdProfile                   profile,
                                 PpdProfileActivationReason   reason,
                                 GError                     **error)
{
    PpdDriverTlp *tlp = PPD_DRIVER_TLP (driver);
    gboolean ret = FALSE;
    const char *subcommand;

    g_return_val_if_fail (tlp->initialized, FALSE);

    if (tlp->initialized) {
        subcommand = profile_to_tlp_subcommand (profile);
        ret = call_tlp (subcommand, error);
        if (!ret)
            return ret;
    }

    if (ret)
        tlp->activated_profile = profile;

    return ret;
}

static void
ppd_driver_tlp_finalize (GObject *object)
{
    G_OBJECT_CLASS (ppd_driver_tlp_parent_class)->finalize (object);
}

static void
ppd_driver_tlp_class_init (PpdDriverTlpClass *klass)
{
    GObjectClass *object_class;
    PpdDriverClass *driver_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->constructor = ppd_driver_tlp_constructor;
    object_class->finalize = ppd_driver_tlp_finalize;

    driver_class = PPD_DRIVER_CLASS(klass);
    driver_class->probe = ppd_driver_tlp_probe;
    driver_class->activate_profile = ppd_driver_tlp_activate_profile;
}

static void
ppd_driver_tlp_init (PpdDriverTlp *self)
{
}
