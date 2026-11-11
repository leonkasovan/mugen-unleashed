#ifndef MUGEN_VITA_INPUT_H
#define MUGEN_VITA_INPUT_H
/* SDL2 controller policy, independent of raw joystick topology. */
void VitaInput_EnsureMapping(int device_index);
void VitaInput_HandleControllerEvent(const SDL_Event *event);
void VitaInput_ClearCancel(int slot); /* -1 clears all slots. */
#endif
