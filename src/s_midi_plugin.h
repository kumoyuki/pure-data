#pragma once

#include "m_pd.h"
#include "s_stuff.h"
#include "s_utf8.h"

struct midi_plugin
{
    char const* mp_name;

    void (*mp_init)();
    void (*mp_open_midi)(int nmidiin, int *midiinvec, int nmidiout, int *midioutvec);
    void (*mp_close_midi)(void);
    void (*mp_putmidimess)(int portno, int a, int b, int c);
    void (*mp_putmidibyte)(int portno, int byte);
    void (*mp_poll_midi)(void);
    void (*mp_getdevs) (
        char *indevlist, int *nindevs,
        char *outdevlist, int *noutdevs, int maxndev, int devdescsize);
};

/* hack to avoid (temporarily) working based on sys_midiapi because of
 * header file dependencies
 */
extern struct midi_plugin* midi_system;
