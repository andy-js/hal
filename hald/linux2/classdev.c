/***************************************************************************
 * CVSID: $Id$
 *
 * classdev.c : Handling of functional kernel devices
 *
 * Copyright (C) 2004 David Zeuthen, <david@fubar.dk>
 *
 * Licensed under the Academic Free License version 2.0
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include "../osspec.h"
#include "../logger.h"
#include "../hald.h"
#include "../callout.h"
#include "../device_info.h"
#include "../hald_conf.h"

#include "util.h"
#include "coldplug.h"
#include "hotplug_helper.h"

#include "hotplug.h"
#include "classdev.h"

/*--------------------------------------------------------------------------------------------------------------*/

static HalDevice *
input_add (const gchar *sysfs_path, const gchar *device_file, HalDevice *physdev)
{
	HalDevice *d;

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	if (physdev != NULL) {
		hal_device_property_set_string (d, "input.physical_device", physdev->udi);
		hal_device_property_set_string (d, "info.parent", physdev->udi);
	} else {
		hal_device_property_set_string (d, "info.parent", "/org/freedesktop/Hal/devices/computer");
	}
	hal_device_property_set_string (d, "info.category", "input");
	hal_device_add_capability (d, "input");

	hal_device_property_set_string (d, "input.device", device_file);

	return d;
}

static gboolean
input_post_probing (HalDevice *d)
{
	return TRUE;
}

static gboolean
input_compute_udi (HalDevice *d)
{
	gchar udi[256];

	hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
			      "%s_logicaldev_input",
			      hal_device_property_get_string (d, "info.parent"));
	hal_device_set_udi (d, udi);
	hal_device_property_set_string (d, "info.udi", udi);

	return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static HalDevice *
bluetooth_add (const gchar *sysfs_path, const gchar *device_file, HalDevice *physdev)
{
	HalDevice *d;

	d = NULL;

	if (physdev == NULL) {
		goto out;
	}

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	hal_device_property_set_string (d, "info.parent", physdev->udi);

	hal_device_property_set_string (d, "info.category", "bluetooth_hci");
	hal_device_add_capability (d, "bluetooth_hci");

	hal_device_property_set_string (d, "bluetooth_hci.physical_device", physdev->udi);
	hal_util_set_string_from_file (d, "bluetooth_hci.interface_name", sysfs_path, "name");

out:
	return d;
}

static gboolean
bluetooth_compute_udi (HalDevice *d)
{
	gchar udi[256];

	hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
			      "%s_bluetooth_hci",
			      hal_device_property_get_string (d, "info.parent"));
	hal_device_set_udi (d, udi);
	hal_device_property_set_string (d, "info.udi", udi);
	return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static gboolean
