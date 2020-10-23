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

#ifndef __UP_INPUT_H__
#define __UP_INPUT_H__

#include <glib-object.h>
#include <gudev/gudev.h>

G_BEGIN_DECLS

#define UP_TYPE_INPUT  	(up_input_get_type ())
G_DECLARE_FINAL_TYPE (UpInput, up_input, UP, INPUT, GObject)

GType			 up_input_get_type		(void);
UpInput			*up_input_new			(void);
UpInput			*up_input_new_for_switch	(guint		 watched_switch);
gboolean		 up_input_coldplug		(UpInput	*input,
							 GUdevDevice	*d);
gboolean		 up_input_get_switch_value	(UpInput	*input);

G_END_DECLS

#endif /* __UP_INPUT_H__ */

