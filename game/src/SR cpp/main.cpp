#include <string>

#include "instance.h"

int main(int argc, char** argv)
{
	std::string map_path = (argc > 1) ? argv[1] : "game/assets/maps/pitfall.sr";

	instance inst;
	inst.init();

	inst.run(map_path);

	glfwTerminate();
}
