// gui_gtk.h
//
// Minimalne floating okno GTK3 do wyboru 4 plików WAV (po jednym na
// instrument). Reszta parametrów (kroki sequencera, randomize, MIDI notes)
// idzie przez generic UI hosta (clap.params) — nie budujemy tu pełnego
// edytora patternu.

#pragma once

#include <string>

namespace drumseq {

class DrumSeqPlugin;

// Nieprzezroczysty uchwyt okna (implementacja w gui_gtk.cpp).
struct GtkSampleLoaderWindow;

// Tworzy okno (jeszcze ukryte). Zwraca nullptr przy błędzie init GTK.
GtkSampleLoaderWindow *createGtkSampleLoaderWindow(DrumSeqPlugin *plugin);

void destroyGtkSampleLoaderWindow(GtkSampleLoaderWindow *win);
void showGtkSampleLoaderWindow(GtkSampleLoaderWindow *win);
void hideGtkSampleLoaderWindow(GtkSampleLoaderWindow *win);

} // namespace drumseq
