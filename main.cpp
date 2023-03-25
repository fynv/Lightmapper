#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cstdio>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include "utils/Utils.h"

//#include "Test0.hpp"
#include "Test1.hpp"

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	Test* test = (Test*)glfwGetWindowUserPointer(window);
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS)
		{
			test->set_mouse_down(true);

			int vw, vh;
			glfwGetFramebufferSize(window, &vw, &vh);
			double cx = (double)vw / 2.0;
			double cy = (double)vh / 2.0;
			glfwSetCursorPos(window, cx, cy);

			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
		}
		else if (action == GLFW_RELEASE)
		{			
			test->set_mouse_down(false);
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}
	

}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
	Test* test = (Test*)glfwGetWindowUserPointer(window);
	if (test->mouse_down)
	{
		if (test->recieve_delta)
		{
			int vw, vh;
			glfwGetFramebufferSize(window, &vw, &vh);
			double cx = (double)vw / 2.0;
			double cy = (double)vh / 2.0;
			glfwSetCursorPos(window, cx, cy);
			test->rotation_delta(xpos - cx, ypos - cy);
		}
		else
		{
			test->recieve_delta = true;
		}
	}

}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	Test* test = (Test*)glfwGetWindowUserPointer(window);
	if (action == GLFW_PRESS || action == GLFW_REPEAT)
	{
		if (key == GLFW_KEY_W)
		{
			test->move(0, -1);
		}
		else if (key == GLFW_KEY_S)
		{
			test->move(0, 1);
		}
		else if (key == GLFW_KEY_A)
		{
			test->move(-1, 0);
		}
		else if (key == GLFW_KEY_D)
		{
			test->move(1, 0);
		}
	}

}

int main()
{
	int default_width = 1280;
	int default_height = 720;

	glfwInit();
	GLFWwindow* window = glfwCreateWindow(default_width, default_height, "Test", nullptr, nullptr);
	glfwMakeContextCurrent(window);
	glewInit();

	Test test(default_width, default_height);
	glfwSetWindowUserPointer(window, &test);

	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, cursor_position_callback);
	glfwSetKeyCallback(window, key_callback);

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		int vw, vh;
		glfwGetFramebufferSize(window, &vw, &vh);

		test.Draw(vw, vh);

		glfwSwapBuffers(window);
	}


	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}

