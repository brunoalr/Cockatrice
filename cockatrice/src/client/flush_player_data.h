#ifndef FLUSH_PLAYER_DATA_H
#define FLUSH_PLAYER_DATA_H

// The web build keeps player files in memory and writes them to the browser's storage now and then;
// this writes them at once. Elsewhere files are already on disk and it does nothing.
void flushPlayerData();

#endif
