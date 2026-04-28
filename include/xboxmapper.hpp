#ifndef XBOXMAPPER_HPP
#define XBOXMAPPER_HPP

#include <atomic>
#include <string>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#include <systemd/sd-bus.h>
#include <sys/eventfd.h>

class Controller_Mapper
{
	static int on_prepare_for_shutdown(sd_bus_message* msg, void* userdata, sd_bus_error* error);
	static int on_prepare_for_sleep(sd_bus_message* msg, void* userdata, sd_bus_error* error);
	static int on_session_removed(sd_bus_message* msg, void* userdata, sd_bus_error* error);
	static int on_session_new(sd_bus_message* msg, void* userdata, sd_bus_error* error);
	
	private:
		int old_group_id = 0;
		int event_signal_fd = -1;
		std::atomic_bool exit_signal = false;
		std::atomic_bool grab_signal = false;
		struct libevdev* dev = nullptr;
		struct libevdev_uinput* virt_dev = nullptr;
		int ptr_speed_setting = 0;
		float ptr_velocity[2] = { 0.0f };
		float ptr_accumulator[2] = { 0.0f };

		struct udev* udev_ctx = nullptr;
		struct udev_monitor* udev_mon = nullptr;
		sd_bus* bus = nullptr;
		std::string target_user;

		int change_group_permissions();
		int return_to_original_group_permissions(int gid);
		void process_key_event(unsigned code, int value);
		void process_abs_event(unsigned code, int value);
		void enable_virtual_device();
		bool substring_contains(const std::string& to_search, const std::string& to_compare, bool case_sensitive=true);
		bool check_process_running(const std::string& name);

	public:
		Controller_Mapper();
		~Controller_Mapper();

		void wait_for_triggers();
		bool is_not_exit_signal();
		void check_for_device(const std::string& at_path);
		void wait_for_new_event();
		bool has_been_found();
		void map_keys();
};

#endif	// XBOXMAPPER_HPP