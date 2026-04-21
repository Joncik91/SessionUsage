#include <glib.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libayatana-appindicator/app-indicator.h>
#include <pango/pangocairo.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CODEXBAR_REFRESH_SECONDS 120
#define CODEXBAR_DEFAULT_PROVIDERS "claude,copilot"
#define CODEXBAR_MENU_ICON_SIZE 16
#define CODEXBAR_LOGO_ALPHA_TRIM_THRESHOLD 8
#define SESSIONUSAGE_APP_NAME "SessionUsage"

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

static gchar *resolve_repository_directory(const gchar *executable_directory)
{
    if (executable_directory == NULL) {
        return NULL;
    }

    gchar *build_dir = g_path_get_dirname(executable_directory);
    gchar *project_dir = g_path_get_dirname(build_dir);
    gchar *repo_dir = g_path_get_dirname(project_dir);
    g_free(project_dir);
    g_free(build_dir);
    return repo_dir;
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
    gchar *repo_dir = resolve_repository_directory(self_dir);
    gchar *candidates[] = {
        g_build_filename(self_dir, "sessionusage-symbolic.svg", NULL),
        g_build_filename(build_dir, "sessionusage-symbolic.svg", NULL),
        g_build_filename(project_dir, "sessionusage-symbolic.svg", NULL),
        g_build_filename(repo_dir, "sessionusage-symbolic.svg", NULL),
        g_build_filename(self_dir, "codexbar.png", NULL),
        g_build_filename(build_dir, "codexbar.png", NULL),
        g_build_filename(project_dir, "codexbar.png", NULL),
        g_build_filename(repo_dir, "codexbar.png", NULL),
        g_build_filename(project_dir, "Icon.icon", "Assets", "codexbar.png", NULL),
        g_build_filename(repo_dir, "Icon.icon", "Assets", "codexbar.png", NULL),
        NULL,
    };

    gchar *resolved = NULL;
    for (gint index = 0; candidates[index] != NULL; index++) {
        if (is_regular_file(candidates[index])) {
            resolved = g_strdup(candidates[index]);
        }
        g_free(candidates[index]);
        candidates[index] = NULL;
        if (resolved != NULL) {
            break;
        }
    }

    for (gint index = 0; candidates[index] != NULL; index++) {
        g_free(candidates[index]);
    }

    g_free(repo_dir);
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
    gchar *detail = NULL;
    if (provider != NULL && g_strcmp0(provider, "claude") == 0 &&
        error_message != NULL &&
        (strstr(error_message, "Claude CLI is not installed") != NULL ||
         strstr(error_message, "not on PATH") != NULL)) {
        detail = g_strdup("Claude unavailable: sign in with Claude Code or make the `claude` CLI runnable.");
    } else if (provider != NULL && g_strcmp0(provider, "copilot") == 0 &&
               error_message != NULL &&
               strstr(error_message, "No available fetch strategy for copilot") != NULL) {
        detail = g_strdup(
            "Copilot unavailable: sign in with `gh auth login`, or set COPILOT_API_TOKEN or providers[].apiKey in ~/.codexbar/config.json.");
    } else {
        detail = g_strdup(error_message != NULL ? error_message : "Unknown error");
    }

    gchar *formatted = g_strdup_printf("== %s ==\n%s", provider != NULL ? provider : "unknown", detail);
    g_free(detail);
    return formatted;
}

static JsonObject *json_object_member_as_object(JsonObject *object, const gchar *member_name)
{
    JsonNode *member = object != NULL ? json_object_get_member(object, member_name) : NULL;
    if (member == NULL || !JSON_NODE_HOLDS_OBJECT(member)) {
        return NULL;
    }
    return json_node_get_object(member);
}

static gchar *json_object_dup_string_member(JsonObject *object, const gchar *member_name)
{
    JsonNode *member = object != NULL ? json_object_get_member(object, member_name) : NULL;
    if (member == NULL || !JSON_NODE_HOLDS_VALUE(member)) {
        return NULL;
    }

    const gchar *value = json_node_get_string(member);
    return value != NULL && *value != '\0' ? g_strdup(value) : NULL;
}

static gboolean json_object_lookup_double_member(JsonObject *object, const gchar *member_name, gdouble *value_out)
{
    JsonNode *member = object != NULL ? json_object_get_member(object, member_name) : NULL;
    if (member == NULL || !JSON_NODE_HOLDS_VALUE(member)) {
        return FALSE;
    }

    *value_out = json_node_get_double(member);
    return TRUE;
}

