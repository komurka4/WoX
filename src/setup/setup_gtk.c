/* Phase 331: the setup tool's GTK 3 window (Linux). Shows and collects the values of setup_model.c; every rule is
 * there. Owner-approved host tool, not original behaviour. */
#include "setup_model.h"
#include "setup_title.h" /* SETUP_MAIN_TITLE: the main window title with SETUP_VERSION (Phase 338) */
#include "xeen/wox_config.h"
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char exe_dir[SETUP_PATH_MAX], wox_path[SETUP_PATH_MAX + 16];
    setup_doc doc;
    setup_settings s;
    setup_xeen_cfg cfg;
    int cfg_ok;                    /* a proper XEEN.CFG is loaded */
    setup_cfg_state cfg_state;     /* the last read_cfg's state (Create default... for ABSENT and INVALID) */
    char cfg_dir_read[SETUP_PATH_MAX]; /* the XEEN.CFG location it was read from */
    GtkWidget *window, *dir_entry[SETUP_DIRS], *speech, *music, *cfg_note, *cfg_create, *rom_note, *rom_get;
    GtkWidget *aspect_note;
    GtkWidget *fullscreen, *scale, *aspect, *locked;
    GtkSizeGroup *labels; /* the first column of the XEEN.CFG and Game files frames, aligned */
    char data_before[SETUP_PATH_MAX]; /* the data text when XEEN.CFG's last followed it */
    /* a running ROM download (Phase 335): curl or wget writes the archive to a temporary file */
    int downloading;
    setup_rom_set download_set;
    char download_dir[SETUP_PATH_MAX];
    char *download_file;
    int download_err;  /* the downloader's standard error */
    guint download_tick; /* the progress timer */
    /* the window's state when it was built (Phase 335: Cancel asks only after a change) */
    setup_settings initial;
    int initial_speech, initial_music;
} app_state;

static const char *const dir_labels[SETUP_DIRS] = {"XEEN.CFG location",   "Data location",       "Save location",
                                                   "MT-32 roms location", "SC-55 roms location", "CD tracks location"};

/* ------------------------------------------------------------------ dialogs */
/** A message dialog of TYPE with BUTTONS: TEXT's first paragraph as the headline, the rest (after a blank line) as
 * the details. Returns the dialog's response. */
