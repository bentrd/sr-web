#ifndef SR_NETWORK_SR_API_H
#define SR_NETWORK_SR_API_H

// Forward decl to avoid pulling all of instance.h into JS-facing TUs.
struct instance;

namespace net
{
	// Register the active instance pointer that all sr_* C ABI calls
	// dispatch through. Must be called from main() before the loop runs.
	// Safe to call once; passing nullptr unregisters.
	void set_active_instance(instance* inst);
	instance* active_instance();
}

extern "C"
{
void sr_load_rg_challenge();
float sr_get_rg_consecutive();
float sr_get_rg_best();
void sr_reset_rg_challenge();

// Push a single controller input bit for the current frame.
// Called from JS each rAF with the current gamepad state.
// action: emu::input enum index (0=left, 1=right, ..., 7=swap)
// pressed: 1 for held, 0 for released
void sr_push_controller_input(int action, int pressed);
}

#endif
