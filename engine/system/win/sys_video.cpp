// SimpleGraphic Engine
// (c) David Gowor, 2014
//
// Module: System Video
// Platform: Windows
//

#include "sys_local.h"
#include "core.h"

#include <GLFW/glfw3.h>

#include <optional>
#include <utility>

// ====================
// sys_IVideo Interface
// ====================

class sys_video_c: public sys_IVideo {
public:
	// Interface
	int		Apply(sys_vidSet_s* set);

	void	SetActive(bool active);
	void	SetForeground();
	bool	IsActive();
	void	FramebufferSizeChanged(int width, int height);
	void	SizeChanged(int width, int height, bool max);
	void	PosChanged(int x, int y);
	void	GetMinSize(int &width, int &height);
	void	SetVisible(bool vis);
	bool	IsVisible();
	void	SetTitle(const char* title);
	void*	GetWindowHandle();
	void	GetRelativeCursor(int &x, int &y);
	void	SetRelativeCursor(int x, int y);
	bool	IsCursorOverWindow();

	// Encapsulated
	sys_video_c(sys_IMain* sysHnd);
	~sys_video_c();

	sys_main_c* sys = nullptr;

	bool	initialised = false;
	GLFWwindow* wnd = nullptr;

	void	RefreshMonitorInfo();

	int		numMon = 0;			// Number of monitors
	int		priMon = 0;			// Index of primary monitor
	struct {
		GLFWmonitor* hnd = nullptr;
		int		left = 0;
		int		top = 0;
		int		width = 0;
		int		height = 0;
	} mon[16];					// Array of monitor specs

	int		defRes[2] = {};		// Default resolution
	sys_vidSet_s cur;			// Current settings
	int		scrSize[2] = {};	// Screen size
	int		minSize[2] = {};	// Minimum window size
	char	curTitle[512] = {};	// Window title

	bool cursorInWindow = false;

	struct CursorPos {
		int x, y;
	};
	std::optional<CursorPos> lastCursorPos;

	struct ClickEvent
	{
		std::chrono::system_clock::time_point time;
		CursorPos pos;
		byte button;
	};
	std::optional<ClickEvent> lastClick;
};

sys_IVideo* sys_IVideo::GetHandle(sys_IMain* sysHnd)
{
	return new sys_video_c(sysHnd);
}

void sys_IVideo::FreeHandle(sys_IVideo* hnd)
{
	delete (sys_video_c*)hnd;
}

sys_video_c::sys_video_c(sys_IMain* sysHnd)
	: sys((sys_main_c*)sysHnd)
{
	initialised = false;

	minSize[0] = minSize[1] = 0;

	strcpy(curTitle, CFG_TITLE);

	glfwInit();
}

sys_video_c::~sys_video_c()
{
	if (initialised) {
		glfwDestroyWindow(wnd);
	}

	glfwTerminate();
}

std::optional<std::pair<double, double>> PlatformGetCursorPos() {
#if _WIN32
	POINT curPos;
	GetCursorPos(&curPos);
	return std::make_pair((double)curPos.x, (double)curPos.y);
#else
#warning LV: Global cursor position queries not implemented yet on this OS.
	// TODO(LV): Implement on other OSes
	return {};
#endif
}

// ==================
// System Video Class
// ==================

