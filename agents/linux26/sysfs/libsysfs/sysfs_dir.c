/*
 * syfs_dir.c
 *
 * Directory utility functions for libsysfs
 *
 * Copyright (C) IBM Corp. 2003
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include "libsysfs.h"
#include "sysfs.h"

/**
 * sysfs_del_attribute: routine for dlist integration
 */
static void sysfs_del_attribute(void *attr)
{
        sysfs_close_attribute((struct sysfs_attribute *)attr);
}

/**
 * sysfs_del_link: routine for dlist integration
 */
static void sysfs_del_link(void *ln)
{
        sysfs_close_link((struct sysfs_link *)ln);
}

/**
 * sysfs_del_dir: routine for dlist integration
 */
static void sysfs_del_directory(void *dir)
{
        sysfs_close_directory((struct sysfs_directory *)dir);
}

/**
 * dir_attribute_name_equal: compares dir attributes by name
 * @a: attribute name for comparison
 * @b: sysfs_attribute to be compared.
 * returns 1 if a==b->name or 0 if not equal
 */
static int dir_attribute_name_equal(void *a, void *b)
{
	if (a == NULL || b == NULL)
		return 0;

	if (strcmp(((unsigned char *)a), ((struct sysfs_attribute *)b)->name) 
	    == 0)
		return 1;
	return 0;
}

/**
 * dir_link_name_equal: compares dir links by name
 * @a: link name for comparison
 * @b: sysfs_link to be compared.
 * returns 1 if a==b->name or 0 if not equal
 */
static int dir_link_name_equal(void *a, void *b)
{
	if (a == NULL || b == NULL)
		return 0;

	if (strcmp(((unsigned char *)a), ((struct sysfs_link *)b)->name) 
	    == 0)
		return 1;
	return 0;
}

/**
 * dir_subdir_name_equal: compares subdirs by name
 * @a: name of subdirectory to compare
 * @b: sysfs_directory subdirectory to be compared
 * returns 1 if a==b->name or 0 if not equal
 */
static int dir_subdir_name_equal(void *a, void *b)
{
	if (a == NULL || b == NULL)
		return 0;

	if (strcmp(((unsigned char *)a), ((struct sysfs_directory *)b)->name)
	    == 0)
		return 1;
	return 0;
}

/**
 * sysfs_close_attribute: closes and cleans up attribute
 * @sysattr: attribute to close.
 */
void sysfs_close_attribute(struct sysfs_attribute *sysattr)
{
	if (sysattr != NULL) {
		if (sysattr->value != NULL)
			free(sysattr->value);
		free(sysattr);
	}
}

/**
 * alloc_attribute: allocates and initializes attribute structure
 * returns struct sysfs_attribute with success and NULL with error.
 */
static struct sysfs_attribute *alloc_attribute(void)
{
	return (struct sysfs_attribute *)
			calloc(1, sizeof(struct sysfs_attribute));
}

/**
 * sysfs_open_attribute: creates sysfs_attribute structure
 * @path: path to attribute.
 * returns sysfs_attribute struct with success and NULL with error.
 */
struct sysfs_attribute *sysfs_open_attribute(const unsigned char *path)
{
	struct sysfs_attribute *sysattr = NULL;
	struct stat fileinfo;
	
	if (path == NULL) {
		errno = EINVAL;
		return NULL;
	}
	sysattr = alloc_attribute();
	if (sysattr == NULL) {
		dprintf("Error allocating attribute at %s\n", path);
		return NULL;
	}
	if (sysfs_get_name_from_path(path, sysattr->name, SYSFS_NAME_LEN) 
	    != 0) {
		dprintf("Error retrieving attribute name from path: %s\n", 
			path);
		sysfs_close_attribute(sysattr);
		return NULL;
	}
	strncpy(sysattr->path, path, sizeof(sysattr->path));
	if ((stat(sysattr->path, &fileinfo)) != 0) {
		dprintf("Stat failed: No such attribute?\n");
		sysattr->method = 0;
		free(sysattr);
		sysattr = NULL;
	} else {
		if (fileinfo.st_mode & S_IRUSR)
			sysattr->method |= SYSFS_METHOD_SHOW;
		if (fileinfo.st_mode & S_IWUSR)
			sysattr->method |= SYSFS_METHOD_STORE;
	}

