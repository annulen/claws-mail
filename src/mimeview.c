/*
 * Sylpheed -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 1999-2001 Hiroyuki Yamamoto
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "defs.h"

#include <glib.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkctree.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkvpaned.h>
#include <gtk/gtktext.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkmenu.h>
#include <gtk/gtkdnd.h>
#include <gtk/gtkselection.h>
#include <stdio.h>
#include <unistd.h>

#include "intl.h"
#include "main.h"
#include "mimeview.h"
#include "textview.h"
#include "procmime.h"
#include "summaryview.h"
#include "menu.h"
#include "filesel.h"
#include "alertpanel.h"
#include "inputdialog.h"
#include "utils.h"
#include "gtkutils.h"
#include "prefs_common.h"
#include "rfc2015.h"

typedef enum
{
	COL_MIMETYPE = 0,
	COL_SIZE     = 1,
	COL_NAME     = 2
} MimeViewColumnPos;

#define N_MIMEVIEW_COLS	3

static void mimeview_set_multipart_tree		(MimeView	*mimeview,
						 MimeInfo	*mimeinfo,
						 GtkCTreeNode	*parent);
static GtkCTreeNode *mimeview_append_part	(MimeView	*mimeview,
						 MimeInfo	*partinfo,
						 GtkCTreeNode	*parent);
static void mimeview_show_message_part		(MimeView	*mimeview,
						 MimeInfo	*partinfo);
static void mimeview_show_image_part		(MimeView	*mimeview,
						 MimeInfo	*partinfo);
static void mimeview_change_view_type		(MimeView	*mimeview,
						 MimeViewType	 type);
static void mimeview_clear			(MimeView	*mimeview);

static void mimeview_selected		(GtkCTree	*ctree,
					 GtkCTreeNode	*node,
					 gint		 column,
					 MimeView	*mimeview);
static void mimeview_start_drag 	(GtkWidget	*widget,
					 gint		 button,
					 GdkEvent	*event,
					 MimeView	*mimeview);
static void mimeview_button_pressed	(GtkWidget	*widget,
					 GdkEventButton	*event,
					 MimeView	*mimeview);
static void mimeview_key_pressed	(GtkWidget	*widget,
					 GdkEventKey	*event,
					 MimeView	*mimeview);

static void mimeview_drag_data_get      (GtkWidget	  *widget,
					 GdkDragContext   *drag_context,
					 GtkSelectionData *selection_data,
					 guint		   info,
					 guint		   time,
					 MimeView	  *mimeview);

static void mimeview_display_as_text	(MimeView	*mimeview);
static void mimeview_save_as		(MimeView	*mimeview);
static void mimeview_launch		(MimeView	*mimeview);
static void mimeview_open_with		(MimeView	*mimeview);
static void mimeview_view_file		(const gchar	*filename,
					 MimeInfo	*partinfo,
					 const gchar	*cmdline);
#if USE_GPGME
static void mimeview_check_signature	(MimeView	*mimeview);
#endif

static GtkItemFactoryEntry mimeview_popup_entries[] =
{
	{N_("/_Open"),		  NULL, mimeview_launch,	  0, NULL},
	{N_("/Open _with..."),	  NULL, mimeview_open_with,	  0, NULL},
	{N_("/_Display as text"), NULL, mimeview_display_as_text, 0, NULL},
	{N_("/_Save as..."),	  NULL, mimeview_save_as,	  0, NULL}
#if USE_GPGME
        ,
        {N_("/_Check signature"), NULL, mimeview_check_signature, 0, NULL}
#endif
};

static GtkTargetEntry mimeview_mime_types[] =
{
	{"text/uri-list", 0, 0}
};

MimeView *mimeview_create(void)
{
	MimeView *mimeview;

	GtkWidget *paned;
	GtkWidget *scrolledwin;
	GtkWidget *ctree;
	GtkWidget *vbox;
	GtkWidget *popupmenu;
	GtkItemFactory *popupfactory;

	gchar *titles[] = {_("MIME Type"), _("Size"), _("Name")};
	gint n_entries;
	gint i;

	debug_print(_("Creating MIME view...\n"));
	mimeview = g_new0(MimeView, 1);

	scrolledwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwin),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_ALWAYS);
	gtk_widget_set_usize(scrolledwin, -1, 80);

	ctree = gtk_sctree_new_with_titles(N_MIMEVIEW_COLS, 0, titles);
	gtk_clist_set_selection_mode(GTK_CLIST(ctree), GTK_SELECTION_BROWSE);
	gtk_ctree_set_line_style(GTK_CTREE(ctree), GTK_CTREE_LINES_NONE);
	gtk_clist_set_column_justification(GTK_CLIST(ctree), COL_SIZE,
					   GTK_JUSTIFY_RIGHT);
	gtk_clist_set_column_width(GTK_CLIST(ctree), COL_MIMETYPE, 240);
	gtk_clist_set_column_width(GTK_CLIST(ctree), COL_SIZE, 64);
	for (i = 0; i < N_MIMEVIEW_COLS; i++)
		GTK_WIDGET_UNSET_FLAGS(GTK_CLIST(ctree)->column[i].button,
				       GTK_CAN_FOCUS);
	gtk_container_add(GTK_CONTAINER(scrolledwin), ctree);

	gtk_signal_connect(GTK_OBJECT(ctree), "tree_select_row",
			   GTK_SIGNAL_FUNC(mimeview_selected), mimeview);
	gtk_signal_connect(GTK_OBJECT(ctree), "button_press_event",
			   GTK_SIGNAL_FUNC(mimeview_button_pressed), mimeview);
	gtk_signal_connect(GTK_OBJECT(ctree), "key_press_event",
			   GTK_SIGNAL_FUNC(mimeview_key_pressed), mimeview);
	gtk_signal_connect(GTK_OBJECT (ctree),"start_drag",
			   GTK_SIGNAL_FUNC (mimeview_start_drag), mimeview);
	gtk_signal_connect(GTK_OBJECT(ctree), "drag_data_get",
			   GTK_SIGNAL_FUNC(mimeview_drag_data_get), mimeview);
    
	vbox = gtk_vbox_new(FALSE, 0);

	paned = gtk_vpaned_new();
	gtk_paned_add1(GTK_PANED(paned), scrolledwin);
	gtk_paned_add2(GTK_PANED(paned), vbox);
	gtk_widget_show_all(paned);

	n_entries = sizeof(mimeview_popup_entries) /
		sizeof(mimeview_popup_entries[0]);
	popupmenu = menu_create_items(mimeview_popup_entries, n_entries,
				      "<MimeView>", &popupfactory, mimeview);

	mimeview->paned       = paned;
	mimeview->scrolledwin = scrolledwin;
	mimeview->ctree       = ctree;
	mimeview->vbox        = vbox;
	mimeview->popupmenu   = popupmenu;
	mimeview->popupfactory = popupfactory;

	return mimeview;
}

void mimeview_init(MimeView *mimeview)
{
}

/* 
 * Check whether the message is OpenPGP signed
 */