static int message(GtkWidget *parent, GtkMessageType type, GtkButtonsType buttons, const char *title, const char *text)
{
    const char *rest = strstr(text, "\n\n");
    char *head = g_strndup(text, rest ? (gsize)(rest - text) : strlen(text));
    GtkWidget *d = gtk_message_dialog_new(parent ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL, type, buttons, "%s",
                                          head);
    int r;
    g_free(head);
    if (rest) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", rest + 2);
    }
    gtk_window_set_title(GTK_WINDOW(d), title);
    gtk_window_set_default_size(GTK_WINDOW(d), 460, -1);
    r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r;
}
static int ask(GtkWidget *parent, GtkMessageType type, const char *title, const char *text)
{
    return message(parent, type, GTK_BUTTONS_YES_NO, title, text) == GTK_RESPONSE_YES;
}
static void tell(GtkWidget *parent, GtkMessageType type, const char *title, const char *text)
{
    (void)message(parent, type, GTK_BUTTONS_OK, title, text);
}
/** A label aligned to the left of its cell. */
static GtkWidget *left_label(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    return l;
}
/** A big choice button: TITLE in large bold, then the optional NOTE (Pango markup) in smaller text. */
static GtkWidget *choice_button(const char *title, const char *note_markup)
{
    GtkWidget *b = gtk_button_new(), *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8), *t = gtk_label_new(NULL);
    char *m = g_markup_printf_escaped("<span size='large' weight='bold'>%s</span>", title);
    gtk_label_set_markup(GTK_LABEL(t), m);
    g_free(m);
    gtk_box_pack_start(GTK_BOX(box), t, FALSE, FALSE, 0);
    if (note_markup) {
        GtkWidget *n = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(n), note_markup);
        gtk_label_set_line_wrap(GTK_LABEL(n), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(n), 30);
        gtk_label_set_justify(GTK_LABEL(n), GTK_JUSTIFY_CENTER);
        gtk_box_pack_start(GTK_BOX(box), n, FALSE, FALSE, 0);
    }
    gtk_widget_set_size_request(b, 230, 150);
    gtk_container_add(GTK_CONTAINER(b), box);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    return b;
}
static void respond_one(GtkButton *b, gpointer dialog)
{
    (void)b;
    gtk_dialog_response(GTK_DIALOG(dialog), 1);
}
static void respond_two(GtkButton *b, gpointer dialog)
{
    (void)b;
    gtk_dialog_response(GTK_DIALOG(dialog), 2);
}
/** One first-run screen: HEADING, two choice buttons, FOOTER. Returns 1, 2 or 0 (closed). */
static int choose(const char *intro, const char *heading, GtkWidget *one, GtkWidget *two, const char *footer)
{
    GtkWidget *d = gtk_dialog_new(), *area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24), *h = gtk_label_new(NULL);
    char *m = g_markup_printf_escaped("<b>%s</b>", heading);
    int r;
    gtk_window_set_title(GTK_WINDOW(d), "WoX - Setup");
    gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(area), 18);
    gtk_box_set_spacing(GTK_BOX(area), 14);
    if (intro) {
        GtkWidget *i = gtk_label_new(intro);
        gtk_widget_set_halign(i, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(area), i, FALSE, FALSE, 0);
    }
    gtk_label_set_markup(GTK_LABEL(h), m);
    g_free(m);
    gtk_widget_set_halign(h, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(area), h, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), one, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), two, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(area), row, FALSE, FALSE, 0);
    g_signal_connect(one, "clicked", G_CALLBACK(respond_one), d);
    g_signal_connect(two, "clicked", G_CALLBACK(respond_two), d);
    if (footer) {
        GtkWidget *f = gtk_label_new(footer);
        gtk_widget_set_halign(f, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(area), f, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(d);
    r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r == 1 || r == 2 ? r : 0;
}
/** The first run: the two choices, then wox.cfg (and the GOG folder) written; REPLACE: an existing wox.cfg is
 * replaced (its welcome says so). 1 done, 0 closed or failed (then nothing was written). */
static int first_run(app_state *a, int replace)
{
    char err[1600];
    int version, style;
    int gog_found = setup_has_gog_image(a->exe_dir);
    version = choose(replace ? SETUP_TEXT_WELCOME_REPLACE : SETUP_TEXT_WELCOME_NEW,
                     "Which version of the game do you have?",
                     choice_button("GOG version", gog_found ? "<b>Recommended</b>" : NULL),
                     choice_button("Other version", SETUP_TEXT_OTHER_NOTE), NULL);
    if (!version) {
        return 0;
    }
    style = choose(NULL, "How would you like to play?",
                   choice_button("Vanilla version",
                                 "<i>For those who want to play like it was 1993, prepared pen and piece of paper and "
                                 "love to spend hours dealing with equipment</i>"),
                   choice_button("Enhanced version", "<i>" SETUP_TEXT_ENHANCED_NOTE "</i>"),
                   "You can customize any addons and bugfixes at any time");
    if (!style) {
        return 0;
    }
    if (!setup_first_run(a->wox_path, version == 1, style == 2, err, sizeof err)) {
        tell(NULL, GTK_MESSAGE_ERROR, "Setup", err);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ the settings window */
static void entry_text(app_state *a, setup_dir k, char *out, size_t n)
{
    snprintf(out, n, "%s", gtk_entry_get_text(GTK_ENTRY(a->dir_entry[k])));
}
/** The Music list's choice (setup_music). */
static setup_music music_choice(app_state *a)
{
    int i = gtk_combo_box_get_active(GTK_COMBO_BOX(a->music));
    return i < 0 ? SETUP_MUSIC_NONE : (setup_music)i;
}
/** The settings with the ROM fields' current text. */
static setup_settings rom_fields(app_state *a)
{
    setup_settings s = a->s;
    entry_text(a, SETUP_DIR_MT32, s.dir[SETUP_DIR_MT32], sizeof s.dir[0]);
    entry_text(a, SETUP_DIR_SC55, s.dir[SETUP_DIR_SC55], sizeof s.dir[0]);
    return s;
}
/** Shows or hides the ROM warning, and the Download button left of it, for the Music choice. A running download
 * keeps its progress there until it ends. */
static void update_rom_note(app_state *a)
{
    char msg[1600];
    setup_settings s = rom_fields(a);
    int warn;
    if (a->downloading) {
        return;
    }
    warn = a->cfg_ok && setup_rom_warning(&s, music_choice(a), a->exe_dir, msg, sizeof msg);
    if (warn) {
        gtk_label_set_text(GTK_LABEL(a->rom_note), msg);
    }
    gtk_widget_set_visible(a->rom_note, warn);
    gtk_widget_set_visible(a->rom_get, warn);
}

/* ------------------------------------------------------------------ the ROM download (Phase 335) */
/** "Downloading the SC-55 ROMs... 5.2 MB" from the temporary file's size. */
static gboolean download_tick(gpointer user)
{
    app_state *a = user;
    char text[200];
    GStatBuf st;
    double mb = a->download_file && !g_stat(a->download_file, &st) ? (double)st.st_size / 1e6 : 0.0;
    snprintf(text, sizeof text, "Downloading the %s ROMs from archive.org... %.1f MB",
             setup_rom_set_name(a->download_set), mb);
    gtk_label_set_text(GTK_LABEL(a->rom_note), text);
    return G_SOURCE_CONTINUE;
}
/** The downloader's last words (its standard error), for the failure message. */
static void read_downloader_error(app_state *a, char *out, size_t n)
{
    size_t len = 0;
    out[0] = 0;
    if (a->download_err >= 0) {
        ssize_t got;
        while (len + 1u < n && (got = read(a->download_err, out + len, n - 1u - len)) > 0) {
            len += (size_t)got;
        }
        out[len] = 0;
        close(a->download_err);
        a->download_err = -1;
    }
    while (len && (out[len - 1u] == '\n' || out[len - 1u] == '\r')) {
        out[--len] = 0;
    }
}
/** The downloader ended: extracts the ROMs from its archive, deletes the archive, then reports. */
static void download_done(GPid pid, gint status, gpointer user)
{
    app_state *a = user;
    char msg[1600], err[600], why[600];
    const char *name = setup_rom_set_name(a->download_set);
    gchar *data = NULL;
    gsize size = 0;
    int good = 0;
    g_spawn_close_pid(pid);
    g_source_remove(a->download_tick);
    read_downloader_error(a, why, sizeof why);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(msg, sizeof msg, "The %s ROMs could not be downloaded.\n\n%s\n\n%s", name,
                 why[0] ? why : "The download failed.", setup_rom_set_url(a->download_set));
    } else if (!g_file_get_contents(a->download_file, &data, &size, NULL)) {
        snprintf(msg, sizeof msg, "The %s ROMs could not be downloaded.\n\nThe downloaded file cannot be read.", name);
    } else if (!setup_rom_install(a->download_set, (const unsigned char *)data, size, a->download_dir, err,
                                  sizeof err)) {
        snprintf(msg, sizeof msg, "The %s ROMs could not be installed.\n\n%s", name, err);
    } else {
        snprintf(msg, sizeof msg, "The %s ROMs are installed in %s.", name, a->download_dir);
        good = 1;
    }
    g_free(data);
    g_unlink(a->download_file);
    g_free(a->download_file);
    a->download_file = NULL;
    a->downloading = 0;
    gtk_widget_set_sensitive(a->rom_get, TRUE);
    gtk_button_set_label(GTK_BUTTON(a->rom_get), "Download");
    update_rom_note(a);
    tell(a->window, good ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR, "Download", msg);
}
/** The Download button: curl (or wget) fetches the Music choice's ROM archive into a temporary file. */
static void on_rom_get(GtkButton *b, gpointer user)
{
    app_state *a = user;
    setup_settings s = rom_fields(a);
    setup_rom_set set = setup_music_rom_set(music_choice(a));
    const char *url = setup_rom_set_url(set);
    char *curl, *wget;
    GPid pid;
    GError *error = NULL;
    gint fd;
    (void)b;
    if (a->downloading || !url) {
        return;
    }
    curl = g_find_program_in_path("curl");
    wget = curl ? NULL : g_find_program_in_path("wget");
    if (!curl && !wget) {
        tell(a->window, GTK_MESSAGE_ERROR, "Download", "The ROMs cannot be downloaded.\n\nInstall curl or wget.");
        return;
    }
    fd = g_file_open_tmp("wox-roms-XXXXXX.zip", &a->download_file, &error);
    if (fd < 0) {
        tell(a->window, GTK_MESSAGE_ERROR, "Download", error ? error->message : "No temporary file.");
        g_clear_error(&error);
        g_free(curl);
        return;
    }
    close(fd);
    {
        const char *curl_argv[] = {curl, "-fsSL", "--max-filesize", "67108864", "-o", a->download_file, url, NULL};
        const char *wget_argv[] = {wget, "-q", "-O", a->download_file, url, NULL};
        if (!g_spawn_async_with_pipes(NULL, (gchar **)(curl ? curl_argv : wget_argv), NULL,
                                      G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, &pid, NULL,
                                      NULL, &a->download_err, &error)) {
            tell(a->window, GTK_MESSAGE_ERROR, "Download", error ? error->message : "The download did not start.");
            g_clear_error(&error);
            g_unlink(a->download_file);
            g_free(a->download_file);
            a->download_file = NULL;
            g_free(curl);
            g_free(wget);
            return;
        }
    }
    g_free(curl);
    g_free(wget);
    a->downloading = 1;
    a->download_set = set;
    setup_rom_set_dir(&s, set, a->exe_dir, a->download_dir, sizeof a->download_dir);
    gtk_widget_set_sensitive(a->rom_get, FALSE);
    gtk_button_set_label(GTK_BUTTON(a->rom_get), "Downloading...");
    a->download_tick = g_timeout_add(250, download_tick, a);
    download_tick(a);
    g_child_watch_add(pid, download_done, a);
}
/** (Re)reads XEEN.CFG from the location field: the lists follow it, or are disabled with the reason shown. OFFER_CREATE:
 * a folder without one offers the default first. */
static void read_cfg(app_state *a, int offer_create)
{
    char text[SETUP_PATH_MAX], dir[SETUP_PATH_MAX], note[1600], q[2400];
    setup_cfg_state state;
    entry_text(a, SETUP_DIR_XEEN_CFG, text, sizeof text);
    snprintf(a->cfg_dir_read, sizeof a->cfg_dir_read, "%s", text);
    state = setup_xeen_cfg_locate(a->exe_dir, text, &a->cfg, dir, sizeof dir, note, sizeof note);
    if (state == SETUP_CFG_ABSENT && offer_create) {
        setup_create_question(dir, q, sizeof q);
        if (ask(a->window, GTK_MESSAGE_QUESTION, "XEEN.CFG not found", q)) {
            if (setup_xeen_cfg_create(dir, q, sizeof q)) {
                state = setup_xeen_cfg_locate(a->exe_dir, text, &a->cfg, dir, sizeof dir, note, sizeof note);
            } else {
                tell(a->window, GTK_MESSAGE_ERROR, "XEEN.CFG", q);
            }
        }
    }
    a->cfg_ok = state == SETUP_CFG_PROPER;
    a->cfg_state = state;
    gtk_widget_set_sensitive(a->speech, a->cfg_ok);
    gtk_widget_set_sensitive(a->music, a->cfg_ok);
    gtk_combo_box_set_active(GTK_COMBO_BOX(a->speech), !a->cfg_ok ? -1 : setup_xeen_cfg_speech(&a->cfg) ? 0 : 1);
    gtk_combo_box_set_active(GTK_COMBO_BOX(a->music), !a->cfg_ok       ? -1
                                                      : a->s.system_midi ? SETUP_MUSIC_SYSTEM
                                                                         : (int)setup_xeen_cfg_music(&a->cfg));
    gtk_label_set_text(GTK_LABEL(a->cfg_note), a->cfg_ok ? "" : note);
    gtk_widget_set_visible(a->cfg_note, !a->cfg_ok);
    gtk_widget_set_visible(a->cfg_create, state == SETUP_CFG_ABSENT || state == SETUP_CFG_INVALID);
    update_rom_note(a);
}
/** Create default...: the default XEEN.CFG where there is none, or (after a question) in place of an invalid one. */
static void on_create_cfg(GtkButton *b, gpointer user)
{
    app_state *a = user;
    char text[SETUP_PATH_MAX], dir[SETUP_PATH_MAX], q[2400];
    (void)b;
    if (a->cfg_state != SETUP_CFG_INVALID) {
        read_cfg(a, 1);
        return;
    }
    entry_text(a, SETUP_DIR_XEEN_CFG, text, sizeof text);
    setup_resolve(a->exe_dir, text, dir, sizeof dir);
    setup_replace_question(dir, gtk_label_get_text(GTK_LABEL(a->cfg_note)), q, sizeof q);
    if (ask(a->window, GTK_MESSAGE_WARNING, "XEEN.CFG", q)) {
        if (!setup_xeen_cfg_replace(dir, q, sizeof q)) {
            tell(a->window, GTK_MESSAGE_ERROR, "XEEN.CFG", q);
        }
        read_cfg(a, 0);
    }
}
/** The XEEN.CFG field was edited (Enter or focus left): re-read when it names another folder. */
static void cfg_field_done(app_state *a)
{
    char text[SETUP_PATH_MAX];
    entry_text(a, SETUP_DIR_XEEN_CFG, text, sizeof text);
    if (strcmp(text, a->cfg_dir_read) != 0) {
        read_cfg(a, 1);
    }
}
/** The Data field changed: an XEEN.CFG location that followed it (auto) keeps following it. */
static void data_field_done(app_state *a)
{
    char data[SETUP_PATH_MAX], cfg[SETUP_PATH_MAX];
    entry_text(a, SETUP_DIR_DATA, data, sizeof data);
    entry_text(a, SETUP_DIR_XEEN_CFG, cfg, sizeof cfg);
    if (strcmp(data, a->data_before) != 0 && !strcmp(cfg, a->data_before)) {
        gtk_entry_set_text(GTK_ENTRY(a->dir_entry[SETUP_DIR_XEEN_CFG]), data);
        read_cfg(a, 1);
    }
    snprintf(a->data_before, sizeof a->data_before, "%s", data);
}
/** Enter or focus-out in a directory field. */
static void field_done(app_state *a, GtkWidget *w)
{
    if (w == a->dir_entry[SETUP_DIR_XEEN_CFG]) {
        cfg_field_done(a);
    } else if (w == a->dir_entry[SETUP_DIR_DATA]) {
        data_field_done(a);
    } else {
        update_rom_note(a);
    }
}
static void on_entry_activate(GtkEntry *e, gpointer user)
{
    field_done(user, GTK_WIDGET(e));
}
static gboolean on_entry_focus_out(GtkWidget *w, GdkEvent *ev, gpointer user)
{
    (void)ev;
    field_done(user, w);
    return FALSE;
}
static void on_browse(GtkButton *b, gpointer user)
{
    app_state *a = user;
    GtkWidget *entry = g_object_get_data(G_OBJECT(b), "entry");
    GtkFileChooserNative *d = gtk_file_chooser_native_new("Choose a folder", GTK_WINDOW(a->window),
                                                          GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Select", "_Cancel");
    char start[SETUP_PATH_MAX];
    setup_resolve(a->exe_dir, gtk_entry_get_text(GTK_ENTRY(entry)), start, sizeof start);
    if (g_file_test(start, G_FILE_TEST_IS_DIR)) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), start);
    }
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *chosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d)), text[SETUP_PATH_MAX];
        if (chosen) {
            setup_relative_to(a->exe_dir, chosen, text, sizeof text);
            gtk_entry_set_text(GTK_ENTRY(entry), text);
            g_free(chosen);
            field_done(a, entry);
        }
    }
    g_object_unref(d);
}
static void on_music_changed(GtkComboBox *c, gpointer user)
{
    (void)c;
    update_rom_note(user);
}
static void on_aspect_toggled(GtkToggleButton *t, gpointer user)
{
    app_state *a = user;
    gtk_widget_set_visible(a->aspect_note, gtk_toggle_button_get_active(t));
}

