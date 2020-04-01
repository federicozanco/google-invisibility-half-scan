/*
 * Google Invisibility Half Scan Plugin
 *  Copyright (C) 2010, Federico Zanco <federico.zanco ( at ) gmail.com>
 *
 * 
 * License:
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02111-1301, USA.
 *
 */
 
#define PLUGIN_ID			"google-invisibility-half-scan"
#define PLUGIN_NAME			"Google Invisibility Half Scan Plugin"
#define PLUGIN_VERSION		"0.2.1"
#define PLUGIN_STATIC_NAME	"google-invisibility-half-scan"
#define PLUGIN_SUMMARY		"Check if a buddy is invisible"
#define PLUGIN_DESCRIPTION	"Check if a buddy is invisible"
#define PLUGIN_AUTHOR		"Federico Zanco <federico.zanco ( at ) gmail.com>"
#define PLUGIN_WEBSITE		"http://www.siorarina.net/google-invisibility-half-scan/"

#define PREF_PREFIX									"/plugins/core/" PLUGIN_ID
#define	PREF_ACCOUNT								PREF_PREFIX "/account"
#define PREF_FILTER_ONLINE_PASSIVE_GOOGLE_CLIENTS	PREF_PREFIX "/filter_google_clients"
#define PREF_TIMEOUT								PREF_PREFIX "/timeout"
#define PREF_TIMEOUT_MASSIVE						PREF_PREFIX "/timeout-massive"
#define	PREF_TIMEOUT_DEFAULT						5
#define	PREF_TIMEOUT_MASSIVE_DEFAULT				10
#define TIMEOUT_MIN									1
#define TIMEOUT_MAX									3600

#define GMAIL_DOMAIN 		"gmail.com"

#define GMAIL_RESOURCE		"gmail"
#define GTALK_RESOURCE		"TalkGadget"
#define IGOOGLE_RESOURCE	"iGoogle"




#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* config.h may define PURPLE_PLUGINS; protect the definition here so that we
 * don't get complaints about redefinition when it's not necessary. */
#ifndef PURPLE_PLUGINS
# define PURPLE_PLUGINS
#endif

#ifdef GLIB_H
# include <glib.h>
#endif

//#include <unistd.h>


/* This will prevent compiler errors in some instances and is better explained in the
 * how-to documents on the wiki */
#ifndef G_GNUC_NULL_TERMINATED
# if __GNUC__ >= 4
#  define G_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
# else
#  define G_GNUC_NULL_TERMINATED
# endif
#endif

#include <blist.h>
#include <notify.h>
#include <debug.h>
#include <plugin.h>
#include <version.h>
#include <prefs.h>
#include <string.h>


static PurplePlugin *this_plugin = NULL;
static GList *invisible_buddies_list = NULL;
static guint timer = 0;


static gboolean
is_google_account(PurpleAccount *account)
{
/*	// this shoud be the right way but JabberStream...
	return ((JabberStream *) 
				((PurpleConnection *) 
					(purple_account_get_connection(account)))
						->proto_data)
							->googletalk;	*/
	
	return g_strcmp0(purple_account_get_protocol_id(account), "prpl-jabber") == 0
		&& g_strstr_len(purple_account_get_username(account), -1, GMAIL_DOMAIN);
}


static void
unset_timeout()
{
	if (timer)
	{
		purple_debug_info(PLUGIN_STATIC_NAME, "unset_timeout\n");
		purple_timeout_remove(timer);
		timer = 0;
	}
}


static char *
get_resource(const char *full_jid)
{
	gchar **name = NULL;
	
	name = g_strsplit(full_jid, "/", 2);
	
	g_free(name[0]);
	
	return name[1];
}


static gboolean
has_google_chat_vcard(const char *full_jid, xmlnode *presence)
{
	// these are the only clients that send vcard when going unavailable	
	if (g_str_has_prefix(get_resource(full_jid), GTALK_RESOURCE) ||
		g_str_has_prefix(get_resource(full_jid), IGOOGLE_RESOURCE) ||
		g_str_has_prefix(get_resource(full_jid), GMAIL_RESOURCE))
	{
		// here's the trick! If the presence stanza has a vcard node then
		// we're sure the buddy's shared status is not invisible
		if (xmlnode_get_child_with_namespace(presence, "x", "vcard-temp:x:update"))
			return TRUE;
	}
	
	return FALSE;
}


