// main.cpp -- FFB Co-op.exe entry point.
//
// The start flow is src/app.cpp (run_app, #6); its Windows side is
// src/app_win.cpp. This file only hands over to it.
#include "app.h"

int main() {
    return ffb::run_windows();
}
