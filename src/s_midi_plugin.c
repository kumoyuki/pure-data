#include "s_midi_plugin.h"

struct midi_plugin* midi_system = 0;

void sys_do_open_midi(int nmidiindev, int *midiindev, int nmidioutdev, int *midioutdev) {
    if(midi_system != 0)
        (*midi_system->mp_open_midi)(nmidiindev, midiindev, nmidioutdev, midioutdev);
    return; }


void sys_close_midi(void) {
    if(midi_system != 0)
        (*midi_system->mp_close_midi)();
    return; }


void sys_putmidimess(int portno, int a, int b, int c) {
    if(midi_system != 0)
        (*midi_system->mp_putmidimess)(portno, a, b, c);
    return; }


void sys_putmidibyte(int portno, int byte) {
    if(midi_system != 0)
        (*midi_system->mp_putmidibyte)(portno, byte);
    return; }


void sys_poll_midi(void) {
    if(midi_system != 0)
        (*midi_system->mp_poll_midi)();
    return; }


void midi_getdevs(
  char *indevlist, int *nindevs, char *outdevlist, int *noutdevs, int maxndev, int devdescsize) {
    if(midi_system != 0)
        (*midi_system->mp_getdevs)(indevlist, nindevs, outdevlist, noutdevs, maxndev, devdescsize);
    return; }