static gboolean
jabber_presence_received_cb(PurpleConnection *gc, const char *type, const char *from, xmlnode *presence, gpointer data)
{
	PurpleBuddy *buddy = NULL;
	char *m = NULL;

	purple_debug_info(PLUGIN_STATIC_NAME, "jabber_presence_received_cb\n");

	buddy = (PurpleBuddy *) data;
	
	// look only for the buddy we testing	
	if (g_strcmp0(from, purple_buddy_get_name(buddy)))
		return FALSE;
		
	// if presence stanza has a vcard then we know for sure that buddy's shared
	// status is not invisible
	if (purple_prefs_get_bool(PREF_FILTER_ONLINE_PASSIVE_GOOGLE_CLIENTS) 
		&& has_google_chat_vcard(from, presence))
		return FALSE;
	
	m = g_strdup_printf(
			"Got a response from %s (type 'unavailable'). This means that he or she could be Invisible but we cannot be sure becouse could have an iGoogle or Gmail page opened with the chat turned off. What is certain is that he or she has a resource opened.",
			purple_buddy_get_alias(buddy));
	
	purple_notify_info(
		this_plugin,
		PLUGIN_NAME,
		"...maybe YES!",
		m);

	// remove signal handler
	purple_signal_disconnect(
		purple_find_prpl("prpl-jabber"),
		"jabber-receiving-presence",
		this_plugin,
		PURPLE_CALLBACK(jabber_presence_received_cb));

	unset_timeout();
	
	return FALSE;
}


static gboolean
no_response_cb(gpointer buddy)
{
	char *m = NULL;
	
	purple_debug_info(PLUGIN_STATIC_NAME, "no_response_cb\n");
	
	unset_timeout();
	
	// remove signal handler
	purple_signal_disconnect(
		purple_find_prpl("prpl-jabber"),
		"jabber-receiving-presence",
		this_plugin,
		PURPLE_CALLBACK(jabber_presence_received_cb));
	
	m = g_strdup_printf(
			"This means that %s has no active resources and probably is Offline (But we cannot be sure of this!).",
			purple_buddy_get_alias(buddy));
		
	purple_notify_info(
		this_plugin,
		PLUGIN_NAME,
		"No Response...",
		m);
		
	g_free(m);
	
	return FALSE;
}


static gboolean
jabber_presence_received_massive_cb(PurpleConnection *gc, const char *type, const char *from, xmlnode *presence, gpointer data)
{
	PurpleBuddy *buddy = NULL;
	PurpleAccount *account = NULL;

	purple_debug_info(PLUGIN_STATIC_NAME, "jabber_presence_received_massive_cb\n");

	// we're looking only for the unavailable presences
	if (g_strcmp0(type, "unavailable"))
		return FALSE;

	account = purple_connection_get_account(gc);
	buddy =  purple_find_buddy(account, from);

	// if the buddy was online maybe this present was "legal"...
	if (purple_presence_is_online(purple_buddy_get_presence(buddy)))
		return FALSE;

	// if presence stanza has a vcard then we know for sure that buddy's shared
	// status is not invisible
	if (purple_prefs_get_bool(PREF_FILTER_ONLINE_PASSIVE_GOOGLE_CLIENTS) 
		&& has_google_chat_vcard(from, presence))
		return FALSE;
	
	invisible_buddies_list = g_list_prepend(invisible_buddies_list, buddy);
	
	return FALSE;
}


static gboolean
massive_scan_report_cb(gpointer data)
{
	PurpleNotifySearchResults *res = NULL;
	GList *ibl = NULL;
	GList *l = NULL;
	
	purple_debug_info(PLUGIN_STATIC_NAME, "massive_scan_report_cb\n");
	
	unset_timeout();
	
	// remove signal handler
	purple_signal_disconnect(
		purple_find_prpl("prpl-jabber"),
		"jabber-receiving-presence",
		this_plugin,
		PURPLE_CALLBACK(jabber_presence_received_massive_cb));

	// all not invisible!
	if (!invisible_buddies_list)
	{
		purple_notify_info(
			this_plugin,
			PLUGIN_NAME,
			"NONE",
			"No unavailable presences received. This should mean that all scanned buddies are really OFFLINE!");
		
		unset_timeout();
		
		return FALSE;
	}
	
	res = purple_notify_searchresults_new();
	
	purple_notify_searchresults_column_add(res,
		purple_notify_searchresults_column_new("Buddy"));
	purple_notify_searchresults_column_add(res,
		purple_notify_searchresults_column_new("Gmail"));
	purple_notify_searchresults_column_add(res,
		purple_notify_searchresults_column_new("Account"));
	
	
	for (ibl = invisible_buddies_list; ibl; ibl = ibl->next)
	{
		l = NULL;
	
		l = g_list_append(l, (gpointer ) g_strdup(purple_buddy_get_alias(ibl->data)));
		l = g_list_append(l, (gpointer ) g_strdup(purple_buddy_get_name(ibl->data)));
		l = g_list_append(l, (gpointer ) g_strdup(purple_account_get_username(
											purple_buddy_get_account(ibl->data))));
											
		purple_notify_searchresults_row_add(res, l);
		
		purple_debug_info(PLUGIN_STATIC_NAME, "BUDDY: %s\n", purple_buddy_get_alias(ibl->data));
	}
	
	purple_notify_searchresults(
		purple_account_get_connection(
			purple_buddy_get_account(
				invisible_buddies_list->data)),
		"Massive Scan Report",
		"Found something...",
		"\nThese buddies COULD be Invisible (although we're not sure becouse they could have a iGoogle or Gmail page opend with chat turned off):\n",
		res,
		NULL,
		NULL);
	
	g_list_free(invisible_buddies_list);
	invisible_buddies_list = NULL;
	
	return FALSE;
}