classdev_remove (HalDevice *d)
{
	if (!hal_device_store_remove (hald_get_gdl (), d)) {
		HAL_WARNING (("Error removing device"));
	}

	return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

struct ClassDevHandler_s;
typedef struct ClassDevHandler_s ClassDevHandler;

struct ClassDevHandler_s
{
	const gchar *subsystem;
	HalDevice *(*add) (const gchar *sysfs_path, const gchar *device_file, HalDevice *parent);
	const gchar *prober;
	gboolean (*post_probing) (HalDevice *d);
	gboolean (*compute_udi) (HalDevice *d);
	gboolean (*remove) (HalDevice *d);
}; 

/*--------------------------------------------------------------------------------------------------------------*/

static ClassDevHandler classdev_handler_input = 
{ 
	.subsystem    = "input",
	.add          = input_add,
	.prober       = "hald-probe-input",
	.post_probing = input_post_probing,
	.compute_udi  = input_compute_udi,
	.remove       = classdev_remove
};

static ClassDevHandler classdev_handler_bluetooth = 
{ 
	.subsystem    = "bluetooth",
	.add          = bluetooth_add,
	.prober       = NULL,
	.post_probing = NULL,
	.compute_udi  = bluetooth_compute_udi,
	.remove       = classdev_remove
};

static ClassDevHandler *classdev_handlers[] = {
	&classdev_handler_input,
	&classdev_handler_bluetooth,
	NULL
};

/*--------------------------------------------------------------------------------------------------------------*/

static void 
add_classdev_after_probing (HalDevice *d, ClassDevHandler *handler, void *end_token)
{
	/* Merge properties from .fdi files */
	di_search_and_merge (d);

	/* Compute UDI */
	if (!handler->compute_udi (d)) {
		hal_device_store_remove (hald_get_tdl (), d);
		goto out;
	}
	
	/* TODO: Merge persistent properties */

	/* TODO: Run callouts */

	/* Move from temporary to global device store */
	hal_device_store_remove (hald_get_tdl (), d);
	hal_device_store_add (hald_get_gdl (), d);

out:
	hotplug_event_end (end_token);
}

static void 
add_classdev_probing_helper_done(HalDevice *d, gboolean timed_out, gint return_code, gpointer data1, gpointer data2)
{
	void *end_token = (void *) data1;
	ClassDevHandler *handler = (ClassDevHandler *) data2;

	HAL_INFO (("entering; timed_out=%d, return_code=%d", timed_out, return_code));

	/* Discard device if probing reports failure */
	if (return_code != 0) {
		hal_device_store_remove (hald_get_tdl (), d);
		hotplug_event_end (end_token);
		goto out;
	}

	/* Do things post probing */
	if (!handler->post_probing (d)) {
		hotplug_event_end (end_token);
		goto out;
	}

	add_classdev_after_probing (d, handler, end_token);

out:
	;
}

void
hotplug_event_begin_add_classdev (const gchar *subsystem, const gchar *sysfs_path, const gchar *device_file, 
				  HalDevice *physdev, void *end_token)
{
	guint i;

	HAL_INFO (("class_add: subsys=%s sysfs_path=%s dev=%s physdev=0x%08x", subsystem, sysfs_path, device_file, physdev));

	for (i = 0; classdev_handlers [i] != NULL; i++) {
		ClassDevHandler *handler;

		handler = classdev_handlers[i];
		if (strcmp (handler->subsystem, subsystem) == 0) {
			HalDevice *d;

			/* attempt to add the device */
			d = handler->add (sysfs_path, device_file, physdev);
			if (d == NULL) {
				/* didn't find anything - thus, ignore this hotplug event */
				hotplug_event_end (end_token);
				goto out;
			}

			/* Add to temporary device store */
			hal_device_store_add (hald_get_tdl (), d);

			if (handler->prober != NULL) {
				/* probe the device */
				if (!helper_invoke (handler->prober, d, (gpointer) end_token, (gpointer) handler, add_classdev_probing_helper_done, HAL_HELPER_TIMEOUT)) {
					hal_device_store_remove (hald_get_tdl (), d);
					hotplug_event_end (end_token);
				}
				goto out;
			} else {
				add_classdev_after_probing (d, handler, end_token);
				goto out;
			}				
		}
	}

	/* didn't find anything - thus, ignore this hotplug event */
	hotplug_event_end (end_token);
out:
	;
}

void
hotplug_event_begin_remove_classdev (const gchar *subsystem, const gchar *sysfs_path, void *end_token)
{
	guint i;
	HalDevice *d;


	HAL_INFO (("class_rem: subsys=%s sysfs_path=%s", subsystem, sysfs_path));

	d = hal_device_store_match_key_value_string (hald_get_gdl (), "linux.sysfs_path_device", sysfs_path);
	if (d == NULL) {
		HAL_WARNING (("Error removing device"));
	} else {

		for (i = 0; classdev_handlers [i] != NULL; i++) {
			ClassDevHandler *handler;
			
			handler = classdev_handlers[i];
			if (strcmp (handler->subsystem, subsystem) == 0) {
				if (handler->remove (d)) {
					/* let the handler end the event */
					hotplug_event_end (end_token);
					goto out;
				}
			}
		}
	}

	/* didn't find anything - thus, ignore this hotplug event */
	hotplug_event_end (end_token);
out:
	;
}