#if USE_GPGME
static gboolean mimeview_is_signed(MimeView *mimeview)
{
	MimeInfo *partinfo;

        debug_print("mimeview_is signed of %p\n", mimeview);

        if (!mimeview) return FALSE;
	if (!mimeview->opened) return FALSE;

        debug_print("mimeview_is_signed: open\n" );

	if (!mimeview->file) return FALSE;

        debug_print("mimeview_is_signed: file\n" );

	partinfo = gtk_ctree_node_get_row_data
		(GTK_CTREE(mimeview->ctree), mimeview->opened);
	g_return_val_if_fail(partinfo != NULL, FALSE);

	/* walk the tree and see whether there is a signature somewhere */
	do {
		if (rfc2015_has_signature(partinfo))
			return TRUE;
        } while ((partinfo = partinfo->parent) != NULL);

	debug_print("mimeview_is_signed: FALSE\n" );

	return FALSE;
}

static void set_unchecked_signature(MimeInfo *mimeinfo)
{
	MimeInfo *sig_partinfo;

	sig_partinfo = rfc2015_find_signature(mimeinfo);
	if (sig_partinfo == NULL) return;

	g_free(sig_partinfo->sigstatus);
	sig_partinfo->sigstatus =
		g_strdup(_("Select \"Check signature\" to check"));

	g_free(sig_partinfo->sigstatus_full);
	sig_partinfo->sigstatus_full = NULL;
}
#endif /* USE_GPGME */

