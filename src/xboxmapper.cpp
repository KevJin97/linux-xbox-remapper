#include <climits>
#include <sys/poll.h>
#include <xboxmapper.hpp>

#include "libevdev/libevdev-uinput.h"
#include "libevdev/libevdev.h"
#include "libudev.h"

#include <atomic>
#include <iostream>
#include <fcntl.h>
#include <filesystem>
#include <string.h>
#include <poll.h>
#include <unistd.h>

#include <grp.h>
#include <linux/input.h>
#include <sys/eventfd.h>
#include <systemd/sd-bus.h>

static void signal_exit(Controller_Mapper* self_opaque, int event_signal_fd, std::atomic_bool& exit_signal)
{
	exit_signal.store(true, std::memory_order_release);
	exit_signal.notify_all();
	uint64_t val = 1;
	write(event_signal_fd, &val, sizeof(val));
}

int Controller_Mapper::change_group_permissions()
{
	// Set Permissions
	auto grp = getgrnam("input");
	if (grp == NULL)
	{
		std::cerr << "getgrnam(\"input\") failed" << std::endl;
		return -1;
	}
	int oldgid = getgid();
	if (setgid(grp->gr_gid) < 0)
	{
		std::cerr << "Failed to change group to input" << std::endl;
		return -1;
	}
	
	return oldgid;
}

int Controller_Mapper::return_to_original_group_permissions(int gid)
{
	if (setgid(gid) < 0)
	{
		std::cerr << "Could not return Group ID back to original" << std::endl;
		return -1;
	}
	return 0;
}

void Controller_Mapper::process_key_event(unsigned code, int value)
{
	int keycode = 0;

	switch (code)
	{
		case KEY_RECORD:
			break;

		case BTN_SOUTH:
			keycode = KEY_ENTER;
			break;

		case BTN_EAST:
			keycode = KEY_BACKSPACE;
			break;

		case BTN_NORTH:
			break;

		case BTN_WEST:
			keycode = BTN_BACK;
			break;
			
		case BTN_TL:
			libevdev_uinput_write_event(this->virt_dev, EV_KEY, KEY_LEFTSHIFT, value);
		case BTN_TR:
			keycode = KEY_TAB;
			break;

		case BTN_SELECT:
			++this->ptr_speed_setting %= 4;
			break;

		case BTN_START:
		case BTN_MODE:
		case BTN_THUMBL:
		case BTN_THUMBR:
		default:
			return;
	}

	libevdev_uinput_write_event(this->virt_dev, EV_KEY, keycode, value);
}

void Controller_Mapper::process_abs_event(unsigned code, int value)
{
	constexpr int (*absolute)(const int&) = [](const int& value)
	{
		int mask = value >> 31;
		return (value ^ mask) - mask;
	};

	constexpr int pp_arrowkeys[2][2] = { { KEY_LEFT, KEY_RIGHT }, { KEY_UP, KEY_DOWN } };
	constexpr int p_mousebuttons[2] = { BTN_RIGHT, BTN_LEFT };
	constexpr int scaling_factor[4] = { 20000, 17500, 15000, 12500 };
	constexpr int deadzone = 700;
	
	bool option = 1;
	int mapped_code = 0;
	int mapped_value = 0;

	switch (code)
	{
		// Choose between options left/right or up/down and if pressed more than halfway, enable button press
		case ABS_X:
			option = 0;
		case ABS_Y:
			mapped_code = pp_arrowkeys[option][value < 0];
			mapped_value = (absolute(value) >= INT16_MAX / 2);
			break;

		// Choose between options left/right pointer motions and scale the speed if greater than deadzone
		case ABS_RX:
			option = 0;
		case ABS_RY:
			this->ptr_velocity[option] = (absolute(value) > deadzone) ?
				(float)((1 - ((value >> 31) & 2)) * (absolute(value) - deadzone)) / scaling_factor[this->ptr_speed_setting]
				:
				0.0f;
			return;
		
		// Choose option left/right mouse clicks and if pressed more than halfway, enable button press
		case ABS_Z:
			option = 0;
		case ABS_RZ:
			mapped_code = p_mousebuttons[option];
			mapped_value = (value >= 1024 / 2);
			break;

		// Choose between options left/right or up/down and if pressed more than halfway, enable button press
		case ABS_HAT0X:
			option = 0;
		case ABS_HAT0Y:
			if (value == 0)
			{
				libevdev_uinput_write_event(this->virt_dev, EV_KEY, pp_arrowkeys[option][value <= 0], 0);
			}
			mapped_code = pp_arrowkeys[option][value > 0];
			mapped_value = absolute(value);
			break;

		default:
			return;
	}

	libevdev_uinput_write_event(this->virt_dev, EV_KEY, mapped_code, mapped_value);
}