	return sysattr;
}

/**
 * sysfs_write_attribute: write value to the attribute
 * @sysattr: attribute to write
 * @new_value: value to write
 * @len: length of "new_value"
 * returns 0 with success and -1 with error.
 */
int sysfs_write_attribute(struct sysfs_attribute *sysattr,
		const unsigned char *new_value, size_t len)
{
	int fd;
	int length;
	
	if (sysattr == NULL || new_value == NULL || len == 0) {
		errno = EINVAL;
		return -1;
	}
	
	if (!(sysattr->method & SYSFS_METHOD_STORE)) {
		dprintf ("Store method not supported for attribute %s\n",
			sysattr->path);
		return -1;
	}
	if (sysattr->method & SYSFS_METHOD_SHOW) {
		if ((strncmp(sysattr->value, new_value, sysattr->len)) == 0) {
			dprintf("Attribute %s already has the requested value %s\n",
					sysattr->name, new_value);
			return 0;	
		}
	}
	/* 
	 * open O_WRONLY since some attributes have no "read" but only
	 * "write" permission 
	 */ 
	if ((fd = open(sysattr->path, O_WRONLY)) < 0) {
		dprintf("Error reading attribute %s\n", sysattr->path);
		return -1;
	}

	length = write(fd, new_value, len);
	if (length < 0) {
		dprintf("Error writing to the attribute %s - invalid value?\n",
			sysattr->name);
		close(fd);
		return -1;
	} else if (length != len) {
		dprintf("Could not write %d bytes to attribute %s\n", 
					len, sysattr->name);
		/* 
		 * since we could not write user supplied number of bytes,
		 * restore the old value if one available
		 */
		if (sysattr->method & SYSFS_METHOD_SHOW) {
			length = write(fd, sysattr->value, sysattr->len);
			close(fd);
			return -1;
		}
	}
	
	/*
	 * Validate length that has been copied. Alloc appropriate area
	 * in sysfs_attribute. Verify first if the attribute supports reading
	 * (show method). If it does not, do not bother
	 */ 
	if (sysattr->method & SYSFS_METHOD_SHOW) {
		if (length != sysattr->len) {
			sysattr->value = (char *)realloc(sysattr->value, 
								length);
			sysattr->len = length;
			strncpy(sysattr->value, new_value, length);
		} else {
			/*"length" of the new value is same as old one */ 
			strncpy(sysattr->value, new_value, length);
		}
	}
			
	close(fd);	
	return 0;
}


/**
 * sysfs_read_attribute: reads value from attribute
 * @sysattr: attribute to read
 * returns 0 with success and -1 with error.
 */
