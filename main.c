/*
 * Copyright (c) 2014 gnome-mpv
 *
 * This file is part of GNOME MPV.
 *
 * GNOME MPV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GNOME MPV is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNOME MPV.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gdk/gdkx.h>
#include <pthread.h>
#include <stdlib.h>

#include "def.h"
#include "common.h"
#include "mpv.h"
#include "playlist.h"
#include "main_window.h"
#include "main_menu_bar.h"
#include "control_box.h"
#include "playlist_widget.h"
#include "pref_dialog.h"
#include "open_loc_dialog.h"

static void destroy_handler(GtkWidget *widget, gpointer data);
static void open_handler(GtkWidget *widget, gpointer data);
static void open_loc_handler(GtkWidget *widget, gpointer data);
static void pref_handler(GtkWidget *widget, gpointer data);
static void play_handler(GtkWidget *widget, gpointer data);
static void stop_handler(GtkWidget *widget, gpointer data);
static void forward_handler(GtkWidget *widget, gpointer data);
static void rewind_handler(GtkWidget *widget, gpointer data);
static void chapter_previous_handler(GtkWidget *widget, gpointer data);
static void chapter_next_handler(GtkWidget *widget, gpointer data);
static void fullscreen_handler(GtkWidget *widget, gpointer data);
static void normal_size_handler(GtkWidget *widget, gpointer data);
static void double_size_handler(GtkWidget *widget, gpointer data);
static void half_size_handler(GtkWidget *widget, gpointer data);
static void about_handler(GtkWidget *widget, gpointer data);
static void volume_handler(GtkWidget *widget, gpointer data);

static void drag_data_handler(	GtkWidget *widget,
				GdkDragContext *context,
				gint x,
				gint y,
				GtkSelectionData *sel_data,
				guint info,
				guint time,
				gpointer data);

static void seek_handler(	GtkWidget *widget,
				GtkScrollType scroll,
				gdouble value,
				gpointer data );

static gboolean key_press_handler(	GtkWidget *widget,
					GdkEvent *event,
					gpointer data );

static void destroy_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = data;

	pthread_mutex_lock(ctx->mpv_event_mutex);

	ctx->exit_flag = TRUE;

	pthread_mutex_unlock(ctx->mpv_event_mutex);

	pthread_join(*(ctx->mpv_event_handler_thread), NULL);

	pthread_mutex_destroy(ctx->mpv_event_mutex);
	pthread_cond_destroy(ctx->mpv_ctx_init_cv);
	pthread_cond_destroy(ctx->mpv_ctx_destroy_cv);

	mpv_terminate_destroy(ctx->mpv_ctx);
	g_key_file_free(ctx->config_file);

	gtk_main_quit();
}

static void open_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = (gmpv_handle*)data;
	GtkFileChooser *file_chooser;
	GtkWidget *open_dialog;

	open_dialog
		= gtk_file_chooser_dialog_new(	"Open File",
						GTK_WINDOW(ctx->gui),
						GTK_FILE_CHOOSER_ACTION_OPEN,
						"_Cancel",
						GTK_RESPONSE_CANCEL,
						"_Open",
						GTK_RESPONSE_ACCEPT,
						NULL );

	file_chooser = GTK_FILE_CHOOSER(open_dialog);

	gtk_file_chooser_set_select_multiple(file_chooser, TRUE);

	if(gtk_dialog_run(GTK_DIALOG(open_dialog)) == GTK_RESPONSE_ACCEPT)
	{
		GSList *uri_list = gtk_file_chooser_get_filenames(file_chooser);
		GSList *uri = uri_list;

		ctx->paused = FALSE;

		while(uri)
		{
			mpv_load(ctx, uri->data, (uri != uri_list), TRUE);

			uri = g_slist_next(uri);
		}

		g_slist_free_full(uri_list, g_free);
	}

	gtk_widget_destroy(open_dialog);
}

static void open_loc_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = (gmpv_handle*)data;
	OpenLocDialog *open_loc_dialog;

	open_loc_dialog
		= OPEN_LOC_DIALOG(open_loc_dialog_new(GTK_WINDOW(ctx->gui)));

	if(gtk_dialog_run(GTK_DIALOG(open_loc_dialog)) == GTK_RESPONSE_ACCEPT)
	{
		const gchar *loc_str;

		loc_str = open_loc_dialog_get_string(open_loc_dialog);

		ctx->paused = FALSE;

		mpv_load(ctx, loc_str, FALSE, TRUE);
	}

	gtk_widget_destroy(GTK_WIDGET(open_loc_dialog));
}

static void pref_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = data;
	PrefDialog *pref_dialog;
	gchar *buffer;
	const gchar *quit_cmd[] = {"quit_watch_later", NULL};

	load_config(ctx);

	buffer = get_config_string(ctx, "main", "mpv-options");

	pref_dialog = PREF_DIALOG(pref_dialog_new(GTK_WINDOW(ctx->gui)));

	if(buffer)
	{
		pref_dialog_set_string(pref_dialog, buffer);

		g_free(buffer);
	}

	if(gtk_dialog_run(GTK_DIALOG(pref_dialog)) == GTK_RESPONSE_ACCEPT)
	{
		gint64 playlist_pos;
		gdouble time_pos;
		gint playlist_pos_rc;
		gint time_pos_rc;

		set_config_string(	ctx,
					"main",
					"mpv-options",
					pref_dialog_get_string(pref_dialog) );

		save_config(ctx);

		mpv_check_error(mpv_set_property_string(	ctx->mpv_ctx,
								"pause",
								"yes" ));

		playlist_pos_rc = mpv_get_property(	ctx->mpv_ctx,
							"playlist-pos",
							MPV_FORMAT_INT64,
							&playlist_pos );

		time_pos_rc = mpv_get_property(	ctx->mpv_ctx,
						"time-pos",
						MPV_FORMAT_DOUBLE,
						&time_pos );

		/* Suspend mpv_event_handler loop */
		pthread_mutex_lock(ctx->mpv_event_mutex);

		ctx->mpv_ctx_reset = TRUE;

		pthread_cond_wait(	ctx->mpv_ctx_destroy_cv,
					ctx->mpv_event_mutex );

		pthread_mutex_unlock(ctx->mpv_event_mutex);

		/* Reset ctx->mpv_ctx */
		mpv_check_error(mpv_command(ctx->mpv_ctx, quit_cmd));

		mpv_detach_destroy(ctx->mpv_ctx);

		ctx->mpv_ctx = mpv_create();

		mpv_init(ctx, ctx->vid_area_wid);

		/* Wake up mpv_event_handler loop */
		pthread_mutex_lock(ctx->mpv_event_mutex);

		ctx->mpv_ctx_reset = FALSE;

		pthread_cond_signal(ctx->mpv_ctx_init_cv);

		pthread_mutex_unlock(ctx->mpv_event_mutex);

		if(ctx->playlist_store)
		{
			gint rc;

			rc = mpv_request_event(	ctx->mpv_ctx,
						MPV_EVENT_FILE_LOADED,
						0 );

			mpv_check_error(rc);

			mpv_load(ctx, NULL, FALSE, TRUE);

			rc = mpv_request_event(	ctx->mpv_ctx,
						MPV_EVENT_FILE_LOADED,
						1 );

			mpv_check_error(rc);

			if(playlist_pos_rc >= 0 && playlist_pos > 0)
			{
				mpv_set_property(	ctx->mpv_ctx,
							"playlist-pos",
							MPV_FORMAT_INT64,
							&playlist_pos );
			}

			if(time_pos_rc >= 0 && time_pos > 0)
			{
				mpv_set_property(	ctx->mpv_ctx,
							"time-pos",
							MPV_FORMAT_DOUBLE,
							&time_pos );
			}

			mpv_set_property(	ctx->mpv_ctx,
						"pause",
						MPV_FORMAT_FLAG,
						&ctx->paused );
		}
	}

	gtk_widget_destroy(GTK_WIDGET(pref_dialog));
}