/* ------------------------------------------------------------------ Addons and Bugfixes */
typedef struct {
    GtkWidget *check[32], *style[SETUP_WIZARD_STYLES];
} flag_window;
static void on_parent_toggled(GtkToggleButton *t, gpointer child)
{
    gtk_widget_set_sensitive(child, gtk_toggle_button_get_active(t));
}
/** A gray key label (the wox.cfg spelling) for the flag windows' third column. */
static GtkWidget *key_label(const char *key)
{
    GtkWidget *l = gtk_label_new(key);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}
/** One checkbox row per flag of FLAGS (COUNT) from VALUES; a flag under a parent is indented and follows it. */
static void add_flag_rows(GtkWidget *grid, flag_window *w, const setup_flag *flags, unsigned count, const int *values)
{
    for (unsigned i = 0; i < count; ++i) {
        w->check[i] = gtk_check_button_new_with_label(flags[i].label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->check[i]), values[i]);
        gtk_widget_set_margin_start(w->check[i], flags[i].parent >= 0 ? 24 : 0);
        gtk_grid_attach(GTK_GRID(grid), w->check[i], 0, (int)i, 2, 1);
        gtk_grid_attach(GTK_GRID(grid), key_label(flags[i].key), 2, (int)i, 1, 1);
        if (flags[i].parent >= 0) {
            g_signal_connect(w->check[flags[i].parent], "toggled", G_CALLBACK(on_parent_toggled), w->check[i]);
            gtk_widget_set_sensitive(w->check[i], values[flags[i].parent]);
        }
    }
}
/** The three wizard_eye_plus style lists from row ROW on, enabled while the add-on's checkbox is on. */
static void add_style_rows(GtkWidget *grid, flag_window *w, int row, const unsigned *styles)
{
    static const char *const kinds[SETUP_WIZARD_STYLES] = {"Indoor maps", "Outdoor maps", "Sky maps"};
    GtkWidget *wizard_eye = w->check[setup_addon_index("wizard_eye_plus")];
    for (unsigned k = 0; k < SETUP_WIZARD_STYLES; ++k, ++row) {
        GtkWidget *label = gtk_label_new(kinds[k]);
        w->style[k] = gtk_combo_box_text_new();
        for (unsigned v = 0; v < SETUP_WIZARD_STYLES; ++v) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->style[k]), setup_wizard_style_names[v]);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->style[k]), (int)styles[k]);
        gtk_widget_set_margin_start(label, 48);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_halign(w->style[k], GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w->style[k], 1, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), key_label(setup_wizard_style_keys[k]), 2, row, 1, 1);
        gtk_widget_set_sensitive(w->style[k], gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wizard_eye)));
        g_signal_connect(wizard_eye, "toggled", G_CALLBACK(on_parent_toggled), w->style[k]);
    }
}
/** The Addons (ADDONS) or Bugfixes window: its OK keeps the changes in A's settings (written by Save only). */
static void flags_dialog(app_state *a, int addons)
{
    const setup_flag *flags = addons ? setup_addons : setup_bugfixes;
    unsigned count = addons ? setup_addon_count : setup_bugfix_count;
    int *values = addons ? a->s.addon : a->s.bugfix;
    GtkWidget *d = gtk_dialog_new_with_buttons(addons ? "Addons" : "Bugfixes", GTK_WINDOW(a->window), GTK_DIALOG_MODAL,
                                               "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, NULL);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(d)), *grid = gtk_grid_new();
    GtkWidget *intro = left_label(addons ? "Optional additions that are not part of the original game."
                                         : "Corrections of original game errors (not original behaviour).");
    flag_window w;
    memset(&w, 0, sizeof w);
    gtk_container_set_border_width(GTK_CONTAINER(area), 14);
    gtk_box_pack_start(GTK_BOX(area), intro, FALSE, FALSE, 6);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    add_flag_rows(grid, &w, flags, count, values);
    if (addons) {
        add_style_rows(grid, &w, (int)count, a->s.wizard_style);
    }
    gtk_box_pack_start(GTK_BOX(area), grid, FALSE, FALSE, 6);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        for (unsigned i = 0; i < count; ++i) {
            values[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w.check[i]));
        }
        for (unsigned k = 0; addons && k < SETUP_WIZARD_STYLES; ++k) {
            int v = gtk_combo_box_get_active(GTK_COMBO_BOX(w.style[k]));
            a->s.wizard_style[k] = v < 0 ? SETUP_STYLE_BLACK : (unsigned)v;
        }
    }
    gtk_widget_destroy(d);
}
static void on_addons(GtkButton *b, gpointer user)
{
    (void)b;
    flags_dialog(user, 1);
}
static void on_bugfixes(GtkButton *b, gpointer user)
{
    (void)b;
    flags_dialog(user, 0);
}

