#include <glib.h>
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CODEXBAR_REFRESH_SECONDS 120

typedef struct {
    AppIndicator *indicator;
    GtkWidget *menu;
    gchar *cli_path;
} CodexBarLinuxState;

static gboolean refresh_indicator(gpointer user_data);
static void refresh_clicked_cb(GtkWidget *widget, gpointer user_data);
static void quit_clicked_cb(GtkWidget *widget, gpointer user_data);

static gchar *duplicate_line(const gchar *start, gsize length)
{
    gchar *line = g_malloc(length + 1);
    memcpy(line, start, length);
    line[length] = '\0';
    return line;
}

static gchar *resolve_executable_directory(void)
{
    gchar buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return NULL;
    }

    buffer[length] = '\0';
    return g_path_get_dirname(buffer);
}

static gchar *resolve_cli_path(void)
{
    const gchar *env_path = g_getenv("CODEXBAR_CLI");
    if (env_path != NULL && *env_path != '\0' && g_file_test(env_path, G_FILE_TEST_IS_EXECUTABLE)) {
        return g_strdup(env_path);
    }

    gchar *self_dir = resolve_executable_directory();
    if (self_dir != NULL) {
        gchar *sibling_cli = g_build_filename(self_dir, "CodexBarCLI", NULL);
        if (g_file_test(sibling_cli, G_FILE_TEST_IS_EXECUTABLE)) {
            g_free(self_dir);
            return sibling_cli;
        }
        g_free(sibling_cli);

        gchar *sibling_alias = g_build_filename(self_dir, "codexbar", NULL);
        if (g_file_test(sibling_alias, G_FILE_TEST_IS_EXECUTABLE)) {
            g_free(self_dir);
            return sibling_alias;
        }
        g_free(sibling_alias);
        g_free(self_dir);
    }

    gchar *path_cli = g_find_program_in_path("codexbar");
    if (path_cli != NULL) {
        return path_cli;
    }

    return g_find_program_in_path("CodexBarCLI");
}

static gboolean run_cli(CodexBarLinuxState *state, gchar **output, gchar **error_message)
{
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint exit_status = 0;
    GError *spawn_error = NULL;
    gchar *argv[] = {
        state->cli_path,
        "--format",
        "text",
        "--no-color",
        NULL,
    };

    gboolean spawned = g_spawn_sync(
        NULL,
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        &stdout_text,
        &stderr_text,
        &exit_status,
        &spawn_error);

    if (!spawned) {
        *error_message = g_strdup_printf(
            "Failed to launch CodexBarCLI: %s",
            spawn_error != NULL ? spawn_error->message : "unknown error");
        g_clear_error(&spawn_error);
        g_free(stdout_text);
        g_free(stderr_text);
        return FALSE;
    }

    GError *exit_error = NULL;
    if (!g_spawn_check_exit_status(exit_status, &exit_error)) {
        const gchar *details = stderr_text != NULL && *stderr_text != '\0'
            ? stderr_text
            : (exit_error != NULL ? exit_error->message : "unknown error");
        *error_message = g_strdup_printf("CodexBarCLI exited with an error: %s", details);
        g_clear_error(&exit_error);
        g_free(stdout_text);
        g_free(stderr_text);
        return FALSE;
    }

    *output = stdout_text;
    g_free(stderr_text);
    return TRUE;
}

static gchar *extract_first_percent_label(const gchar *output)
{
    const gchar *cursor = output;
    while (cursor != NULL && *cursor != '\0') {
        const gchar *line_end = strchr(cursor, '\n');
        gsize line_length = line_end != NULL ? (gsize) (line_end - cursor) : strlen(cursor);
        gchar *line = duplicate_line(cursor, line_length);
        g_strstrip(line);

        const gchar *marker = strstr(line, "% ");
        while (marker != NULL) {
            const gchar *start = marker;
            while (start > line && g_ascii_isdigit((guchar) start[-1])) {
                start--;
            }
            if (start < marker && g_ascii_isdigit((guchar) *start)) {
                gchar *label = duplicate_line(start, (gsize) (marker - start + 1));
                g_free(line);
                return label;
            }
            marker = strstr(marker + 1, "% ");
        }

        g_free(line);
        cursor = line_end != NULL ? line_end + 1 : NULL;
    }

    return NULL;
}

static void append_disabled_row(GtkWidget *menu, const gchar *label)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
}