static void play_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = data;
	gboolean loaded;

	pthread_mutex_lock(ctx->mpv_event_mutex);

	ctx->paused = !ctx->paused;
	loaded = ctx->loaded;

	pthread_mutex_unlock(ctx->mpv_event_mutex);

	if(!loaded)
	{
		mpv_load(ctx, NULL, FALSE, TRUE);
	}
	else
	{
		mpv_check_error(mpv_set_property(	ctx->mpv_ctx,
							"pause",
							MPV_FORMAT_FLAG,
							&ctx->paused ));
	}

	control_box_set_playing_state
		(CONTROL_BOX(ctx->gui->control_box), !ctx->paused);
}

static void stop_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = (gmpv_handle *)data;
	const gchar *seek_cmd[] = {"seek", "0", "absolute", NULL};

	mpv_check_error(mpv_set_property_string(ctx->mpv_ctx, "pause", "yes"));
	mpv_check_error(mpv_command(ctx->mpv_ctx, seek_cmd));

	ctx->paused = TRUE;

	control_reset(ctx);
	update_seek_bar(ctx);
}

static void forward_handler(GtkWidget *widget, gpointer data)
{
	seek_relative(data, 10);
}

static void rewind_handler(GtkWidget *widget, gpointer data)
{
	seek_relative(data, -10);
}