/* ------------------------------------------------------------------ Save and Cancel */
/** The window's fields and switches into S (the add-on and bug-fix lists are already in a->s). */
static void collect_into(app_state *a, setup_settings *s)
{
    for (unsigned k = 0; k < SETUP_DIRS; ++k) {
        entry_text(a, (setup_dir)k, s->dir[k], sizeof s->dir[k]);
    }
    s->fullscreen = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(a->fullscreen));
    s->aspect = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(a->aspect));
    s->locked_scale = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(a->locked));
    s->scale = (unsigned)gtk_combo_box_get_active(GTK_COMBO_BOX(a->scale)) + 1u;
    if (a->cfg_ok) {
        s->system_midi = music_choice(a) == SETUP_MUSIC_SYSTEM;
    }
}
/** Collects the window's values into A->s. */
static void collect(app_state *a)
{
    collect_into(a, &a->s);
}
/** Remembers the window's state as built (window_changed compares with it). */
static void remember_window(app_state *a)
{
    a->initial = a->s;
    collect_into(a, &a->initial);
    a->initial_speech = gtk_combo_box_get_active(GTK_COMBO_BOX(a->speech));
    a->initial_music = gtk_combo_box_get_active(GTK_COMBO_BOX(a->music));
}
/** Whether anything Save would write differs from the window as it was built. */
static int window_changed(app_state *a)
{
    setup_settings now = a->s;
    collect_into(a, &now);
    return !setup_settings_same(&now, &a->initial) ||
           gtk_combo_box_get_active(GTK_COMBO_BOX(a->speech)) != a->initial_speech ||
           gtk_combo_box_get_active(GTK_COMBO_BOX(a->music)) != a->initial_music;
}
/** The folders Save needs: a missing save or XEEN.CFG folder is created on request; a missing data folder is only
 * confirmed (setup_missing_folder, setup_missing_data). 1 when Save may go on. */
