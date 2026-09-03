// gui_gtk.cpp
//
// Floating okno GTK3: 4 wiersze (Hi-Hat / Kick / Snare / Tom) z etykietą
// ścieżki + przyciskiem "Load…". Po wyborze pliku wywołuje
// DrumSeqPlugin::loadSampleForInstrument().

#include "gui_gtk.h"
#include "plugin.h"
#include "sequencer.h"

#include <gtk/gtk.h>

#include <cstring>
#include <string>

namespace drumseq {

struct GtkSampleLoaderWindow {
   DrumSeqPlugin *plugin = nullptr;
   GtkWidget *window = nullptr;
   GtkWidget *path_labels[kNumInstruments]{};
};

namespace {

gboolean on_delete(GtkWidget * /*w*/, GdkEvent * /*e*/, gpointer data) {
   // Ukryj zamiast niszczyć — host może znowu wywołać show().
   auto *win = static_cast<GtkSampleLoaderWindow *>(data);
   if (win && win->window)
      gtk_widget_hide(win->window);
   return TRUE; // stop default destroy
}

struct ButtonCtx {
   GtkSampleLoaderWindow *win = nullptr;
   int instrument = 0;
};

void on_load_clicked(GtkButton * /*btn*/, gpointer user_data) {
   auto *ctx = static_cast<ButtonCtx *>(user_data);
   if (!ctx || !ctx->win || !ctx->win->plugin)
      return;

   GtkWidget *dialog = gtk_file_chooser_dialog_new(
       "Wybierz sample WAV",
       GTK_WINDOW(ctx->win->window),
       GTK_FILE_CHOOSER_ACTION_OPEN,
       "_Anuluj", GTK_RESPONSE_CANCEL,
       "_Otwórz", GTK_RESPONSE_ACCEPT,
       nullptr);

   GtkFileFilter *filter = gtk_file_filter_new();
   gtk_file_filter_set_name(filter, "WAV audio");
   gtk_file_filter_add_pattern(filter, "*.wav");
   gtk_file_filter_add_pattern(filter, "*.WAV");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

   const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
   if (response == GTK_RESPONSE_ACCEPT) {
      char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
      if (filename) {
         ctx->win->plugin->loadSampleForInstrument(ctx->instrument, filename);
         if (ctx->win->path_labels[ctx->instrument]) {
            const char *base = strrchr(filename, '/');
            base = base ? base + 1 : filename;
            gtk_label_set_text(GTK_LABEL(ctx->win->path_labels[ctx->instrument]), base);
         }
         g_free(filename);
      }
   }
   gtk_widget_destroy(dialog);
}

} // namespace

GtkSampleLoaderWindow *createGtkSampleLoaderWindow(DrumSeqPlugin *plugin) {
   if (!plugin)
      return nullptr;

   // gtk_init jest bezpieczne wywołać wielokrotnie.
   if (!gtk_init_check(nullptr, nullptr))
      return nullptr;

   auto *win = new GtkSampleLoaderWindow();
   win->plugin = plugin;

   win->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(win->window), "DrumSeq32 — Sample Loader");
   gtk_window_set_default_size(GTK_WINDOW(win->window), 420, 220);
   gtk_window_set_resizable(GTK_WINDOW(win->window), FALSE);
   gtk_window_set_type_hint(GTK_WINDOW(win->window), GDK_WINDOW_TYPE_HINT_DIALOG);
   g_signal_connect(win->window, "delete-event", G_CALLBACK(on_delete), win);

   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
   gtk_container_add(GTK_CONTAINER(win->window), vbox);

   GtkWidget *info = gtk_label_new(
       "Wybierz pliki WAV dla 4 instrumentów.\n"
       "Kroki sequencera i reszta parametrów — przez UI hosta.");
   gtk_label_set_justify(GTK_LABEL(info), GTK_JUSTIFY_LEFT);
   gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 0);

   for (int i = 0; i < kNumInstruments; ++i) {
      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

      GtkWidget *name = gtk_label_new(kInstrumentNames[i]);
      gtk_widget_set_size_request(name, 140, -1);
      gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
      gtk_box_pack_start(GTK_BOX(row), name, FALSE, FALSE, 0);

      const std::string current = plugin->samplePathForInstrument(i);
      const char *label_text = current.empty() ? "(brak)" : current.c_str();
      // basename jeśli pełna ścieżka
      if (!current.empty()) {
         const char *base = strrchr(current.c_str(), '/');
         if (base)
            label_text = base + 1;
      }
      win->path_labels[i] = gtk_label_new(label_text);
      gtk_label_set_ellipsize(GTK_LABEL(win->path_labels[i]), PANGO_ELLIPSIZE_MIDDLE);
      gtk_label_set_xalign(GTK_LABEL(win->path_labels[i]), 0.0f);
      gtk_box_pack_start(GTK_BOX(row), win->path_labels[i], TRUE, TRUE, 0);

      GtkWidget *btn = gtk_button_new_with_label("Load…");
      auto *ctx = new ButtonCtx{win, i};
      g_signal_connect(btn, "clicked", G_CALLBACK(on_load_clicked), ctx);
      // Zapamiętaj ctx, żeby zwolnić przy destroy — uproszczenie: trzymamy
      // w data okna listę (poniżej używamy g_object_set_data_full).
      g_object_set_data_full(G_OBJECT(btn), "btn-ctx", ctx, [](gpointer p) {
         delete static_cast<ButtonCtx *>(p);
      });
      gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 0);

      gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 0);
   }

   gtk_widget_show_all(vbox);
   // Okno startuje ukryte — host wywoła guiShow().
   return win;
}

void destroyGtkSampleLoaderWindow(GtkSampleLoaderWindow *win) {
   if (!win)
      return;
   if (win->window) {
      gtk_widget_destroy(win->window);
      win->window = nullptr;
   }
   delete win;
}

void showGtkSampleLoaderWindow(GtkSampleLoaderWindow *win) {
   if (win && win->window) {
      gtk_widget_show_all(win->window);
      gtk_window_present(GTK_WINDOW(win->window));
   }
}

void hideGtkSampleLoaderWindow(GtkSampleLoaderWindow *win) {
   if (win && win->window)
      gtk_widget_hide(win->window);
}

} // namespace drumseq