static void chapter_previous_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = (gmpv_handle *)data;
	const gchar *cmd[] = {"osd-msg", "cycle", "chapter", "down", NULL};

	if(!ctx->loaded)
	{
		mpv_load(ctx, NULL, FALSE, TRUE);
	}

	mpv_check_error(mpv_command(ctx->mpv_ctx, cmd));
}

static void chapter_next_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = (gmpv_handle *)data;
	const gchar *cmd[] = {"osd-msg", "cycle", "chapter", NULL};

	if(!ctx->loaded)
	{
		mpv_load(ctx, NULL, FALSE, TRUE);
	}

	mpv_check_error(mpv_command(ctx->mpv_ctx, cmd));
}

static void fullscreen_handler(GtkWidget *widget, gpointer data)
{
	main_window_toggle_fullscreen(((gmpv_handle *)data)->gui);
}

static void normal_size_handler(GtkWidget *widget, gpointer data)
{
	resize_window_to_fit((gmpv_handle *)data, 1);
}

static void double_size_handler(GtkWidget *widget, gpointer data)
{
	resize_window_to_fit((gmpv_handle *)data, 2);
}

static void half_size_handler(GtkWidget *widget, gpointer data)
{
	resize_window_to_fit((gmpv_handle *)data, 0.5);
}

static void about_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = (gmpv_handle*)data;

	gtk_show_about_dialog(	GTK_WINDOW(ctx->gui),
				"logo-icon-name",
				ICON_NAME,
				"version",
				APP_VERSION,
				"comments",
				APP_DESC,
				"license-type",
				GTK_LICENSE_GPL_3_0,
				NULL );
}

static void volume_handler(GtkWidget *widget, gpointer data)
{
	gmpv_handle *ctx = data;
	gdouble value;

	g_object_get(widget, "value", &value, NULL);

	value *= 100;

	mpv_set_property(ctx->mpv_ctx, "volume", MPV_FORMAT_DOUBLE, &value);
}

static void drag_data_handler(	GtkWidget *widget,
				GdkDragContext *context,
				gint x,
				gint y,
				GtkSelectionData *sel_data,
				guint info,
				guint time,
				gpointer data)
{
	gboolean append = (widget == ((gmpv_handle *)data)->gui->playlist);

	if(sel_data && gtk_selection_data_get_length(sel_data) > 0)
	{
		gmpv_handle *ctx = data;
		gchar **uri_list = gtk_selection_data_get_uris(sel_data);

		ctx->paused = FALSE;

		if(uri_list)
		{
			int i;

			for(i = 0; uri_list[i]; i++)
			{
				mpv_load(	ctx,
						uri_list[i],
						(append || i != 0),
						TRUE );
			}

			g_strfreev(uri_list);
		}
		else
		{
			const guchar *raw_data
				= gtk_selection_data_get_data(sel_data);

			mpv_load(ctx, (const gchar *)raw_data, append, TRUE);
		}
	}
}