static int check_folders(app_state *a)
{
    char q[2400];
    setup_dir which;
    while (setup_missing_folder(&a->s, a->exe_dir, &which, q, sizeof q)) {
        if (!ask(a->window, GTK_MESSAGE_QUESTION, "Setup", q)) {
            return 0;
        }
        if (!setup_make_dirs(a->exe_dir, a->s.dir[which])) {
            tell(a->window, GTK_MESSAGE_ERROR, "Setup", "The folder could not be created.");
            return 0;
        }
    }
    return !setup_missing_data(&a->s, a->exe_dir, q, sizeof q) || ask(a->window, GTK_MESSAGE_WARNING, "Setup", q);
}
static void on_save(GtkButton *b, gpointer user)
{
    app_state *a = user;
    char err[1600];
    (void)b;
    cfg_field_done(a); /* a typed location is read before it is written */
    collect(a);
    if (!check_folders(a)) {
        return;
    }
    if (!setup_save(a->wox_path, &a->doc, &a->s, a->cfg_ok ? &a->cfg : NULL,
                    gtk_combo_box_get_active(GTK_COMBO_BOX(a->speech)) == 0, music_choice(a), err, sizeof err)) {
        tell(a->window, GTK_MESSAGE_ERROR, "Setup: not saved", err);
        return;
    }
    gtk_main_quit();
}
static void build_window(app_state *a);
/** Initial setup...: the first run again (the window hidden meanwhile); when it completes, wox.cfg has been
 * replaced and the window is built again from it. Not while a ROM download runs (its result goes to this window). */
