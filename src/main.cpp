#include <xboxmapper.hpp>

#include <string>
#include <thread>

int main()
{
	const std::string at_path = "/dev/input";
	Controller_Mapper controller;
	std::thread device_monitor([&]{ controller.wait_for_triggers(); });

	while (controller.is_not_exit_signal())
	{
		controller.check_for_device(at_path);

		if (controller.has_been_found())
		{
			controller.map_keys();
		}
		else
		{
			controller.wait_for_new_event();
		}
	}

	device_monitor.join();
	return 0;
}