static void seek_handler(	GtkWidget *widget,
				GtkScrollType scroll,
				gdouble value,
				gpointer data )
{
	gmpv_handle *ctx = data;
	const gchar *cmd[] = {"seek", NULL, "absolute", NULL};
	gchar *value_str = g_strdup_printf("%.2f", value);

	cmd[1] = value_str;

	if(!ctx->loaded)
	{
		mpv_load(ctx, NULL, FALSE, TRUE);
	}

	mpv_check_error(mpv_command(ctx->mpv_ctx, cmd));

	g_free(value_str);
}

static gboolean key_press_handler(	GtkWidget *widget,
					GdkEvent *event,
					gpointer data )
{
	gmpv_handle *ctx = data;
	guint keyval = ((GdkEventKey*)event)->keyval;
	guint state = ((GdkEventKey*)event)->state;

	const guint mod_mask =	GDK_MODIFIER_MASK
				&~(GDK_SHIFT_MASK
				|GDK_LOCK_MASK
				|GDK_MOD2_MASK
				|GDK_MOD3_MASK
				|GDK_MOD4_MASK
				|GDK_MOD5_MASK);

	/* Make sure that no modifier key is active except certain keys like
	 * caps lock.
	 */
	if((state&mod_mask) == 0)
	{
		/* Accept F11 and f for entering/exiting fullscreen mode. ESC is
		 * only used for exiting fullscreen mode.
		 */
		if((ctx->gui->fullscreen && keyval == GDK_KEY_Escape)
		|| keyval == GDK_KEY_F11
		|| keyval == GDK_KEY_f)
		{
			fullscreen_handler(NULL, ctx);
		}
		else if(keyval == GDK_KEY_Delete
		&& main_window_get_playlist_visible(ctx->gui))
		{
			const gchar *cmd[] = {"playlist_remove", NULL, NULL};
			PlaylistWidget *playlist;
			GtkTreePath *path;
			gchar *index_str;
			gint index;

			playlist = PLAYLIST_WIDGET(ctx->gui->playlist);

			gtk_tree_view_get_cursor
				(	GTK_TREE_VIEW(playlist->tree_view),
					&path,
					NULL );

			index = gtk_tree_path_get_indices(path)[0];
			index_str = g_strdup_printf("%d", index);
			cmd[1] = index_str;

			g_signal_handlers_block_matched
				(	playlist->list_store,
					G_SIGNAL_MATCH_DATA,
					0,
					0,
					NULL,
					NULL,
					ctx );

			playlist_widget_remove(playlist, index);
			mpv_check_error(mpv_command(ctx->mpv_ctx, cmd));

			g_signal_handlers_unblock_matched
				(	playlist->list_store,
					G_SIGNAL_MATCH_DATA,
					0,
					0,
					NULL,
					NULL,
					ctx );

			g_free(index_str);
		}
		else if(keyval == GDK_KEY_v)
		{
			const gchar *cmd[] = {	"osd-msg",
						"cycle",
						"sub-visibility",
						NULL };

			mpv_command(ctx->mpv_ctx, cmd);
		}
		else if(keyval == GDK_KEY_s)
		{
			const gchar *cmd[] = {	"osd-msg",
						"screenshot",
						NULL };

			mpv_command(ctx->mpv_ctx, cmd);
		}
		else if(keyval == GDK_KEY_S)
		{
			const gchar *cmd[] = {	"osd-msg",
						"screenshot",
						"video",
						NULL };

			mpv_command(ctx->mpv_ctx, cmd);
		}
		else if(keyval == GDK_KEY_j)
		{
			const gchar *cmd[] = {	"osd-msg",
						"cycle",
						"sub",
						NULL };

			mpv_command(ctx->mpv_ctx, cmd);
		}
		else if(keyval == GDK_KEY_J)
		{
			const gchar *cmd[] = {	"osd-msg",
						"cycle",
						"sub",
						"down",
						NULL };

			mpv_command(ctx->mpv_ctx, cmd);
		}
		else if(keyval == GDK_KEY_Left)
		{
			seek_relative(ctx, -10);
		}
		else if(keyval == GDK_KEY_Right)
		{
			seek_relative(ctx, 10);
		}
		else if(keyval == GDK_KEY_Up)
		{
			seek_relative(ctx, 60);
		}
		else if(keyval == GDK_KEY_Down)
		{
			seek_relative(ctx, -60);
		}
		else if(keyval == GDK_KEY_space || keyval == GDK_KEY_p)
		{
			play_handler(NULL, ctx);
		}
		else if(keyval == GDK_KEY_U)
		{
			stop_handler(NULL, ctx);
		}
		else if(keyval == GDK_KEY_exclam)
		{
			chapter_previous_handler(NULL, ctx);
		}
		else if(keyval == GDK_KEY_at)
		{
			chapter_next_handler(NULL, ctx);
		}
		else if(keyval == GDK_KEY_less)
		{
			playlist_previous_handler(NULL, ctx);
		}
		else if(keyval == GDK_KEY_greater)
		{
			playlist_next_handler(NULL, ctx);
		}
	}

	return FALSE;
}

