#include <glib.h>
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CODEXBAR_REFRESH_SECONDS 120
#define CODEXBAR_DEFAULT_PROVIDERS "claude,copilot"

typedef struct {
    AppIndicator *indicator;
    GtkWidget *menu;
    gchar *cli_path;
    gchar *icon_path;
    gchar *icon_theme_path;
    gchar *icon_name;
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

static gchar *icon_name_from_path(const gchar *path)
{
    gchar *basename = g_path_get_basename(path);
    gchar *extension = strrchr(basename, '.');
    if (extension != NULL) {
        *extension = '\0';
    }
    return basename;
}

static gboolean is_regular_file(const gchar *path)
{
    return path != NULL && g_file_test(path, G_FILE_TEST_IS_REGULAR);
}

static gchar *resolve_icon_path(void)
{
    const gchar *env_path = g_getenv("CODEXBAR_ICON");
    if (is_regular_file(env_path)) {
        return g_strdup(env_path);
    }

    gchar *self_dir = resolve_executable_directory();
    if (self_dir == NULL) {
        return NULL;
    }

    gchar *build_dir = g_path_get_dirname(self_dir);
    gchar *project_dir = g_path_get_dirname(build_dir);
    gchar *candidates[] = {
        g_build_filename(self_dir, "codexbar.png", NULL),
        g_build_filename(build_dir, "codexbar.png", NULL),
        g_build_filename(project_dir, "codexbar.png", NULL),
        g_build_filename(project_dir, "Icon.icon", "Assets", "codexbar.png", NULL),
        NULL,
    };

    gchar *resolved = NULL;
    for (gint index = 0; candidates[index] != NULL; index++) {
        if (is_regular_file(candidates[index])) {
            resolved = g_strdup(candidates[index]);
        }
        g_free(candidates[index]);
        if (resolved != NULL) {
            break;
        }
    }

    g_free(project_dir);
    g_free(build_dir);
    g_free(self_dir);
    return resolved;
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

static gchar *resolve_provider_csv(void)
{
    const gchar *providers = g_getenv("CODEXBAR_PROVIDERS");
    if (providers != NULL && *providers != '\0') {
        return g_strdup(providers);
    }

    const gchar *provider = g_getenv("CODEXBAR_PROVIDER");
    if (provider != NULL && *provider != '\0') {
        return g_strdup(provider);
    }

    return g_strdup(CODEXBAR_DEFAULT_PROVIDERS);
}

static gboolean claude_oauth_credentials_available(void)
{
    const gchar *oauth_token = g_getenv("CODEXBAR_CLAUDE_OAUTH_TOKEN");
    if (oauth_token != NULL && *oauth_token != '\0') {
        return TRUE;
    }

    const gchar *home_dir = g_get_home_dir();
    if (home_dir == NULL || *home_dir == '\0') {
        return FALSE;
    }

    gchar *credentials_path = g_build_filename(home_dir, ".claude", ".credentials.json", NULL);
    gboolean exists = g_file_test(credentials_path, G_FILE_TEST_EXISTS);
    g_free(credentials_path);
    return exists;
}

static const gchar *default_source_for_provider(const gchar *provider)
{
    if (g_strcmp0(provider, "claude") == 0) {
        return claude_oauth_credentials_available() ? "oauth" : "cli";
    }
    if (g_strcmp0(provider, "codex") == 0) {
        return "cli";
    }
    if (g_strcmp0(provider, "copilot") == 0) {
        return "api";
    }
    return NULL;
}

static gchar *format_provider_error(const gchar *provider, const gchar *error_message)
{
    if (provider != NULL && g_strcmp0(provider, "claude") == 0 &&
        error_message != NULL &&
        (strstr(error_message, "Claude CLI is not installed") != NULL ||
         strstr(error_message, "not on PATH") != NULL)) {
        return g_strdup(
            "== claude ==\nClaude unavailable: sign in with Claude Code or make the `claude` CLI runnable.");
    }

    if (provider != NULL && g_strcmp0(provider, "copilot") == 0 &&
        error_message != NULL &&
        strstr(error_message, "No available fetch strategy for copilot") != NULL) {
        return g_strdup(
            "== copilot ==\nCopilot unavailable: sign in with `gh auth login`, or set COPILOT_API_TOKEN or providers[].apiKey in ~/.codexbar/config.json.");
    }

    return g_strdup_printf("== %s ==\n%s", provider != NULL ? provider : "unknown", error_message != NULL ? error_message : "Unknown error");
}

static gboolean run_cli(CodexBarLinuxState *state, const gchar *provider, gchar **output, gchar **error_message)
{
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint exit_status = 0;
    GError *spawn_error = NULL;
    const gchar *source = default_source_for_provider(provider);
    GPtrArray *args = g_ptr_array_new();
    g_ptr_array_add(args, state->cli_path);
    g_ptr_array_add(args, "--format");
    g_ptr_array_add(args, "text");
    if (provider != NULL && *provider != '\0') {
        g_ptr_array_add(args, "--provider");
        g_ptr_array_add(args, (gpointer) provider);
    }
    if (source != NULL) {
        g_ptr_array_add(args, "--source");
        g_ptr_array_add(args, (gpointer) source);
    }
    g_ptr_array_add(args, "--no-color");
    g_ptr_array_add(args, NULL);

    gboolean spawned = g_spawn_sync(
        NULL,
        (gchar **) args->pdata,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        &stdout_text,
        &stderr_text,
        &exit_status,
        &spawn_error);
    g_ptr_array_free(args, TRUE);

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

static void append_menu_row(GtkWidget *menu, const gchar *label, gboolean sensitive, gboolean use_markup)
{
    GtkWidget *item = gtk_menu_item_new();
    GtkWidget *menu_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(menu_label), 0.0);
    gtk_label_set_use_markup(GTK_LABEL(menu_label), use_markup);
    gtk_label_set_line_wrap(GTK_LABEL(menu_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(menu_label), 48);
    if (use_markup) {
        gtk_label_set_markup(GTK_LABEL(menu_label), label);
    } else {
        gtk_label_set_text(GTK_LABEL(menu_label), label);
    }
    gtk_container_add(GTK_CONTAINER(item), menu_label);
    gtk_widget_set_sensitive(item, sensitive);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(menu_label);
    gtk_widget_show(item);
}

static void append_disabled_row(GtkWidget *menu, const gchar *label)
{
    append_menu_row(menu, label, FALSE, FALSE);
}

static void append_header_row(GtkWidget *menu, const gchar *markup)
{
    append_menu_row(menu, markup, TRUE, TRUE);
}

static gchar *provider_header_markup(const gchar *line)
{
    gsize length = strlen(line);
    if (length < 8 || !g_str_has_prefix(line, "== ") || !g_str_has_suffix(line, " ==")) {
        return NULL;
    }

    gchar *header = g_strndup(line + 3, length - 6);
    gchar *escaped = g_markup_escape_text(header, -1);
    gchar *markup = g_strdup_printf("<b>%s</b>", escaped);
    g_free(escaped);
    g_free(header);
    return markup;
}

static GtkWidget *build_menu(CodexBarLinuxState *state, const gchar *content, gboolean is_error)
{
    GtkWidget *menu = gtk_menu_new();
    gboolean last_was_divider = TRUE;

    append_header_row(menu, "<b>CodexBar</b>");
    append_disabled_row(menu, is_error ? "Linux tray status" : "Linux tray usage");

    GtkWidget *top_separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), top_separator);
    gtk_widget_show(top_separator);
    last_was_divider = TRUE;

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
                gchar *header_markup = provider_header_markup(line);
                if (header_markup != NULL) {
                    append_header_row(menu, header_markup);
                    g_free(header_markup);
                } else {
                    append_disabled_row(menu, line);
                }
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

    gchar *provider_csv = resolve_provider_csv();
    gchar **providers = g_strsplit(provider_csv, ",", -1);
    GString *content = g_string_new(NULL);
    gchar *label = NULL;
    gboolean hadSuccess = FALSE;
    gboolean hadError = FALSE;

    for (gint index = 0; providers[index] != NULL; index++) {
        gchar *provider = g_strstrip(providers[index]);
        if (*provider == '\0') {
            continue;
        }

        gchar *output = NULL;
        gchar *error_message = NULL;
        if (run_cli(state, provider, &output, &error_message)) {
            if (content->len > 0) {
                g_string_append(content, "\n\n");
            }
            g_string_append(content, output);
            if (label == NULL) {
                label = extract_first_percent_label(output);
            }
            hadSuccess = TRUE;
            g_free(output);
            continue;
        }

        if (content->len > 0) {
            g_string_append(content, "\n\n");
        }
        gchar *formatted_error = format_provider_error(provider, error_message);
        g_string_append(content, formatted_error);
        g_free(formatted_error);
        hadError = TRUE;
        g_free(error_message);
    }

    if (hadSuccess) {
        app_indicator_set_status(state->indicator, APP_INDICATOR_STATUS_ACTIVE);
        if (label != NULL) {
            app_indicator_set_label(state->indicator, label, "100%");
        } else {
            app_indicator_set_label(state->indicator, NULL, NULL);
        }
        apply_menu(state, build_menu(state, content->str, FALSE));
    } else {
        app_indicator_set_status(state->indicator, APP_INDICATOR_STATUS_ATTENTION);
        app_indicator_set_label(state->indicator, "ERR", "ERR");
        apply_menu(state, build_menu(state, content->len > 0 ? content->str : "No providers configured.", TRUE));
    }

    g_free(label);
    g_string_free(content, TRUE);
    g_strfreev(providers);
    g_free(provider_csv);
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
    state->icon_path = resolve_icon_path();
    if (state->icon_path != NULL) {
        state->icon_theme_path = g_path_get_dirname(state->icon_path);
        state->icon_name = icon_name_from_path(state->icon_path);
    }
    state->indicator = app_indicator_new(
        "codexbar-linux",
        state->icon_name != NULL ? state->icon_name : "utilities-terminal",
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

    // Follow the upstream AppIndicator example for the MVP tray shell:
    // initialize GTK, create an indicator, attach a GtkMenu, and enter the
    // main loop. Source:
    // https://github.com/AyatanaIndicators/libayatana-appindicator/blob/31e8bb083b307e1cc96af4874a94707727bd1e79/example/simple-client.c
    if (state->icon_theme_path != NULL) {
        app_indicator_set_icon_theme_path(state->indicator, state->icon_theme_path);
    }
    app_indicator_set_status(state->indicator, APP_INDICATOR_STATUS_ACTIVE);
    if (state->icon_name != NULL) {
        app_indicator_set_icon_full(state->indicator, state->icon_name, "CodexBar");
    }
    app_indicator_set_attention_icon_full(state->indicator, "dialog-error", "CodexBar Linux error");
    app_indicator_set_title(state->indicator, "CodexBar");

    refresh_indicator(state);
    g_timeout_add_seconds(CODEXBAR_REFRESH_SECONDS, refresh_indicator, state);
    gtk_main();

    g_free(state->icon_name);
    g_free(state->icon_theme_path);
    g_free(state->icon_path);
    g_free(state->cli_path);
    g_free(state);
    return 0;
}
