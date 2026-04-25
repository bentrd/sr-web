#include "instance.h"

int main()
{
	instance inst;
	inst.init();

	inst.run();

	glfwTerminate();
}
