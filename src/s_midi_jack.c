#include "s_midi_plugin.h"

/* see the code for RtMidi.cpp for inspiration here
 * https://ccrma.stanford.edu/~emgraber/256a/final/rtmidi/RtMidi.cpp
 */

#include "s_jack.h"
/* the PD ringbuffer has egregious errors in its use of atomics
#include "z_ringbuffer.h"
*/

#include "jack/midiport.h"

#include <errno.h>
#include <string.h>
#include <stdatomic.h>
#include <stdarg.h>

#include <unistd.h>
#include <sys/syscall.h>

#ifndef SYS_gettid
#error "SYS_gettid unavailable on this system"
#endif

#define gettid() ((pid_t)syscall(SYS_gettid))

#include "jack/ringbuffer.h"

/* abstracting the PD ringbuffer API */
typedef jack_ringbuffer_t jmrb;
int jmrb_available_to_read(jmrb* rb) { return jack_ringbuffer_read_space(rb); }
int jmrb_available_to_write(jmrb* rb) { return jack_ringbuffer_write_space(rb); }
void jmrb_clear_buffer(jmrb* rb) { jack_ringbuffer_reset(rb); } /* not thread safe w/JACK */
void jmrb_free(jmrb* rb) { jack_ringbuffer_free(rb); }
jmrb* jmrb_create(size_t size) { return jack_ringbuffer_create(size); }
int jmrb_read_from_buffer(jmrb *rb, char *dest, int len) { return jack_ringbuffer_read(rb, dest, len) < 0; }

int jmrb_write_to_buffer(jmrb *rb, int n, ...) {
    va_list args;
    va_start(args, n);
    for(int i=0; i < n; i++) {
        const char* buffer = va_arg(args, const char*);
        int size = va_arg(args, int);
        jack_ringbuffer_write(rb, buffer, size);
        continue; }
    va_end(args);
    return 0; }


// just to remember that there exist
extern int sys_midiapi;
extern int sys_nmidiin;          // number of devices currently configured
extern int sys_nmidiout;         // number of devices currently configured
extern int sys_midiindevlist[];  // index into list used for drop down
extern int sys_midioutdevlist[]; // index into list used for drop down

pthread_mutex_t jm_mutex;

#define JACK_RINGBUFFER_SIZE 16384
#define JACK_MIDI_CLIENT_NAME "pd-midi"

static void jack_midi_init();
static jack_client_t* j_client = 0;
static jack_time_t j_last_time;

// midi ports in the global environment
static size_t j_inport_count = 0;
static char const** j_inport_names = 0;  // list used in dropdown
static size_t j_outport_count = 0;
static char const** j_outport_names = 0; // list used in dropdown

// midi ports we create
struct jm_port {
    unsigned long flags;
    jack_port_t* port;
    jmrb* buffer; };


void jmp_clear(struct jm_port* p) {
    if(p) {
        if(p->port)
            jack_port_unregister(j_client, p->port);
        
        if(p->buffer)
            jmrb_free(p->buffer);

        p->port = 0;
        p->buffer = 0;
        p->flags = 0; }
    
    return; }


void jmp_free(struct jm_port* p) {
    if(p) {
        jmp_clear(p);
        free(p); }
    return; }


struct jm_port* jmp_create(unsigned long flags, size_t size) {
    struct jm_port* jmp = (struct jm_port*)malloc(sizeof *jmp);
    if(jmp) {
        jmp->flags = flags;
        jmp->port = 0;
        jmp->buffer = jmrb_create(size); }

    return jmp; }


jack_port_t* jmp_create_port(struct jm_port* p, char const* name) {
    if(j_client == 0) return 0;
    
    char const* client_name = jack_get_client_name(j_client);

    p->port = jack_port_register(j_client, name, JACK_DEFAULT_MIDI_TYPE, p->flags, 0);
    if(p->port == 0) {
        char full_name[512];
        sprintf(full_name, "%s:%s", client_name, name);
        p->port = jack_port_by_name(j_client, full_name); }

    return p->port; }


struct jm_ports {
    size_t count;
    size_t using;
    struct jm_port** ports;
} inports, outports;


void jm_ports_init(struct jm_ports* ps) {
    if(ps)
        return;

    ps->count = 0;
    ps->using = 0;
    ps->ports = 0;

    return; }


static void jmps_resize(struct jm_ports* ps, int nmidi) {
    if(!ps)
        return;
    
    if(nmidi >= ps->count) {
        ps->count = nmidi;
        ps->ports = realloc(ps->ports, nmidi * (sizeof *ps->ports)); }

    return; }


static struct jm_ports* jm_resize_ports(unsigned long flags, int nmidi);
static int jack_process_midi(jack_nframes_t n_frames, void* j);


