/*
 * Seahorse
 *
 * Copyright (C) 2003 Jacob Perkins
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __SEAHORSE_UTIL_H__
#define __SEAHORSE_UTIL_H__

#include <gtk/gtk.h>
#include <time.h>
#include <gpgme.h>

typedef enum {
	SEAHORSE_CRYPT_SUFFIX,
	SEAHORSE_SIG_SUFFIX,
	SEAHORSE_ASC_SUFFIX
} SeahorseSuffix;

gchar*		seahorse_util_get_date_string		(const time_t		time);

void		seahorse_util_handle_error		(gpgme_error_t		err);

gpgme_error_t	seahorse_util_write_data_to_file	(const gchar		*path,
							 gpgme_data_t		data);

gchar*		seahorse_util_write_data_to_text	(gpgme_data_t		data);

gboolean	seahorse_util_check_suffix		(const gchar		*path,
							 SeahorseSuffix		suffix);

gchar*		seahorse_util_add_suffix		(gpgme_ctx_t		ctx,
							 const gchar		*path,
							 SeahorseSuffix		suffix);

gchar*		seahorse_util_remove_suffix		(const gchar		*path);

void        seahorse_util_free_keys (gpgme_key_t* keys);

/* For checking GPG error codes */
#define GPG_IS_OK(e)        (gpgme_err_code (e) == GPG_ERR_NO_ERROR)
#define GPG_OK              (gpgme_error (GPG_ERR_NO_ERROR))
#define GPG_E(e)            (gpgme_error (e))

#endif /* __SEAHORSE_UTIL_H__ */
