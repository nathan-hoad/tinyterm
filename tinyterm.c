/*
 * MIT/X Consortium License
 *
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

#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

#include <glib.h>
#include <gdk/gdkkeysyms.h>
#include <vte/vte.h>

#include "config.h"

static GApplication *_application = NULL;   // needs to be global for signal_handler to work

/* callback to set window urgency hint on beep events */
static void
window_urgency_hint_cb(VteTerminal* vte, gpointer user_data)
{
	gtk_window_set_urgency_hint(GTK_WINDOW (gtk_widget_get_toplevel(GTK_WIDGET (vte))), TRUE);
}

/* callback to unset window urgency hint on focus */
gboolean
window_focus_cb(GtkWindow* window)
{
	gtk_window_set_urgency_hint(window, FALSE);
	return FALSE;
}

/* callback to dynamically change window title */
static void
window_title_cb(VteTerminal* vte)
{
	gtk_window_set_title(GTK_WINDOW (gtk_widget_get_toplevel(GTK_WIDGET (vte))), vte_terminal_get_window_title(vte));
}

/* callback to react to key press events */
static gboolean
key_press_cb(VteTerminal* vte, GdkEventKey* event)
{
	if ((event->state & (TINYTERM_MODIFIER)) == (TINYTERM_MODIFIER)) {
		switch (gdk_keyval_to_upper(event->keyval)) {
			case TINYTERM_KEY_COPY:
				vte_terminal_copy_clipboard(vte);
				return TRUE;
			case TINYTERM_KEY_PASTE:
				vte_terminal_paste_clipboard(vte);
				return TRUE;
		}
	}
	return FALSE;
}

static void
vte_config(VteTerminal* vte)
{
	GRegex* regex = g_regex_new(url_regex, G_REGEX_CASELESS, G_REGEX_MATCH_NOTEMPTY, NULL);

	vte_terminal_search_set_gregex(vte, regex, G_REGEX_MATCH_NOTEMPTY);
	vte_terminal_search_set_wrap_around     (vte, TINYTERM_SEARCH_WRAP_AROUND);
	vte_terminal_set_audible_bell           (vte, TINYTERM_AUDIBLE_BELL);
	vte_terminal_set_cursor_shape           (vte, TINYTERM_CURSOR_SHAPE);
	vte_terminal_set_cursor_blink_mode      (vte, TINYTERM_CURSOR_BLINK);
	vte_terminal_set_word_char_exceptions   (vte, TINYTERM_WORD_CHARS);
	vte_terminal_set_scrollback_lines       (vte, TINYTERM_SCROLLBACK_LINES);
	PangoFontDescription *font = pango_font_description_from_string(TINYTERM_FONT);
	vte_terminal_set_font(vte, font);

	GdkRGBA color_fg, color_bg;
	GdkRGBA color_palette[16];
	gdk_rgba_parse(&color_fg, TINYTERM_COLOR_FOREGROUND);
	gdk_rgba_parse(&color_bg, TINYTERM_COLOR_BACKGROUND);
	gdk_rgba_parse(&color_palette[0], TINYTERM_COLOR00);
	gdk_rgba_parse(&color_palette[1], TINYTERM_COLOR01);
	gdk_rgba_parse(&color_palette[2], TINYTERM_COLOR02);
	gdk_rgba_parse(&color_palette[3], TINYTERM_COLOR03);
	gdk_rgba_parse(&color_palette[4], TINYTERM_COLOR04);
	gdk_rgba_parse(&color_palette[5], TINYTERM_COLOR05);
	gdk_rgba_parse(&color_palette[6], TINYTERM_COLOR06);
	gdk_rgba_parse(&color_palette[7], TINYTERM_COLOR07);
	gdk_rgba_parse(&color_palette[8], TINYTERM_COLOR08);
	gdk_rgba_parse(&color_palette[9], TINYTERM_COLOR09);
	gdk_rgba_parse(&color_palette[10], TINYTERM_COLOR0A);
	gdk_rgba_parse(&color_palette[11], TINYTERM_COLOR0B);
	gdk_rgba_parse(&color_palette[12], TINYTERM_COLOR0C);
	gdk_rgba_parse(&color_palette[13], TINYTERM_COLOR0D);
	gdk_rgba_parse(&color_palette[14], TINYTERM_COLOR0E);
	gdk_rgba_parse(&color_palette[15], TINYTERM_COLOR0F);

	vte_terminal_set_colors(vte, &color_fg, &color_bg, &color_palette, 16);
}

static void
vte_spawn(VteTerminal* vte, char* working_directory, char* command, char** environment)
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
	VtePty* pty = vte_terminal_pty_new_sync(vte, VTE_PTY_NO_HELPER, NULL, &error);
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

/* callback to exit TinyTerm with exit status of child process */
static void
window_close(GtkWindow* window, gint status, gpointer user_data)
{
	GtkApplication *app = (GtkApplication*)user_data;

	int count = 0;
	GList *windows = gtk_application_get_windows(app);

	while (windows != NULL) {
		windows = windows->next;
		count++;
	}

	if (count == 1) {
		g_application_quit(G_APPLICATION(app));
	}
}

static void
vte_exit_cb(VteTerminal *vte, gint status, gpointer user_data)
{
	GtkWindow *window = (GtkWindow*)user_data;

	gtk_widget_destroy(GTK_WIDGET(window));
}