static void on_rerun(GtkButton *b, gpointer user)
{
    app_state *a = user;
    GtkWidget *old = a->window;
    char err[1600];
    int ok;
    (void)b;
    if (a->downloading) {
        tell(a->window, GTK_MESSAGE_INFO, "Setup", "Please wait until the ROM download ends.");
        return;
    }
    if (!ask(a->window, GTK_MESSAGE_WARNING, "Initial setup", SETUP_TEXT_RERUN_QUESTION)) {
        return;
    }
    gtk_widget_hide(old); /* only the first run's screens while it runs */
    ok = first_run(a, 1);
    if (!ok) {
        gtk_widget_show(old);
        return;
    }
    setup_doc_free(&a->doc);
    if (!setup_doc_load(&a->doc, a->wox_path, err, sizeof err)) {
        tell(a->window, GTK_MESSAGE_ERROR, "Setup", err);
        gtk_main_quit();
        return;
    }
    setup_settings_from_doc(&a->doc, &a->s);
    g_object_unref(a->labels);
    build_window(a); /* a new window and size group from the new wox.cfg */
    gtk_widget_destroy(old);
}
/** Cancel (or closing the window): asks first only when something was changed (Phase 335). */
static int confirm_cancel(app_state *a)
{
    return !window_changed(a) || ask(a->window, GTK_MESSAGE_QUESTION, "Cancel",
               "Are you sure you want to abandon all changes?\n\nNothing will be saved.");
}
static void on_cancel(GtkButton *b, gpointer user)
{
    (void)b;
    if (confirm_cancel(user)) {
        gtk_main_quit();
    }
}
static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer user)
{
    (void)w;
    (void)e;
    if (confirm_cancel(user)) {
        gtk_main_quit();
    }
    return TRUE;
}