void jm_bind_ports(unsigned long flags, int nmidi, int* midivec, char const** names) {
    int rc = 0;

    struct jm_ports* ps = jm_resize_ports(flags, nmidi);
    ps->using = 0;
    
    for(size_t n = 0; n < nmidi; n++) {
        struct jm_port* p = ps->ports[n];
        
        char* side = ps == &inports ?"in" :"out";
        char name[256];
        sprintf(name, "midi-%s-%zu", side, n);
        jack_port_t* port = jmp_create_port(p, name);
        
        char const* port_name = jack_port_name(port);
        if(port != 0) {
            size_t port_number = midivec[n];
            jack_port_t* remote = jack_port_by_name(j_client, names[port_number]);

            if(flags & JackPortIsInput)
                rc = jack_connect(j_client, names[port_number], port_name);
            else
                rc = jack_connect(j_client, port_name, names[port_number]);

            ps->ports[n]->port = port;
            ps->using += 1;

            if(rc == 0)
                fprintf(stderr, "jack_connect %s %s -> %s, rc=%d\n",
                        side,
                        names[port_number],
                        name,
                        rc);
            else
                fprintf(stderr, "jack_connect %s %s -> %s, rc=%d, errno=%d \"%s\"\n",
                        side,
                        names[port_number],
                        name,
                        rc, errno, strerror(errno)); }
        else
            fprintf(stderr, "failed to register port %s\n", name);
        
        continue; }

    return; }


jack_client_t* jm_get_client() {
    if(j_client != 0)
        return j_client;
    
    j_client = jack_client_open(JACK_MIDI_CLIENT_NAME, JackNoStartServer, NULL);
    if(j_client == 0) {
        fprintf(stderr, "jm_get_client: could not start jack connection\n");
        return 0; }
    else
        fprintf(stderr, "j_client = %p, name=%s\n", j_client, jack_get_client_name(j_client));

    t_audiosettings as;
    sys_get_audio_settings(&as);
    jack_set_process_callback(j_client, jack_process_midi, NULL);
    
    int rc = jack_activate(j_client);
    if(rc != 0)
        fprintf(stderr, "jack_activate -> rc=%d\n", rc);

    return j_client; }


char const** jm_get_ports(enum JackPortFlags pf) {
    if(j_client == 0)
        return 0;
    
    return
        // JACK_DEFAULT_MIDI_TYPE. to directly hook up to alsa (not a2j) devices, I imagine
        // that a type name of "ALSA" would get it done. considering implementation of a
        // smarter config UI to do this (like in midisnoop/qjackctl)
        jack_get_ports(j_client, NULL, JACK_DEFAULT_MIDI_TYPE, pf); }


void jm_midi_in(int device, jack_midi_event_t* event) {
    char* buffer = event->buffer;
    
    if(event->size < sizeof event->buffer)
        buffer = (char*)&event->buffer;
    
    for(size_t s=0; s < event->size; s++)
        sys_midibytein(device, buffer[s]);
    
    return; }


char const* jm_print_event(
    char* buffer, size_t bsz, jack_midi_event_t* e,
    char const* lb, char const* rb) {
    size_t rbz = strlen(rb);
    
    char header[128];
    sprintf(header, "%s%ld, ", lb, e->time);
    
    size_t hsz = strlen(header);
    if(hsz + rbz >= bsz) {
        strncpy(buffer, header, bsz-1);
        return buffer; }
    
    sprintf(buffer, "%s", header);
    size_t rsz = bsz - hsz - rbz;
    if(rsz < 2) // a bit messy, should copy in rb and check appropriately
        return buffer;

    char* cursor = buffer + strlen(buffer);
    if(e->buffer != 0) {
        size_t byte = 0;
        while(byte < e->size && cursor < (buffer + bsz - rbz - 3)) {
            sprintf(cursor, "%02x", e->buffer[byte]);
            byte += 1;
            cursor += 2;
            continue;  }}
    else
        cursor += sprintf(cursor, "sz=%zd", e->size);
                                    
    if(cursor < buffer + bsz - rbz - 1)
        sprintf(cursor, "%s", rb);
    
    return buffer; }


char const* jmx_print_event(
    char* buffer, size_t bsz, jack_midi_event_t* e,
    char const* lb, char const* rb) {
    jack_midi_event_t ex;
    ex.time = e->time;
    ex.size = e->size;
    ex.buffer = e->size < sizeof e->buffer ?(char*)&e->buffer :(char*)e->buffer;

    return jm_print_event(buffer, bsz, &ex, lb, rb); }

        
struct jm_ports* jm_resize_ports(unsigned long flags, int nmidi) {
    // so much history for one so young
    int input = flags & JackPortIsInput;
    struct jm_ports* ports = input ?&inports :&outports;
    
    jmps_resize(ports, nmidi);
    for(size_t i = ports->using; i < ports->count; i++)
        ports->ports[i] = jmp_create(flags, JACK_RINGBUFFER_SIZE);
    
