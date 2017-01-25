/*
 * MIT/X Consortium License
 *
 * © 2017 Justin Frank
 * © 2013 Jakub Klinkovský
 * © 2009 Sebastian Linke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include <sys/stat.h>
#include <sys/wait.h>

#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <vte/vte.h>

#include "config.h"

static void window_urgency_hint_cb(VteTerminal* vte, gpointer user_data);
static gboolean window_focus_cb(GtkWindow* window);
static void window_title_cb(VteTerminal* vte);
static void increase_font_size(VteTerminal* vte);
static void decrease_font_size(VteTerminal* vte);
static gboolean key_press_cb(VteTerminal* vte, GdkEventKey* event);
static void vte_config(VteTerminal* vte);
static void vte_spawn(VteTerminal* vte, char* working_directory, char* command,
    char** environment);
static gboolean read_config_file(VteTerminal* vte, const char* config_path);
static void window_close(GtkWindow* window, gint status, gpointer user_data);
static void vte_exit_cb(VteTerminal* vte, gint status, gpointer user_data);
static void parse_arguments(int argc, char* argv[], char** command,
    char** directory, gboolean* keep, char** title);
static void signal_handler(int signal);
static void new_window(GtkApplication* app, gchar** argv, gint argc);
static void activate(GApplication* app, gpointer user_data);
static void command_line(GApplication *app,
    GApplicationCommandLine *command_line, gpointer user_data);
static GtkWidget* create_vte_terminal(GtkWindow* window, gboolean keep,
    const char* title);
static void set_geometry_hints(VteTerminal* vte, GdkGeometry* hints);

/* The application is global for use with signal handlers. */
static GApplication *_application = NULL;

/* Callback to set window urgency hint on beep events. */
static void
window_urgency_hint_cb(VteTerminal* vte, gpointer user_data)
{
	(void)user_data;

	gtk_window_set_urgency_hint(
	    GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(vte))), TRUE);
}

/* Callback to unset window urgency hint on focus. */
static gboolean
window_focus_cb(GtkWindow* window)
{
	gtk_window_set_urgency_hint(window, FALSE);
	return FALSE;
}

/* Callback to dynamically change window title. */
static void
window_title_cb(VteTerminal* vte)
{
	gtk_window_set_title(
	    GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(vte))),
	    vte_terminal_get_window_title(vte));
}

static void
increase_font_size(VteTerminal* vte)
{
	PangoFontDescription* font =
	    pango_font_description_copy_static(vte_terminal_get_font(vte));
	pango_font_description_set_size(font,
	    (pango_font_description_get_size(font) / PANGO_SCALE + 1)
		 * PANGO_SCALE);
	vte_terminal_set_font(vte, font);
	pango_font_description_free(font);
}

/* Decrease the font size of vte. */
static void
decrease_font_size(VteTerminal* vte)
{
	PangoFontDescription* font =
	    pango_font_description_copy_static(vte_terminal_get_font(vte));
	const gint size =
	    pango_font_description_get_size(font) / PANGO_SCALE - 1;
	if (size > 0) {
		pango_font_description_set_size(font, size * PANGO_SCALE);
		vte_terminal_set_font(vte, font);
	}
	pango_font_description_free(font);
}

/* Callback to react to key press events. */
static gboolean
key_press_cb(VteTerminal* vte, GdkEventKey* event)
{
	if ((event->state & (MODIFIER)) != (MODIFIER))
		return FALSE;
	switch (gdk_keyval_to_upper(event->keyval)) {
	case GDK_KEY_C:
		vte_terminal_copy_clipboard(vte);
		break;
	case GDK_KEY_V:
		vte_terminal_paste_clipboard(vte);
		return TRUE;
	case GDK_KEY_equal:
		increase_font_size(vte);
		break;
	case GDK_KEY_minus:
		decrease_font_size(vte);
		break;
	}
	return TRUE;
}

/*
 * Reads the config file into the terminal.
 *
 * Returns true on success, false on failure.
 */
static gboolean
read_config_file(VteTerminal* vte, const char* config_path)
{
	GKeyFile* config_file = g_key_file_new();
	if (!g_key_file_load_from_file(config_file, config_path, 0, NULL))
		return FALSE;
	PangoFontDescription* font = pango_font_description_from_string(
	    g_key_file_get_string(config_file, "Font", "font", NULL));
	if (font == NULL)
		return FALSE;
	vte_terminal_set_font(vte, font);
	pango_font_description_free(font);

	GdkRGBA color_fg, color_bg;
	if (!gdk_rgba_parse(&color_fg, g_key_file_get_string(config_file,
		    "Colors", "foreground", NULL)))
		return FALSE;
	if (!gdk_rgba_parse(&color_bg, g_key_file_get_string(config_file,
		"Colors", "background", NULL)))
		return FALSE;

	GdkRGBA color_palette[16];
	for (int i = 0; i < 16; ++i) {
		char key[8];
		snprintf(key, sizeof(key), "color%02x", i);
		if (!gdk_rgba_parse(&color_palette[i],
			g_key_file_get_string(config_file, "Colors", key,
			    NULL)))
			return FALSE;
	}
	vte_terminal_set_colors(vte, &color_fg, &color_bg, color_palette, 16);
	return TRUE;
}