int main(int argc, char **argv)
{
	gmpv_handle ctx;
	MainMenuBar *menu;
	ControlBox *control_box;
	PlaylistWidget *playlist;
	GtkTargetEntry target_entry[3];
	pthread_t mpv_event_handler_thread;
	pthread_mutex_t mpv_event_mutex;
	pthread_cond_t mpv_ctx_init_cv;
	pthread_cond_t mpv_ctx_destroy_cv;

	gtk_init(&argc, &argv);
	g_set_application_name(APP_NAME);
	gtk_window_set_default_icon_name(ICON_NAME);

	ctx.mpv_ctx = mpv_create();
	ctx.exit_flag = FALSE;
	ctx.mpv_ctx_reset = FALSE;
	ctx.paused = TRUE;
	ctx.loaded = FALSE;
	ctx.new_file = TRUE;
	ctx.sub_visible = TRUE;
	ctx.log_buffer = NULL;
	ctx.config_file = g_key_file_new();
	ctx.gui = MAIN_WINDOW(main_window_new());
	ctx.playlist_store = PLAYLIST_WIDGET(ctx.gui->playlist)->list_store;
	ctx.mpv_event_handler_thread = &mpv_event_handler_thread;
	ctx.mpv_ctx_init_cv = &mpv_ctx_init_cv;
	ctx.mpv_ctx_destroy_cv = &mpv_ctx_destroy_cv;
	ctx.mpv_event_mutex = &mpv_event_mutex;
	ctx.mpv_event_mutex = &mpv_event_mutex;

	ctx.vid_area_wid = gdk_x11_window_get_xid
				(gtk_widget_get_window(ctx.gui->vid_area));

	menu = MAIN_MENU(ctx.gui->menu);
	control_box = CONTROL_BOX(ctx.gui->control_box);
	playlist = PLAYLIST_WIDGET(ctx.gui->playlist);

	target_entry[0].target = "text/uri-list";
	target_entry[0].flags = 0;
	target_entry[0].info = 0;
	target_entry[1].target = "text/plain";
	target_entry[1].flags = 0;
	target_entry[1].info = 1;
	target_entry[2].target = "STRING";
	target_entry[2].flags = 0;
	target_entry[2].info = 1;

	pthread_mutex_init(ctx.mpv_event_mutex, NULL);
	pthread_cond_init(ctx.mpv_ctx_init_cv, NULL);
	pthread_cond_init(ctx.mpv_ctx_destroy_cv, NULL);

	gtk_drag_dest_set(	GTK_WIDGET(ctx.gui->vid_area),
				GTK_DEST_DEFAULT_ALL,
				target_entry,
				3,
				GDK_ACTION_LINK );

	gtk_drag_dest_set(	GTK_WIDGET(ctx.gui->playlist),
				GTK_DEST_DEFAULT_ALL,
				target_entry,
				3,
				GDK_ACTION_COPY );

	gtk_drag_dest_add_uri_targets(GTK_WIDGET(ctx.gui->vid_area));
	gtk_drag_dest_add_uri_targets(GTK_WIDGET(ctx.gui->playlist));

	g_signal_connect(	ctx.gui->vid_area,
				"drag-data-received",
				G_CALLBACK(drag_data_handler),
				&ctx );

	g_signal_connect(	ctx.gui->playlist,
				"drag-data-received",
				G_CALLBACK(drag_data_handler),
				&ctx );

	g_signal_connect(	ctx.gui,
				"destroy",
				G_CALLBACK(destroy_handler),
				&ctx );

	g_signal_connect(	ctx.gui,
				"key-press-event",
				G_CALLBACK(key_press_handler),
				&ctx );

	g_signal_connect(	control_box->play_button,
				"clicked",
				G_CALLBACK(play_handler),
				&ctx );

	g_signal_connect(	control_box->stop_button,
				"clicked",
				G_CALLBACK(stop_handler),
				&ctx );

	g_signal_connect(	control_box->forward_button,
				"clicked",
				G_CALLBACK(forward_handler),
				&ctx );

	g_signal_connect(	control_box->rewind_button,
				"clicked",
				G_CALLBACK(rewind_handler),
				&ctx );

	g_signal_connect(	control_box->previous_button,
				"clicked",
				G_CALLBACK(chapter_previous_handler),
				&ctx );

	g_signal_connect(	control_box->next_button,
				"clicked",
				G_CALLBACK(chapter_next_handler),
				&ctx );

	g_signal_connect(	control_box->fullscreen_button,
				"clicked",
				G_CALLBACK(fullscreen_handler),
				&ctx );

	g_signal_connect(	control_box->volume_button,
				"value-changed",
				G_CALLBACK(volume_handler),
				&ctx );

	g_signal_connect(	menu->open_menu_item,
				"activate",
				G_CALLBACK(open_handler),
				&ctx );

	g_signal_connect(	menu->open_loc_menu_item,
				"activate",
				G_CALLBACK(open_loc_handler),
				&ctx );

	g_signal_connect(	menu->quit_menu_item,
				"activate",
				G_CALLBACK(destroy_handler),
				&ctx );

	g_signal_connect(	menu->pref_menu_item,
				"activate",
				G_CALLBACK(pref_handler),
				&ctx );

	g_signal_connect(	menu->playlist_menu_item,
				"activate",
				G_CALLBACK(playlist_handler),
				&ctx );

	g_signal_connect(	menu->fullscreen_menu_item,
				"activate",
				G_CALLBACK(fullscreen_handler),
				&ctx );

	g_signal_connect(	menu->normal_size_menu_item,
				"activate",
				G_CALLBACK(normal_size_handler),
				&ctx );

	g_signal_connect(	menu->double_size_menu_item,
				"activate",
				G_CALLBACK(double_size_handler),
				&ctx );

	g_signal_connect(	menu->half_size_menu_item,
				"activate",
				G_CALLBACK(half_size_handler),
				&ctx );

	g_signal_connect(	menu->about_menu_item,
				"activate",
				G_CALLBACK(about_handler),
				&ctx );

	g_signal_connect(	playlist->tree_view,
				"row-activated",
				G_CALLBACK(playlist_row_handler),
				&ctx );

	g_signal_connect(	playlist->list_store,
				"row-inserted",
				G_CALLBACK(playlist_row_inserted_handler),
				&ctx );

	g_signal_connect(	playlist->list_store,
				"row-deleted",
				G_CALLBACK(playlist_row_deleted_handler),
				&ctx );

	g_signal_connect(	control_box->seek_bar,
				"change-value",
				G_CALLBACK(seek_handler),
				&ctx );

	mpv_init(&ctx, ctx.vid_area_wid);

	pthread_create(	&mpv_event_handler_thread,
			NULL,
			mpv_event_handler,
			&ctx );

	g_timeout_add(	SEEK_BAR_UPDATE_INTERVAL,
			(GSourceFunc)update_seek_bar,
			&ctx );

	/* Start playing the file given as command line argument, if any */
	if(argc >= 2)
	{
		gint i = 0;

		pthread_mutex_lock(ctx.mpv_event_mutex);

		ctx.paused = FALSE;

		pthread_mutex_unlock(ctx.mpv_event_mutex);

		for(i = 1; i < argc; i++)
		{
			gchar *path = get_name_from_path(argv[i]);

			playlist_widget_append
				(	PLAYLIST_WIDGET(ctx.gui->playlist),
					path,
					argv[i] );

			g_free(path);
		}

		g_idle_add((GSourceFunc)mpv_load_from_ctx, &ctx);
	}
	else
	{
		control_box_set_enabled(control_box, FALSE);
	}

	gtk_main();

	return EXIT_SUCCESS;
}