void mimeview_show_message(MimeView *mimeview, MimeInfo *mimeinfo,
			   const gchar *file)
{
	GtkCTree *ctree = GTK_CTREE(mimeview->ctree);
	GtkCTreeNode *node;
	gchar buf[BUFFSIZE];
	FILE *fp;

	mimeview_clear(mimeview);

	g_return_if_fail(file != NULL);
	g_return_if_fail(mimeinfo != NULL);

	mimeview->mimeinfo = mimeinfo;

	if ((fp = fopen(file, "r")) == NULL) {
		FILE_OP_ERROR(file, "fopen");
		return;
	}
	mimeview->file = g_strdup(file);

	/* skip headers */
	if (mimeinfo->mime_type == MIME_MULTIPART) {
		if (fseek(fp, mimeinfo->fpos, SEEK_SET) < 0)
		perror("fseek");
		while (fgets(buf, sizeof(buf), fp) != NULL)
			if (buf[0] == '\r' || buf[0] == '\n') break;
	}

	procmime_scan_multipart_message(mimeinfo, fp);
#if USE_GPGME
	if (prefs_common.auto_check_signatures)
		rfc2015_check_signature(mimeinfo, fp);
	else
		set_unchecked_signature(mimeinfo);
#endif
	fclose(fp);

	gtk_signal_handler_block_by_func(GTK_OBJECT(ctree), mimeview_selected,
					 mimeview);

	mimeview_set_multipart_tree(mimeview, mimeinfo, NULL);

	gtk_signal_handler_unblock_by_func(GTK_OBJECT(ctree),
					   mimeview_selected, mimeview);

	if ((node = gtk_ctree_node_nth(ctree, 0))) {
		gtk_ctree_select(ctree, node);
		gtk_widget_grab_focus(mimeview->ctree);
	}
}

void mimeview_destroy(MimeView *mimeview)
{
	procmime_mimeinfo_free_all(mimeview->mimeinfo);
	g_free(mimeview->file);
	g_free(mimeview);
}

static void mimeview_set_multipart_tree(MimeView *mimeview,
					MimeInfo *mimeinfo,
					GtkCTreeNode *parent)
{
	g_return_if_fail(mimeinfo != NULL);

	if (!mimeinfo->main && mimeinfo->parent)
		parent = mimeview_append_part(mimeview, mimeinfo, parent);

	if (mimeinfo->sub && mimeinfo->sub->children)
		mimeview_set_multipart_tree(mimeview, mimeinfo->sub, parent);

	if (mimeinfo->children) {
		MimeInfo *child;

		child = mimeinfo->children;
		while (child) {
			mimeview_set_multipart_tree(mimeview, child, parent);
			child = child->next;
		}
	}
}

static gchar *get_part_name(MimeInfo *partinfo)
{
#if USE_GPGME
	if (partinfo->sigstatus)
		return partinfo->sigstatus;
	else
#endif
	if (partinfo->name)
		return partinfo->name;
	else if (partinfo->filename)
		return partinfo->filename;
	else
		return "";
}

static GtkCTreeNode *mimeview_append_part(MimeView *mimeview,
					  MimeInfo *partinfo,
					  GtkCTreeNode *parent)
{
	GtkCTree *ctree = GTK_CTREE(mimeview->ctree);
	GtkCTreeNode *node;
	gchar *str[N_MIMEVIEW_COLS];

	str[COL_MIMETYPE] =
		partinfo->content_type ? partinfo->content_type : "";
	str[COL_SIZE] = to_human_readable(partinfo->size);
	str[COL_NAME] = get_part_name(partinfo);

	node = gtk_ctree_insert_node(ctree, parent, NULL, str, 0,
				     NULL, NULL, NULL, NULL,
				     FALSE, TRUE);
	gtk_ctree_node_set_row_data(ctree, node, partinfo);

	return node;
}

static void mimeview_show_message_part(MimeView *mimeview, MimeInfo *partinfo)
{
	FILE *fp;
	const gchar *fname;
#if USE_GPGME
	MimeInfo *pi;
#endif

	if (!partinfo) return;

#if USE_GPGME
	for (pi = partinfo; pi && !pi->plaintextfile ; pi = pi->parent)
		;
	fname = pi ? pi->plaintextfile : mimeview->file;
#else
	fname = mimeview->file;
#endif /* USE_GPGME */
	if (!fname) return;

	if ((fp = fopen(fname, "r")) == NULL) {
		FILE_OP_ERROR(fname, "fopen");
		return;
	}

	if (fseek(fp, partinfo->fpos, SEEK_SET) < 0) {
		FILE_OP_ERROR(mimeview->file, "fseek");
		fclose(fp);
		return;
	}

	mimeview_change_view_type(mimeview, MIMEVIEW_TEXT);
	textview_show_part(mimeview->textview, partinfo, fp);

	fclose(fp);
}