static void
vte_config(VteTerminal* vte)
{
	VteRegex* regex = vte_regex_new_for_search(url_regex,
	    strlen(url_regex), 0, NULL);

	vte_terminal_search_set_regex(vte, regex, 0);
	vte_terminal_search_set_wrap_around(vte, SEARCH_WRAP_AROUND);
	vte_terminal_set_audible_bell(vte, AUDIBLE_BELL);
	vte_terminal_set_cursor_shape(vte, CURSOR_SHAPE);
	vte_terminal_set_cursor_blink_mode(vte, CURSOR_BLINK);
	vte_terminal_set_word_char_exceptions(vte, WORD_CHARS);
	vte_terminal_set_scrollback_lines(vte, SCROLLBACK_LINES);

	char* config_dir = g_strconcat(g_get_user_config_dir(), "/miniterm",
	    NULL);
	char* config_path = g_strconcat(config_dir, "/miniterm.conf", NULL);

	GKeyFile* config_file = g_key_file_new();
	if (g_key_file_load_from_file(config_file, config_path, 0, NULL)) {
		read_config_file(vte, config_path);
	} else {
		mkdir(config_dir, 0777);
		FILE* file = fopen(config_path, "w");
		if (file) {
			fprintf(file, "[Font]\n#font=\n\n"
			    "[Colors]\n#foreground=\n#background=\n"
			    "#color00=\n#color01=\n#color02=\n#color03=\n"
			    "#color04=\n#color05=\n#color06=\n#color07=\n"
			    "#color08=\n#color09=\n#color0a=\n#color0b=\n"
			    "#color0c=\n#color0d=\n#color0e=\n#color0f=\n");
			fclose(file);
		}
	}
	g_free(config_dir);
	g_free(config_path);
	g_key_file_free(config_file);
}

