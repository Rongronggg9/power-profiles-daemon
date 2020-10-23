/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "up-input.h"

struct _UpInput
{
	GObject			 parent_instance;

	guint			 watched_switch;
	int			 last_switch_state;
	int			 eventfp;
	struct input_event	 event;
	gsize			 offset;
	GIOChannel		*channel;
};

G_DEFINE_TYPE (UpInput, up_input, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_WATCHED_SWITCH
};

enum {
	SWITCH_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* we must use this kernel-compatible implementation */
#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)


/**
 * up_input_get_device_sysfs_path:
 **/
static char *
up_input_get_device_sysfs_path (GUdevDevice *device)
{
  const char *root;

  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), FALSE);

  root = g_getenv ("UMOCKDEV_DIR");
  if (!root || *root == '\0')
    return g_strdup (g_udev_device_get_sysfs_path (device));

  return g_build_filename (root,
                           g_udev_device_get_sysfs_path (device),
                           NULL);
}

/**
 * up_input_str_to_bitmask:
 **/
static gint
up_input_str_to_bitmask (const gchar *s, glong *bitmask, size_t max_size)
{
	gint i, j;
	gchar **v;
	gint num_bits_set = 0;

	memset (bitmask, 0, max_size);
	v = g_strsplit (s, " ", max_size);
	for (i = g_strv_length (v) - 1, j = 0; i >= 0; i--, j++) {
		gulong val;

		val = strtoul (v[i], NULL, 16);
		bitmask[j] = val;

		while (val != 0) {
			num_bits_set++;
			val &= (val - 1);
		}
	}
	g_strfreev(v);

	return num_bits_set;
}

/**
 * up_input_event_io:
 **/
static gboolean
up_input_event_io (GIOChannel *channel, GIOCondition condition, gpointer data)
{
	UpInput *input = (UpInput*) data;
	GError *error = NULL;
	gsize read_bytes;
	glong bitmask[NBITS(SW_MAX)];

	/* uninteresting */
	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL))
		return FALSE;

	/* read event */
	while (g_io_channel_read_chars (channel,
		((gchar*)&input->event) + input->offset,
		sizeof(struct input_event) - input->offset,
		&read_bytes, &error) == G_IO_STATUS_NORMAL) {

		/* not enough data */
		if (input->offset + read_bytes < sizeof (struct input_event)) {
			input->offset = input->offset + read_bytes;
			g_debug ("incomplete read");
			goto out;
		}

		/* we have all the data */
		input->offset = 0;

		g_debug ("event.value=%d ; event.code=%d (0x%02x)",
			   input->event.value,
			   input->event.code,
			   input->event.code);

		/* switch? */
		if (input->event.type != EV_SW) {
			g_debug ("not a switch event");
			continue;
		}

		/* is not the watched switch */
		if (input->event.code != input->watched_switch) {
			g_debug ("not the watched switch");
			continue;
		}

		/* check switch state */
		if (ioctl (g_io_channel_unix_get_fd(channel), EVIOCGSW(sizeof (bitmask)), bitmask) < 0) {
			g_debug ("ioctl EVIOCGSW failed");
			continue;
		}

		/* are we set */
		input->last_switch_state = test_bit (input->event.code, bitmask);
		g_signal_emit_by_name (G_OBJECT (input),
				       "switch-changed",
				       input->last_switch_state);
	}
out:
	return TRUE;
}

/**
 * up_input_coldplug:
 **/
