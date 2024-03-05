#pragma once

#include "m_pd.h"
#include "s_stuff.h"
#include "s_audio_paring.h"

#ifdef __APPLE__
#include <jack/weakjack.h>
#endif
#include <jack/jack.h>

#define MAX_CLIENTS 100
#define MAX_JACK_PORTS 128  /* higher values seem to give bad xrun problems */
#define BUF_JACK 4096
/* taken from the PipeWire libjack implementation: the larger of the
 * `JACK_CLIENT_NAME_SIZE` definitions I could find in the wild. */
#define CLIENT_NAME_SIZE_FALLBACK 128

EXTERN jack_nframes_t jack_out_max;
EXTERN jack_nframes_t jack_filled;
EXTERN int jack_started;
EXTERN jack_port_t *input_port[MAX_JACK_PORTS];
EXTERN jack_port_t *output_port[MAX_JACK_PORTS];
EXTERN jack_client_t *jack_client;
EXTERN char * desired_client_name;
EXTERN char *jack_client_names[MAX_CLIENTS];
EXTERN int jack_dio_error;
EXTERN t_audiocallback jack_callback;
EXTERN int jack_should_autoconnect;
EXTERN int jack_blocksize; /* should this be PERTHREAD? */
EXTERN pthread_mutex_t jack_mutex;
EXTERN pthread_cond_t jack_sem;
EXTERN PA_VOLATILE char *jack_outbuf;
EXTERN PA_VOLATILE sys_ringbuf jack_outring;
EXTERN PA_VOLATILE char *jack_inbuf;
EXTERN PA_VOLATILE sys_ringbuf jack_inring;