static GtkWidget *build_menu(CodexBarLinuxState *state, const gchar *content, gboolean is_error)
{
    GtkWidget *menu = gtk_menu_new();
    gboolean last_was_divider = TRUE;

    if (content == NULL || *content == '\0') {
        append_disabled_row(menu, is_error ? "No CodexBar output available." : "No usage output yet.");
        last_was_divider = FALSE;
    } else {
        const gchar *cursor = content;
        while (cursor != NULL && *cursor != '\0') {
            const gchar *line_end = strchr(cursor, '\n');
            gsize line_length = line_end != NULL ? (gsize) (line_end - cursor) : strlen(cursor);
            gchar *line = duplicate_line(cursor, line_length);
            g_strstrip(line);

            if (*line == '\0') {
                if (!last_was_divider) {
                    GtkWidget *separator = gtk_separator_menu_item_new();
                    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
                    gtk_widget_show(separator);
                    last_was_divider = TRUE;
                }
            } else {
                append_disabled_row(menu, line);
                last_was_divider = FALSE;
            }

            g_free(line);
            cursor = line_end != NULL ? line_end + 1 : NULL;
        }
    }

    if (!last_was_divider) {
        GtkWidget *separator = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
        gtk_widget_show(separator);
    }

    GtkWidget *refresh_item = gtk_menu_item_new_with_label("Refresh");
    g_signal_connect(refresh_item, "activate", G_CALLBACK(refresh_clicked_cb), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh_item);
    gtk_widget_show(refresh_item);

    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(quit_clicked_cb), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
    gtk_widget_show(quit_item);

    return menu;
}

static void apply_menu(CodexBarLinuxState *state, GtkWidget *menu)
{
    app_indicator_set_menu(state->indicator, GTK_MENU(menu));
    if (state->menu != NULL) {
        g_object_unref(state->menu);
    }
    state->menu = menu;
    g_object_ref_sink(state->menu);
}

static gboolean refresh_indicator(gpointer user_data)
{
    CodexBarLinuxState *state = (CodexBarLinuxState *) user_data;

    if (state->cli_path == NULL) {
        app_indicator_set_status(state->indicator, APP_INDICATOR_STATUS_ATTENTION);
        app_indicator_set_label(state->indicator, "ERR", "ERR");
        apply_menu(state, build_menu(
            state,
            "CodexBarCLI was not found.\nSet CODEXBAR_CLI or place CodexBarCLI next to CodexBarLinux.",
            TRUE));
        return TRUE;
    }

    gchar *output = NULL;
    gchar *error_message = NULL;
    if (!run_cli(state, &output, &error_message)) {
        app_indicator_set_status(state->indicator, APP_INDICATOR_STATUS_ATTENTION);
        app_indicator_set_label(state->indicator, "ERR", "ERR");
        apply_menu(state, build_menu(state, error_message, TRUE));
        g_free(error_message);
        return TRUE;
    }

    gchar *label = extract_first_percent_label(output);
    app_indicator_set_status(state->indicator, APP_INDICATOR_STATUS_ACTIVE);
    if (label != NULL) {
        app_indicator_set_label(state->indicator, label, "100%");
    } else {
        app_indicator_set_label(state->indicator, NULL, NULL);
    }
    apply_menu(state, build_menu(state, output, FALSE));
    g_free(label);
    g_free(output);
    return TRUE;
}

static void refresh_clicked_cb(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    refresh_indicator(user_data);
}

static void quit_clicked_cb(GtkWidget *widget, gpointer user_data)
{
    (void) widget;
    CodexBarLinuxState *state = (CodexBarLinuxState *) user_data;
    if (state->menu != NULL) {
        g_object_unref(state->menu);
        state->menu = NULL;
    }
    gtk_main_quit();
}

int main(int argc, char **argv)
{
    (void) argv;
    gtk_init(&argc, &argv);

    CodexBarLinuxState *state = g_new0(CodexBarLinuxState, 1);
    state->cli_path = resolve_cli_path();
    state->indicator = app_indicator_new(
        "codexbar-linux",
        "utilities-terminal",
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

    // Follow the upstream AppIndicator example for the MVP tray shell:
    // initialize GTK, create an indicator, attach a GtkMenu, and enter the
    // main loop. Source:
    // https://github.com/AyatanaIndicators/libayatana-appindicator/blob/31e8bb083b307e1cc96af4874a94707727bd1e79/example/simple-client.c
    app_indicator_set_status(state->indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_attention_icon_full(state->indicator, "dialog-error", "CodexBar Linux error");
    app_indicator_set_title(state->indicator, "CodexBar Linux");

    refresh_indicator(state);
    g_timeout_add_seconds(CODEXBAR_REFRESH_SECONDS, refresh_indicator, state);
    gtk_main();

    g_free(state->cli_path);
    g_free(state);
    return 0;
}