static void
vte_spawn(VteTerminal* vte, char* working_directory, char* command,
    char** environment)
{
	GError* error = NULL;
	char** command_argv = NULL;

	/* Parse command into array */
	if (!command)
		command = vte_get_user_shell();
	g_shell_parse_argv(command, NULL, &command_argv, &error);
	if (error) {
		g_printerr("Failed to parse command: %s\n", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
	}

	/* Create pty object */
	VtePty* pty = vte_terminal_pty_new_sync(vte, VTE_PTY_NO_HELPER, NULL,
	    &error);
	if (error) {
		g_printerr("Failed to create pty: %s\n", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
	}
	vte_terminal_set_pty(vte, pty);

	int child_pid;

	/* Spawn default shell (or specified command) */
	g_spawn_async(working_directory, command_argv, environment,
			(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN),  // flags from GSpawnFlags
			(GSpawnChildSetupFunc)vte_pty_child_setup, // an extra child setup function to run in the child just before exec()
			pty,          // user data for child_setup
			&child_pid,   // a location to store the child PID
			&error);      // return location for a GError
	if (error) {
		g_printerr("%s\n", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
	}
	vte_terminal_watch_child(vte, child_pid);
	g_strfreev(command_argv);
}

/* Callback to exit TinyTerm with exit status of child process. */
static void
window_close(GtkWindow* window, gint status, gpointer user_data)
{
	(void)window;
	(void)status;

	GtkApplication* app = (GtkApplication*)user_data;

	int count = 0;
	GList* windows = gtk_application_get_windows(app);

	while (windows != NULL) {
		windows = windows->next;
		++count;
	}

	if (count == 1)
		g_application_quit(G_APPLICATION(app));
}

static void
vte_exit_cb(VteTerminal* vte, gint status, gpointer user_data)
{
	(void)vte;
	(void)status;

	gtk_window_close(GTK_WINDOW(user_data));
}

static void
parse_arguments(int argc, char* argv[], char** command, char** directory,
    gboolean* keep, char** title)
{
	gboolean version = FALSE; /* Show version? */
	const GOptionEntry entries[] = {
		{"version",   'v', 0, G_OPTION_ARG_NONE,    &version,   "Display program version and exit.", 0},
		{"execute",   'e', 0, G_OPTION_ARG_STRING,  command,    "Execute command instead of default shell.", "COMMAND"},
		{"directory", 'd', 0, G_OPTION_ARG_STRING,  directory,  "Sets the working directory for the shell (or the command specified via -e).", "PATH"},
		{"keep",      'k', 0, G_OPTION_ARG_NONE,    keep,       "Don't exit the terminal after child process exits.", 0},
		{"title",     't', 0, G_OPTION_ARG_STRING,  title,      "Set value of WM_NAME property; disables window_title_cb (default: 'TinyTerm')", "TITLE"},
		{ NULL }
	};

	GError* error = NULL;
	GOptionContext* context = g_option_context_new(NULL);
	g_option_context_set_help_enabled(context, TRUE);
	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_parse(context, &argc, &argv, &error);
	g_option_context_free(context);

	if (error) {
		g_printerr("option parsing failed: %s\n", error->message);
		g_error_free(error);
		exit(EXIT_FAILURE);
	}

	if (version) {
		g_print("tinyterm " MINITERM_VERSION "\n");
		exit(EXIT_SUCCESS);
	}
}

static void
signal_handler(int signal)
{
	(void)signal;

	g_application_quit(_application);
}

static GtkWidget*
create_vte_terminal(GtkWindow* window, gboolean keep, const char* title)
{
	GtkWidget* vte_widget = vte_terminal_new();
	VteTerminal* vte = VTE_TERMINAL(vte_widget);
	if (!keep)
		g_signal_connect(vte, "child-exited",
		    G_CALLBACK(vte_exit_cb), window);
	g_signal_connect(vte, "key-press-event", G_CALLBACK (key_press_cb),
	    NULL);
#ifdef URGENT_ON_BELL
	g_signal_connect(vte, "bell", G_CALLBACK(window_urgency_hint_cb),
	    NULL);
	g_signal_connect(window, "focus-in-event",
	    G_CALLBACK(window_focus_cb), NULL);
	g_signal_connect(window, "focus-out-event",
	    G_CALLBACK(window_focus_cb), NULL);
#endif /* URGENT_ON_BELL */
#ifdef DYNAMIC_WINDOW_TITLE
	if (!title)
		g_signal_connect(vte, "window-title-changed",
		    G_CALLBACK(window_title_cb), NULL);
#endif /* DYNAMIC_WINDOW_TITLE */
	return vte_widget;
}

static void
set_geometry_hints(VteTerminal* vte, GdkGeometry* hints)
{
	hints->base_width = vte_terminal_get_char_width(vte);
	hints->base_height = vte_terminal_get_char_height(vte);
	hints->min_width = vte_terminal_get_char_width(vte);
	hints->min_height = vte_terminal_get_char_height(vte);
	hints->width_inc = vte_terminal_get_char_width(vte);
	hints->height_inc = vte_terminal_get_char_height(vte);
}

static void
new_window(GtkApplication* app, gchar** argv, gint argc)
{
	GtkWidget* window;
	GtkWidget* box;
	GdkPixbuf* icon;
	GdkGeometry geo_hints;
	GtkIconTheme* icon_theme;
	GError* error = NULL;

	/* Variables for parsed command-line arguments */
	char* command = NULL;
	char* directory = NULL;
	gboolean keep = FALSE;
	char* title = NULL;

	parse_arguments(argc, argv, &command, &directory, &keep, &title);

	/* Create window. */
	window = gtk_application_window_new(GTK_APPLICATION(app));
	g_signal_connect(window, "delete-event", G_CALLBACK(window_close), app);

	gtk_window_set_title(GTK_WINDOW(window), title ? title : "TinyTerm");

	/* Set window icon supplied by an icon theme. */
	icon_theme = gtk_icon_theme_get_default();
	icon = gtk_icon_theme_load_icon(icon_theme, "terminal", 48, 0, &error);
	if (error)
		g_error_free(error);
	if (icon)
		gtk_window_set_icon(GTK_WINDOW (window), icon);

	/* Create main box. */
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(window), box);


	/* Create vte terminal widget */
	GtkWidget* vte_widget = create_vte_terminal(GTK_WINDOW(window), keep,
	    title);
	gtk_box_pack_start(GTK_BOX(box), vte_widget, TRUE, TRUE, 0);
	VteTerminal* vte = VTE_TERMINAL(vte_widget);

	/* Apply geometry hints to handle terminal resizing */
	set_geometry_hints(vte, &geo_hints);
	gtk_window_set_geometry_hints(GTK_WINDOW(window), vte_widget,
	    &geo_hints,
	    GDK_HINT_RESIZE_INC | GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE);

	vte_config(vte);
	vte_spawn(vte, directory, command, NULL);

	/* Cleanup. */
	g_free(command);
	g_free(directory);
	g_free(title);

	/* Show widgets and run main loop. */
	gtk_widget_show_all(window);
}

static void
activate(GApplication* app, gpointer user_data)
{
	(void)user_data;

	new_window(GTK_APPLICATION(app), NULL, 0);
}

static void
command_line(GApplication *app, GApplicationCommandLine *command_line,
    gpointer user_data)
{
	(void)user_data;

	gchar** argc;
	gint argv;
	argc = g_application_command_line_get_arguments(command_line, &argv);
	new_window(GTK_APPLICATION(app), argc, argv);
}

/*
 * This program is a minimalist vte based terminal emulator that uses a basic
 * config file.
 */
int
main (int argc, char* argv[])
{
	gtk_init(&argc, &argv);
	/* Register signal handler. */
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	GtkApplication* app = gtk_application_new("us.laelath.tinyterm",
	    G_APPLICATION_HANDLES_COMMAND_LINE);
	_application = G_APPLICATION(app);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	g_signal_connect(app, "command-line", G_CALLBACK(command_line), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
