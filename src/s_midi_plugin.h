#pragma once

#include "m_pd.h"
#include "s_stuff.h"
#include "s_utf8.h"

struct midi_plugin
{
    char const* mp_name;

        /* mp_init may be called multiple times during a PD run. it needs to
         * be able to understand enough of its context to do the right thing\
         * *AND* not clobber any user configuration done since the last
         * invocation
         */
    void (*mp_init)();

    void (*mp_open_midi)(int nmidiin, int *midiinvec, int nmidiout, int *midioutvec);
    void (*mp_close_midi)(void);
    void (*mp_putmidimess)(int portno, int a, int b, int c);
    void (*mp_putmidibyte)(int portno, int byte);
    void (*mp_poll_midi)(void);

        /* note that in the normal order of things, mP-getdevs will be called *before* mp_init()
         * this means that it may need to call mp_init for iteself, depending on the particular
         * midi plugin.
         */
    void (*mp_getdevs) (
        char *indevlist, int *nindevs,
        char *outdevlist, int *noutdevs, int maxndev, int devdescsize);
    void (*mp_save)(int nmidiindev, int *midiindev, int nmidioutdev, int *midioutdev);
};

/* hack to avoid (temporarily) working based on sys_midiapi because of
 * header file dependencies
 */
extern struct midi_plugin* midi_system;
