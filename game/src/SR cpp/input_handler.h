#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <memory>
#include <array>
#include <bitset>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

struct inputs
{
	std::bitset<1 << 16> held_keys;
	std::bitset<1 << 16> pressed_keys;
	std::bitset<8> held_buttons;
	std::bitset<8> pressed_buttons;

	double cursor_x;
	double cursor_y;
	double scroll_x;
	double scroll_y;
};

struct input_handler
{
	std::unique_ptr<inputs> m_inputs;

	input_handler();

	void init_callbacks(GLFWwindow* win);

	static input_handler* ih_inst;

	static void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods);
	static void mouse_callback(GLFWwindow* win, int button, int action, int mods);
	static void scroll_callback(GLFWwindow* win, double xoffset, double yoffset);
	static void cursor_callback(GLFWwindow* win, double xpos, double ypos);
};

#endif
