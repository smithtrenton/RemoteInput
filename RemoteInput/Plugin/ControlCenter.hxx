#ifndef ControlCenter_HXX_INCLUDED
#define ControlCenter_HXX_INCLUDED

#include <atomic>
#include "SharedEvent.hxx"
#include "Reflection.hxx"
#include "MemoryMap.hxx"


struct ImageData;

class ControlCenter final {
private:
	pid_t pid;
	bool is_controller;
	std::atomic_bool stopped;
	std::unique_ptr<Mutex> map_lock;
	std::unique_ptr<Semaphore> command_signal;
	std::unique_ptr<Semaphore> response_signal;
	std::unique_ptr<Reflection> reflector;
	std::unique_ptr<MemoryMap<char>> memory_map;

	bool init_maps();
	bool init_locks();
	bool init_signals();
	void process_command();

	ImageData* get_image_data() const;
	bool send_command(std::function<void(ImageData*)> &&writer);

public:
	ControlCenter(pid_t pid, bool is_controller, std::unique_ptr<Reflection> &&reflector);
	~ControlCenter();

	void terminate();
	bool hasReflector();
	void set_parent(pid_t pid);

	std::int32_t parent_id() const;
	std::int32_t get_width() const;
	std::int32_t get_height() const;
	std::uint8_t* get_image() const;

	void get_target_dimensions(std::int32_t* width, std::int32_t* height);
	void get_mouse_position(std::int32_t* x, std::int32_t* y);
	void move_mouse(std::int32_t x, std::int32_t y);
	void hold_mouse(std::int32_t x, std::int32_t y, std::int32_t button);
	void release_mouse(std::int32_t x, std::int32_t y, std::int32_t button);
	
	jobject reflect_object(const ReflectionHook &hook);
	void reflect_release_object(const jobject object);
	char reflect_char(const ReflectionHook &hook);
	std::uint8_t reflect_byte(const ReflectionHook &hook);
	bool reflect_boolean(const ReflectionHook &hook);
	std::int16_t reflect_short(const ReflectionHook &hook);
	std::int32_t reflect_int(const ReflectionHook &hook);
	std::int64_t reflect_long(const ReflectionHook &hook);
	float reflect_float(const ReflectionHook &hook);
	double reflect_double(const ReflectionHook &hook);
	std::string reflect_string(const ReflectionHook &hook);
	jobjectArray reflect_array(const ReflectionHook &hook);
	std::size_t reflect_array_size(const jobjectArray array);
	jobject reflect_array_index(const jobjectArray array, std::size_t index);
};

#endif /* ControlCenter_HXX_INCLUDED */