static gchar *json_object_dup_identity_string_member(JsonObject *usage, const gchar *member_name)
{
    JsonObject *identity = json_object_member_as_object(usage, "identity");
    gchar *value = json_object_dup_string_member(identity, member_name);
    if (value != NULL) {
        return value;
    }
    return json_object_dup_string_member(usage, member_name);
}

static gchar *humanize_provider_name(const gchar *provider)
{
    typedef struct {
        const gchar *identifier;
        const gchar *label;
    } ProviderName;

    static const ProviderName names[] = {
        { "abacus", "Abacus" },
        { "amp", "Amp" },
        { "augment", "Augment" },
        { "claude", "Claude" },
        { "codex", "Codex" },
        { "copilot", "Copilot" },
        { "factory", "Factory" },
        { "gemini", "Gemini" },
        { "jetbrains", "JetBrains" },
        { "kilo", "Kilo" },
        { "kimi", "Kimi" },
        { "ollama", "Ollama" },
        { "openrouter", "OpenRouter" },
        { "perplexity", "Perplexity" },
        { "qwen", "Qwen" },
        { "warp", "Warp" },
        { "zai", "Z.ai" },
        { NULL, NULL },
    };

    if (provider == NULL || *provider == '\0') {
        return g_strdup("Unknown");
    }

    for (gint index = 0; names[index].identifier != NULL; index++) {
        if (g_strcmp0(provider, names[index].identifier) == 0) {
            return g_strdup(names[index].label);
        }
    }

    gchar *label = g_strdup(provider);
    label[0] = (gchar) g_ascii_toupper((guchar) label[0]);
    return label;
}

typedef struct {
    const gchar *display_name;
    const gchar *badge_text;
    const gchar *logo_filename;
    guint8 red;
    guint8 green;
    guint8 blue;
} ProviderVisualStyle;

static const ProviderVisualStyle *provider_visual_style_for_name(const gchar *display_name)
{
    static const ProviderVisualStyle styles[] = {
        { "Abacus", "Ab", "ProviderIcon-abacus.svg", 59, 130, 246 },
        { "Amp", "Am", "ProviderIcon-amp.svg", 16, 185, 129 },
        { "Augment", "Au", "ProviderIcon-augment.svg", 14, 165, 233 },
        { "Claude", "Cl", "ProviderIcon-claude.png", 204, 124, 94 },
        { "Codex", "Cx", "ProviderIcon-codex.png", 16, 185, 129 },
        { "Copilot", "GH", "ProviderIcon-copilot.png", 168, 85, 247 },
        { "Cursor", "Cu", "ProviderIcon-cursor.png", 59, 130, 246 },
        { "Factory", "Fa", "ProviderIcon-factory.png", 245, 158, 11 },
        { "Gemini", "Ge", "ProviderIcon-gemini.png", 99, 102, 241 },
        { "JetBrains", "JB", "ProviderIcon-jetbrains.svg", 236, 72, 153 },
        { "Kilo", "Ki", "ProviderIcon-kilo.svg", 249, 115, 22 },
        { "Kimi", "Km", "ProviderIcon-kimi.svg", 234, 88, 12 },
        { "Ollama", "Ol", "ProviderIcon-ollama.svg", 20, 184, 166 },
        { "OpenRouter", "Or", "ProviderIcon-openrouter.svg", 99, 102, 241 },
        { "Perplexity", "Px", "ProviderIcon-perplexity.svg", 6, 182, 212 },
        { "Qwen", "Qw", NULL, 37, 99, 235 },
        { "Warp", "Wp", "ProviderIcon-warp.svg", 139, 92, 246 },
        { "Z.ai", "Za", "ProviderIcon-zai.png", 244, 63, 94 },
        { NULL, NULL, NULL, 0, 0, 0 },
    };

    if (display_name == NULL) {
        return NULL;
    }

    for (gint index = 0; styles[index].display_name != NULL; index++) {
        if (g_strcmp0(display_name, styles[index].display_name) == 0) {
            return &styles[index];
        }
    }
    return NULL;
}

static gchar *fallback_badge_text(const gchar *display_name)
{
    gchar initials[3] = { 0 };
    gint count = 0;

    if (display_name != NULL) {
        for (const gchar *cursor = display_name; *cursor != '\0' && count < 2; cursor++) {
            if (g_ascii_isalnum((guchar) *cursor)) {
                initials[count++] = (gchar) g_ascii_toupper((guchar) *cursor);
            }
        }
    }

    if (count == 0) {
        initials[count++] = '?';
    }
    initials[count] = '\0';
    return g_strdup(initials);
}