static void mimeview_show_image_part(MimeView *mimeview, MimeInfo *partinfo)
{
	gchar *filename;

	if (!partinfo) return;

	filename = procmime_get_tmp_file_name(partinfo);

	if (procmime_get_part(filename, mimeview->file, partinfo) < 0)
		alertpanel_error
			(_("Can't get the part of multipart message."));
	else {
		mimeview_change_view_type(mimeview, MIMEVIEW_IMAGE);
		imageview_show_image(mimeview->imageview, partinfo, filename);
		unlink(filename);
	}

	g_free(filename);
}

static void mimeview_change_view_type(MimeView *mimeview, MimeViewType type)
{
	TextView  *textview  = mimeview->textview;
	ImageView *imageview = mimeview->imageview;

	if (mimeview->type == type) return;

	switch (type) {
	case MIMEVIEW_IMAGE:
		gtk_container_remove(GTK_CONTAINER(mimeview->vbox),
				     GTK_WIDGET_PTR(textview));
		gtk_container_add(GTK_CONTAINER(mimeview->vbox),
				  GTK_WIDGET_PTR(imageview));
		break;
	case MIMEVIEW_TEXT:
		gtk_container_remove(GTK_CONTAINER(mimeview->vbox),
				     GTK_WIDGET_PTR(imageview));
		gtk_container_add(GTK_CONTAINER(mimeview->vbox),
				  GTK_WIDGET_PTR(textview));
		break;
	default:
		return;
	}

	mimeview->type = type;
}

static void mimeview_clear(MimeView *mimeview)
{
	GtkCList *clist = GTK_CLIST(mimeview->ctree);

	procmime_mimeinfo_free_all(mimeview->mimeinfo);
	mimeview->mimeinfo = NULL;

	gtk_clist_clear(clist);
	textview_clear(mimeview->textview);

	mimeview->opened = NULL;

	g_free(mimeview->file);
	mimeview->file = NULL;
}

static void mimeview_selected(GtkCTree *ctree, GtkCTreeNode *node, gint column,
			      MimeView *mimeview)
{
	MimeInfo *partinfo;

	if (mimeview->opened == node) return;
	mimeview->opened = node;
	gtk_ctree_node_moveto(ctree, node, -1, 0.5, 0);

	partinfo = gtk_ctree_node_get_row_data(ctree, node);
	if (!partinfo) return;

	/* ungrab the mouse event */
	if (GTK_WIDGET_HAS_GRAB(ctree)) {
		gtk_grab_remove(GTK_WIDGET(ctree));
		if (gdk_pointer_is_grabbed())
			gdk_pointer_ungrab(GDK_CURRENT_TIME);
	}

	switch (partinfo->mime_type) {
	case MIME_TEXT:
	case MIME_TEXT_HTML:
	case MIME_MESSAGE_RFC822:
	case MIME_MULTIPART:
		mimeview_show_message_part(mimeview, partinfo);
		break;
#if (HAVE_GDK_PIXBUF || HAVE_GDK_IMLIB)
	case MIME_IMAGE:
		mimeview_show_image_part(mimeview, partinfo);
		break;
#endif
	default:
		mimeview_change_view_type(mimeview, MIMEVIEW_TEXT);
#if USE_GPGME
		if (g_strcasecmp(partinfo->content_type,
				 "application/pgp-signature") == 0)
			textview_show_signature_part(mimeview->textview,
						     partinfo);
		else
#endif
			textview_show_mime_part(mimeview->textview, partinfo);
		break;
	}
}

static void mimeview_start_drag (GtkWidget *widget, gint button,
				 GdkEvent *event, MimeView *mimeview)
{
	GtkTargetList *list;
	GdkDragContext *context;
	MimeInfo *partinfo;
	GtkCTree *ctree;

	g_return_if_fail(mimeview != NULL);
	ctree = GTK_CTREE(mimeview->ctree);
	partinfo = gtk_ctree_node_get_row_data(ctree, mimeview->opened);
	if (partinfo->filename == NULL && partinfo->name == NULL) return;

	list = gtk_target_list_new(mimeview_mime_types, 1);
	context = gtk_drag_begin(widget, list,
				 GDK_ACTION_COPY, button, event);
	gtk_drag_set_icon_default(context);
}

