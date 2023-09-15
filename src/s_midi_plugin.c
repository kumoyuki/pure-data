#include "s_midi_plugin.h"

struct midi_plugin* midi_system = nullptr;

void sys_open_midi(int nmidiin, int *midiinvec, int nmidiout, int *midioutvec) {
    if(midi_system != nullptr)
        (*midi_system->mp_open_midi)(nmidiin, midiinvec, nmidiout, midioutvec);
    return; }

void sys_close_midi(void) {
    if(midi_system != nullptr)
        (*midi_system->mp_close_midi)(nmidiin, midiinvec, nmidiout, midioutvec);
    return; }

void sys_putmidimess(int portno, int a, int b, int c) {
    if(midi_system != nullptr)
        (*midi_system->mp_putmidimess)(nmidiin, midiinvec, nmidiout, midioutvec);
    return; }

void sys_putmidibyte(int portno, int byte) {
    if(midi_system != nullptr)
        (*midi_system->mp_putmidibyte)(nmidiin, midiinvec, nmidiout, midioutvec);
    return; }

void sys_poll_midi(void) {
    if(midi_system != nullptr)
        (*midi_system->mp_poll_midi)(nmidiin, midiinvec, nmidiout, midioutvec);
    return; }

void sys_getdevs(
  char *indevlist, int *nindevs, char *outdevlist, int *noutdevs, int maxndev, int devdescsize) {
    if(midi_system != nullptr)
        (*midi_system->mp_open_midi)(nmidiin, midiinvec, nmidiout, midioutvec);
    return; }