static void
set_timeout(GSourceFunc callback, gpointer data)
{
	if (!timer) 
	{
		purple_debug_info(PLUGIN_STATIC_NAME, "set_timeout\n");
		timer = purple_timeout_add_seconds(
					purple_prefs_get_int(PREF_TIMEOUT),
					callback,
					data);
	}
}


static void
send_presence_probe(PurpleBuddy *buddy)
{
	xmlnode *presence_probe = NULL;
	
	purple_debug_info(PLUGIN_STATIC_NAME, "send_presence_probe\n");
	
	presence_probe = xmlnode_new("presence");
	xmlnode_set_attrib(presence_probe, "type", "probe");
	xmlnode_set_attrib(presence_probe, "to", purple_buddy_get_name(buddy));
	xmlnode_set_attrib(presence_probe, "id", "gihs");
	
	purple_signal_emit(
		purple_connection_get_prpl(
			purple_account_get_connection(
				purple_buddy_get_account(buddy))),
		"jabber-sending-xmlnode",
		purple_account_get_connection(
			purple_buddy_get_account(buddy)),
		&presence_probe);
	
	if (presence_probe != NULL)
		xmlnode_free(presence_probe);
}


static void
do_scan_cb(PurpleBlistNode *node, gpointer data)
{
	PurpleAccount *account = NULL;
	PurpleBuddy *buddy = NULL;
	char *m = NULL;
	
	g_usleep(10000000);
	
	purple_debug_info(PLUGIN_STATIC_NAME, "do_scan_cb\n");
	
	// one half scan at a time...
	if (timer)
	{
		purple_notify_warning(
				this_plugin,
				PLUGIN_NAME,
				"Sorry...",
				"one half scan at a time. Please wait the end of the current scan before starting a new one.");
				
		return;
	}

	buddy = PURPLE_BUDDY(node);
	account = purple_buddy_get_account(buddy);
	
	// the account has to be connected before scanning can start...
	if (!purple_account_is_connected(account))
	{
		m = g_strdup_printf(
				"The account %s is OFFLINE! You have to connect this account before scanning.",
				purple_account_get_username(account));
		
		purple_notify_warning(
				this_plugin,
				PLUGIN_NAME,
				"WARNING!",
				m);
		
		g_free(m);
		
		return;
	}
	
	// if buddy is online... cannot be Invisible!
	if (purple_presence_is_online(purple_buddy_get_presence(buddy)))
	{
		m = g_strdup_printf(
			"%s is ONLINE! Obviously he or she cannot be Invisible...",
			purple_buddy_get_alias(buddy));
				
		purple_notify_warning(
				this_plugin,
				PLUGIN_NAME,
				"EHMMM...",
				m);
				
		g_free(m);
		
		return;
	}
	
	// jabber receiving presence
	purple_signal_connect(
		purple_find_prpl("prpl-jabber"),
		"jabber-receiving-presence",
		this_plugin,
		PURPLE_CALLBACK(jabber_presence_received_cb),
		(void *) buddy);
		
	send_presence_probe(buddy);
	
	set_timeout(no_response_cb, (gpointer ) buddy);
}


static void
do_massive_scan_cb(PurplePluginAction *action)
{
	PurpleAccount *account = NULL;
	GList *accounts = NULL;
	GSList *bl = NULL;
	GSList *f = NULL;

	purple_debug_info(PLUGIN_STATIC_NAME, "do_massive_scan_cb\n");
	
	// one half scan at a time...
	if (timer)
	{
		purple_notify_warning(
				this_plugin,
				PLUGIN_NAME,
				"Sorry...",
				"one half scan at a time. Please wait the end of the current scan before starting a new one.");
				
		return;
	}
	
	// jabber receiving presence
	purple_signal_connect(
		purple_find_prpl("prpl-jabber"),
		"jabber-receiving-presence",
		this_plugin,
		PURPLE_CALLBACK(jabber_presence_received_massive_cb),
		NULL);
	
	for (accounts = purple_accounts_get_all_active(); accounts; accounts = accounts->next)
	{
		account = accounts->data;
		
		// only active and online accounts...
		if (is_google_account(account) && purple_account_is_connected(account))
		{
			f = purple_find_buddies(account, NULL);
			
			for (bl = f; bl; bl = bl->next)
			{
				// scan only offline buddies...
				if (!purple_presence_is_online(purple_buddy_get_presence(bl->data)))
					send_presence_probe(bl->data);
			}
			
			g_slist_free(f);
		}
	}
	
	set_timeout(massive_scan_report_cb, NULL);
}


