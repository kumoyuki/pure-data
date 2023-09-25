#include "s_midi_plugin.h"

/* see the code for RtMidi.cpp for inspiration here
 * https://ccrma.stanford.edu/~emgraber/256a/final/rtmidi/RtMidi.cpp
 */

#include "s_jack.h"
#include "jack/midiport.h"

/* PD appears to have its own, nominally JACK-compatible ringbuffers
#include "jack/ringbuffer.h"
*/

#define JACK_RINGBUFFER_SIZE 16384
#define JACK_CLIENT_NAME "pd-midi"

static jack_client_t* j_client = 0;
static jack_port_t* j_port = 0;
static sys_ringbuf* j_buffer_size = 0;
static sys_ringbuf* j_buffer_message = 0;
static jack_time_t j_last_time;


static int jack_process_midi(jack_nframes_t n_frames, void* j) {
    return 0; }


void jack_do_open_midi(int nmidiin, int *midiinvec, int nmidiout, int *midioutvec) {
    fprintf(stderr, "jack_do_open_midi n_midi_in=%d, n_midi_out=%d\n", nmidiin, nmidiout);

#if defined(USE_LOCAL_MIDI)
    fprintf(stderr, "jack_do_open_midi n_midi_in=%d, n_midi_out=%d\n", nmidiin, nmidiout);
    if(j_client != 0)
        return;

    j_client = jack_client_open(JACK_CLIENT_NAME, JackNoStartServer, NULL);
    if(j_client == 0) {
        fprintf(stderr, "could not start jack connection\n");
        return; }
    else
        fprintf(stderr, "j_client = %p, name=%s\n", j_client, jack_get_client_name(j_client));

    jack_set_process_callback(j_client, jack_process_midi, NULL);
    int rc = jack_activate(j_client);
    fprintf(stderr, "jack_activate -> rc=%d\n", rc);
#else
    j_client = jack_client;
#endif

    fprintf(stderr, "registering midi port(s)...\n");
    jack_port_register(jack_client, "pd-midi", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    return; }


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
    j_client = 0;
    j_port = 0;
    j_buffer_size = 0;
    j_buffer_message = 0;

    return; }


static void jack_midi_save(int nmidiindev, int *midiindev, int nmidioutdev, int *midioutdev)
{
}


void jackmidi_process() {
    return; }

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