static gchar *resolve_provider_logo_path(const gchar *logo_filename)
{
    if (logo_filename == NULL || *logo_filename == '\0') {
        return NULL;
    }

    gchar *preferred_filenames[4] = { NULL, NULL, NULL, NULL };
    const gchar *extension = strrchr(logo_filename, '.');
    if (extension != NULL) {
        gchar *basename = g_strndup(logo_filename, extension - logo_filename);
        preferred_filenames[0] = g_strconcat(basename, ".png", NULL);
        preferred_filenames[1] = g_strconcat(basename, "@2x.png", NULL);
        preferred_filenames[2] = g_strconcat(basename, ".svg", NULL);
        g_free(basename);
    } else {
        preferred_filenames[0] = g_strdup(logo_filename);
    }

    const gchar *env_dir = g_getenv("CODEXBAR_PROVIDER_ICONS_DIR");
    if (env_dir != NULL && *env_dir != '\0') {
        for (gint index = 0; preferred_filenames[index] != NULL; index++) {
            gchar *candidate = g_build_filename(env_dir, preferred_filenames[index], NULL);
            if (is_regular_file(candidate)) {
                for (gint free_index = 0; free_index < G_N_ELEMENTS(preferred_filenames); free_index++) {
                    g_free(preferred_filenames[free_index]);
                }
                return candidate;
            }
            g_free(candidate);
        }
    }

    gchar *self_dir = resolve_executable_directory();
    if (self_dir == NULL) {
        for (gint index = 0; index < G_N_ELEMENTS(preferred_filenames); index++) {
            g_free(preferred_filenames[index]);
        }
        return NULL;
    }

    gchar *build_dir = g_path_get_dirname(self_dir);
    gchar *project_dir = g_path_get_dirname(build_dir);
    gchar *repo_dir = g_path_get_dirname(project_dir);
    gchar *search_roots[] = {
        g_build_filename(self_dir, "ProviderIcons", NULL),
        g_build_filename(self_dir, "Assets", "ProviderIcons", NULL),
        g_build_filename(build_dir, "ProviderIcons", NULL),
        g_build_filename(build_dir, "Assets", "ProviderIcons", NULL),
        g_build_filename(project_dir, "ProviderIcons", NULL),
        g_build_filename(project_dir, "Assets", "ProviderIcons", NULL),
        g_build_filename(repo_dir, "ProviderIcons", NULL),
        g_build_filename(repo_dir, "Assets", "ProviderIcons", NULL),
        NULL,
    };

    gchar *resolved = NULL;
    for (gint directory_index = 0; search_roots[directory_index] != NULL && resolved == NULL; directory_index++) {
        for (gint filename_index = 0; preferred_filenames[filename_index] != NULL; filename_index++) {
            gchar *candidate = g_build_filename(search_roots[directory_index], preferred_filenames[filename_index], NULL);
            if (is_regular_file(candidate)) {
                resolved = g_strdup(candidate);
                g_free(candidate);
                break;
            }
            g_free(candidate);
        }
    }

    for (gint index = 0; search_roots[index] != NULL; index++) {
        g_free(search_roots[index]);
    }
    for (gint index = 0; index < G_N_ELEMENTS(preferred_filenames); index++) {
        g_free(preferred_filenames[index]);
    }
    g_free(repo_dir);
    g_free(project_dir);
    g_free(build_dir);
    g_free(self_dir);
    return resolved;
}

static GdkPixbuf *create_badge_pixbuf(const gchar *text, guint8 red, guint8 green, guint8 blue, gint size)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *context = cairo_create(surface);

    cairo_set_source_rgba(context, 0, 0, 0, 0);
    cairo_paint(context);

    cairo_set_source_rgb(context, red / 255.0, green / 255.0, blue / 255.0);
    cairo_arc(context, size / 2.0, size / 2.0, (size / 2.0) - 1.0, 0, 2 * G_PI);
    cairo_fill(context);

    cairo_set_source_rgba(context, 1, 1, 1, 0.22);
    cairo_arc(context, size / 2.0, size / 2.0, (size / 2.0) - 1.0, 0, 2 * G_PI);
    cairo_set_line_width(context, 1.0);
    cairo_stroke(context);

    if (text != NULL && *text != '\0') {
        PangoLayout *layout = pango_cairo_create_layout(context);
        PangoFontDescription *font = pango_font_description_from_string(size <= 16 ? "Sans Bold 7.5" : "Sans Bold 8.5");
        pango_layout_set_font_description(layout, font);
        pango_layout_set_text(layout, text, -1);

        gint text_width = 0;
        gint text_height = 0;
        pango_layout_get_pixel_size(layout, &text_width, &text_height);

        cairo_set_source_rgb(context, 1, 1, 1);
        cairo_move_to(context, (size - text_width) / 2.0, ((size - text_height) / 2.0) - 0.5);
        pango_cairo_show_layout(context, layout);

        pango_font_description_free(font);
        g_object_unref(layout);
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, size, size);
    cairo_destroy(context);
    cairo_surface_destroy(surface);
    return pixbuf;
}

