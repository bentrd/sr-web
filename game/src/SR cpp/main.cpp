#include <string>

#include "instance.h"
#include "network/sr_api.h"

int main(int argc, char** argv)
{
	std::string map_path = (argc > 1) ? argv[1] : "game/assets/maps/pitfall.sr";

	instance inst;
	net::set_active_instance(&inst);
	inst.init();

	inst.run(map_path);

	glfwTerminate();
}
