/*
 * Seahorse
 *
 * Copyright (C) 2008 Stefan Walter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "seahorse-gpgme-dialogs.h"
#include "seahorse-pgp-backend.h"
#include "seahorse-pgp-commands.h"
#include "seahorse-server-source.h"
#include "seahorse-transfer.h"
#include "seahorse-unknown-source.h"

#include "seahorse-progress.h"
#include "seahorse-servers.h"
#include "seahorse-util.h"

#include <gnome-keyring.h>

#include <string.h>

static SeahorsePgpBackend *pgp_backend = NULL;

struct _SeahorsePgpBackend {
	GObject parent;
	SeahorseGpgmeKeyring *keyring;
	SeahorseDiscovery *discovery;
	SeahorseUnknownSource *unknown;
	GHashTable *remotes;
};

struct _SeahorsePgpBackendClass {
	GObjectClass parent_class;
};

static void         seahorse_pgp_backend_collection_init  (GcrCollectionIface *iface);

G_DEFINE_TYPE_WITH_CODE (SeahorsePgpBackend, seahorse_pgp_backend, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GCR_TYPE_COLLECTION, seahorse_pgp_backend_collection_init);
);

static void
seahorse_pgp_backend_init (SeahorsePgpBackend *self)
{
	g_return_if_fail (pgp_backend == NULL);
	pgp_backend = self;

	self->remotes = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                       g_free, g_object_unref);

	/* Let these classes register themselves, when the backend is created */
	g_type_class_unref (g_type_class_ref (SEAHORSE_TYPE_PGP_COMMANDS));

	seahorse_gpgme_generate_register ();
}

#ifdef WITH_KEYSERVER

static void
on_settings_keyservers_changed (GSettings *settings,
                                gchar *key,
                                gpointer user_data)
{
	SeahorsePgpBackend *self = SEAHORSE_PGP_BACKEND (user_data);
	SeahorseServerSource *source;
	gchar **keyservers;
	GHashTable *check;
	const gchar *uri;
	GHashTableIter iter;
	guint i;

	check = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_iter_init (&iter, self->remotes);
	while (g_hash_table_iter_next (&iter, (gpointer*)&uri, (gpointer*)&source))
		g_hash_table_replace (check, (gpointer)uri, source);

	/* Load and strip names from keyserver list */
	keyservers = seahorse_servers_get_uris ();

	for (i = 0; keyservers[i] != NULL; i++) {
		uri = keyservers[i];

		/* If we don't have a keysource then add it */
		if (!g_hash_table_lookup (self->remotes, uri)) {
			source = seahorse_server_source_new (uri);
			if (source != NULL) {
				seahorse_pgp_backend_add_remote (self, uri, source);
				g_object_unref (source);
			}
		}

		/* Mark this one as present */
		g_hash_table_remove (check, uri);
	}

	/* Now remove any extras */
	g_hash_table_iter_init (&iter, check);
	while (g_hash_table_iter_next (&iter, (gpointer*)&uri, NULL))
		seahorse_pgp_backend_remove_remote (self, uri);

	g_hash_table_destroy (check);
	g_strfreev (keyservers);
}

#endif /* WITH_KEYSERVER */

static void
seahorse_pgp_backend_constructed (GObject *obj)
{
	SeahorsePgpBackend *self = SEAHORSE_PGP_BACKEND (obj);

	G_OBJECT_CLASS (seahorse_pgp_backend_parent_class)->constructed (obj);

	self->keyring = seahorse_gpgme_keyring_new ();
	seahorse_gpgme_keyring_load_async (self->keyring, NULL, NULL, NULL);

	self->discovery = seahorse_discovery_new ();
	self->unknown = seahorse_unknown_source_new ();

#ifdef WITH_KEYSERVER
	g_signal_connect (seahorse_context_pgp_settings (NULL), "changed::keyservers",
	                  G_CALLBACK (on_settings_keyservers_changed), self);

	/* Initial loading */
	on_settings_keyservers_changed (seahorse_context_pgp_settings (NULL), "keyservers", self);
#endif
}

static void
seahorse_pgp_backend_finalize (GObject *obj)
{
	SeahorsePgpBackend *self = SEAHORSE_PGP_BACKEND (obj);

#ifdef WITH_KEYSERVER
	g_signal_handlers_disconnect_by_func (seahorse_context_pgp_settings (NULL),
	                                      on_settings_keyservers_changed, self);
#endif

	g_clear_object (&self->keyring);
	g_clear_object (&self->discovery);
	g_clear_object (&self->unknown);
	g_hash_table_destroy (self->remotes);
	pgp_backend = NULL;

	G_OBJECT_CLASS (seahorse_pgp_backend_parent_class)->finalize (obj);
}