static void
blist_node_extended_menu(PurpleBlistNode *node, GList **m)
{
	PurpleMenuAction *action = NULL;

	if (purple_blist_node_get_type(node) != PURPLE_BLIST_BUDDY_NODE)
		return;
	
	if (g_strcmp0(
				purple_account_get_protocol_id(
					purple_buddy_get_account(PURPLE_BUDDY(node))), 
				"prpl-jabber")
		||
		!is_google_account(
			purple_buddy_get_account(PURPLE_BUDDY(node))))
		return;

	*m = g_list_append(*m, action);
	action = purple_menu_action_new("Is it Invisible?", PURPLE_CALLBACK(do_scan_cb), NULL, NULL);
	*m = g_list_append(*m, action);
}


static gboolean
plugin_load (PurplePlugin *plugin)
{
	purple_debug_info(PLUGIN_STATIC_NAME, "plugin_load\n");
	
	this_plugin = plugin;
	
	// add plugin pref
	purple_prefs_add_none(PREF_PREFIX);
	
	// check timeout pref (and set it)
	if (!purple_prefs_exists(PREF_TIMEOUT))
		purple_prefs_add_int(PREF_TIMEOUT, PREF_TIMEOUT_DEFAULT);
		
	// check timeout pref (and set it)
	if (!purple_prefs_exists(PREF_TIMEOUT_MASSIVE))
		purple_prefs_add_int(PREF_TIMEOUT_MASSIVE, PREF_TIMEOUT_MASSIVE_DEFAULT);
	
	// check filter google clients pref (and set it)
	if (!purple_prefs_exists(PREF_FILTER_ONLINE_PASSIVE_GOOGLE_CLIENTS))
		purple_prefs_add_bool(PREF_FILTER_ONLINE_PASSIVE_GOOGLE_CLIENTS, FALSE);
		
	purple_signal_connect(purple_blist_get_handle(), "blist-node-extended-menu",
		plugin,
		PURPLE_CALLBACK(blist_node_extended_menu),
		NULL);
	
	return TRUE;
}


static GList *
plugin_actions(PurplePlugin *plugin, gpointer context)
{
    GList *l = NULL;
    PurplePluginAction *action = NULL;
    
    action = purple_plugin_action_new("Massive scan...", do_massive_scan_cb);
    
    l = g_list_append(l, action);
    
    return l;
}


static PurplePluginPrefFrame *
get_plugin_pref_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *pref;
	
	frame = purple_plugin_pref_frame_new();
	
	// timeout
	pref = purple_plugin_pref_new_with_name_and_label(
				PREF_TIMEOUT,
				"Time out for a single scan (in seconds)");	
	purple_plugin_pref_set_bounds(pref, TIMEOUT_MIN, TIMEOUT_MAX);
	purple_plugin_pref_frame_add(frame, pref);
	
	// timeout
	pref = purple_plugin_pref_new_with_name_and_label(
				PREF_TIMEOUT_MASSIVE,
				"Time out for massive scan (in seconds)");	
	purple_plugin_pref_set_bounds(pref, TIMEOUT_MIN, TIMEOUT_MAX);
	purple_plugin_pref_frame_add(frame, pref);
	
	pref = purple_plugin_pref_new_with_name_and_label(
				PREF_FILTER_ONLINE_PASSIVE_GOOGLE_CLIENTS,
				"Filter Gmail, GTalk and iGoogle with chat disabled (this means that they definetly are not invisible in shared status)");
	purple_plugin_pref_frame_add(frame, pref);
	
	return frame;
}


static PurplePluginUiInfo prefs_info = {
	get_plugin_pref_frame,
	0,
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};


/* For specific notes on the meanings of each of these members, consult the C Plugin Howto
 * on the website. */
static PurplePluginInfo info = {
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	NULL,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,
	PLUGIN_ID,
	PLUGIN_NAME,
	PLUGIN_VERSION,
	PLUGIN_SUMMARY,
	PLUGIN_DESCRIPTION,
	PLUGIN_AUTHOR,
	PLUGIN_WEBSITE,
	plugin_load,
	NULL,
	NULL,
	NULL,
	NULL,
	&prefs_info,
	plugin_actions,
	NULL,
	NULL,
	NULL,
	NULL
};


static void
init_plugin (PurplePlugin * plugin)
{
}


PURPLE_INIT_PLUGIN (PLUGIN_ID, init_plugin, info)
