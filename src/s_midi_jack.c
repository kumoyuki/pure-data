#include "s_midi_plugin.h"

/* see the code for RtMidi.cpp for inspiration here
 * https://ccrma.stanford.edu/~emgraber/256a/final/rtmidi/RtMidi.cpp
 */

#include "s_jack.h"
#include "jack/midiport.h"

#include <errno.h>
#include <string.h>

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

// midi ports in the global environment
static size_t j_inport_count = 0;
static char const** j_inport_names = 0;
static size_t j_outport_count = 0;
static char const** j_outport_names = 0;

// midi ports we create
static size_t jm_inport_count = 0;
static char const** jm_inport_names = 0;
static size_t jm_outport_count = 0;
static char const** jm_outport_names = 0;


char const** jackmidi_get_ports(enum JackPortFlags pf) {
    if(j_client == 0)
        return 0;
    
    return
            // JACK_DEFAULT_MIDI_TYPE
        jack_get_ports(j_client, NULL, JACK_DEFAULT_MIDI_TYPE, pf); }


static int jack_process_midi(jack_nframes_t n_frames, void* j) {
    return 0; }


void jack_do_open_midi(int nmidiin, int *midiinvec, int nmidiout, int *midioutvec) {
    int rc = 0;
    
    fprintf(stderr, "jack_do_open_midi %s client=%p, n_midi_in=%d, n_midi_out=%d\n",
            jack_get_version_string(), jack_client, nmidiin, nmidiout);

#if defined(USE_LOCAL_MIDI)
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
    fprintf(stderr, "j_client <- jack_client (%p)\n", jack_client);
    j_client = jack_client;
#endif

    if(j_outport_count == 0) return;
    fprintf(stderr, "registering %d midi input port(s), %zd outports\n", nmidiin, j_outport_count);
    for(size_t n = 0; n < nmidiin; n++) {
        int port_number = midiinvec[n];
        fprintf(stderr, "  midiinvec[%zd] = %d\n", n, port_number);

        char name[256];
        sprintf(name, "midi-in-%zu", n);
        fprintf(stderr, "creating local port %s with client %p\n", name, j_client);
         // indexes into j_outport_names, these are the connections we must make 
        jack_port_t* port =
            jack_port_register(j_client, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
        if(port == 0) {
            fprintf(stderr, "looking up port %s\n", name);
            // this needs the client name glued in front of it
            port = jack_port_by_name(j_client, name); }
        
        fprintf(stderr, "created jack_port_t* port = %p\n", port);
        if(port != 0) {
            jack_port_t* remote = jack_port_by_name(j_client, j_outport_names[port_number]);
            rc = jack_connect(j_client, j_outport_names[port_number], name);
            fprintf(stderr, "jack_connect %s(%p) -> %s(%p), rc=%d, errno=%d \"%s\"\n",
                    j_outport_names[port_number], remote, name, port, rc, errno, strerror(errno)); }
        else
            fprintf(stderr, "failed to register port %s\n", name);
        
        continue; }

    if(j_inport_count == 0) return;
    for(size_t n = 0; n < nmidiout; n++) {
        char name[256];
        sprintf(name, "midi-out-%zu", n);
        // index to j_inport_names, these are the connections we must make 
        fprintf(stderr, "  midioutvec[%zd] = %d\n", n, midioutvec[n]);
        /* jack_port_register(j_client, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0); */
        continue; }

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

void jack_midi_getdevs(
    char *indevlist, int *nindevs, char *outdevlist, int *noutdevs, int maxndev, int devdescsize)
{
    int use_devs = 0;
    
    fprintf(stderr, "jack_midi_getdevs: client %p reading available midi ports...\n", j_client);

    // clean up old devs
    if(j_inport_names != 0) jack_free(j_inport_names);
    if(j_outport_names != 0) jack_free(j_outport_names);

    // get midi connection ports
    j_inport_names = jackmidi_get_ports(JackPortIsInput);
    for(j_inport_count = 0; j_inport_names && j_inport_names[j_inport_count]; j_inport_count++) {
        jack_port_t* port = jack_port_by_name(j_client, j_inport_names[j_inport_count]); 
        fprintf(stderr, "  midi inport: %s, %s\n",
                j_inport_names[j_inport_count],
                // jack_port_short_name(port),
                jack_port_type(port));
        
        continue; }
    
    j_outport_names = jackmidi_get_ports(JackPortIsOutput);
    for(j_outport_count = 0; j_outport_names && j_outport_names[j_outport_count]; j_outport_count++)
        fprintf(stderr, "  midi outport: %s\n", j_outport_names[j_outport_count]);

    // make devices appear in UI
    use_devs = (j_outport_count > maxndev) ?maxndev :j_outport_count;
    *nindevs = use_devs;
    for(size_t i = 0; i < use_devs; i++)
        strcpy(indevlist + i * devdescsize, j_outport_names[i]);
    
    use_devs = (j_outport_count > maxndev) ?maxndev :j_inport_count;
    *noutdevs = use_devs;
    for(size_t i = 0; i < use_devs; i++)
        strcpy(outdevlist + i * devdescsize, j_inport_names[i]);

    return;
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