static void
seahorse_pgp_backend_class_init (SeahorsePgpBackendClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->constructed = seahorse_pgp_backend_constructed;
	gobject_class->finalize = seahorse_pgp_backend_finalize;
}

static guint
seahorse_pgp_backend_get_length (GcrCollection *collection)
{
	return 1;
}

static GList *
seahorse_pgp_backend_get_objects (GcrCollection *collection)
{
	SeahorsePgpBackend *self = SEAHORSE_PGP_BACKEND (collection);
	return g_list_append (NULL, self->keyring);
}

static gboolean
seahorse_pgp_backend_contains (GcrCollection *collection,
                               GObject *object)
{
	SeahorsePgpBackend *self = SEAHORSE_PGP_BACKEND (collection);
	return G_OBJECT (self->keyring) == object;
}

static void
seahorse_pgp_backend_collection_init (GcrCollectionIface *iface)
{
	iface->contains = seahorse_pgp_backend_contains;
	iface->get_length = seahorse_pgp_backend_get_length;
	iface->get_objects = seahorse_pgp_backend_get_objects;
}

SeahorsePgpBackend *
seahorse_pgp_backend_get (void)
{
	g_return_val_if_fail (pgp_backend, NULL);
	return pgp_backend;
}

GcrCollection *
seahorse_pgp_backend_initialize (void)
{
	SeahorsePgpBackend *self;

	self = g_object_new (SEAHORSE_TYPE_PGP_BACKEND, NULL);

	return GCR_COLLECTION (self);
}

SeahorseGpgmeKeyring *
seahorse_pgp_backend_get_default_keyring (SeahorsePgpBackend *self)
{
	self = self ? self : seahorse_pgp_backend_get ();
	g_return_val_if_fail (SEAHORSE_IS_PGP_BACKEND (self), NULL);
	g_return_val_if_fail (self->keyring, NULL);
	return self->keyring;
}

SeahorsePgpKey *
seahorse_pgp_backend_get_default_key (SeahorsePgpBackend *self)
{
	SeahorsePgpKey *key = NULL;
	GSettings *settings;
	const gchar *keyid;
	gchar *value;

	self = self ? self : seahorse_pgp_backend_get ();
	g_return_val_if_fail (SEAHORSE_IS_PGP_BACKEND (self), NULL);

	settings = seahorse_context_pgp_settings (NULL);
	if (settings != NULL) {
		value = g_settings_get_string (settings, "default-key");
		if (value != NULL && value[0]) {
			if (g_str_has_prefix (value, "openpgp:"))
				keyid = value + strlen ("openpgp:");
			else
				keyid = value;
			key = SEAHORSE_PGP_KEY (seahorse_gpgme_keyring_lookup (self->keyring, keyid));
		}
		g_free (value);
	}

	return key;
}

SeahorseDiscovery *
seahorse_pgp_backend_get_discovery (SeahorsePgpBackend *self)
{
	self = self ? self : seahorse_pgp_backend_get ();
	g_return_val_if_fail (SEAHORSE_IS_PGP_BACKEND (self), NULL);
	g_return_val_if_fail (self->discovery, NULL);
	return self->discovery;
}

SeahorseServerSource *
seahorse_pgp_backend_lookup_remote (SeahorsePgpBackend *self,
                                    const gchar *uri)
{
	self = self ? self : seahorse_pgp_backend_get ();
	g_return_val_if_fail (SEAHORSE_IS_PGP_BACKEND (self), NULL);

	return g_hash_table_lookup (self->remotes, uri);
}

void
seahorse_pgp_backend_add_remote (SeahorsePgpBackend *self,
                                 const gchar *uri,
                                 SeahorseServerSource *source)
{
	self = self ? self : seahorse_pgp_backend_get ();
	g_return_if_fail (SEAHORSE_IS_PGP_BACKEND (self));
	g_return_if_fail (uri != NULL);
	g_return_if_fail (SEAHORSE_IS_SERVER_SOURCE (source));
	g_return_if_fail (g_hash_table_lookup (self->remotes, uri) == NULL);

	g_hash_table_insert (self->remotes, g_strdup (uri), g_object_ref (source));
}

void
seahorse_pgp_backend_remove_remote (SeahorsePgpBackend *self,
                                    const gchar *uri)
{
	self = self ? self : seahorse_pgp_backend_get ();
	g_return_if_fail (SEAHORSE_IS_PGP_BACKEND (self));
	g_return_if_fail (uri != NULL);

	g_hash_table_remove (self->remotes, uri);
}

typedef struct {
	GCancellable *cancellable;
	gint num_searches;
	GList *objects;
} search_remote_closure;

static void
search_remote_closure_free (gpointer user_data)
{
	search_remote_closure *closure = user_data;
	g_clear_object (&closure->cancellable);
	g_list_free (closure->objects);
	g_free (closure);
}

