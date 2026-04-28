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

// Bytes the log half of sr_run_consume_finished would write right now
// (0 if no run is pending). Lets JS allocate the right buffer size.
unsigned int sr_run_finished_log_size();

// Bytes the savestate half of sr_run_consume_finished would write right
// now (0 if no run is pending). The savestate is captured at the moment
// the run was armed and is constant for the lifetime of that run.
unsigned int sr_run_finished_savestate_size();

// Atomically drain the pending finished run: copies the input log into
// out_log, the starting savestate into out_savestate, fills the run's
// peak metric (max_speed for grapple_challenge, max_streak for
// rg_challenge), the duration in ticks, then re-arms the recorder
// against the current player state.
//
// Returns the number of log bytes written. 0 means no run was pending,
// either buffer was too small, or the re-arm savestate capture failed.
unsigned int sr_run_consume_finished(
	unsigned char* out_log, unsigned int log_buf_size,
	unsigned char* out_savestate, unsigned int savestate_buf_size,
	float* out_max_speed,
	int* out_max_streak,
	unsigned int* out_duration_ticks);
}

#endif