static void
parse_arguments(int argc, char* argv[], char** command, char** directory, gboolean* keep, char** name, char** title)
{
	gboolean version = FALSE;   // show version?
	const GOptionEntry entries[] = {
		{"version",   'v', 0, G_OPTION_ARG_NONE,    &version,   "Display program version and exit.", 0},
		{"execute",   'e', 0, G_OPTION_ARG_STRING,  command,    "Execute command instead of default shell.", "COMMAND"},
		{"directory", 'd', 0, G_OPTION_ARG_STRING,  directory,  "Sets the working directory for the shell (or the command specified via -e).", "PATH"},
		{"keep",      'k', 0, G_OPTION_ARG_NONE,    keep,       "Don't exit the terminal after child process exits.", 0},
		{"name",      'n', 0, G_OPTION_ARG_STRING,  name,       "Set first value of WM_CLASS property; second value is always 'TinyTerm' (default: 'tinyterm')", "NAME"},
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
		g_print("tinyterm " TINYTERM_VERSION "\n");
		exit(EXIT_SUCCESS);
	}
}

static void
signal_handler(int signal)
{
	g_application_quit(_application);
}

void new_window(GtkApplication *app, gchar **argv, gint argc)
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
	char* name = NULL;
	char* title = NULL;

	parse_arguments(argc, argv, &command, &directory, &keep, &name, &title);

	/* Create window */
	window = gtk_application_window_new(GTK_APPLICATION(app));
	g_signal_connect(window, "delete-event", G_CALLBACK(window_close), app);

	gtk_window_set_wmclass(GTK_WINDOW (window), name ? name : "tinyterm", "TinyTerm");
	gtk_window_set_title(GTK_WINDOW (window), title ? title : "TinyTerm");

	/* Set window icon supplied by an icon theme */
	icon_theme = gtk_icon_theme_get_default();
	icon = gtk_icon_theme_load_icon(icon_theme, "terminal", 48, 0, &error);
	if (error)
		g_error_free(error);
	if (icon)
		gtk_window_set_icon(GTK_WINDOW (window), icon);

	/* Create main box */
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER (window), box);

	/* Create vte terminal widget */
	GtkWidget* vte_widget = vte_terminal_new();
	gtk_box_pack_start(GTK_BOX (box), vte_widget, TRUE, TRUE, 0);
	VteTerminal* vte = VTE_TERMINAL (vte_widget);
	if (!keep)
		g_signal_connect(vte, "child-exited", G_CALLBACK (vte_exit_cb), window);
	g_signal_connect(vte, "key-press-event", G_CALLBACK (key_press_cb), NULL);
#ifdef TINYTERM_URGENT_ON_BELL
	g_signal_connect(vte, "bell", G_CALLBACK (window_urgency_hint_cb), NULL);
	g_signal_connect(window, "focus-in-event",  G_CALLBACK (window_focus_cb), NULL);
	g_signal_connect(window, "focus-out-event", G_CALLBACK (window_focus_cb), NULL);
#endif // TINYTERM_URGENT_ON_BELL
#ifdef TINYTERM_DYNAMIC_WINDOW_TITLE
	if (!title)
		g_signal_connect(vte, "window-title-changed", G_CALLBACK (window_title_cb), NULL);
#endif // TINYTERM_DYNAMIC_WINDOW_TITLE

	/* Apply geometry hints to handle terminal resizing */
	geo_hints.base_width  = vte_terminal_get_char_width(vte);
	geo_hints.base_height = vte_terminal_get_char_height(vte);
	geo_hints.min_width   = vte_terminal_get_char_width(vte);
	geo_hints.min_height  = vte_terminal_get_char_height(vte);
	geo_hints.width_inc   = vte_terminal_get_char_width(vte);
	geo_hints.height_inc  = vte_terminal_get_char_height(vte);
	gtk_window_set_geometry_hints(GTK_WINDOW (window), vte_widget, &geo_hints,
			GDK_HINT_RESIZE_INC | GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE);

	vte_config(vte);
	vte_spawn(vte, directory, command, NULL);

	/* cleanup */
	g_free(command);
	g_free(directory);
	g_free(name);
	g_free(title);

	/* Show widgets and run main loop */
	gtk_widget_show_all(window);
}

static void
activate(GApplication *app, gpointer user_data)
{
	new_window(GTK_APPLICATION(app), NULL, 0);
}

static void
command_line(GApplication *app, GApplicationCommandLine *command_line, gpointer user_data)
{
	gchar **argc;
	gint argv;
	argc = g_application_command_line_get_arguments(command_line, &argv);
	new_window(GTK_APPLICATION(app), argc, argv);
}

int
main (int argc, char* argv[])
{
	gtk_init(&argc, &argv);
	/* register signal handler */
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	GtkCssProvider *provider = gtk_css_provider_new();

	gtk_css_provider_load_from_data(provider, TINYTERM_STYLE, strlen(TINYTERM_STYLE), NULL);

	gtk_style_context_add_provider_for_screen(
			gdk_screen_get_default(), provider,
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	GtkApplication *app;
	int status;
	app = gtk_application_new("com.laelath.github.tinyterm", G_APPLICATION_HANDLES_COMMAND_LINE);
	_application = G_APPLICATION(app);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	g_signal_connect(app, "command-line", G_CALLBACK(command_line), NULL);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}