    return ports; }

    
int jm_same_event(jack_midi_event_t* e1, jack_midi_event_t* e2) {
    char e1b[1024];
//    jm_print_event(e1b, 1023, e1, "<", "|");
    char e2b[1024];
//    jm_print_event(e2b, 1023, e2, "|", ">");
//    fprintf(stderr, "jse: %s%s\n", e1b, e2b);
    return e1->size == e2->size
        && memcmp(e1->buffer, e2->buffer, e1->size) == 0
        && e1->time == e2->time
        ; }

#define JACK_MIDI_MAX_BUFFER 1024
static unsigned long long jm_sequence = 0;
static char jm_last_event_buffer[JACK_MIDI_MAX_BUFFER+1];
static jack_midi_event_t jm_last_event = {0, 0, jm_last_event_buffer};
static size_t jm_last_event_buffer_size = 0;
static char jm_lb[JACK_MIDI_MAX_BUFFER+1];
static _Atomic int jm_pending_count = 0;
static _Atomic int jm_is_polling = 0;


void jm_set_last_event(jack_midi_event_t* e) {
    char this[1024];
    char was[1024];

    jm_sequence += 1;
    jm_last_event.time = e->time;
    jm_last_event.size = e->size;

    if(JACK_MIDI_MAX_BUFFER <= e->size) {
//        fprintf(stderr, "jm_set_last_event: too big e->size=%zd\n", e->size*2);
        return; }

    memcpy(jm_last_event.buffer, e->buffer, e->size);
    return; }
    

struct jmr_header {
    jack_nframes_t time;
    size_t size; };

typedef enum { false, true } bool;

bool jmr_read(jmrb* rb, jack_midi_event_t* e) {
    char eb[1024];
    int rc = 0;
    size_t available = jmrb_available_to_read(rb);
    if(available > -0) {
        /* the atomic write ensures that we will always have at least one full MIDI
         * message in the pipeline
         */
        e->time = 0;
        rc = jmrb_read_from_buffer(rb, (void*)&e->time, sizeof e->time);
        if(rc != 0) goto abort_read;

        e->size = 0;
        rc = jmrb_read_from_buffer(rb, (void*)&e->size, sizeof e->size);
        if(rc != 0) goto abort_read;

        e->buffer = 0;
        if(e->size < sizeof e->buffer) {
            rc = jmrb_read_from_buffer(rb, (void*)&e->buffer, e->size);
            if(rc != 0) goto abort_read; }
        else {
            e->buffer = alloca(e->size);
            rc = jmrb_read_from_buffer(rb, e->buffer, e->size);
            if(rc != 0) goto abort_read; }

        /* fprintf(stderr, "%s\n", jmx_print_event(eb, 1024, e, "|", ">")); fflush(stderr); */
        return true; }

    return false;

  abort_read:
    fprintf(stderr, "aborted read, rc=%d\n", rc);
    jmrb_clear_buffer(rb);
    return false; }


bool jmr_write(jmrb* rb, jack_midi_event_t* e) {
    char eb[1024];
//    fprintf(stderr, "%s\n", jm_print_event(eb, 1023, e, "<", "|"));

    size_t required = sizeof *e + e->size;
    if(required >= jmrb_available_to_write(rb))
        return false;

    int rc = jmrb_write_to_buffer(rb, 3,
                                  (const char*)&e->time, (uint32_t)(sizeof e->time),
                                  (const char*)&e->size, sizeof e->size,
                                  (const char*)e->buffer, e->size);
    if(rc != 0)
        fprintf(stderr, "jmr_write: failed to write: %p\n", rb);
    
    return rc == 0; }


static uint64_t jpm_tick = 0;