static GdkPixbuf *create_logo_chip_pixbuf(guint8 red, guint8 green, guint8 blue, gint size)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *context = cairo_create(surface);
    gdouble corner_radius = MAX(2.0, size / 4.0);
    gdouble inset = 0.5;
    gdouble extent = size - 1.0;

    cairo_set_source_rgba(context, 0, 0, 0, 0);
    cairo_paint(context);

    cairo_new_sub_path(context);
    cairo_arc(context, extent - corner_radius, inset + corner_radius, corner_radius, -G_PI / 2.0, 0);
    cairo_arc(context, extent - corner_radius, extent - corner_radius, corner_radius, 0, G_PI / 2.0);
    cairo_arc(context, inset + corner_radius, extent - corner_radius, corner_radius, G_PI / 2.0, G_PI);
    cairo_arc(context, inset + corner_radius, inset + corner_radius, corner_radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_close_path(context);

    cairo_set_source_rgb(context, red / 255.0, green / 255.0, blue / 255.0);
    cairo_fill_preserve(context);

    cairo_set_source_rgba(context, 1, 1, 1, 0.18);
    cairo_set_line_width(context, 1.0);
    cairo_stroke(context);

    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, size, size);
    cairo_destroy(context);
    cairo_surface_destroy(surface);
    return pixbuf;
}

static GdkPixbuf *trim_transparent_padding(GdkPixbuf *pixbuf)
{
    if (pixbuf == NULL || !gdk_pixbuf_get_has_alpha(pixbuf)) {
        return pixbuf != NULL ? g_object_ref(pixbuf) : NULL;
    }

    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);
    gint rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    gint channels = gdk_pixbuf_get_n_channels(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);

    gint min_x = width;
    gint min_y = height;
    gint max_x = -1;
    gint max_y = -1;

    for (gint y = 0; y < height; y++) {
        for (gint x = 0; x < width; x++) {
            guchar *pixel = pixels + (y * rowstride) + (x * channels);
            if (pixel[3] < CODEXBAR_LOGO_ALPHA_TRIM_THRESHOLD) {
                continue;
            }

            min_x = MIN(min_x, x);
            min_y = MIN(min_y, y);
            max_x = MAX(max_x, x);
            max_y = MAX(max_y, y);
        }
    }

    if (max_x < 0 || max_y < 0) {
        return g_object_ref(pixbuf);
    }

    if (min_x == 0 && min_y == 0 && max_x == width - 1 && max_y == height - 1) {
        return g_object_ref(pixbuf);
    }

    GdkPixbuf *subpixbuf = gdk_pixbuf_new_subpixbuf(
        pixbuf,
        min_x,
        min_y,
        max_x - min_x + 1,
        max_y - min_y + 1);
    GdkPixbuf *trimmed = gdk_pixbuf_copy(subpixbuf);
    g_object_unref(subpixbuf);
    return trimmed;
}

static GdkPixbuf *scale_pixbuf_to_fit(GdkPixbuf *pixbuf, gint max_size)
{
    if (pixbuf == NULL) {
        return NULL;
    }

    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);
    if (width <= 0 || height <= 0) {
        return g_object_ref(pixbuf);
    }

    if (width == max_size && height == max_size) {
        return g_object_ref(pixbuf);
    }

    gint scaled_width = max_size;
    gint scaled_height = max_size;
    if (width > height) {
        scaled_height = MAX(1, (gint) (((gdouble) height * ((gdouble) max_size / (gdouble) width)) + 0.5));
    } else if (height > width) {
        scaled_width = MAX(1, (gint) (((gdouble) width * ((gdouble) max_size / (gdouble) height)) + 0.5));
    }

    return gdk_pixbuf_scale_simple(pixbuf, scaled_width, scaled_height, GDK_INTERP_BILINEAR);
}