int sysfs_read_attribute(struct sysfs_attribute *sysattr)
{
	unsigned char *fbuf = NULL;
	unsigned char *vbuf = NULL;
	size_t length = 0;
	int pgsize = 0;
	int fd;

	if (sysattr == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (!(sysattr->method & SYSFS_METHOD_SHOW)) {
		dprintf("Show method not supported for attribute %s\n",
			sysattr->path);
		return -1;
	}
	pgsize = getpagesize();
	fbuf = (unsigned char *)calloc(1, pgsize+1);
	if (fbuf == NULL) {
		dprintf("calloc failed\n");
		return -1;
	}
	if ((fd = open(sysattr->path, O_RDONLY)) < 0) {
		dprintf("Error reading attribute %s\n", sysattr->path);
		free(fbuf);
		return -1;
	}
	length = read(fd, fbuf, pgsize);
	if (length < 0) {
		dprintf("Error reading from attribute %s\n", sysattr->path);
		close(fd);
		free(fbuf);
		return -1;
	}
	sysattr->len = length;
	close(fd);
	vbuf = (unsigned char *)realloc(fbuf, length+1);
	if (vbuf == NULL) {
		dprintf("realloc failed\n");
		free(fbuf);
		return -1;
	}
	sysattr->value = vbuf;

	return 0;
}

/**
 * sysfs_read_attribute_value: given path to attribute, return its value.
 *	values can be up to a pagesize, if buffer is smaller the value will 
 *	be truncated. 
 * @attrpath: sysfs path to attribute
 * @value: buffer to put value
 * @vsize: size of value buffer
 * returns 0 with success and -1 with error.
 */
int sysfs_read_attribute_value(const unsigned char *attrpath, 
					unsigned char *value, size_t vsize)
{
	struct sysfs_attribute *attr = NULL;
	size_t length = 0;

	if (attrpath == NULL || value == NULL) {
		errno = EINVAL;
		return -1;
	}

	attr = sysfs_open_attribute(attrpath);
	if (attr == NULL) {
		dprintf("Invalid attribute path %s\n", attrpath);
		errno = EINVAL;
		return -1;
	}
	if((sysfs_read_attribute(attr)) != 0 || attr->value == NULL) {
		dprintf("Error reading from attribute %s\n", attrpath);
		sysfs_close_attribute(attr);
		return -1;
	}
	length = strlen(attr->value);
	if (length > vsize) 
		dprintf("Value length %d is larger than supplied buffer %d\n",
			length, vsize);
	strncpy(value, attr->value, vsize);
	sysfs_close_attribute(attr);

	return 0;
}

/**
 * sysfs_get_value_from_attrbutes: given a linked list of attributes and an 
 * 	attribute name, return its value
 * @attr: attribute to search
 * @name: name to look for
 * returns unsigned char * value - could be NULL
 */
unsigned char *sysfs_get_value_from_attributes(struct dlist *attr, 
					const unsigned char *name)
{	
	struct sysfs_attribute *cur = NULL;
	
	if (attr == NULL || name == NULL) {
		errno = EINVAL;
		return NULL;
	}
	dlist_for_each_data(attr, cur, struct sysfs_attribute) {
		if (strcmp(cur->name, name) == 0)
			return cur->value;
	}
	return NULL;
}

/**
 * sysfs_close_link: closes and cleans up link.
 * @ln: link to close.
 */
void sysfs_close_link(struct sysfs_link *ln)
{
	if (ln != NULL) 
		free(ln);
}

/**
 * sysfs_close_directory: closes directory, cleans up attributes and links
 * @sysdir: sysfs_directory to close
 */
void sysfs_close_directory(struct sysfs_directory *sysdir)
{
	if (sysdir != NULL) {
		if (sysdir->subdirs != NULL) 
			dlist_destroy(sysdir->subdirs);
		if (sysdir->links != NULL)
			dlist_destroy(sysdir->links);
		if (sysdir->attributes != NULL) 
			dlist_destroy(sysdir->attributes);
		free(sysdir);
	}
}

/**
 * alloc_directory: allocates and initializes directory structure
 * returns struct sysfs_directory with success or NULL with error.
 */
static struct sysfs_directory *alloc_directory(void)
{
	return (struct sysfs_directory *)
			calloc(1, sizeof(struct sysfs_directory));
}

/**
 * alloc_link: allocates and initializes link structure
 * returns struct sysfs_link with success or NULL with error.
 */
static struct sysfs_link *alloc_link(void)
{
	return (struct sysfs_link *)calloc(1, sizeof(struct sysfs_link));
}

/**
 * sysfs_read_all_subdirs: calls sysfs_read_directory for all subdirs
 * @sysdir: directory whose subdirs need reading.
 * returns 0 with success and -1 with error.
 */
int sysfs_read_all_subdirs(struct sysfs_directory *sysdir)
{
	struct sysfs_directory *cursub = NULL;

	if (sysdir == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (sysdir->subdirs == NULL)
		return 0;
	dlist_for_each_data(sysdir->subdirs, cursub, struct sysfs_directory) {
		if (sysfs_read_directory(cursub) != 0) 
			dprintf ("Error reading subdirectory %s\n",
				cursub->name);
	}
	return 0;
}

/**
 * sysfs_open_directory: opens a sysfs directory, creates dir struct, and
 *		returns.
 * @path: path of directory to open.
 * returns: struct sysfs_directory * with success and NULL on error.
 */
struct sysfs_directory *sysfs_open_directory(const unsigned char *path)
{
	struct sysfs_directory *sdir = NULL;

	if (path == NULL) {
		errno = EINVAL;
		return NULL;
	}
	sdir = alloc_directory();
	if (sdir == NULL) {
		dprintf("Error allocating directory %s\n", path);
		return NULL;
	}
	if (sysfs_get_name_from_path(path, sdir->name, SYSFS_NAME_LEN) != 0) {
		dprintf("Error getting directory name from path: %s\n", path);
		sysfs_close_directory(sdir);
		return NULL;
	}
	strncpy(sdir->path, path, sizeof(sdir->path));

	return sdir;
}

/**
 * sysfs_open_link: opens a sysfs link, creates struct, and returns
 * @path: path of link to open.
 * returns: struct sysfs_link * with success and NULL on error.
 */
struct sysfs_link *sysfs_open_link(const unsigned char *linkpath)
{
	struct sysfs_link *ln = NULL;

	if (linkpath == NULL || strlen(linkpath) > SYSFS_PATH_MAX) {
		errno = EINVAL;
		return NULL;
	}

	ln = alloc_link();
	if (ln == NULL) {
		dprintf("Error allocating link %s\n", linkpath);
		return NULL;
	}
	strcpy(ln->path, linkpath);
	if ((sysfs_get_name_from_path(linkpath, ln->name, SYSFS_NAME_LEN)) != 0
	    || (sysfs_get_link(linkpath, ln->target, SYSFS_PATH_MAX)) != 0) {
		errno = EINVAL;
		dprintf("Invalid link path %s\n", linkpath);
		return NULL;
	}

	return ln;
}

/**
 * sysfs_read_directory: grabs attributes, links, and subdirectories
 * @sysdir: sysfs directory to open
 * returns 0 with success and -1 with error.
 */
int sysfs_read_directory(struct sysfs_directory *sysdir)
{
	DIR *dir = NULL;
	struct dirent *dirent = NULL;
	struct stat astats;
	struct sysfs_attribute *attr = NULL;
	struct sysfs_directory *subdir = NULL;
	struct sysfs_link *ln = NULL;
	unsigned char file_path[SYSFS_PATH_MAX];
	int retval = 0;

	if (sysdir == NULL) {
		errno = EINVAL;
		return -1;
	}
	dir = opendir(sysdir->path);
	if (dir == NULL) {
		dprintf("Error opening directory %s\n", sysdir->path);
		return -1;
	}
	while(((dirent = readdir(dir)) != NULL) && retval == 0) {
		if (0 == strcmp(dirent->d_name, "."))
			 continue;
		if (0 == strcmp(dirent->d_name, ".."))
			continue;
		memset(file_path, 0, SYSFS_PATH_MAX);
		strncpy(file_path, sysdir->path, sizeof(file_path));
		strncat(file_path, "/", sizeof(file_path));
		strncat(file_path, dirent->d_name, sizeof(file_path));
		if ((lstat(file_path, &astats)) != 0) {
			dprintf("stat failed\n");
			continue;
		}
		if (S_ISREG(astats.st_mode)) {	
			attr = sysfs_open_attribute(file_path);
			if (attr == NULL) {
				dprintf("Error opening attribute %s\n",
					file_path);
				retval = -1;
				break;
			}
			if (attr->method & SYSFS_METHOD_SHOW) {
				if ((sysfs_read_attribute(attr)) != 0) {
					dprintf("Error reading attribute %s\n",
						file_path);
					sysfs_close_attribute(attr);
					continue;
				}
			}
			                        
			if (sysdir->attributes == NULL) {
				sysdir->attributes = dlist_new_with_delete
					(sizeof(struct sysfs_attribute),
					 		sysfs_del_attribute);
			}
			dlist_unshift(sysdir->attributes, attr);
		} else if (S_ISDIR(astats.st_mode)) {
			subdir = sysfs_open_directory(file_path);
			if (subdir == NULL) {
				dprintf("Error opening directory %s\n",
					file_path);
				retval = -1;
				break;
			}
			if (sysdir->subdirs == NULL)
				sysdir->subdirs = dlist_new_with_delete
					(sizeof(struct sysfs_directory),
							sysfs_del_directory);
			dlist_unshift(sysdir->subdirs, subdir);
		} else if (S_ISLNK(astats.st_mode)) {
			ln = sysfs_open_link(file_path);
			if (ln == NULL) {
				dprintf("Error opening link %s\n", file_path);
				retval = -1;
				break;
			}
			if (sysdir->links == NULL)
				sysdir->links = dlist_new_with_delete
						(sizeof(struct sysfs_link),
						 		sysfs_del_link);
			dlist_unshift(sysdir->links, ln);
		}
	}
	closedir(dir);
	return(retval);
}

/**
 * sysfs_get_directory_attribute: retrieves attribute attrname
 * @dir: directory to retrieve attribute from
 * @attrname: name of attribute to look for
 * returns sysfs_attribute if found and NULL if not found
 */
struct sysfs_attribute *sysfs_get_directory_attribute
			(struct sysfs_directory *dir, unsigned char *attrname)
{
	struct sysfs_directory *sdir = NULL;
	struct sysfs_attribute *attr = NULL;
	
	if (dir == NULL || attrname == NULL) {
		errno = EINVAL;
		return NULL;
	}
	
	attr = (struct sysfs_attribute *)dlist_find_custom(dir->attributes,
		attrname, dir_attribute_name_equal);
	if (attr != NULL)
		return attr;
	
	if (dir->subdirs != NULL) {
		dlist_for_each_data(dir->subdirs, sdir, 
					struct sysfs_directory) {
			if (sdir->attributes == NULL)
				continue;
			attr = sysfs_get_directory_attribute(sdir, attrname);
			if (attr != NULL)
				return attr;
		}
	}
	return NULL;
}

/**
 * sysfs_get_directory_link: retrieves link from one directory list
 * @dir: directory to retrieve link from
 * @linkname: name of link to look for
 * returns reference to sysfs_link if found and NULL if not found
 */
struct sysfs_link *sysfs_get_directory_link
			(struct sysfs_directory *dir, unsigned char *linkname)
{
	if (dir == NULL || linkname == NULL) {
		errno = EINVAL;
		return NULL;
	}
	return (struct sysfs_link *)dlist_find_custom(dir->links,
		linkname, dir_link_name_equal);
}

/**
 * sysfs_get_subdirectory: retrieves subdirectory by name.
 * @dir: directory to search for subdirectory.
 * @subname: subdirectory name to get.
 * returns reference to subdirectory or NULL if not found
 */
struct sysfs_directory *sysfs_get_subdirectory(struct sysfs_directory *dir,
						unsigned char *subname)
{
	struct sysfs_directory *sub = NULL, *cursub = NULL;

	if (dir == NULL || dir->subdirs == NULL || subname == NULL) {
		errno = EINVAL;
		return NULL;
	}
	sub = (struct sysfs_directory *)dlist_find_custom(dir->subdirs,
		subname, dir_subdir_name_equal);
	if (sub != NULL) 
		return sub;

	if (dir->subdirs != NULL) {
		dlist_for_each_data(dir->subdirs, cursub, 
					struct sysfs_directory) {
			if (cursub->subdirs == NULL)
				continue;
			sub = sysfs_get_subdirectory(cursub, subname);
			if (sub != NULL)
				return sub;
		}
	}
	return NULL;
}

/**
 * sysfs_get_subdirectory_link: looks through all subdirs for specific link.
 * @dir: directory and subdirectories to search for link.
 * @linkname: link name to get.
 * returns reference to link or NULL if not found
 */
struct sysfs_link *sysfs_get_subdirectory_link(struct sysfs_directory *dir,
						unsigned char *linkname)
{
	struct sysfs_directory *cursub = NULL;
	struct sysfs_link *ln = NULL;

	if (dir == NULL || dir->links == NULL || linkname == NULL) {
		errno = EINVAL;
		return NULL;
	}

	ln = sysfs_get_directory_link(dir, linkname);
	if (ln != NULL)
		return ln;

	if (dir->subdirs == NULL)
		return NULL;

	if (dir->subdirs != NULL) {
		dlist_for_each_data(dir->subdirs, cursub, 
						struct sysfs_directory) {
			if (cursub->subdirs == NULL)
				continue;
			ln = sysfs_get_subdirectory_link(cursub, linkname);
			if (ln != NULL)
				return ln;
		}
	}
	return NULL;
}
