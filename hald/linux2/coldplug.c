/***************************************************************************
 * CVSID: $Id$
 *
 * coldplug.c : Synthesize hotplug events when starting up
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
#include "hotplug.h"


static void 
coldplug_compute_visit_device (const gchar *path, GHashTable *sysfs_to_bus_map);

/*#define HAL_COLDPLUG_VERBOSE*/

/** This function serves one major purpose : build an ordered list of
 *  pairs (sysfs path, subsystem) to process when starting up:
 *  coldplugging. The ordering is arranged such that all bus-devices
 *  are visited in the same order as performing a traversal through
 *  the tree; e.g. bus-device A is not processed before bus-device B
 *  if B is a parent of A connection-wise.
 *
 *  After all bus-devices are added to the list, then all block are
 *  processed in the order they appear.
 *
 *  Finally, all class devices are added to the list.
 *
 *  @return                     Ordered list of sysfs paths or NULL 
 *                              if there was an error
 */
gboolean 
coldplug_synthesize_events (void)
{
	GDir *dir;
	GError *err = NULL;
	gchar path[HAL_PATH_MAX];
	gchar path1[HAL_PATH_MAX];
	const gchar *f;
	const gchar *f1;
	const gchar *f2;

	/** Mapping from sysfs path to subsystem for bus devices. This is consulted
	 *  when traversing /sys/devices
	 *
	 *  Example:
	 *
	 * /sys/devices/pci0000:00/0000:00:07.2/usb1/1-1/1-1:1.0/host7/7:0:0:0  -> scsi
	 * /sys/devices/pci0000:00/0000:00:07.1/ide1/1.1                        -> ide
	 * /sys/devices/pci0000:00/0000:00:07.1/ide1/1.0                        -> ide
	 * /sys/devices/pci0000:00/0000:00:07.1/ide0/0.0                        -> ide
	 * /sys/devices/pci0000:00/0000:00:07.2/usb1/1-1/1-1:1.0                -> usb
	 * /sys/devices/pci0000:00/0000:00:07.2/usb1/1-1                        -> usb
	 * /sys/devices/pci0000:00/0000:00:07.2/usb1/1-0:1.0                    -> usb
	 * /sys/devices/pci0000:00/0000:00:07.2/usb1                            -> usb
	 * /sys/devices/pci0000:00/0000:00:04.1/0000:06:00.0                    -> pci
	 * /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0                    -> pci
	 * /sys/devices/pci0000:00/0000:00:08.0                                 -> pci
	 * /sys/devices/platform/vesafb0                                        -> platform
	 */
	GHashTable *sysfs_to_bus_map = NULL;

	/* build bus map */
	sysfs_to_bus_map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	g_snprintf (path, HAL_PATH_MAX, "%s/bus", hal_sysfs_path);
	if ((dir = g_dir_open (path, 0, &err)) == NULL) {
		HAL_ERROR (("Unable to open %/bus: %s", hal_sysfs_path, err->message));
		g_error_free (err);
		goto error;
	}
	while ((f = g_dir_read_name (dir)) != NULL) {
		GDir *dir1;

		g_snprintf (path, HAL_PATH_MAX, "%s/bus/%s", hal_sysfs_path, f);
		if ((dir1 = g_dir_open (path, 0, &err)) == NULL) {
			HAL_ERROR (("Unable to open %/bus/%s: %s", hal_sysfs_path, f, err->message));
			g_error_free (err);
			goto error;
		}
		while ((f1 = g_dir_read_name (dir1)) != NULL) {

			if (strcmp (f1, "devices") == 0) {
				GDir *dir2;

				g_snprintf (path, HAL_PATH_MAX, "%s/bus/%s/%s", 
					    hal_sysfs_path, f, f1);
				if ((dir2 = g_dir_open (path, 0, &err)) == NULL) {
					HAL_ERROR (("Unable to open %s/bus/%s/%s: %s", 
						    hal_sysfs_path, f, f1, err->message));
					g_error_free (err);
					goto error;
				}
				while ((f2 = g_dir_read_name (dir2)) != NULL) {
					gchar *target;
					gchar *normalized_target;
					g_snprintf (path, HAL_PATH_MAX, "%s/bus/%s/%s/%s", 
						    hal_sysfs_path, f, f1, f2);
					if ((target = g_file_read_link (path, &err)) == NULL) {
						HAL_ERROR (("%s/bus/%s/%s/%s is not a symlink: %s!", 
							    hal_sysfs_path, 
							    f, f1, f2, err->message));
						g_error_free (err);
						goto error;
					}

					g_snprintf (path, HAL_PATH_MAX, "%s/bus/%s/%s", hal_sysfs_path, f, f1);
					normalized_target = hal_util_get_normalized_path (path, target);
					g_free (target);

					g_hash_table_insert (sysfs_to_bus_map, normalized_target, g_strdup(f));

				}
				g_dir_close (dir2);
			}
		}
		g_dir_close (dir1);
	}
	g_dir_close (dir);

	/* Now traverse /sys/devices and consult the map we've just
	 * built; this includes adding a) bus devices; and b) class
	 * devices that sit in /sys/devices */
	g_snprintf (path, HAL_PATH_MAX, "%s/devices", hal_sysfs_path);
	if ((dir = g_dir_open (path, 0, &err)) == NULL) {
		HAL_ERROR (("Unable to open %/devices: %s", hal_sysfs_path, err->message));
		g_error_free (err);
		goto error;
	}
	while ((f = g_dir_read_name (dir)) != NULL) {
		GDir *dir1;

		g_snprintf (path, HAL_PATH_MAX, "%s/devices/%s", hal_sysfs_path, f);
		if ((dir1 = g_dir_open (path, 0, &err)) == NULL) {
			HAL_ERROR (("Unable to open %/devices/%s: %s", hal_sysfs_path, f, err->message));
			g_error_free (err);
			goto error;
		}
		while ((f1 = g_dir_read_name (dir1)) != NULL) {

			g_snprintf (path, HAL_PATH_MAX, "%s/devices/%s/%s", hal_sysfs_path, f, f1);
			coldplug_compute_visit_device (path, sysfs_to_bus_map);

		}
		g_dir_close (dir1);
	}
	g_dir_close (dir);

	g_hash_table_destroy (sysfs_to_bus_map);


	/* add class devices */
	g_snprintf (path, HAL_PATH_MAX, "%s/class", hal_sysfs_path);
	if ((dir = g_dir_open (path, 0, &err)) == NULL) {
		HAL_ERROR (("Unable to open %/class: %s", hal_sysfs_path, err->message));
		goto error;
	}
	while ((f = g_dir_read_name (dir)) != NULL) {
		GDir *dir1;

		g_snprintf (path, HAL_PATH_MAX, "%s/class/%s", hal_sysfs_path, f);
		if ((dir1 = g_dir_open (path, 0, &err)) == NULL) {
			HAL_ERROR (("Unable to open %/class/%s: %s", hal_sysfs_path, f, err->message));
			g_error_free (err);
			goto error;
		}
		while ((f1 = g_dir_read_name (dir1)) != NULL) {
			HotplugEvent *hotplug_event;
			gchar *target;
			gchar *normalized_target;

			g_snprintf (path, HAL_PATH_MAX, "%s/class/%s/%s", hal_sysfs_path, f, f1);
#ifdef HAL_COLDPLUG_VERBOSE
			printf ("class: %s (%s)\n", path, f);
#endif

			g_snprintf (path1, HAL_PATH_MAX, "%s/class/%s/%s/device", hal_sysfs_path, f, f1);
			if (((target = g_file_read_link (path1, NULL)) != NULL)) {
				normalized_target = hal_util_get_normalized_path (path1, target);
				g_free (target);
			} else {
				normalized_target = NULL;
			}


			hotplug_event = g_new0 (HotplugEvent, 1);
			hotplug_event->is_add = TRUE;
			g_strlcpy (hotplug_event->subsystem, f, sizeof (hotplug_event->subsystem));
			g_strlcpy (hotplug_event->sysfs_path, path, sizeof (hotplug_event->sysfs_path));
			hal_util_get_device_file (path, hotplug_event->device_file, sizeof (hotplug_event->device_file));
			if (normalized_target != NULL)
				g_strlcpy (hotplug_event->wait_for_sysfs_path, normalized_target, sizeof (hotplug_event->wait_for_sysfs_path));
			else
				hotplug_event->wait_for_sysfs_path[0] = '\0';
			hotplug_event->net_ifindex = -1;

			hotplug_event_enqueue (hotplug_event);

			g_free (normalized_target);

		}
		g_dir_close (dir1);
	}
	g_dir_close (dir);

	/* add block devices */
	g_snprintf (path, HAL_PATH_MAX, "%s/block", hal_sysfs_path);
	if ((dir = g_dir_open (path, 0, &err)) == NULL) {
		HAL_ERROR (("Unable to open %s: %s", path, err->message));
		g_error_free (err);
		goto error;
	}
	while ((f = g_dir_read_name (dir)) != NULL) {
		GDir *dir1;
		gsize flen;
		HotplugEvent *hotplug_event;
		gchar *target;
		gchar *normalized_target;

		g_snprintf (path, HAL_PATH_MAX, "%s/block/%s", hal_sysfs_path, f);
#ifdef HAL_COLDPLUG_VERBOSE
		printf ("block: %s (block)\n",  path);
#endif

		g_snprintf (path1, HAL_PATH_MAX, "%s/block/%s/device", hal_sysfs_path, f);
		if (((target = g_file_read_link (path1, NULL)) != NULL)) {
			normalized_target = hal_util_get_normalized_path (path1, target);
			g_free (target);
		} else {
			normalized_target = NULL;
		}

		hotplug_event = g_new0 (HotplugEvent, 1);
		hotplug_event->is_add = TRUE;
		g_strlcpy (hotplug_event->subsystem, "block", sizeof (hotplug_event->subsystem));
		g_strlcpy (hotplug_event->sysfs_path, path, sizeof (hotplug_event->sysfs_path));
		hal_util_get_device_file (path, hotplug_event->device_file, sizeof (hotplug_event->device_file));
		if (normalized_target != NULL)
			g_strlcpy (hotplug_event->wait_for_sysfs_path, normalized_target, sizeof (hotplug_event->wait_for_sysfs_path));
		else
			hotplug_event->wait_for_sysfs_path[0] = '\0';
		hotplug_event->net_ifindex = -1;
		hotplug_event_enqueue (hotplug_event);
		g_free (normalized_target);

		flen = strlen (f);

		if ((dir1 = g_dir_open (path, 0, &err)) == NULL) {
			HAL_ERROR (("Unable to open %s: %s", path, err->message));
			g_error_free (err);
			goto error;
		}
		while ((f1 = g_dir_read_name (dir1)) != NULL) {
			if (strncmp (f, f1, flen) == 0) {
				g_snprintf (path1, HAL_PATH_MAX, "%s/%s", path, f1);
#ifdef HAL_COLDPLUG_VERBOSE
				printf ("block: %s (block)\n", path1);
#endif

				hotplug_event = g_new0 (HotplugEvent, 1);
				hotplug_event->is_add = TRUE;
				g_strlcpy (hotplug_event->subsystem, "block", sizeof (hotplug_event->subsystem));
				g_strlcpy (hotplug_event->sysfs_path, path1, sizeof (hotplug_event->sysfs_path));
				g_strlcpy (hotplug_event->wait_for_sysfs_path, path, sizeof (hotplug_event->wait_for_sysfs_path));
				hal_util_get_device_file (path1, hotplug_event->device_file, sizeof (hotplug_event->device_file));
				hotplug_event->net_ifindex = -1;
				hotplug_event_enqueue (hotplug_event);
			}
		}
		g_dir_close (dir1);		
	}
	g_dir_close (dir);

	return TRUE;
error:
	HAL_ERROR (("Error building the orderered list of sysfs paths"));
	return FALSE;
}