int sys_video_c::Apply(sys_vidSet_s* set)
{
	cur = *set;

	GLFWmonitor** monitors = glfwGetMonitors(&numMon);
	for (int i = 0; i < numMon; ++i) {
		mon[i].hnd = monitors[i];
		glfwGetMonitorPos(monitors[i], &mon[i].left, &mon[i].top);
		GLFWvidmode const* mode = glfwGetVideoMode(monitors[i]);
		mon[i].width = mode->width;
		mon[i].height = mode->height;
	}
	priMon = 0;

	// Determine which monitor to create window on
	if (cur.display >= numMon) {
		sys->con->Warning("display #%d doesn't exist (max display number is %d)", cur.display, numMon - 1);
		cur.display = 0;
	} else if (cur.display < 0) {
		// Use monitor containing the mouse cursor if available, otherwise primary monitor
		cur.display = 0;
		if (auto curPos = PlatformGetCursorPos()) {
			auto [curX, curY] = *curPos;
			for (int m = 0; m < numMon; ++m) {
				int right = mon[m].left + mon[m].width;
				int bottom = mon[m].top + mon[m].height;
				if (curX >= mon[m].left && curY >= mon[m].top && curX < right && curY < bottom) {
					cur.display = m;
					break;
				}
			}
		}
	}
	defRes[0] = mon[cur.display].width;
	defRes[1] = mon[cur.display].height;

	minSize[0] = minSize[1] = 0;

	if (sys->debuggerRunning) {
		// Force topmost off if debugger is attached
		cur.flags&= ~VID_TOPMOST;
	}
	if (cur.mode[0] == 0) {
		// Use default resolution if one isn't specified
		Vector2Copy(defRes, cur.mode);
	}
	Vector2Copy(cur.mode, vid.size);
	Vector2Copy(defRes, scrSize);

	struct WindowRect {
		int left, top;
		int right, bottom;
	};

	// Get window rectangle
	WindowRect wrec;
	if (cur.flags & VID_USESAVED) {
		// TODO(LV): Move offscreen windows to a monitor.
		wrec.left = cur.save.pos[0];
		wrec.top = cur.save.pos[1];
		if (cur.save.maximised) {
			cur.flags|= VID_MAXIMIZE;
		} else {
			cur.mode[0] = cur.save.size[0];
			cur.mode[1] = cur.save.size[1];
		}
	} else {
		wrec.left = (scrSize[0] - cur.mode[0])/2 + mon[cur.display].left;
		wrec.top = (scrSize[1] - cur.mode[1])/2 + mon[cur.display].top;
	}
	vid.pos[0] = wrec.left;
	vid.pos[1] = wrec.top;
	wrec.right = wrec.left + cur.mode[0];
	wrec.bottom = wrec.top + cur.mode[1];
	// TODO(LV): Verify that stored coordinates are aligned right.

	if (initialised) {
		glfwSetWindowSize(wnd, cur.mode[0], cur.mode[1]);
		if (cur.shown) {
			glfwShowWindow(wnd);
			sys->conWin->SetForeground();
		}
	}
	else {
		glfwWindowHint(GLFW_RESIZABLE, !!(cur.flags & VID_RESIZABLE));
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
		glfwWindowHint(GLFW_FLOATING, !!(cur.flags & VID_TOPMOST));
		glfwWindowHint(GLFW_MAXIMIZED, !!(cur.flags & VID_MAXIMIZE));
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
		glfwWindowHint(GLFW_DEPTH_BITS, 24);
		//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
		wnd = glfwCreateWindow(cur.mode[0], cur.mode[1], curTitle, nullptr, nullptr);
		if (!wnd) {
			char const* errDesc = "Unknown error";
			glfwGetError(&errDesc);
			sys->con->Printf("Could not create window, %s\n", errDesc);
		}
		glfwMakeContextCurrent(wnd);
		glfwSetWindowUserPointer(wnd, sys);
		glfwSetCursorEnterCallback(wnd, [](GLFWwindow* wnd, int entered) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			auto video = (sys_video_c*)sys->video;
			bool is_inside = !!entered;
			video->cursorInWindow = is_inside;
			if (!is_inside) {
				video->lastCursorPos.reset();
			}
		});
		glfwSetCursorPosCallback(wnd, [](GLFWwindow* wnd, double x, double y) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			auto video = (sys_video_c*)sys->video;
			video->lastCursorPos = CursorPos{(int)x, (int)y};
		});
		glfwSetWindowCloseCallback(wnd, [](GLFWwindow* wnd) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			glfwSetWindowShouldClose(wnd, sys->initialised && sys->core->CanExit());
		});
		glfwSetFramebufferSizeCallback(wnd, [](GLFWwindow* wnd, int width, int height) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			sys->video->FramebufferSizeChanged(width, height);
		});
		glfwSetWindowSizeCallback(wnd, [](GLFWwindow* wnd, int width, int height) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			bool maximized = glfwGetWindowAttrib(wnd, GLFW_MAXIMIZED);
			sys->video->SizeChanged(width, height, maximized);
		});
		glfwSetWindowPosCallback(wnd, [](GLFWwindow* wnd, int x, int y) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			sys->video->PosChanged(x, y);
		});
		glfwSetCharCallback(wnd, [](GLFWwindow* wnd, uint32_t codepoint) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			sys->core->KeyEvent(codepoint, KE_CHAR);
		});
		glfwSetKeyCallback(wnd, [](GLFWwindow* wnd, int key, int scancode, int action, int mods) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			if (byte k = sys->GlfwKeyToKey(key)) {
				bool is_down = action == GLFW_PRESS || action == GLFW_REPEAT;
				sys->heldKeyState[k] = is_down;
				sys->core->KeyEvent(k, is_down ? KE_KEYDOWN : KE_KEYUP);
				char ch = sys->GlfwKeyExtraChar(key);
				if (is_down && ch) {
					sys->core->KeyEvent(ch, KE_CHAR);
				}
			}
		});
		glfwSetMouseButtonCallback(wnd, [](GLFWwindow* wnd, int button, int action, int mods) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			auto video = (sys_video_c*)sys->video;
			int sg_key;
			switch(button) {
			case GLFW_MOUSE_BUTTON_LEFT:
				sg_key = KEY_LMOUSE;
				break;
			case GLFW_MOUSE_BUTTON_MIDDLE:
				sg_key = KEY_MMOUSE;
				break;
			case GLFW_MOUSE_BUTTON_RIGHT:
				sg_key = KEY_RMOUSE;
				break;
			case GLFW_MOUSE_BUTTON_4:
				sg_key = KEY_MOUSE4;
				break;
			case GLFW_MOUSE_BUTTON_5:
				sg_key = KEY_MOUSE5;
				break;
			default:
				return;
			}
			bool is_down = action == GLFW_PRESS;
			sys->heldKeyState[sg_key] = is_down;
			keyEvent_s sg_type = is_down ? KE_KEYDOWN : KE_KEYUP;

			// Determine if a click is a doubleclick by checking for recent nearby click of same type.
			auto& lastClick = video->lastClick;
			if (is_down && video->lastCursorPos) {
				auto now = std::chrono::system_clock::now();

				// Was the last click with the same button?
				if (lastClick && lastClick->button == sg_key) {
					std::chrono::milliseconds const DOUBLECLICK_DELAY(500);
					int const DOUBLECLICK_RANGE = 5;

					// Was it recent and close enough?
					auto durationSinceClick = now - lastClick->time;
					auto prevPos = lastClick->pos;
					auto curPos = *video->lastCursorPos;
					if (durationSinceClick <= DOUBLECLICK_DELAY &&
						abs(prevPos.x - curPos.x) <= DOUBLECLICK_RANGE &&
						abs(prevPos.y - curPos.y) <= DOUBLECLICK_RANGE)
					{
							sg_type = KE_DBLCLK;
					}
				}
				if (sg_type == KE_DBLCLK) {
					lastClick.reset();
				}
				else {
					ClickEvent e;
					e.time = now;
					e.pos = *video->lastCursorPos;
					e.button = sg_key;
					lastClick = e;
				}
			}
			sys->core->KeyEvent(sg_key, sg_type);
		});
		glfwSetScrollCallback(wnd, [](GLFWwindow* wnd, double xoffset, double yoffset) {
			auto sys = (sys_main_c*)glfwGetWindowUserPointer(wnd);
			if (yoffset > 0) {
				sys->core->KeyEvent(KEY_MWHEELUP, KE_KEYDOWN);
				sys->core->KeyEvent(KEY_MWHEELUP, KE_KEYUP);
			} else if (yoffset < 0) {
				sys->core->KeyEvent(KEY_MWHEELDOWN, KE_KEYDOWN);
				sys->core->KeyEvent(KEY_MWHEELDOWN, KE_KEYUP);
			}
		});
	}

	glfwSetWindowSizeLimits(wnd, cur.minSize[0], cur.minSize[1], GLFW_DONT_CARE, GLFW_DONT_CARE);

	glfwSetWindowPos(wnd, vid.pos[0], vid.pos[1]);
	glfwGetFramebufferSize(wnd, &vid.fbSize[0], &vid.fbSize[1]);
	glfwGetWindowSize(wnd, &vid.size[0], &vid.size[1]);

	initialised = true;
	return 0;
}

