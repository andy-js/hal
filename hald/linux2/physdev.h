/***************************************************************************
 * CVSID: $Id$
 *
 * physdev.h : Handling of physical kernel devices
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

#ifndef PHYSDEV_H
#define PHYSDEV_H

#include <glib.h>

void hotplug_event_begin_add_physdev (const gchar *subsystem, const gchar *sysfs_path, HalDevice *parent, void *end_token);

void hotplug_event_begin_remove_physdev (const gchar *subsystem, const gchar *sysfs_path, void *end_token);

#endif /* PHYSDEV_H */