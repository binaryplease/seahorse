/*
 * Seahorse
 *
 * Copyright (C) 2005 Stefan Walter
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
 
#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
 
#include "seahorse-key-widget.h"
#include "seahorse-libdialogs.h"
#include "seahorse-util.h"

#include "seahorse-pgp-dialogs.h"
#include "seahorse-pgp-key-op.h"

static void
ok_clicked (GtkButton *button, SeahorseWidget *swidget)
{
	SeahorseKeyWidget *skwidget;
    SeahorsePGPKey *pkey;
	GtkWidget *w; 
	gpgme_subkey_t subkey;
	gpgme_error_t err;
	guint index;
    time_t expiry = 0;
	
	skwidget = SEAHORSE_KEY_WIDGET (swidget);
    pkey = SEAHORSE_PGP_KEY (skwidget->skey);
	index = skwidget->index;	

	struct tm t;

	w = glade_xml_get_widget (swidget->xml, "expire");
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w))) {
		
		memset (&t, 0, sizeof(t));            
		w = glade_xml_get_widget (swidget->xml, "calendar");
		gtk_calendar_get_date (GTK_CALENDAR (w), (guint*)&(t.tm_year), 
							   (guint*)&(t.tm_mon), (guint*)&(t.tm_mday));
		t.tm_year -= 1900;
		expiry = mktime (&t);
	}
	
    subkey = seahorse_pgp_key_get_nth_subkey (pkey, index);    
    g_return_if_fail (subkey != NULL);
    
    if (expiry != subkey->expires) {
        err = seahorse_pgp_key_pair_op_set_expires (pkey, index, expiry);
    
		if (!GPG_IS_OK (err))
			seahorse_pgp_handle_gpgme_error (err, _("Couldn't change expiry date"));
	}
    
	seahorse_widget_destroy (swidget);
}

void
expires_toggled (GtkWidget *widget, SeahorseWidget *swidget)
{
	GtkWidget *expire;
	GtkWidget *cal;
	
	expire = glade_xml_get_widget (swidget->xml, "expire");
	cal = glade_xml_get_widget (swidget->xml, "calendar");

	gtk_widget_set_sensitive (cal, !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (expire)));
}

void
seahorse_pgp_expires_new (SeahorsePGPKey *pkey, GtkWindow *parent, guint index)
{
	SeahorseWidget *swidget;
    gpgme_subkey_t subkey;
    GtkWidget *date, *expire;
	gchar *title;
    gchar *userid;
	
	g_return_if_fail (pkey != NULL && SEAHORSE_IS_PGP_KEY (pkey));
	g_return_if_fail (index <= seahorse_pgp_key_get_num_subkeys (pkey));
	
	swidget = seahorse_key_widget_new_with_index ("expires", parent, SEAHORSE_KEY (pkey), index);
	g_return_if_fail (swidget != NULL);
	
	glade_xml_signal_connect_data (swidget->xml, "on_calendar_change_button_clicked",
		G_CALLBACK (ok_clicked), swidget);
    
    date = glade_xml_get_widget (swidget->xml, "calendar");    
    g_return_if_fail (date != NULL);
    
    subkey = seahorse_pgp_key_get_nth_subkey (pkey, index);
    g_return_if_fail (subkey != NULL);

	expire = glade_xml_get_widget (swidget->xml, "expire");
	glade_xml_signal_connect_data (swidget->xml, "on_expire_toggled",
		G_CALLBACK (expires_toggled), swidget);
	if (!seahorse_key_get_expires (SEAHORSE_KEY (pkey))) {
		gtk_toggle_button_set_active  (GTK_TOGGLE_BUTTON (expire), TRUE);
    	gtk_widget_set_sensitive (date, FALSE);
	} else {
		gtk_toggle_button_set_active  (GTK_TOGGLE_BUTTON (expire), FALSE);
    	gtk_widget_set_sensitive (date, TRUE);
	}
    
    if (subkey->expires) {
        struct tm t;
        time_t time = (time_t)subkey->expires;
        if (gmtime_r(&time, &t)) {
            gtk_calendar_select_month (GTK_CALENDAR (date), t.tm_mon, t.tm_year + 1900);
            gtk_calendar_select_day (GTK_CALENDAR (date), t.tm_mday);
        }
    }
	
    userid = seahorse_key_get_name (SEAHORSE_KEY (pkey), 0);
	if (index)
		title = g_strdup_printf (_("Expiry for Subkey %d of %s"), index, userid);
	else
		title = g_strdup_printf (_("Expiry for %s"), userid);
	g_free (userid);
   
	gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget (swidget->xml,
		                  swidget->name)), title);
}