static GdkPixbuf *create_provider_logo_badge_pixbuf(const ProviderVisualStyle *style, gint size)
{
    if (style == NULL || style->logo_filename == NULL) {
        return NULL;
    }

    gchar *logo_path = resolve_provider_logo_path(style->logo_filename);
    if (logo_path == NULL) {
        return NULL;
    }

    GError *error = NULL;
    gint logo_size = MAX(size, 8);
    GdkPixbuf *logo = gdk_pixbuf_new_from_file(logo_path, &error);
    g_free(logo_path);
    if (logo == NULL) {
        g_clear_error(&error);
        return NULL;
    }

    GdkPixbuf *trimmed_logo = trim_transparent_padding(logo);
    g_object_unref(logo);
    GdkPixbuf *scaled_logo = scale_pixbuf_to_fit(trimmed_logo, logo_size);
    g_object_unref(trimmed_logo);
    if (scaled_logo == NULL) {
        return NULL;
    }

    GdkPixbuf *badge = create_logo_chip_pixbuf(style->red, style->green, style->blue, size);
    if (badge == NULL) {
        g_object_unref(scaled_logo);
        return NULL;
    }

    gint logo_width = gdk_pixbuf_get_width(scaled_logo);
    gint logo_height = gdk_pixbuf_get_height(scaled_logo);
    gdk_pixbuf_composite(
        scaled_logo,
        badge,
        (size - logo_width) / 2,
        (size - logo_height) / 2,
        logo_width,
        logo_height,
        (size - logo_width) / 2.0,
        (size - logo_height) / 2.0,
        1.0,
        1.0,
        GDK_INTERP_BILINEAR,
        255);

    g_object_unref(scaled_logo);
    return badge;
}

static GtkWidget *create_theme_icon_image(const gchar *preferred_name, const gchar *fallback_name)
{
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    const gchar *icon_name = NULL;

    if (theme != NULL && preferred_name != NULL && gtk_icon_theme_has_icon(theme, preferred_name)) {
        icon_name = preferred_name;
    } else if (theme != NULL && fallback_name != NULL && gtk_icon_theme_has_icon(theme, fallback_name)) {
        icon_name = fallback_name;
    } else if (preferred_name != NULL) {
        icon_name = preferred_name;
    } else {
        icon_name = fallback_name;
    }

    if (icon_name == NULL) {
        return NULL;
    }

    GtkWidget *image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
    gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
    return image;
}

static GtkWidget *create_provider_badge_image(const gchar *provider_name)
{
    const ProviderVisualStyle *style = provider_visual_style_for_name(provider_name);
    GdkPixbuf *pixbuf = create_provider_logo_badge_pixbuf(style, CODEXBAR_MENU_ICON_SIZE);
    if (pixbuf == NULL) {
        gchar *badge_text = style != NULL ? g_strdup(style->badge_text) : fallback_badge_text(provider_name);
        guint8 red = style != NULL ? style->red : 71;
        guint8 green = style != NULL ? style->green : 85;
        guint8 blue = style != NULL ? style->blue : 105;
        pixbuf = create_badge_pixbuf(badge_text, red, green, blue, CODEXBAR_MENU_ICON_SIZE);
        g_free(badge_text);
    }

    GtkWidget *image = pixbuf != NULL ? gtk_image_new_from_pixbuf(pixbuf) : NULL;
    if (pixbuf != NULL) {
        g_object_unref(pixbuf);
    }
    return image;
}

static GtkWidget *create_app_icon_image(CodexBarLinuxState *state, gint size)
{
    if (state != NULL && state->icon_path != NULL) {
        GError *error = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(state->icon_path, size, size, TRUE, &error);
        if (pixbuf != NULL) {
            GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
            g_object_unref(pixbuf);
            return image;
        }
        g_clear_error(&error);
    }

    return create_theme_icon_image("utilities-system-monitor", "utilities-terminal");
}

static const gchar *status_label_for_indicator(const gchar *indicator)
{
    if (g_strcmp0(indicator, "none") == 0) {
        return "Operational";
    }
    if (g_strcmp0(indicator, "minor") == 0) {
        return "Partial outage";
    }
    if (g_strcmp0(indicator, "major") == 0) {
        return "Major outage";
    }
    if (g_strcmp0(indicator, "critical") == 0) {
        return "Critical issue";
    }
    if (g_strcmp0(indicator, "maintenance") == 0) {
        return "Maintenance";
    }
    return "Status unknown";
}

static GtkWidget *create_line_icon_image(const gchar *line)
{
    if (line == NULL || *line == '\0') {
        return NULL;
    }

    if (g_str_has_prefix(line, "Session:") ||
        g_str_has_prefix(line, "Weekly:") ||
        g_str_has_prefix(line, "Premium:") ||
        g_str_has_prefix(line, "Chat:") ||
        g_str_has_prefix(line, "Opus:") ||
        g_str_has_prefix(line, "Budget:"))
    {
        return create_theme_icon_image("utilities-system-monitor", "dialog-information");
    }

    if (g_str_has_prefix(line, "Source:")) {
        return create_theme_icon_image("utilities-terminal", "dialog-information");
    }

    if (g_str_has_prefix(line, "Profile:") || g_str_has_prefix(line, "Account:")) {
        return create_theme_icon_image("avatar-default", "system-users");
    }

    if (g_str_has_prefix(line, "Plan:")) {
        return create_theme_icon_image("emblem-favorite", "dialog-information");
    }

    if (g_str_has_prefix(line, "Status:")) {
        if (strstr(line, "Operational") != NULL) {
            return create_theme_icon_image("emblem-ok", "dialog-information");
        }
        if (strstr(line, "Critical") != NULL) {
            return create_theme_icon_image("dialog-error", "dialog-warning");
        }
        return create_theme_icon_image("dialog-warning", "dialog-information");
    }

    if (strstr(line, "unavailable") != NULL ||
        strstr(line, "Unknown error") != NULL ||
        g_str_has_prefix(line, "Error"))
    {
        return create_theme_icon_image("dialog-error", "dialog-warning");
    }

    return create_theme_icon_image("dialog-information", "utilities-system-monitor");
}

