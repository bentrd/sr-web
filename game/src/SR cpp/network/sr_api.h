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

// --- Grapple-challenge run recorder (anti-cheat input streaming) ---
// See playground.h::run_recorder for the wire format and semantics.

// Sim-version constant. Bumped when physics or input mapping change in
// ways that would invalidate stored input streams.
unsigned int sr_run_sim_version();

// 1 if a run is currently being recorded.
int sr_run_is_active();

// 1 if a finished run is pending consumption by JS.
int sr_run_is_finished();

// Bytes that sr_run_consume_finished would write right now (0 if no run
// is pending). Lets JS allocate the right buffer size.
unsigned int sr_run_finished_log_size();

// Atomically read out the finished run and clear it. Returns the number
// of bytes copied into out_buf (0 if no run is pending or buf_size is
// too small). out_max_speed / out_start_tick / out_end_tick are filled
// when the call succeeds.
unsigned int sr_run_consume_finished(
	unsigned char* out_buf, unsigned int buf_size,
	float* out_max_speed,
	unsigned int* out_duration_ticks);
}

#endif
