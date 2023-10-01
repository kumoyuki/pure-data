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

// just to remember that there exist
extern int sys_midiapi;
extern int sys_nmidiin;          // number of devices currently configured
extern int sys_nmidiout;         // number of devices currently configured
extern int sys_midiindevlist[];  // index into list used for drop down
extern int sys_midioutdevlist[]; // index into list used for drop down

#define JACK_RINGBUFFER_SIZE 16384
#define JACK_MIDI_CLIENT_NAME "pd-midi"

static jack_client_t* j_client = 0;
static jack_port_t* j_port = 0;
static sys_ringbuf* j_buffer_size = 0;
static sys_ringbuf* j_buffer_message = 0;
static jack_time_t j_last_time;

// midi ports in the global environment
static size_t j_inport_count = 0;
static char const** j_inport_names = 0;  // list used in dropdown
static size_t j_outport_count = 0;
static char const** j_outport_names = 0; // list used in dropdown

// midi ports we create
static size_t jm_inport_count = 0;
static jack_port_t** jm_inports;
static size_t jm_outport_count = 0;
static jack_port_t** jm_outports;


static jack_port_t* jm_create_port(char const* name, unsigned long flags) {
    if(j_client == 0) return 0;
    
    char const* client_name = jack_get_client_name(j_client);
    jack_port_t* port = 0;

    fprintf(stderr, "jm_create_port %s with client %s(%p)\n",
            name, client_name, j_client);
    
    port = jack_port_register(j_client, name, JACK_DEFAULT_MIDI_TYPE, flags, 0);
    if(port == 0) {
        char full_name[512];
        sprintf(full_name, "%s:%s", client_name, name);
        port = jack_port_by_name(j_client, full_name); }

    return port; }


void jm_resize_ports(unsigned long flags, int nmidi) {
    int input = flags & JackPortIsInput;
    size_t* count = input ?&jm_inport_count :&jm_outport_count;
    jack_port_t*** ports = input ?&jm_inports :&jm_outports;
    
    if(nmidi >= *count) {
        *count += 1;
        *ports = realloc(*ports, nmidi * (sizeof **ports)); }

    return; }

    
void jm_bind_ports(unsigned long flags, int nmidi, int* midivec, char const** names) {
    int rc = 0;

    fprintf(stderr, "jm_bind_ports: x%lx, nmidi=%d\n", flags, nmidi);
    jm_resize_ports(flags, nmidi);
        
    for(size_t n = 0; n < nmidi; n++) {
        char* side = flags & JackPortIsInput
            ?"in"
            :"out";
        
        char name[256];
        sprintf(name, "midi-%s-%zu", side, n);
        jack_port_t* port = jm_create_port(name, flags);
        
        fprintf(stderr, "created jack_port_t* port = %p\n", port);
        char const* port_name = jack_port_name(port);
        if(port != 0) {
            size_t port_number = midivec[n];
            jack_port_t* remote = jack_port_by_name(j_client, names[port_number]);

            if(flags & JackPortIsInput)
                rc = jack_connect(j_client, names[port_number], port_name);
            else
                rc = jack_connect(j_client, port_name, names[port_number]);

            if(flags & JackPortIsInput)
                jm_inports[n] = port;
            else
                jm_outports[n] = port;
            
            fprintf(stderr, "jack_connect %s %s(%p) -> %s(%p), rc=%d, errno=%d \"%s\"\n",
                    side, names[port_number], remote, name, rc,
                    errno, strerror(errno)); }
        else
            fprintf(stderr, "failed to register port %s\n", name);
        
        continue; }

    return; }


char const** jm_get_ports(enum JackPortFlags pf) {
    if(j_client == 0)
        return 0;
    
    return
        // JACK_DEFAULT_MIDI_TYPE. to directly hook up to alsa (not a2j) devices, I imagine
        // that a type name of "ALSA" would get it done. considering implementation of a
        // smarter config UI to do this (like in midisnoop/qjackctl)
        jack_get_ports(j_client, NULL, JACK_DEFAULT_MIDI_TYPE, pf); }


static int jack_process_midi(jack_nframes_t n_frames, void* j) {
    // the void is user data, of which we have none

    for(size_t i=0; i < jm_inport_count; i++) {
        jack_port_t* port = jm_inports[i];
        char const* name = jack_port_name(port);
        if(name == 0) {
            fprintf(stderr, "failed to get port name for %zd, %p\n", i, port);
            continue; }

        void* pb = jack_port_get_buffer(port, n_frames);

        jack_nframes_t ic = jack_midi_get_event_count(pb);        
        for(int e = 0; e < ic; e++) {
            jack_midi_event_t event;

            int r = jack_midi_event_get (&event, pb, e);
            for(size_t s=0; r==0 && s<event.size; s++)
                sys_midibytein(i, event.buffer[s]);
            
            continue; }

        continue; }
    
    return 0; }


void jack_do_open_midi(int nmidiin, int *midiinvec, int nmidiout, int *midioutvec) {
    int rc = 0;
    char* client_name = 0;
    char const* port_name = 0;
    
    fprintf(stderr, "jack_do_open_midi %s client=%p, n_midi_in=%d, n_midi_out=%d\n",
            jack_get_version_string(), jack_client, nmidiin, nmidiout);

#if defined(USE_LOCAL_MIDI)
    if(j_client != 0)
        return;

    j_client = jack_client_open(JACK_MIDI_CLIENT_NAME, JackNoStartServer, NULL);
    if(j_client == 0) {
        fprintf(stderr, "could not start jack connection\n");
        return; }
    else
        fprintf(stderr, "j_client = %p, name=%s\n", j_client, jack_get_client_name(j_client));

    jack_set_process_callback(j_client, jack_process_midi, NULL);
    rc = jack_activate(j_client);
//    fprintf(stderr, "jack_activate -> rc=%d\n", rc);
#else
    fprintf(stderr, "j_client <- jack_client (%p)\n", jack_client);
    j_client = jack_client;
#endif

//    fprintf(stderr, "registering %d midi input port(s), %zd outports\n", nmidiin, j_outport_count);
    if(j_outport_count == 0) return;
    jm_bind_ports(JackPortIsInput, nmidiin, midiinvec, j_outport_names);

    if(j_inport_count == 0) return;
    jm_bind_ports(JackPortIsOutput, nmidiout, midioutvec, j_inport_names);

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

    // get midi connection ports, note that we have to count our way through the list
    // so we know how many devices to display
    j_inport_names = jm_get_ports(JackPortIsInput);
    for(j_inport_count = 0; j_inport_names && j_inport_names[j_inport_count]; j_inport_count++)
        ;//fprintf(stderr, "  midi inport: %s\n", j_inport_names[j_inport_count]);
    
    j_outport_names = jm_get_ports(JackPortIsOutput);
    for(j_outport_count = 0; j_outport_names && j_outport_names[j_outport_count]; j_outport_count++)
        ;//fprintf(stderr, "  midi outport: %s\n", j_outport_names[j_outport_count]);

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


void jackmidi_process(jack_nframes_t nf, void* j) {
    jack_process_midi(nf, j);
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