static const gchar *usage_title_for_provider_window(const gchar *provider_id, gint slot_index)
{
    if (g_strcmp0(provider_id, "copilot") == 0) {
        return slot_index == 0 ? "Premium" : "Chat";
    }

    switch (slot_index) {
    case 0:
        return "Session";
    case 1:
        return "Weekly";
    default:
        return "Opus";
    }
}

static gchar *format_remaining_line(const gchar *title, JsonObject *window, gdouble *remaining_out, gboolean *has_remaining_out)
{
    gdouble used_percent = 0;
    if (!json_object_lookup_double_member(window, "usedPercent", &used_percent)) {
        return NULL;
    }

    gdouble remaining_percent = MAX(0.0, 100.0 - used_percent);
    if (remaining_out != NULL) {
        *remaining_out = remaining_percent;
    }
    if (has_remaining_out != NULL) {
        *has_remaining_out = TRUE;
    }

    return g_strdup_printf("%s: %.0f%% left", title, remaining_percent);
}

static gboolean parse_provider_payloads(
    const gchar *json_text,
    gchar **content_out,
    gchar **label_out,
    gboolean *had_success_out,
    gboolean *had_error_out,
    gchar **error_message_out)
{
    JsonParser *parser = json_parser_new();
    GError *parse_error = NULL;
    if (!json_parser_load_from_data(parser, json_text, -1, &parse_error)) {
        *error_message_out = g_strdup_printf(
            "Unable to parse CodexBarCLI JSON output: %s",
            parse_error != NULL ? parse_error->message : "unknown error");
        g_clear_error(&parse_error);
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (root == NULL || !JSON_NODE_HOLDS_ARRAY(root)) {
        *error_message_out = g_strdup("CodexBarCLI JSON output did not contain a payload array.");
        g_object_unref(parser);
        return FALSE;
    }

    JsonArray *payloads = json_node_get_array(root);
    if (payloads == NULL || json_array_get_length(payloads) == 0) {
        *error_message_out = g_strdup("CodexBarCLI JSON output was empty.");
        g_object_unref(parser);
        return FALSE;
    }

    GString *content = g_string_new(NULL);
    gchar *label = NULL;
    gboolean had_success = FALSE;
    gboolean had_error = FALSE;

    for (guint index = 0; index < json_array_get_length(payloads); index++) {
        JsonObject *payload = json_array_get_object_element(payloads, index);
        if (payload == NULL) {
            continue;
        }

        const gchar *provider_id = json_object_get_string_member_with_default(payload, "provider", "unknown");
        gchar *provider_name = humanize_provider_name(provider_id);
        JsonObject *error = json_object_member_as_object(payload, "error");
        JsonObject *usage = json_object_member_as_object(payload, "usage");
        JsonObject *status = json_object_member_as_object(payload, "status");
        gchar *source = json_object_dup_string_member(payload, "source");
        gchar *version = json_object_dup_string_member(payload, "version");
        gchar *account = json_object_dup_string_member(payload, "account");
        gchar *account_email = json_object_dup_identity_string_member(usage, "accountEmail");
        gchar *login_method = json_object_dup_identity_string_member(usage, "loginMethod");

        if (content->len > 0) {
            g_string_append(content, "\n\n");
        }
        g_string_append_printf(content, "== %s ==\n", provider_name);

        if (error != NULL) {
            gchar *message = json_object_dup_string_member(error, "message");
            gchar *formatted = format_provider_error(provider_id, message);
            const gchar *detail = strchr(formatted, '\n');
            g_string_append(content, detail != NULL ? detail + 1 : formatted);
            had_error = TRUE;
            g_free(formatted);
            g_free(message);
        } else if (usage != NULL) {
            gboolean appended_usage = FALSE;
            JsonObject *primary = json_object_member_as_object(usage, "primary");
            JsonObject *secondary = json_object_member_as_object(usage, "secondary");
            JsonObject *tertiary = json_object_member_as_object(usage, "tertiary");
            JsonObject *provider_cost = json_object_member_as_object(usage, "providerCost");

            gdouble remaining = 0;
            gboolean has_remaining = FALSE;
            gchar *line = format_remaining_line(usage_title_for_provider_window(provider_id, 0), primary, &remaining, &has_remaining);
            if (line != NULL) {
                g_string_append(content, line);
                g_free(line);
                appended_usage = TRUE;
                if (label == NULL && has_remaining) {
                    label = g_strdup_printf("%.0f%%", remaining);
                }
            }

            has_remaining = FALSE;
            line = format_remaining_line(usage_title_for_provider_window(provider_id, 1), secondary, &remaining, &has_remaining);
            if (line != NULL) {
                if (appended_usage) {
                    g_string_append_c(content, '\n');
                }
                g_string_append(content, line);
                g_free(line);
                appended_usage = TRUE;
                if (label == NULL && has_remaining) {
                    label = g_strdup_printf("%.0f%%", remaining);
                }
            }

            line = format_remaining_line(usage_title_for_provider_window(provider_id, 2), tertiary, NULL, NULL);
            if (line != NULL) {
                if (appended_usage) {
                    g_string_append_c(content, '\n');
                }
                g_string_append(content, line);
                g_free(line);
                appended_usage = TRUE;
            }

            if (!appended_usage && provider_cost != NULL) {
                gdouble used = 0;
                gdouble limit = 0;
                gchar *currency = json_object_dup_string_member(provider_cost, "currencyCode");
                if (json_object_lookup_double_member(provider_cost, "used", &used) &&
                    json_object_lookup_double_member(provider_cost, "limit", &limit)) {
                    g_string_append_printf(
                        content,
                        "Budget: %.1f / %.1f%s%s",
                        used,
                        limit,
                        currency != NULL ? " " : "",
                        currency != NULL ? currency : "");
                    appended_usage = TRUE;
                }
                g_free(currency);
            }

            if (!appended_usage) {
                g_string_append(content, "Usage data available.");
            }
            had_success = TRUE;
        } else {
            g_string_append(content, "No usage data available.");
        }

        if (source != NULL || version != NULL) {
            g_string_append_printf(
                content,
                "\nSource: %s%s%s%s",
                source != NULL ? source : "auto",
                version != NULL ? " | Version: " : "",
                version != NULL ? version : "",
                "");
        }
        if (account != NULL) {
            g_string_append_printf(content, "\nProfile: %s", account);
        }
        if (account_email != NULL) {
            g_string_append_printf(content, "\nAccount: %s", account_email);
        }
        if (login_method != NULL) {
            g_string_append_printf(content, "\nPlan: %s", login_method);
        }
        if (status != NULL) {
            gchar *indicator = json_object_dup_string_member(status, "indicator");
            gchar *description = json_object_dup_string_member(status, "description");
            g_string_append_printf(content, "\nStatus: %s", status_label_for_indicator(indicator));
            if (description != NULL) {
                g_string_append_printf(content, " - %s", description);
            }
            g_free(indicator);
            g_free(description);
        }

        g_free(login_method);
        g_free(account_email);
        g_free(account);
        g_free(version);
        g_free(source);
        g_free(provider_name);
    }

    g_object_unref(parser);
    *content_out = g_string_free(content, FALSE);
    *label_out = label;
    *had_success_out = had_success;
    *had_error_out = had_error;
    return TRUE;
}

static gboolean run_cli_with_format(
    CodexBarLinuxState *state,
    const gchar *provider,
    const gchar *format,
    gboolean allow_stdout_on_failure,
    gchar **output,
    gchar **error_message)
{
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint exit_status = 0;
    GError *spawn_error = NULL;
    const gchar *source = default_source_for_provider(provider);
    GPtrArray *args = g_ptr_array_new();
    g_ptr_array_add(args, state->cli_path);
    g_ptr_array_add(args, "--format");
    g_ptr_array_add(args, (gpointer) format);
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
        if (allow_stdout_on_failure && stdout_text != NULL && *stdout_text != '\0') {
            *output = stdout_text;
            g_clear_error(&exit_error);
            g_free(stderr_text);
            return TRUE;
        }
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

static gboolean run_cli(CodexBarLinuxState *state, const gchar *provider, gchar **output, gchar **error_message)
{
    return run_cli_with_format(state, provider, "text", FALSE, output, error_message);
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

static GtkWidget *append_menu_row(GtkWidget *menu, const gchar *label, gboolean sensitive, gboolean use_markup, GtkWidget *icon_widget)
{
    GtkWidget *item = gtk_menu_item_new();
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *menu_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(menu_label), 0.0);
    gtk_label_set_use_markup(GTK_LABEL(menu_label), use_markup);
    gtk_label_set_line_wrap(GTK_LABEL(menu_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(menu_label), 48);
    gtk_widget_set_margin_top(row, 1);
    gtk_widget_set_margin_bottom(row, 1);
    if (use_markup) {
        gtk_label_set_markup(GTK_LABEL(menu_label), label);
    } else {
        gtk_label_set_text(GTK_LABEL(menu_label), label);
    }
    if (icon_widget != NULL) {
        gtk_box_pack_start(GTK_BOX(row), icon_widget, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(row), menu_label, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(item), row);
    gtk_widget_set_sensitive(item, sensitive);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    if (icon_widget != NULL) {
        gtk_widget_show(icon_widget);
    }
    gtk_widget_show(row);
    gtk_widget_show(menu_label);
    gtk_widget_show(item);
    return item;
}

static void append_disabled_row(GtkWidget *menu, const gchar *label)
{
    append_menu_row(menu, label, FALSE, FALSE, create_line_icon_image(label));
}

static void append_header_row(GtkWidget *menu, const gchar *markup, GtkWidget *icon_widget)
{
    append_menu_row(menu, markup, TRUE, TRUE, icon_widget);
}

static gchar *provider_header_label(const gchar *line)
{
    gsize length = strlen(line);
    if (length < 8 || !g_str_has_prefix(line, "== ") || !g_str_has_suffix(line, " ==")) {
        return NULL;
    }

    return g_strndup(line + 3, length - 6);
}

static gchar *provider_header_markup(const gchar *header)
{
    gchar *escaped = g_markup_escape_text(header, -1);
    gchar *markup = g_strdup_printf("<b>%s</b>", escaped);
    g_free(escaped);
    return markup;
}

static GtkWidget *build_menu(CodexBarLinuxState *state, const gchar *content, gboolean is_error)
{
    GtkWidget *menu = gtk_menu_new();
    gboolean last_was_divider = TRUE;

    append_header_row(menu, "<b>" SESSIONUSAGE_APP_NAME "</b>", create_app_icon_image(state, CODEXBAR_MENU_ICON_SIZE));
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
                gchar *header_label = provider_header_label(line);
                if (header_label != NULL) {
                    gchar *header_markup = provider_header_markup(header_label);
                    append_header_row(menu, header_markup, create_provider_badge_image(header_label));
                    g_free(header_markup);
                    g_free(header_label);
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

    GtkWidget *refresh_item = append_menu_row(
        menu,
        "Refresh",
        TRUE,
        FALSE,
        create_theme_icon_image("view-refresh-symbolic", "view-refresh"));
    g_signal_connect(refresh_item, "activate", G_CALLBACK(refresh_clicked_cb), state);

    GtkWidget *quit_item = append_menu_row(
        menu,
        "Quit",
        TRUE,
        FALSE,
        create_theme_icon_image("application-exit", "window-close"));
    g_signal_connect(quit_item, "activate", G_CALLBACK(quit_clicked_cb), state);

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

        gchar *json_output = NULL;
        gchar *json_error = NULL;
        gchar *structured_content = NULL;
        gchar *structured_label = NULL;
        gchar *parse_error = NULL;
        gboolean provider_success = FALSE;
        gboolean provider_error = FALSE;

        if (run_cli_with_format(state, provider, "json", TRUE, &json_output, &json_error) &&
            parse_provider_payloads(
                json_output,
                &structured_content,
                &structured_label,
                &provider_success,
                &provider_error,
                &parse_error))
        {
            if (content->len > 0) {
                g_string_append(content, "\n\n");
            }
            g_string_append(content, structured_content);
            if (label == NULL && structured_label != NULL) {
                label = g_strdup(structured_label);
            }
            hadSuccess = hadSuccess || provider_success;
            hadError = hadError || provider_error;
            g_free(structured_label);
            g_free(structured_content);
            g_free(parse_error);
            g_free(json_error);
            g_free(json_output);
            continue;
        }

        g_free(structured_label);
        g_free(structured_content);
        g_free(parse_error);
        g_free(json_error);
        g_free(json_output);

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
        "sessionusage-linux",
        state->icon_name != NULL ? state->icon_name : "utilities-system-monitor",
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
        app_indicator_set_icon_full(state->indicator, state->icon_name, SESSIONUSAGE_APP_NAME);
    }
    app_indicator_set_attention_icon_full(state->indicator, "dialog-error", SESSIONUSAGE_APP_NAME " Linux error");
    app_indicator_set_title(state->indicator, SESSIONUSAGE_APP_NAME);

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