static void
on_source_search_ready (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	search_remote_closure *closure = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	g_return_if_fail (closure->num_searches > 0);

	if (!seahorse_server_source_search_finish (SEAHORSE_SERVER_SOURCE (source),
	                                           result, &error))
		g_simple_async_result_take_error (res, error);

	closure->num_searches--;
	seahorse_progress_end (closure->cancellable, GINT_TO_POINTER (closure->num_searches));

	if (closure->num_searches == 0)
		g_simple_async_result_complete (res);

	g_object_unref (user_data);
}

void
seahorse_pgp_backend_search_remote_async (SeahorsePgpBackend *self,
                                          const gchar *search,
                                          GcrSimpleCollection *results,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
	search_remote_closure *closure;
	GSimpleAsyncResult *res;
	SeahorseServerSource *source;
	GHashTable *servers = NULL;
	GHashTableIter iter;
	gchar **names;
	gchar *uri;
	guint i;

	self = self ? self : seahorse_pgp_backend_get ();
	g_return_if_fail (SEAHORSE_IS_PGP_BACKEND (self));

	/* Get a list of all selected key servers */
	names = g_settings_get_strv (seahorse_context_settings (NULL), "last-search-servers");
	if (names != NULL && names[0] != NULL) {
		servers = g_hash_table_new (g_str_hash, g_str_equal);
		for (i = 0; names[i] != NULL; i++)
			g_hash_table_insert (servers, names[i], GINT_TO_POINTER (TRUE));
		g_strfreev (names);
	}

	res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                 seahorse_pgp_backend_search_remote_async);
	closure = g_new0 (search_remote_closure, 1);
	g_simple_async_result_set_op_res_gpointer (res, closure,
	                                           search_remote_closure_free);
	if (cancellable)
		closure->cancellable = g_object_ref (cancellable);

	g_hash_table_iter_init (&iter, self->remotes);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&source)) {
		if (servers) {
			g_object_get (source, "uri", &uri, NULL);
			if (!g_hash_table_lookup (servers, uri)) {
				g_free (uri);
				continue;
			}
			g_free (uri);
		}

		seahorse_progress_prep_and_begin (closure->cancellable, GINT_TO_POINTER (closure->num_searches), NULL);
		seahorse_server_source_search_async (source, search, results, closure->cancellable,
		                                     on_source_search_ready, g_object_ref (res));
		closure->num_searches++;
	}

	if (closure->num_searches == 0)
		g_simple_async_result_complete_in_idle (res);

	g_object_unref (res);
}

gboolean
seahorse_pgp_backend_search_remote_finish (SeahorsePgpBackend *self,
                                           GAsyncResult *result,
                                           GError **error)
{
	GSimpleAsyncResult *res;

	self = self ? self : seahorse_pgp_backend_get ();
	g_return_val_if_fail (SEAHORSE_IS_PGP_BACKEND (self), FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      seahorse_pgp_backend_search_remote_async), FALSE);

	res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return FALSE;

	return TRUE;
}

typedef struct {
	GCancellable *cancellable;
	gint num_transfers;
} transfer_closure;

static void
transfer_closure_free (gpointer user_data)
{
	transfer_closure *closure = user_data;
	g_clear_object (&closure->cancellable);
	g_free (closure);
}

static void
on_source_transfer_ready (GObject *source,
                          GAsyncResult *result,
                          gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	transfer_closure *closure = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	g_return_if_fail (closure->num_transfers > 0);

	seahorse_transfer_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	closure->num_transfers--;
	seahorse_progress_end (closure->cancellable, GINT_TO_POINTER (closure->num_transfers));

	if (closure->num_transfers == 0) {
		g_simple_async_result_complete (res);
	}

	g_object_unref (user_data);
}

void
seahorse_pgp_backend_transfer_async (SeahorsePgpBackend *self,
                                     GList *keys,
                                     SeahorseSource *to,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	transfer_closure *closure;
	SeahorseObject *object;
	GSimpleAsyncResult *res;
	SeahorseSource *from;
	GList *next;

	self = self ? self : seahorse_pgp_backend_get ();
	g_return_if_fail (SEAHORSE_IS_PGP_BACKEND (self));
	g_return_if_fail (SEAHORSE_IS_SOURCE (to));

	res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                 seahorse_pgp_backend_transfer_async);
	closure = g_new0 (transfer_closure, 1);
	g_simple_async_result_set_op_res_gpointer (res, closure,
	                                           transfer_closure_free);
	if (cancellable)
		closure->cancellable = g_object_ref (cancellable);

	keys = g_list_copy (keys);

	/* Sort by key source */
	keys = seahorse_util_objects_sort (keys);

	while (keys) {

		/* break off one set (same keysource) */
		next = seahorse_util_objects_splice (keys);

		g_assert (SEAHORSE_IS_OBJECT (keys->data));
		object = SEAHORSE_OBJECT (keys->data);

		/* Export from this key source */
		from = seahorse_object_get_source (object);
		g_return_if_fail (from != NULL);

		if (from != to) {
			/* Start a new transfer operation between the two sources */
			seahorse_progress_prep_and_begin (cancellable, GINT_TO_POINTER (closure->num_transfers), NULL);
			seahorse_transfer_async (from, to, keys, cancellable,
			                         on_source_transfer_ready, g_object_ref (res));
			closure->num_transfers++;
		}

		g_list_free (keys);
		keys = next;
	}

	if (closure->num_transfers == 0)
		g_simple_async_result_complete_in_idle (res);

	g_object_unref (res);
}

