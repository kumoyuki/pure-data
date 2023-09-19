#include "s_midi_plugin.h"

/* see the code for RtMidi.cpp for inspiration here
 * https://ccrma.stanford.edu/~emgraber/256a/final/rtmidi/RtMidi.cpp
 */

#include "jack/jack.h"
#include "jack/midiport.h"
#include "jack/ringbuffer.h"


void jack_do_open_midi(int nmidiin, int *midiinvec, int nmidiout, int *midioutvec)
{
}

void jack_close_midi(void)
{
}

void jack_putmidimess(int portno, int a, int b, int c)
{
}

void jack_putmidibyte(int portno, int byte)
{
}

void jack_poll_midi(void)
{
}

void jack_midi_getdevs(char *indevlist, int *nindevs,
    char *outdevlist, int *noutdevs, int maxndev, int devdescsize)
{
}


static void jack_midi_init() {
    fprintf(stderr, "jack_midi_init\n");
}


static void jack_midi_save(int nmidiindev, int *midiindev, int nmidioutdev, int *midioutdev)
{
}

struct midi_plugin* jackmidi_get_plugin() {
    static struct midi_plugin jack = {
        "jack-midi", 
        jack_midi_init,
        jack_do_open_midi,
        jack_close_midi,
        jack_putmidimess,
        jack_putmidibyte,
        jack_poll_midi,
        jack_midi_getdevs,
        jack_midi_save
    };

    fprintf(stderr, "getting jack midi plugin: %p\n", &jack);
    return &jack;
}