gboolean
up_input_coldplug (UpInput *input, GUdevDevice *d)
{
	gboolean ret = FALSE;
	gchar *path;
	gchar *contents = NULL;
	gchar *native_path = NULL;
	const gchar *device_file;
	GError *error = NULL;
	glong bitmask[NBITS(SW_MAX)];
	gint num_bits;
	GIOStatus status;

	/* get sysfs path */
	native_path = up_input_get_device_sysfs_path (d);

	/* is a switch */
	path = g_build_filename (native_path, "../capabilities/sw", NULL);
	if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
		char *path2;
		path2 = g_build_filename (native_path, "capabilities/sw", NULL);
		if (!g_file_test (path2, G_FILE_TEST_EXISTS)) {
			g_debug ("not a switch [%s]", path);
			g_debug ("not a switch [%s]", path2);
			g_free (path2);
			goto out;
		}
		g_free (path);
		path = path2;
	}

	/* get caps */
	ret = g_file_get_contents (path, &contents, NULL, &error);
	if (!ret) {
		g_debug ("failed to get contents for [%s]: %s", path, error->message);
		g_error_free (error);
		goto out;
	}

	/* convert to a bitmask */
	num_bits = up_input_str_to_bitmask (contents, bitmask, sizeof (bitmask));
	if ((num_bits == 0) || (num_bits >= SW_CNT)) {
		g_debug ("invalid bitmask entry for %s", native_path);
		ret = FALSE;
		goto out;
	}

	/* is this the watched switch? */
	if (!test_bit (input->watched_switch, bitmask)) {
		g_debug ("not the watched switch: %s", native_path);
		ret = FALSE;
		goto out;
	}

	/* get device file */
	device_file = g_udev_device_get_device_file (d);
	if (device_file == NULL || device_file[0] == '\0') {
		g_debug ("no device file: %s", native_path);
		ret = FALSE;
		goto out;
	}

	/* open device file */
	input->eventfp = open (device_file, O_RDONLY | O_NONBLOCK);
	if (input->eventfp <= 0) {
		g_warning ("cannot open '%s': %s", device_file, strerror (errno));
		ret = FALSE;
		goto out;
	}

	/* get initial state */
	if (ioctl (input->eventfp, EVIOCGSW(sizeof (bitmask)), bitmask) < 0) {
		g_warning ("ioctl EVIOCGSW on %s failed", native_path);
		ret = FALSE;
		goto out;
	}

	/* create channel */
	g_debug ("watching %s (%i)", device_file, input->eventfp);
	input->channel = g_io_channel_unix_new (input->eventfp);

	/* set binary encoding */
	status = g_io_channel_set_encoding (input->channel, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		g_error_free (error);
		ret = FALSE;
		goto out;
	}

	/* watch this */
	g_io_add_watch (input->channel, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, up_input_event_io, input);

	/* set if we are closed */
	g_debug ("using %s for watched switch event", native_path);
	input->last_switch_state = test_bit (input->watched_switch, bitmask);

out:
	g_free (native_path);
	g_free (path);
	g_free (contents);
	return ret;
}

/**
 * up_input_init:
 **/
static void
up_input_init (UpInput *input)
{
	input->eventfp = -1;
	input->last_switch_state = -1;
}

/**
 * up_input_finalize:
 **/
static void
up_input_finalize (GObject *object)
{
	UpInput *input;

	g_return_if_fail (object != NULL);
	g_return_if_fail (UP_IS_INPUT (object));

	input = UP_INPUT (object);

	if (input->channel) {
		g_io_channel_shutdown (input->channel, FALSE, NULL);
		input->eventfp = -1;
		g_io_channel_unref (input->channel);
	}
	if (input->eventfp >= 0)
		close (input->eventfp);
	G_OBJECT_CLASS (up_input_parent_class)->finalize (object);
}

static void
up_input_set_property (GObject        *object,
		       guint           property_id,
		       const GValue   *value,
		       GParamSpec     *pspec)
{
	UpInput *input = UP_INPUT (object);

	switch (property_id) {
	case PROP_WATCHED_SWITCH:
		input->watched_switch = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

static void
up_input_get_property (GObject        *object,
		       guint           property_id,
		       GValue         *value,
		       GParamSpec     *pspec)
{
	UpInput *input = UP_INPUT (object);

	switch (property_id) {
	case PROP_WATCHED_SWITCH:
		g_value_set_uint (value, input->watched_switch);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

/**
 * up_input_class_init:
 **/
static void
up_input_class_init (UpInputClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = up_input_finalize;
	object_class->set_property = up_input_set_property;
	object_class->get_property = up_input_get_property;

	g_object_class_install_property (object_class, PROP_WATCHED_SWITCH,
					 g_param_spec_uint("watched-switch",
							    "Watched switch",
							    "The input switch to watch",
							    SW_LID, SW_MAX, SW_LID,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals[SWITCH_CHANGED] = g_signal_new ("switch-changed",
						 G_TYPE_FROM_CLASS (klass),
						 G_SIGNAL_RUN_LAST,
						 0,
						 NULL,
						 NULL,
						 g_cclosure_marshal_generic,
						 G_TYPE_NONE,
						 1,
						 G_TYPE_BOOLEAN);
}

/**
 * up_input_new:
 *
 * Returns a #UpInput that watches the computer lid switch.
 **/
UpInput *
up_input_new (void)
{
	return g_object_new (UP_TYPE_INPUT, NULL);
}

/**
 * up_input_new_for_switch:
 * @watched_switch: the identifier for the `SW_` switch to watch
 *
 * Returns a #UpInput that watches the switched passed as argument.
 **/
UpInput *
up_input_new_for_switch (guint watched_switch)
{
	return g_object_new (UP_TYPE_INPUT,
			     "watched-switch", watched_switch,
			     NULL);
}

/**
 * up_input_get_switch_value:
 * @input: a #UpInput
 *
 * Returns the last state of the switch. It is an error
 * to call this without having successfully run
 * up_input_coldplug().
 **/
gboolean
up_input_get_switch_value (UpInput *input)
{
	g_return_val_if_fail (UP_IS_INPUT(input), FALSE);
	g_return_val_if_fail (input->last_switch_state != -1, FALSE);

	return input->last_switch_state;
}