void Controller_Mapper::enable_virtual_device()
{
	struct libevdev* dev = libevdev_new();
	libevdev_enable_property(dev, INPUT_PROP_POINTER);
	libevdev_enable_event_type(dev, EV_SYN);
	libevdev_enable_event_type(dev, EV_KEY);
	libevdev_enable_event_type(dev, EV_REL);
	
	for (unsigned code = 0; code < SYN_CNT; ++code)
	{
		libevdev_enable_event_code(dev, EV_SYN, code, NULL);
	}

	libevdev_enable_event_code(dev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(dev, EV_REL, REL_Y, NULL);

	libevdev_enable_event_code(dev, EV_KEY, KEY_LEFT, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_RIGHT, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_UP, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_DOWN, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_ENTER, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTSHIFT, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_BACKSPACE, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_TAB, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_CAPSLOCK, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_SPACE, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_LEFTMETA, NULL);
	libevdev_enable_event_code(dev, EV_KEY, KEY_ESC, NULL);

	libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(dev, EV_KEY, BTN_MIDDLE, NULL);
	libevdev_enable_event_code(dev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(dev, EV_KEY, BTN_BACK, NULL);
	libevdev_enable_event_code(dev, EV_KEY, BTN_FORWARD, NULL);

	libevdev_set_name(dev, "Virtual-Controller");

	if (this->virt_dev == nullptr)
	{
		if (libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &this->virt_dev) != 0)
		{
			std::cerr << "Creating virtual device failed: " << strerror(errno) << std::endl;
		}
	}

	libevdev_free(dev);
	dev = nullptr;
}

bool Controller_Mapper::substring_contains(const std::string& to_search, const std::string& to_compare, bool case_sensitive)
{
	for (std::size_t n = 0, k = 0; n < to_search.size(); ++n)
	{
		char comparison_chars[2] = { to_search[n], to_compare[k] };

		if (case_sensitive == false)
		{
			if ('A' <= comparison_chars[0] && comparison_chars[0] <= 'Z')
			{
				comparison_chars[0] += 'a' - 'A';
			}
			if ('A' <= comparison_chars[1] && comparison_chars[1] <= 'Z')
			{
				comparison_chars[1] += 'a' - 'A';
			}
		}

		if (comparison_chars[0] == comparison_chars[1])
		{
			if (++k == to_compare.size())
			{
				return true;
			}
		}
		else
		{
			k = 0;
		}
	}

	return false;
}

bool Controller_Mapper::check_process_running(const std::string& name)
{
	for (const auto& entry : std::filesystem::directory_iterator("/proc"))
	{
		if (!entry.is_directory())
			continue;
 
		const std::string& dirname = entry.path().filename().string();
		if (dirname.empty() || !std::isdigit(static_cast<unsigned char>(dirname[0])))
			continue;
 
		std::string comm_path = entry.path().string() + "/comm";
		int fd = open(comm_path.c_str(), O_RDONLY);
		if (fd < 0)
			continue;
 
		char buf[256];
		ssize_t len = read(fd, buf, sizeof(buf) - 1);
		close(fd);
 
		if (len > 0)
		{
			buf[len] = '\0';
			if (len > 0 && buf[len - 1] == '\n')
				buf[len - 1] = '\0';
 
			if (name == buf)
				return true;
		}
	}
	return false;
}

int Controller_Mapper::on_prepare_for_shutdown(sd_bus_message* msg, void* userdata, sd_bus_error* /*error*/)
{
	auto* self = static_cast<Controller_Mapper*>(userdata);
	int active = 0;
	if (sd_bus_message_read(msg, "b", &active) >= 0 && active)
	{
		std::cerr << "[xboxmapper] PrepareForShutdown(true) received — exiting" << std::endl;
		signal_exit(self, self->event_signal_fd, self->exit_signal);
	}
	return 0;
}
 
int Controller_Mapper::on_prepare_for_sleep(sd_bus_message* msg, void* userdata, sd_bus_error* /*error*/)
{
	auto* self = static_cast<Controller_Mapper*>(userdata);
	int active = 0;
	if (sd_bus_message_read(msg, "b", &active) >= 0 && active)
	{
		std::cerr << "[xboxmapper] PrepareForSleep(true) received — exiting" << std::endl;
		signal_exit(self, self->event_signal_fd, self->exit_signal);
	}
	return 0;
}
 
int Controller_Mapper::on_session_removed(sd_bus_message* msg, void* userdata, sd_bus_error* /*error*/)
{
	// SessionRemoved(String session_id, ObjectPath session_path)
	auto* self = static_cast<Controller_Mapper*>(userdata);
 
	const char* session_id = nullptr;
	const char* session_path = nullptr;
	if (sd_bus_message_read(msg, "so", &session_id, &session_path) < 0)
		return 0;
 
	// If we have no target user, we run at the login screen.
	// Any session being removed means the user logged out and we're
	// back at the greeter — that's fine, keep running.
	if (self->target_user.empty())
		return 0;
 
	// A session was removed. We need to check whether our target user
	// still has an active graphical session. Query logind.
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = nullptr;
	int r = sd_bus_call_method(self->bus,
		"org.freedesktop.login1",
		"/org/freedesktop/login1",
		"org.freedesktop.login1.Manager",
		"ListSessions",
		&err, &reply, "");
 
	bool target_session_found = false;
 
	if (r >= 0)
	{
		// ListSessions returns a(susso) — array of (session_id, uid, user_name, seat, object_path)
		r = sd_bus_message_enter_container(reply, 'a', "(susso)");
		if (r >= 0)
		{
			const char* sid = nullptr;
			uint32_t uid = 0;
			const char* uname = nullptr;
			const char* seat = nullptr;
			const char* opath = nullptr;
 
			while (sd_bus_message_read(reply, "(susso)", &sid, &uid, &uname, &seat, &opath) > 0)
			{
				if (self->target_user == uname)
				{
					target_session_found = true;
					break;
				}
			}
			sd_bus_message_exit_container(reply);
		}
		sd_bus_message_unref(reply);
	}
	sd_bus_error_free(&err);
 
	if (!target_session_found)
	{
		std::cerr << "[xboxmapper] Target user '" << self->target_user
		          << "' has no remaining sessions — exiting" << std::endl;
		signal_exit(self, self->event_signal_fd, self->exit_signal);
	}
 
	return 0;
}
 
int Controller_Mapper::on_session_new(sd_bus_message* msg, void* userdata, sd_bus_error* /*error*/)
{
	// SessionNew(String session_id, ObjectPath session_path)
	// When running at the login screen (target_user empty), a new session
	// means a user just logged in. If it's not our target user (or we have
	// no target), we should exit so the per-user instance can take over.
	auto* self = static_cast<Controller_Mapper*>(userdata);
 
	if (self->target_user.empty())
	{
		// We're the greeter instance — any real user session means we stop.
		const char* session_id = nullptr;
		const char* session_path = nullptr;
		if (sd_bus_message_read(msg, "so", &session_id, &session_path) < 0)
			return 0;
 
		std::cerr << "[xboxmapper] SessionNew while at login screen — exiting" << std::endl;
		signal_exit(self, self->event_signal_fd, self->exit_signal);
	}
 
	return 0;
}

Controller_Mapper::Controller_Mapper()
{
	this->old_group_id = this->change_group_permissions();
	this->event_signal_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

Controller_Mapper::~Controller_Mapper()
{
	this->exit_signal.store(true, std::memory_order_release);
	this->exit_signal.notify_all();
 
	// Wake wait_for_new_event if it's blocked
	uint64_t val = 1;
	write(this->event_signal_fd, &val, sizeof(val));
 
	if (this->dev != nullptr)
	{
		libevdev_free(this->dev);
		this->dev = nullptr;
	}
 
	if (this->virt_dev != nullptr)
	{
		libevdev_uinput_destroy(this->virt_dev);
		this->virt_dev = nullptr;
	}
 
	// Note: udev_ctx, udev_mon, and bus are owned by the trigger thread
	// and cleaned up at the end of wait_for_triggers().
 
	if (this->event_signal_fd >= 0)
	{
		close(this->event_signal_fd);
		this->event_signal_fd = -1;
	}

	this->return_to_original_group_permissions(this->old_group_id);
}

void Controller_Mapper::wait_for_triggers()
{
		// ---- udev monitor setup ----
	this->udev_ctx = udev_new();
	if (!this->udev_ctx)
	{
		std::cerr << "[xboxmapper] Failed to create udev context" << std::endl;
		return;
	}
 
	this->udev_mon = udev_monitor_new_from_netlink(this->udev_ctx, "udev");
	if (!this->udev_mon)
	{
		std::cerr << "[xboxmapper] Failed to create udev monitor" << std::endl;
		udev_unref(this->udev_ctx);
		this->udev_ctx = nullptr;
		return;
	}
	udev_monitor_filter_add_match_subsystem_devtype(this->udev_mon, "input", NULL);
	udev_monitor_enable_receiving(this->udev_mon);
	int udev_fd = udev_monitor_get_fd(this->udev_mon);
 
	// ---- D-Bus / logind setup ----
	int dbus_fd = -1;
 
	if (sd_bus_open_system(&this->bus) < 0)
	{
		std::cerr << "[xboxmapper] Failed to connect to system D-Bus" << std::endl;
		this->bus = nullptr;
	}
	else
	{
		// PrepareForShutdown(boolean active)
		sd_bus_add_match(this->bus, NULL,
			"type='signal',"
			"sender='org.freedesktop.login1',"
			"interface='org.freedesktop.login1.Manager',"
			"member='PrepareForShutdown',"
			"path='/org/freedesktop/login1'",
			Controller_Mapper::on_prepare_for_shutdown, this);
 
		// PrepareForSleep(boolean active)
		sd_bus_add_match(this->bus, NULL,
			"type='signal',"
			"sender='org.freedesktop.login1',"
			"interface='org.freedesktop.login1.Manager',"
			"member='PrepareForSleep',"
			"path='/org/freedesktop/login1'",
			Controller_Mapper::on_prepare_for_sleep, this);
 
		// SessionRemoved(String session_id, ObjectPath session_path)
		sd_bus_add_match(this->bus, NULL,
			"type='signal',"
			"sender='org.freedesktop.login1',"
			"interface='org.freedesktop.login1.Manager',"
			"member='SessionRemoved',"
			"path='/org/freedesktop/login1'",
			Controller_Mapper::on_session_removed, this);
 
		// SessionNew(String session_id, ObjectPath session_path)
		sd_bus_add_match(this->bus, NULL,
			"type='signal',"
			"sender='org.freedesktop.login1',"
			"interface='org.freedesktop.login1.Manager',"
			"member='SessionNew',"
			"path='/org/freedesktop/login1'",
			Controller_Mapper::on_session_new, this);
 
		dbus_fd = sd_bus_get_fd(this->bus);
	}
 
	// ---- Main poll loop ----
	while (this->is_not_exit_signal())
	{
		struct pollfd fds[2];
		int nfds = 0;
 
		fds[0].fd = udev_fd;
		fds[0].events = POLLIN;
		nfds = 1;
 
		if (dbus_fd >= 0)
		{
			fds[1].fd = dbus_fd;
			fds[1].events = POLLIN;
			nfds = 2;
		}
 
		// 2-second timeout gives us a periodic heartbeat for the Steam check
		int ret = poll(fds, nfds, 2000);
 
		if (ret < 0)
		{
			if (errno == EINTR)
				continue;
			std::cerr << "[xboxmapper] poll() failed: " << strerror(errno) << std::endl;
			break;
		}
 
		// ---- udev: new input device hotplugged ----
		if (fds[0].revents & POLLIN)
		{
			struct udev_device* udev_dev = udev_monitor_receive_device(this->udev_mon);
			if (udev_dev)
			{
				const char* action = udev_device_get_action(udev_dev);
				if (action && strcmp(action, "add") == 0)
				{
					uint64_t val = 1;
					write(this->event_signal_fd, &val, sizeof(val));
				}
				udev_device_unref(udev_dev);
			}
		}
 
		// ---- D-Bus: dispatch pending messages (callbacks fire here) ----
		if (this->bus && nfds > 1 && (fds[1].revents & POLLIN))
		{
			while (sd_bus_process(this->bus, NULL) > 0)
			{
				// sd_bus_process dispatches one message per call.
				// Our callbacks (on_prepare_for_shutdown, etc.) fire
				// during this call. Keep draining until 0 is returned.
			}
		}
 
		// ---- Periodic: Steam process check ----
		// Only meaningful when we already have a controller attached.
		if (this->dev != nullptr)
		{
			bool steam_running = this->check_process_running("steam");
			this->grab_signal.store(steam_running, std::memory_order_release);
		}
	}
 
	// ---- Cleanup (trigger thread owns these) ----
	if (this->bus)
	{
		sd_bus_unref(this->bus);
		this->bus = nullptr;
	}
	if (this->udev_mon)
	{
		udev_monitor_unref(this->udev_mon);
		this->udev_mon = nullptr;
	}
	if (this->udev_ctx)
	{
		udev_unref(this->udev_ctx);
		this->udev_ctx = nullptr;
	}
}

bool Controller_Mapper::is_not_exit_signal()
{
	return !this->exit_signal.load(std::memory_order_acquire);
}

void Controller_Mapper::check_for_device(const std::string& at_path)
{
	std::string keywords = "xbox";

	for (const auto& entry : std::filesystem::directory_iterator(at_path.c_str()))
	{
		if (entry.path().filename().string().starts_with("event"))
		{
			int fd = open((at_path + "/" + entry.path().filename().string()).c_str(), O_RDONLY | O_NONBLOCK);
			libevdev_new_from_fd(fd, &this->dev);
			// Check if allocation was successful

			std::string device_name = libevdev_get_name(this->dev);

			if (this->substring_contains(device_name, keywords, false))
			{
				this->enable_virtual_device();
				return;
			}
			else
			{
				libevdev_free(this->dev);
				this->dev = nullptr;
			}
		}
	}
}

void Controller_Mapper::wait_for_new_event()
{
	struct pollfd pfd;
	pfd.fd = this->event_signal_fd;
	pfd.events = POLLIN;
 
	poll(&pfd, 1, -1);
 
	if (pfd.revents & POLLIN)
	{
		uint64_t val;
		read(this->event_signal_fd, &val, sizeof(val));
	}
}

bool Controller_Mapper::has_been_found()
{
	return (this->dev == nullptr) ? false : true;
}

void Controller_Mapper::map_keys()
{
	if (this->dev == nullptr || this->virt_dev == nullptr)
		return;
	
	int poll_timeout = -1;	// In milliseconds
	struct pollfd pfd[2];

	pfd[0].fd = libevdev_get_fd(this->dev);
	pfd[0].events = POLLIN | POLLHUP | POLLERR;
	pfd[1].fd = this->event_signal_fd;
	pfd[1].events = POLLIN;

	while (this->is_not_exit_signal())
	{
		if (libevdev_has_event_pending(this->dev))
		{
			struct input_event ev;
			libevdev_next_event(this->dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
			switch (ev.type)
			{
				case EV_SYN:
					libevdev_uinput_write_event(this->virt_dev, EV_SYN, SYN_REPORT, 0);
					break;

				case EV_KEY:
					this->process_key_event(ev.code, ev.value);
					break;

				case EV_ABS:
					this->process_abs_event(ev.code, ev.value);
					break;

				default:
					break;
			}
		}

		if ((int)this->ptr_velocity[0] || (int)this->ptr_velocity[1])
		{
			poll_timeout = 1;
			this->ptr_accumulator[0] += this->ptr_velocity[0];
			this->ptr_accumulator[1] += this->ptr_velocity[1];

			int dx = (int)this->ptr_accumulator[0];
			int dy = (int)this->ptr_accumulator[1];

			if (dx || dy)
			{
				this->ptr_accumulator[0] -= dx;
				this->ptr_accumulator[1] -= dy;
				libevdev_uinput_write_event(this->virt_dev, EV_REL, REL_X, dx);
				libevdev_uinput_write_event(this->virt_dev, EV_REL, REL_Y, dy);
				libevdev_uinput_write_event(this->virt_dev, EV_SYN, SYN_REPORT, 0);
			}
		}
		else
		{
			poll_timeout = -1;
		}
		
		if (poll(pfd, 2, poll_timeout) < 0)	// Handle polling error
		{
			std::cerr << "Input polling failed: " << strerror(errno) << std::endl;
			break;
		}
		else if (pfd[0].revents & (POLLHUP | POLLERR))
		{
			libevdev_free(this->dev);
			this->dev = nullptr;

			libevdev_uinput_destroy(this->virt_dev);
			this->virt_dev = nullptr;
			break;
		}
		if (pfd[1].revents & POLLIN)
		{
			uint64_t val;
			read(this->event_signal_fd, &val, sizeof(val));
			break;
		}
	}
}