/* ------------------------------------------------------------------ building the window */
static GtkWidget *frame(const char *title, GtkWidget **grid)
{
    GtkWidget *f = gtk_frame_new(NULL), *label = gtk_label_new(NULL);
    char *m = g_markup_printf_escaped("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(label), m);
    g_free(m);
    gtk_frame_set_label_widget(GTK_FRAME(f), label);
    *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(*grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(*grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(*grid), 10);
    gtk_container_add(GTK_CONTAINER(f), *grid);
    return f;
}
/** A first-column label of the XEEN.CFG and Game files frames (one width for both). */
static GtkWidget *aligned_label(app_state *a, const char *text)
{
    GtkWidget *l = left_label(text);
    gtk_size_group_add_widget(a->labels, l);
    return l;
}
static void path_row(app_state *a, GtkWidget *grid, int row, setup_dir k)
{
    GtkWidget *b = gtk_button_new_with_label("Browse...");
    a->dir_entry[k] = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(a->dir_entry[k]), a->s.dir[k]);
    gtk_widget_set_hexpand(a->dir_entry[k], TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(a->dir_entry[k]), 40);
    g_object_set_data(G_OBJECT(b), "entry", a->dir_entry[k]);
    g_signal_connect(b, "clicked", G_CALLBACK(on_browse), a);
    g_signal_connect(a->dir_entry[k], "activate", G_CALLBACK(on_entry_activate), a);
    g_signal_connect(a->dir_entry[k], "focus-out-event", G_CALLBACK(on_entry_focus_out), a);
    gtk_grid_attach(GTK_GRID(grid), aligned_label(a, dir_labels[k]), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), a->dir_entry[k], 1, row, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), b, 3, row, 1, 1);
}
static GtkWidget *note_label(const char *css_class)
{
    GtkWidget *l = gtk_label_new(NULL);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 60);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), css_class);
    gtk_widget_set_no_show_all(l, TRUE);
    return l;
}
/** The Music list: each card's label, and its note (setup_music_badge) right-aligned in bold (Phase 335). */
static GtkWidget *music_list(void)
{
    GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *combo;
    GtkCellRenderer *label = gtk_cell_renderer_text_new(), *badge = gtk_cell_renderer_text_new();
    for (unsigned m = 0; m < SETUP_MUSIC_CHOICES; ++m) {
        const char *note = setup_music_badge((setup_music)m);
        char *markup = note ? g_markup_printf_escaped("<b>%s</b>", note) : g_strdup("");
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, setup_music_label((setup_music)m), 1, markup, -1);
        g_free(markup);
    }
    combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), label, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), label, "text", 0, NULL);
    g_object_set(badge, "xalign", 1.0f, "xpad", 8, NULL);
    gtk_cell_layout_pack_end(GTK_CELL_LAYOUT(combo), badge, FALSE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), badge, "markup", 1, NULL);
    gtk_widget_set_size_request(combo, 380, -1);
    return combo;
}
static void build_window(app_state *a)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12), *grid, *row, *b;
    a->labels = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    a->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(a->window), SETUP_MAIN_TITLE);
    gtk_window_set_resizable(GTK_WINDOW(a->window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    g_signal_connect(a->window, "delete-event", G_CALLBACK(on_delete), a);
    /* XEEN.CFG */
    gtk_box_pack_start(GTK_BOX(box), frame("XEEN.CFG", &grid), FALSE, FALSE, 0);
    path_row(a, grid, 0, SETUP_DIR_XEEN_CFG);
    a->cfg_note = note_label("warning");
    a->cfg_create = gtk_button_new_with_label("Create default...");
    gtk_widget_set_no_show_all(a->cfg_create, TRUE);
    g_signal_connect(a->cfg_create, "clicked", G_CALLBACK(on_create_cfg), a);
    gtk_grid_attach(GTK_GRID(grid), a->cfg_note, 1, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), a->cfg_create, 3, 1, 1, 1);
    a->speech = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a->speech), "on");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a->speech), "off");
    a->music = music_list();
    g_signal_connect(a->music, "changed", G_CALLBACK(on_music_changed), a);
    gtk_widget_set_halign(a->speech, GTK_ALIGN_START);
    gtk_widget_set_size_request(a->speech, 90, -1);
    gtk_widget_set_halign(a->music, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), aligned_label(a, "Speech"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), a->speech, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), aligned_label(a, "Music"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), a->music, 1, 3, 2, 1);
    a->rom_note = note_label("warning");
    gtk_grid_attach(GTK_GRID(grid), a->rom_note, 1, 4, 3, 1);
    /* Phase 335: left of the ROM warning, shown with it */
    a->rom_get = gtk_button_new_with_label("Download");
    gtk_widget_set_no_show_all(a->rom_get, TRUE);
    gtk_widget_set_halign(a->rom_get, GTK_ALIGN_START);
    gtk_widget_set_valign(a->rom_get, GTK_ALIGN_START);
    g_signal_connect(a->rom_get, "clicked", G_CALLBACK(on_rom_get), a);
    gtk_grid_attach(GTK_GRID(grid), a->rom_get, 0, 4, 1, 1);
    /* Game files */
    gtk_box_pack_start(GTK_BOX(box), frame("Game files", &grid), FALSE, FALSE, 0);
    for (unsigned k = SETUP_DIR_DATA; k < SETUP_DIRS; ++k) {
        path_row(a, grid, (int)k - 1, (setup_dir)k);
    }
    snprintf(a->data_before, sizeof a->data_before, "%s", a->s.dir[SETUP_DIR_DATA]);
    /* Display */
    gtk_box_pack_start(GTK_BOX(box), frame("Display", &grid), FALSE, FALSE, 0);
    a->fullscreen = gtk_check_button_new_with_label("Fullscreen");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a->fullscreen), a->s.fullscreen);
    gtk_grid_attach(GTK_GRID(grid), a->fullscreen, 0, 0, 2, 1);
    a->locked = gtk_check_button_new_with_label("Lock scale");
    gtk_widget_set_tooltip_text(a->locked, "Keep the window scale when the window is resized or fullscreen");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a->locked), a->s.locked_scale);
    gtk_grid_attach(GTK_GRID(grid), a->locked, 2, 0, 1, 1);
    a->scale = gtk_combo_box_text_new();
    for (unsigned v = 1u; v <= XEEN_WOX_SCALE_MAX; ++v) {
        char label[8];
        snprintf(label, sizeof label, "%u", v);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a->scale), label);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(a->scale), (int)a->s.scale - 1);
    gtk_grid_attach(GTK_GRID(grid), left_label("Window scale"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), a->scale, 1, 1, 1, 1);
    a->aspect = gtk_check_button_new_with_label("CRT aspect");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a->aspect), a->s.aspect);
    g_signal_connect(a->aspect, "toggled", G_CALLBACK(on_aspect_toggled), a);
    gtk_grid_attach(GTK_GRID(grid), a->aspect, 2, 1, 1, 1);
    a->aspect_note = note_label("dim-label");
    gtk_label_set_text(GTK_LABEL(a->aspect_note), "Recommended to set window scale to 5 or 10");
    gtk_grid_attach(GTK_GRID(grid), a->aspect_note, 3, 1, 1, 1);
    /* Enhancements */
    gtk_box_pack_start(GTK_BOX(box), frame("Enhancements", &grid), FALSE, FALSE, 0);
    b = gtk_button_new_with_label("Addons...");
    g_signal_connect(b, "clicked", G_CALLBACK(on_addons), a);
    gtk_grid_attach(GTK_GRID(grid), b, 0, 0, 1, 1);
    b = gtk_button_new_with_label("Bugfixes...");
    g_signal_connect(b, "clicked", G_CALLBACK(on_bugfixes), a);
    gtk_grid_attach(GTK_GRID(grid), b, 1, 0, 1, 1);
    /* Save, Cancel */
    row = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(row), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(row), 8);
    b = gtk_button_new_with_label("Initial setup...");
    g_signal_connect(b, "clicked", G_CALLBACK(on_rerun), a);
    gtk_container_add(GTK_CONTAINER(row), b);
    gtk_button_box_set_child_secondary(GTK_BUTTON_BOX(row), b, TRUE); /* on the left (Phase 335) */
    b = gtk_button_new_with_label("Save");
    g_signal_connect(b, "clicked", G_CALLBACK(on_save), a);
    gtk_container_add(GTK_CONTAINER(row), b);
    gtk_widget_set_can_default(b, TRUE);
    b = gtk_button_new_with_label("Cancel");
    g_signal_connect(b, "clicked", G_CALLBACK(on_cancel), a);
    gtk_container_add(GTK_CONTAINER(row), b);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(a->window), box);
    gtk_widget_show_all(a->window);
    gtk_widget_set_visible(a->aspect_note, a->s.aspect);
    read_cfg(a, 0);
    remember_window(a);
}