gboolean
seahorse_pgp_backend_transfer_finish (SeahorsePgpBackend *self,
                                      GAsyncResult *result,
                                      GError **error)
{
	GSimpleAsyncResult *res;

	self = self ? self : seahorse_pgp_backend_get ();
	g_return_val_if_fail (SEAHORSE_IS_PGP_BACKEND (self), FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      seahorse_pgp_backend_transfer_async), FALSE);

	res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return FALSE;

	return TRUE;
}

void
seahorse_pgp_backend_retrieve_async (SeahorsePgpBackend *self,
                                     GList *keyids,
                                     SeahorseSource *to,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	transfer_closure *closure;
	GSimpleAsyncResult *res;
	SeahorseSource *source;
	GHashTableIter iter;

	self = self ? self : seahorse_pgp_backend_get ();
	g_return_if_fail (SEAHORSE_IS_PGP_BACKEND (self));
	g_return_if_fail (SEAHORSE_IS_SOURCE (to));

	res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                 seahorse_pgp_backend_retrieve_async);
	closure = g_new0 (transfer_closure, 1);
	g_simple_async_result_set_op_res_gpointer (res, closure,
	                                           transfer_closure_free);
	if (cancellable)
		closure->cancellable = g_object_ref (cancellable);

	g_hash_table_iter_init (&iter, self->remotes);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&source)) {
		/* Start a new transfer operation between the two key sources */
		seahorse_progress_prep_and_begin (cancellable, GINT_TO_POINTER (closure->num_transfers), NULL);
		seahorse_transfer_async (source, to, keyids, cancellable,
		                         on_source_transfer_ready, g_object_ref (res));
		closure->num_transfers++;
	}

	if (closure->num_transfers == 0)
		g_simple_async_result_complete_in_idle (res);

	g_object_unref (res);
}

gboolean
seahorse_pgp_backend_retrieve_finish (SeahorsePgpBackend *self,
                                      GAsyncResult *result,
                                      GError **error)
{
	GSimpleAsyncResult *res;

	self = self ? self : seahorse_pgp_backend_get ();
	g_return_val_if_fail (SEAHORSE_IS_PGP_BACKEND (self), FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      seahorse_pgp_backend_retrieve_async), FALSE);

	res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return FALSE;

	return TRUE;
}

GList *
seahorse_pgp_backend_discover_keys (SeahorsePgpBackend *self,
                                    GList *keyids,
                                    GCancellable *cancellable)
{
	GList *robjects = NULL;
	const gchar *keyid;
	GList *todiscover = NULL;
	SeahorseGpgmeKey *key;
	SeahorseObject *object;
	GList *l;

	self = self ? self : seahorse_pgp_backend_get ();
	g_return_val_if_fail (SEAHORSE_IS_PGP_BACKEND (self), NULL);

	/* Check all the ids */
	for (l = keyids; l != NULL; l = g_list_next (l)) {
		keyid = l->data;

		/* Do we know about this object? */
		key = seahorse_gpgme_keyring_lookup (self->keyring, keyid);

		/* No such object anywhere, discover it */
		if (key == NULL) {
			todiscover = g_list_prepend (todiscover, (gpointer)keyid);
			continue;
		}

		/* Our return value */
		robjects = g_list_prepend (robjects, key);
	}

	/* Start a discover process on all todiscover */
	if (todiscover != NULL &&
	    g_settings_get_boolean (seahorse_context_settings (NULL), "server-auto-retrieve")) {
		seahorse_pgp_backend_retrieve_async (self, todiscover, SEAHORSE_SOURCE (self->keyring),
		                                     cancellable, NULL, NULL);
	}

	/* Add unknown objects for all these */
	for (l = todiscover; l != NULL; l = g_list_next (l)) {
		object = seahorse_unknown_source_add_object (self->unknown, keyid, cancellable);
		robjects = g_list_prepend (robjects, object);
	}

	g_list_free (todiscover);

	return robjects;
}