static void
coldplug_compute_visit_device (const gchar *path, GHashTable *sysfs_to_bus_map)
{
	gchar *bus;
	GError *err;
	GDir *dir;
	const gchar *f;

	bus = g_hash_table_lookup (sysfs_to_bus_map, path);
	if (bus != NULL) {
		HotplugEvent *hotplug_event;
		gchar *parent_sysfs_path;

#ifdef HAL_COLDPLUG_VERBOSE
		printf ("bus:   %s (%s)\n", path, bus);
#endif

		hotplug_event = g_new0 (HotplugEvent, 1);
		hotplug_event->is_add = TRUE;
		g_strlcpy (hotplug_event->subsystem, bus, sizeof (hotplug_event->subsystem));
		g_strlcpy (hotplug_event->sysfs_path, path, sizeof (hotplug_event->sysfs_path));
		hotplug_event->net_ifindex = -1;

		parent_sysfs_path = hal_util_get_parent_sysfs_path (path);
		g_strlcpy (hotplug_event->wait_for_sysfs_path, parent_sysfs_path, sizeof (hotplug_event->wait_for_sysfs_path));
		g_free (parent_sysfs_path);

		hotplug_event->device_file[0] = '\0';

		hotplug_event_enqueue (hotplug_event);
	}

	/* visit children; dont follow symlinks though.. */
	err = NULL;
	if ((dir = g_dir_open (path, 0, &err)) == NULL) {
		/*HAL_ERROR (("Unable to open directory: %s", path, err->message));*/
		g_error_free (err);
		goto error;
	}
	while ((f = g_dir_read_name (dir)) != NULL) {
		gchar path_child[HAL_PATH_MAX];
		struct stat statbuf;
	
		g_snprintf (path_child, HAL_PATH_MAX, "%s/%s", path, f);

		if (lstat (path_child, &statbuf) == 0) {

			if (!S_ISLNK (statbuf.st_mode)) {
				/* recursion fun */
				coldplug_compute_visit_device (path_child, sysfs_to_bus_map);
			}
		}
	}
	g_dir_close (dir);

error:
	return;
}