static void mimeview_button_pressed(GtkWidget *widget, GdkEventButton *event,
				    MimeView *mimeview)
{
	GtkCList *clist = GTK_CLIST(widget);
	MimeInfo *partinfo;
	gint row, column;

	if (!event) return;

	if (event->button == 2 || event->button == 3) {
		if (!gtk_clist_get_selection_info(clist, event->x, event->y,
						  &row, &column))
			return;
		gtk_clist_unselect_all(clist);
		gtk_clist_select_row(clist, row, column);
		gtkut_clist_set_focus_row(clist, row);
	}

	if (event->button == 2 ||
	    (event->button == 1 && event->type == GDK_2BUTTON_PRESS)) {
		/* call external program for image, audio or html */
		mimeview_launch(mimeview);
	} else if (event->button == 3) {
		partinfo = gtk_ctree_node_get_row_data
			(GTK_CTREE(mimeview->ctree), mimeview->opened);
		if (partinfo && (partinfo->mime_type == MIME_TEXT ||
				 partinfo->mime_type == MIME_TEXT_HTML ||
				 partinfo->mime_type == MIME_MESSAGE_RFC822 ||
				 partinfo->mime_type == MIME_IMAGE ||
				 partinfo->mime_type == MIME_MULTIPART))
			menu_set_sensitive(mimeview->popupfactory,
					   "/Display as text", FALSE);
		else
			menu_set_sensitive(mimeview->popupfactory,
					   "/Display as text", TRUE);
		if (partinfo &&
		    partinfo->mime_type == MIME_APPLICATION_OCTET_STREAM)
			menu_set_sensitive(mimeview->popupfactory,
					   "/Open", FALSE);
		else
			menu_set_sensitive(mimeview->popupfactory,
					   "/Open", TRUE);
#if USE_GPGME
		menu_set_sensitive(mimeview->popupfactory,
				   "/Check signature",
				   mimeview_is_signed(mimeview));
#endif

		gtk_menu_popup(GTK_MENU(mimeview->popupmenu),
			       NULL, NULL, NULL, NULL,
			       event->button, event->time);
	}
}

#define BREAK_ON_MODIFIER_KEY() \
	if ((event->state & (GDK_MOD1_MASK|GDK_CONTROL_MASK)) != 0) break

static void mimeview_key_pressed(GtkWidget *widget, GdkEventKey *event,
				 MimeView *mimeview)
{
	SummaryView *summaryview;
	GtkCTree *ctree = GTK_CTREE(mimeview->ctree);
	GtkCTreeNode *node;

	if (!event) return;
	if (!mimeview->opened) return;

	switch (event->keyval) {
	case GDK_space:
		if (textview_scroll_page(mimeview->textview, FALSE)) return;

		node = GTK_CTREE_NODE_NEXT(mimeview->opened);
		if (node) {
			gtk_sctree_unselect_all(GTK_SCTREE(ctree));
			gtk_sctree_select(GTK_SCTREE(ctree), node);
			return;
		}
		break;
	case GDK_BackSpace:
	case GDK_Delete:
		textview_scroll_page(mimeview->textview, TRUE);
		return;
	case GDK_Return:
		textview_scroll_one_line(mimeview->textview,
					 (event->state & GDK_MOD1_MASK) != 0);
		return;
	case GDK_n:
	case GDK_N:
		BREAK_ON_MODIFIER_KEY();
		if (!GTK_CTREE_NODE_NEXT(mimeview->opened)) break;

		gtk_signal_emit_by_name(GTK_OBJECT(ctree), "scroll_vertical",
					GTK_SCROLL_STEP_FORWARD, 0.0);
		return;
	case GDK_p:
	case GDK_P:
		BREAK_ON_MODIFIER_KEY();
		if (!GTK_CTREE_NODE_PREV(mimeview->opened)) break;

		gtk_signal_emit_by_name(GTK_OBJECT(ctree), "scroll_vertical",
					GTK_SCROLL_STEP_BACKWARD, 0.0);
		return;
	case GDK_y:
		BREAK_ON_MODIFIER_KEY();
		mimeview_save_as(mimeview);
		return;
	case GDK_t:
		BREAK_ON_MODIFIER_KEY();
		mimeview_display_as_text(mimeview);
		return;
	case GDK_l:
		BREAK_ON_MODIFIER_KEY();
		mimeview_launch(mimeview);
		return;
	default:
		break;
	}

	if (!mimeview->messageview->mainwin) return;
	summaryview = mimeview->messageview->mainwin->summaryview;
	summary_pass_key_press_event(summaryview, event);
}

