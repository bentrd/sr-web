#include "input_handler.h"

input_handler* input_handler::ih_inst = nullptr;

input_handler::input_handler() :
	m_inputs(new inputs())
{}

void input_handler::init_callbacks(GLFWwindow* win)
{
	input_handler::ih_inst = this;

	glfwSetKeyCallback(win, key_callback);
	glfwSetMouseButtonCallback(win, mouse_callback);
	glfwSetCursorPosCallback(win, cursor_callback);
	glfwSetScrollCallback(win, scroll_callback);
}

void input_handler::key_callback(GLFWwindow* win, int key, int scancode, int action, int mods)
{
	if (ih_inst == nullptr)
		return;

	if (action == GLFW_PRESS)
	{
		ih_inst->m_inputs->held_keys[key] = true;
		ih_inst->m_inputs->pressed_keys[key] = true;
	}
	if (action == GLFW_RELEASE)
	{
		ih_inst->m_inputs->held_keys[key] = false;
	}
}

void input_handler::mouse_callback(GLFWwindow* win, int button, int action, int mods)
{
	if (ih_inst == nullptr)
		return;

	if (action == GLFW_PRESS)
	{
		ih_inst->m_inputs->held_buttons[button] = true;
		ih_inst->m_inputs->pressed_buttons[button] = true;
	}
	if (action == GLFW_RELEASE)
	{
		ih_inst->m_inputs->held_buttons[button] = false;
	}
}

void input_handler::scroll_callback(GLFWwindow* win, double xoffset, double yoffset)
{
	if (ih_inst == nullptr)
		return;

	ih_inst->m_inputs->scroll_x += xoffset;
	ih_inst->m_inputs->scroll_y += yoffset;
}

void input_handler::cursor_callback(GLFWwindow* win, double xpos, double ypos)
{
	if (ih_inst == nullptr)
		return;

	ih_inst->m_inputs->cursor_x = xpos;
	ih_inst->m_inputs->cursor_y = ypos;
}