int main(int argc, char **argv)
{
    static app_state a;
    char err[1600];
    a.download_err = -1;
    xeen_wox_config *probe = malloc(sizeof *probe);
    gtk_init(&argc, &argv);
    if (!probe) {
        return 1;
    }
    setup_exe_dir(argc > 0 ? argv[0] : NULL, a.exe_dir, sizeof a.exe_dir);
    snprintf(a.wox_path, sizeof a.wox_path, "%s/wox.cfg", a.exe_dir);
    setup_doc_init(&a.doc);
    if (g_file_test(a.wox_path, G_FILE_TEST_EXISTS)) {
        if (!xeen_wox_config_load(a.wox_path, probe, err, sizeof err)) {
            char q[2000];
            snprintf(q, sizeof q, "Recreate wox.cfg with the default settings?\n\nwox.cfg has an error: %s", err);
            if (!ask(NULL, GTK_MESSAGE_WARNING, "WoX - Setup", q) || !first_run(&a, 1)) {
                free(probe);
                return 0;
            }
        }
    } else if (!first_run(&a, 0)) {
        free(probe);
        return 0;
    }
    free(probe);
    if (!setup_doc_load(&a.doc, a.wox_path, err, sizeof err)) {
        tell(NULL, GTK_MESSAGE_ERROR, "Setup", err);
        return 1;
    }
    setup_settings_from_doc(&a.doc, &a.s);
    build_window(&a);
    gtk_main();
    g_object_unref(a.labels);
    setup_doc_free(&a.doc);
    return 0;
}