static void mimeview_drag_data_get(GtkWidget	    *widget,
				   GdkDragContext   *drag_context,
				   GtkSelectionData *selection_data,
				   guint	     info,
				   guint	     time,
				   MimeView	    *mimeview)
{
	gchar *filename, *uriname;
	MimeInfo *partinfo;

	if (!mimeview->opened) return;
	if (!mimeview->file) return;

	partinfo = gtk_ctree_node_get_row_data
		(GTK_CTREE(mimeview->ctree), mimeview->opened);
	if (!partinfo) return;
	if (!partinfo->filename && !partinfo->name) return;

	filename = partinfo->filename ? partinfo->filename : partinfo->name;
	filename = g_basename(filename);
	if (*filename == '\0') return;

	filename = g_strconcat(get_mime_tmp_dir(), G_DIR_SEPARATOR_S,
			       filename, NULL);

	if (procmime_get_part(filename, mimeview->file, partinfo) < 0)
		alertpanel_error
			(_("Can't save the part of multipart message."));

	uriname = g_strconcat("file:/", filename, NULL);
	gtk_selection_data_set(selection_data, selection_data->target, 8,
			       uriname, strlen(uriname));

	g_free(uriname);
	g_free(filename);
}

static void mimeview_display_as_text(MimeView *mimeview)
{
	MimeInfo *partinfo;

	if (!mimeview->opened) return;

	partinfo = gtk_ctree_node_get_row_data
		(GTK_CTREE(mimeview->ctree), mimeview->opened);
	mimeview_show_message_part(mimeview, partinfo);
}

static void mimeview_save_as(MimeView *mimeview)
{
	gchar *filename;
	MimeInfo *partinfo;

	if (!mimeview->opened) return;
	if (!mimeview->file) return;

	partinfo = gtk_ctree_node_get_row_data
		(GTK_CTREE(mimeview->ctree), mimeview->opened);
	g_return_if_fail(partinfo != NULL);

	filename = filesel_select_file
		(_("Save as"),
		 partinfo->filename ? partinfo->filename : partinfo->name);
	if (!filename) return;
	if (is_file_exist(filename)) {
		AlertValue aval;

		aval = alertpanel(_("Overwrite"),
				  _("Overwrite existing file?"),
				  _("OK"), _("Cancel"), NULL);
		if (G_ALERTDEFAULT != aval) return;
	}

	if (procmime_get_part(filename, mimeview->file, partinfo) < 0)
		alertpanel_error
			(_("Can't save the part of multipart message."));
}

static void mimeview_launch(MimeView *mimeview)
{
	MimeInfo *partinfo;
	gchar *filename;

	if (!mimeview->opened) return;
	if (!mimeview->file) return;

	partinfo = gtk_ctree_node_get_row_data
		(GTK_CTREE(mimeview->ctree), mimeview->opened);
	g_return_if_fail(partinfo != NULL);

	filename = procmime_get_tmp_file_name(partinfo);

	if (procmime_get_part(filename, mimeview->file, partinfo) < 0)
		alertpanel_error
			(_("Can't save the part of multipart message."));
	else
		mimeview_view_file(filename, partinfo, NULL);

	g_free(filename);
}

static void mimeview_open_with(MimeView *mimeview)
{
	MimeInfo *partinfo;
	gchar *filename;

	if (!mimeview->opened) return;
	if (!mimeview->file) return;

	partinfo = gtk_ctree_node_get_row_data
		(GTK_CTREE(mimeview->ctree), mimeview->opened);
	g_return_if_fail(partinfo != NULL);

	filename = procmime_get_tmp_file_name(partinfo);

	if (procmime_get_part(filename, mimeview->file, partinfo) < 0)
		alertpanel_error
			(_("Can't save the part of multipart message."));
	else {
		gchar *cmd;

		cmd = input_dialog
			(_("Open with"),
			 _("Enter the command line to open file:\n"
			   "(`%s' will be replaced with file name)"),
			 "gedit \"%s\"");
		if (cmd) {
			mimeview_view_file(filename, partinfo, cmd);
			g_free(cmd);
		}
	}

	g_free(filename);
}

