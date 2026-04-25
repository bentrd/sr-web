// Web entry point. The browser drives the loop via
// emscripten_set_main_loop_arg, calling instance::tick_frame at the
// monitor refresh rate (we pass fps=0 → use requestAnimationFrame).
//
// Map loading is deferred to JS — sr_load_map (sr_api.cpp) calls
// playground::load. We start without a map so the JS lobby can wait
// for the user to enter a room before pulling the .sr file out of the
// preloaded /maps virtual FS.

#include <emscripten.h>

#include "../src/SR cpp/instance.h"
#include "../src/SR cpp/network/sr_api.h"

namespace
{
	void tick(void* arg)
	{
		auto* inst = static_cast<instance*>(arg);
		inst->tick_frame();
	}
}

int main()
{
	static instance inst;
	net::set_active_instance(&inst);
	inst.init();

	// Pre-load a default map so the canvas isn't blank before JS calls
	// sr_load_map. Pitfall is the ID we ship in every build.
	inst.m_playground.load("/maps/pitfall.sr");

	// fps=0 → tie to requestAnimationFrame
	// simulate_infinite_loop=1 → main() returns control to the browser
	emscripten_set_main_loop_arg(tick, &inst, 0, 1);

	return 0;
}