static int jack_process_midi(jack_nframes_t n_frames, void* j) {
    // the void is user data, of which we have none
    jpm_tick += 1;

    t_audiosettings as;
    sys_get_audio_settings(&as);

    // data from JACK -> ringbuffer
    for(size_t i=0; i < inports.using; i++) {
        struct jm_port* jmp = inports.ports[i];
        jack_port_t* port = jmp->port;
        if(port == 0)
            continue;
        
        char const* name = jack_port_name(port);
        if(name == 0) {
            fprintf(stderr, "failed to get port name for %zd, %p\n", i, port);
            continue; }

        void* pb = jack_port_get_buffer(port, n_frames);

        jack_nframes_t ic = jack_midi_get_event_count(pb);
        if(ic > 0) {
            pid_t tid = gettid();
            for(int e = 0; e < ic; e++) {
                jack_midi_event_t event;
                
                int r = jack_midi_event_get (&event, pb, e);
                if(r == 0) {
                    int is_same = 0; /* jm_same_event(&jm_last_event, &event); artifact of a bad init! */
                    if(jm_sequence == 0 || !is_same) {
                        bool ok = jmr_write(jmp->buffer, &event);
                        if(!ok)
                            pd_error(0, "JACK MIDI: %s failed to write to ringbuffer", name);
                    
                        /* jm_set_last_event(&event); */ }}
                else
                    fprintf(stderr, "jack_process_midi(%d/%lld) jack_midi_event_get => %d\n",
                            gettid(), jpm_tick, r);

                continue; }}

        continue; }

    // data from ring buffer -> JACK
    for(size_t i=0; i < outports.using; i++) {
        struct jm_port* jmp = outports.ports[i];
        jack_port_t* port = jmp->port;
        if(port == 0)
            continue;
        
        char const* name = jack_port_name(port);
        if(name == 0) {
            fprintf(stderr, "failed to get port name for %zd, %p\n", i, port);
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
    j_client = jm_get_client();
    if(j_client == 0)
        return;
#else
    fprintf(stderr, "j_client <- jack_client (%p)\n", jack_client);
    j_client = jack_client;
#endif

    if(j_outport_count == 0) return;
    jm_bind_ports(JackPortIsInput, nmidiin, midiinvec, j_outport_names);

    if(j_inport_count == 0) return;
    jm_bind_ports(JackPortIsOutput, nmidiout, midioutvec, j_inport_names);

    return; }


void jack_close_midi(void)
{
    if(j_client != 0) {
        for(size_t p = 0; p < inports.using; p++)
            if(inports.ports[p] != 0) {
                fprintf(stderr, "jack_close_midi: closing inport %s\n",
                        jack_port_name(inports.ports[p]->port));
                jack_port_unregister(j_client, inports.ports[p]->port); }
        
        for(size_t p = 0; p < outports.using; p++)
            if(outports.ports[p] != 0) {
                fprintf(stderr, "jack_close_midi: closing outport %s\n",
                        jack_port_name(outports.ports[p]->port));
                jack_port_unregister(j_client, outports.ports[p]->port); }
        
        jack_client_close(j_client);
        j_client = 0; }
        
    return;
}


void jack_putmidimess(int portno, int a, int b, int c)
{
    fprintf(stderr, "jack_putmidimess: port=%d, %02x %02x %02x\n",
            portno, a, b, c);
#if defined(JACK_POLLING_IS_NOT_LOSING)
    jack_nframes_t nf = jack_cycle_wait(j_client);

    // these guts should work in callback handler
    struct jm_port* jmp = outports.ports[portno];
    void* pb = jack_port_get_buffer(jmp->port, nf);
    jack_midi_clear_buffer(pb);
    jack_midi_data_t* e = jack_midi_event_reserve(pb, nf, 3);
    e[0] = a;
    e[1] = b;
    e[2] = c;
    jack_midi_event_write(pb, nf, e, 3);

    jack_cycle_signal(j_client, 0);
#else
        // jmr_write() the output ringbuffer for the channel used
        // in jack_process_midi()
        //
        // it is not clear if there should be a separate queueing stage
        // into jack_poll_midi
#endif
    return;
}


void jack_putmidibyte(int portno, int byte)
{
    fprintf(stderr, "jack_putmidibyte: port=%d, %02x\n", portno, byte);
    return;
}

void jack_poll_midi(void)
{
    if(j_client == 0)
        return;

    for(size_t i=0; i < inports.using; i++) {
        struct jm_port* jmp = inports.ports[i];
        jack_port_t* port = jmp->port;
        if(port == 0)
            continue;

        char const* name = jack_port_name(port);
        jmrb* buffer = inports.ports[i]->buffer;
        jack_midi_event_t event;
        while(jmr_read(buffer, &event)) {
            /*
              char buffer[64];
              jmx_print_event(buffer, 64, &event, "|", ">");
              fprintf(stderr, "jack_poll_midi: %s\n", buffer);
            */
            jm_midi_in(i, &event);
            continue; }
        
        continue; }
    
    return;
}

void jack_midi_getdevs(
    char *indevlist, int *nindevs, char *outdevlist, int *noutdevs, int maxndev, int devdescsize)
{
    int use_devs = 0;

    jack_midi_init();
    if(j_client == 0)
        return;
    
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
    if(j_client == 0) {
        jm_ports_init(&inports);
        jm_ports_init(&outports);
 
        pthread_mutex_init(&jm_mutex, 0);
        
        j_client = jm_get_client(); }
    
    return; }


static void jack_midi_save(int nmidiindev, int *midiindev, int nmidioutdev, int *midioutdev)
{
        /* it turns out that the default system save behavior
         * is pretty much all that is needed. nothing to see 
         * here. Move along to s_file.c; search for midi
         */
}


void jackmidi_process(jack_nframes_t nf, void* j) {
        /* jack_process_midi(nf, j); */
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