static void mimeview_view_file(const gchar *filename, MimeInfo *partinfo,
			       const gchar *cmdline)
{
	static gchar *default_image_cmdline = "display '%s'";
	static gchar *default_audio_cmdline = "play '%s'";
	static gchar *default_html_cmdline =
		"netscape -remote 'openURL(%s,raise)'";
	static gchar *mime_cmdline = "metamail -d -b -x -c %s '%s'";
	gchar buf[1024];
	gchar m_buf[1024];
	const gchar *cmd;
	const gchar *def_cmd;
	const gchar *p;

	if (cmdline) {
		cmd = cmdline;
		def_cmd = NULL;
	} else if (MIME_APPLICATION_OCTET_STREAM == partinfo->mime_type) {
		return;
	} else if (MIME_IMAGE == partinfo->mime_type) {
		cmd = prefs_common.mime_image_viewer;
		def_cmd = default_image_cmdline;
	} else if (MIME_AUDIO == partinfo->mime_type) {
		cmd = prefs_common.mime_audio_player;
		def_cmd = default_audio_cmdline;
	} else if (MIME_TEXT_HTML == partinfo->mime_type) {
		cmd = prefs_common.uri_cmd;
		def_cmd = default_html_cmdline;
	} else {
		g_snprintf(m_buf, sizeof(m_buf), mime_cmdline,
			   partinfo->content_type, "%s");
		cmd = m_buf;
		def_cmd = NULL;
	}

	if (cmd && (p = strchr(cmd, '%')) && *(p + 1) == 's' &&
	    !strchr(p + 2, '%'))
		g_snprintf(buf, sizeof(buf), cmd, filename);
	else {
		if (cmd)
			g_warning(_("MIME viewer command line is invalid: `%s'"), cmd);
		if (def_cmd)
			g_snprintf(buf, sizeof(buf), def_cmd, filename);
		else
			return;
	}

	execute_command_line(buf);
}

#if USE_GPGME
static void update_node_name(GtkCTree *ctree, GtkCTreeNode *node,
			     gpointer data)
{
	MimeInfo *partinfo;
	gchar *part_name;

	partinfo = gtk_ctree_node_get_row_data(ctree, node);
	g_return_if_fail(partinfo != NULL);

	part_name = get_part_name(partinfo);
	gtk_ctree_node_set_text(ctree, node, COL_NAME, part_name);
}

static void mimeview_update_names(MimeView *mimeview)
{
	GtkCTree *ctree = GTK_CTREE(mimeview->ctree);

	gtk_ctree_pre_recursive(ctree, NULL, update_node_name, NULL);
}

static void mimeview_update_signature_info(MimeView *mimeview)
{
	MimeInfo *partinfo;

	if (!mimeview) return;
	if (!mimeview->opened) return;

	partinfo = gtk_ctree_node_get_row_data
		(GTK_CTREE(mimeview->ctree), mimeview->opened);
	if (!partinfo) return;

	if (g_strcasecmp(partinfo->content_type,
			 "application/pgp-signature") == 0) {
		mimeview_change_view_type(mimeview, MIMEVIEW_TEXT);
		textview_show_signature_part(mimeview->textview, partinfo);
	}
}

static void mimeview_check_signature(MimeView *mimeview)
{
	MimeInfo *mimeinfo;
	gchar buf[BUFFSIZE];
	FILE *fp;

	g_return_if_fail (mimeview_is_signed(mimeview));

	mimeinfo = gtk_ctree_node_get_row_data
		(GTK_CTREE(mimeview->ctree), mimeview->opened);
	g_return_if_fail(mimeinfo != NULL);
	g_return_if_fail(mimeview->file != NULL);

	while (mimeinfo->parent)
		mimeinfo = mimeinfo->parent;

	if ((fp = fopen(mimeview->file, "r")) == NULL) {
		FILE_OP_ERROR(mimeview->file, "fopen");
		return;
	}

	/* skip headers */
	if (mimeinfo->mime_type == MIME_MULTIPART) {
		if (fseek(fp, mimeinfo->fpos, SEEK_SET) < 0)
		FILE_OP_ERROR(mimeview->file, "fseek");
		while (fgets(buf, sizeof(buf), fp) != NULL)
			if (buf[0] == '\r' || buf[0] == '\n') break;
	}

	procmime_scan_multipart_message(mimeinfo, fp);
	rfc2015_check_signature(mimeinfo, fp);
	fclose(fp);

	mimeview_update_names(mimeview);
	mimeview_update_signature_info(mimeview);
}
#endif /* USE_GPGME */