void sys_video_c::SetActive(bool active)
{
	if (initialised) {
		glfwFocusWindow(wnd);
	}
}

void sys_video_c::SetForeground()
{
	if (initialised) {
		glfwFocusWindow(wnd);
	}
}

bool sys_video_c::IsActive()
{
	return glfwGetWindowAttrib(wnd, GLFW_FOCUSED);
}

void sys_video_c::FramebufferSizeChanged(int width, int height) {
	vid.fbSize[0] = width;
	vid.fbSize[1] = height;
}

void sys_video_c::SizeChanged(int width, int height, bool max)
{
	vid.size[0] = width;
	vid.size[1] = height;
	vid.maximised = max;
}

void sys_video_c::PosChanged(int x, int y)
{
	vid.pos[0] = x;
	vid.pos[1] = y;
}

void sys_video_c::GetMinSize(int &width, int &height)
{
	width = minSize[0];
	height = minSize[1];
}

void sys_video_c::SetVisible(bool vis)
{
	if ( !initialised ) return;
	if (vis) {
		glfwShowWindow(wnd);
	} else {
		glfwHideWindow(wnd);
	}
}

bool sys_video_c::IsVisible()
{
	if ( !initialised || !wnd ) return false;
	return !!glfwGetWindowAttrib(wnd, GLFW_VISIBLE);
}

void sys_video_c::SetTitle(const char* title)
{
	strcpy(curTitle, (title && *title)? title : CFG_TITLE);
	if (initialised) {
		glfwSetWindowTitle(wnd, curTitle);
	}
}

void* sys_video_c::GetWindowHandle()
{
	return wnd;
}

void sys_video_c::GetRelativeCursor(int &x, int &y)
{
	if ( !initialised ) return;
	double xpos, ypos;
	glfwGetCursorPos(wnd, &xpos, &ypos);
	x = (int)floor(xpos);
	y = (int)floor(ypos);
}

void sys_video_c::SetRelativeCursor(int x, int y)
{
	if ( !initialised ) return;
	glfwSetCursorPos(wnd, (double)x, (double)y);
}

bool sys_video_c::IsCursorOverWindow()
{
	if (initialised && wnd && IsVisible())
	{
		return cursorInWindow;
	}
	return true;
}

void sys_video_c::RefreshMonitorInfo()
{
	GLFWmonitor** monitors = glfwGetMonitors(&numMon);
	for (int m = 0; m < numMon; m++) {
		mon[m].hnd = monitors[m];
		glfwGetMonitorPos(monitors[m], &mon[m].left, &mon[m].top);
		GLFWvidmode const* mode = glfwGetVideoMode(monitors[m]);
		mon[m].width = mode->width;
		mon[m].height = mode->height;
	}
}
