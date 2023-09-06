#include "ControlCenter.hxx"
#include <thread>
#include <unordered_map>
#include <cstring>
#include <utility>
#include "EIOS.hxx"
#include "Platform.hxx"
#include "NativeHooks.hxx"
#include "Graphics.hxx"
#include "ReflectionHook.hxx"
#include "RemoteVM.hxx"

ControlCenter::ControlCenter(std::int32_t pid, bool is_controller, std::unique_ptr<Reflection> &&reflector) : pid(pid), is_controller(is_controller), stopped(is_controller), map_lock(), command_signal(), response_signal(), sync_signal(), reflector(std::move(reflector)), io_controller(), remote_vm()
{
    if (pid <= 0)
    {
        throw std::runtime_error("Invalid Process ID");
    }

    if (!init_maps())
    {
        throw std::runtime_error("Cannot Initialize Maps");
    }

    if (!init_wait())
    {
        throw std::runtime_error("Cannot Initialize Wait Signal");
    }

    /*if (!init_locks())
    {
        throw std::runtime_error("Cannot Initialize Locks");
    }*/

    if (!init_signals())
    {
        throw std::runtime_error("Cannot Initialize Signals");
    }

    if (!is_controller)
    {
        this->set_parent_process_id(-1);
        this->set_parent_thread_id(-1);

        if (this->reflector)
        {
            std::thread thread = std::thread([&]{

                #if defined(_WIN32) || defined(_WIN64)
                HANDLE this_process = GetCurrentProcess();
                SetPriorityClass(this_process, NORMAL_PRIORITY_CLASS);

                HANDLE this_thread = GetCurrentThread();
                SetThreadPriority(this_thread, THREAD_PRIORITY_HIGHEST);
                #else
                pthread_t this_thread = pthread_self();
                if (this_thread)
                {
                    struct sched_param params = {0};
                    params.sched_priority = sched_get_priority_max(SCHED_FIFO);
                    pthread_setschedparam(this_thread, SCHED_FIFO, &params);
                }
                #endif // defined

                if (this->reflector->Attach())
                {
                    this->io_controller = std::make_unique<InputOutput>(this->reflector.get());
                    this->remote_vm = std::make_unique<RemoteVM>(this->reflector->getEnv(),
                                                                 this);

                    while(!stopped)
                    {
                        if (!command_signal || /*!map_lock ||*/ !response_signal || !memory_map)
                        {
                            break;
                        }

                        command_signal->wait();

                        /*while(!command_signal->try_wait())
                        {
                            std::this_thread::sleep_for(std::chrono::nanoseconds(1));
                        }*/

                        if (stopped)
                        {
                            break;
                        }

                        process_command();
                        response_signal->signal();
                    }

                    if (this->remote_vm)
                    {
                        this->remote_vm.reset();
                    }

                    if (this->io_controller)
                    {
                        this->io_controller.reset();
                    }

                    if (this->reflector)
                    {
                        this->reflector->Detach();
                        this->reflector.reset();
                    }
                }
            });

            thread.detach();
        }
    }
}

ControlCenter::~ControlCenter()
{
    terminate();
    //reflector.reset();

    if (map_lock)
    {
        map_lock.reset();
    }

    if (memory_map)
    {
        memory_map.reset();
    }
}

void ControlCenter::terminate() noexcept
{
    if (is_controller)
    {
        stop_all_processing();
    }
    else
    {
        if (!stopped)
        {
            stopped = true;

            if (command_signal)
            {
                command_signal->signal();
            }
        }
    }
}

void ControlCenter::process_command() noexcept
{
    ImageData& image_data = memory_map->image_data();
    Stream& stream = image_data.data_stream();
    image_data.prepare_for_read();
    image_data.prepare_for_write();

    switch(image_data.command())
    {
        case EIOSCommand::COMMAND_NONE:
            break;

        case EIOSCommand::KILL_APPLICATION:
        {
            stopped = true;
            response_signal->signal();

            if (this->io_controller)
            {
                this->io_controller.reset();
            }


            if (this->reflector)
            {
                this->reflector->Detach();
                this->reflector.reset();
            }

            std::exit(0);
        }
            break;

        case EIOSCommand::GET_TARGET_DIMENSIONS:
        {
            image_data.set_width(get_width());
            image_data.set_height(get_height());
        }
            break;

        case EIOSCommand::HAS_FOCUS:
        {
            bool result = io_controller->has_focus();
            stream.write(result);
        }
            break;

        case EIOSCommand::GAIN_FOCUS:
        {
            io_controller->gain_focus();
        }
            break;

        case EIOSCommand::LOSE_FOCUS:
        {
            io_controller->lose_focus();
        }
            break;

        case EIOSCommand::IS_KEYBOARD_INPUT_ENABLED:
        {
            bool result = io_controller->is_keyboard_input_enabled();
            stream.write(result);
        }
            break;

        case EIOSCommand::SET_KEYBOARD_INPUT_ENABLED:
        {
            bool enabled = stream.read<bool>();
            io_controller->set_keyboard_input_enabled(enabled);
        }
            break;

        case EIOSCommand::IS_MOUSE_INPUT_ENABLED:
        {
            bool result = io_controller->is_mouse_input_enabled();
            stream.write(result);
        }
            break;

        case EIOSCommand::SET_MOUSE_INPUT_ENABLED:
        {
            bool enabled = stream.read<bool>();
            io_controller->set_mouse_input_enabled(enabled);
        }
            break;

        case EIOSCommand::GET_MOUSE:
        {
            std::int32_t x = -1;
            std::int32_t y = -1;

            io_controller->get_mouse_position(&x, &y);
            stream.write(x);
            stream.write(y);
        }
            break;

        case EIOSCommand::GET_REAL_MOUSE:
        {
            std::int32_t x = -1;
            std::int32_t y = -1;

            io_controller->get_real_mouse_position(&x, &y);
            stream.write(x);
            stream.write(y);
        }
            break;

        case EIOSCommand::MOVE_MOUSE:
        {
            std::int32_t x = stream.read<std::int32_t>();
            std::int32_t y = stream.read<std::int32_t>();
            io_controller->move_mouse(x, y);
        }
            break;

        case EIOSCommand::HOLD_MOUSE:
        {
            std::int32_t x = stream.read<std::int32_t>();
            std::int32_t y = stream.read<std::int32_t>();
            std::int32_t button = stream.read<std::int32_t>();
            io_controller->hold_mouse(x, y, button);
        }
            break;

        case EIOSCommand::RELEASE_MOUSE:
        {
            std::int32_t x = stream.read<std::int32_t>();
            std::int32_t y = stream.read<std::int32_t>();
            std::int32_t button = stream.read<std::int32_t>();
            io_controller->release_mouse(x, y, button);
        }
            break;

        case EIOSCommand::SCROLL_MOUSE:
        {
            std::int32_t x = stream.read<std::int32_t>();
            std::int32_t y = stream.read<std::int32_t>();
            std::int32_t lines = stream.read<std::int32_t>();
            io_controller->scroll_mouse(x, y, lines);
        }
            break;

        case EIOSCommand::IS_MOUSE_HELD:
        {
            std::int32_t button = stream.read<std::int32_t>();
            bool result = io_controller->is_mouse_held(button);
            stream.write(result);
        }
            break;

        case EIOSCommand::SEND_STRING:
        {
            std::string string = stream.read<std::string>();
            std::int32_t keywait = stream.read<std::int32_t>();
            std::int32_t keymodwait = stream.read<std::int32_t>();
            io_controller->send_string(string, keywait, keymodwait);
        }
            break;

        case EIOSCommand::HOLD_KEY:
        {
            std::int32_t keycode = stream.read<std::int32_t>();
            io_controller->hold_key(keycode);
        }
            break;

        case EIOSCommand::RELEASE_KEY:
        {
            std::int32_t keycode = stream.read<std::int32_t>();
            io_controller->release_key(keycode);
        }
            break;

        case EIOSCommand::IS_KEY_HELD:
        {
            std::int32_t keycode = stream.read<std::int32_t>();
            bool result = io_controller->is_key_held(keycode);
            stream.write(result);
        }
            break;

        case EIOSCommand::GET_KEYBOARD_SPEED:
        {
            std::int32_t speed = io_controller->get_keyboard_speed();
            stream.write(speed);
        }
            break;

        case EIOSCommand::SET_KEYBOARD_SPEED:
        {
            std::int32_t speed = stream.read<std::int32_t>();
            io_controller->set_keyboard_speed(speed);
        }
            break;

        case EIOSCommand::GET_KEYBOARD_REPEAT_DELAY:
        {
            std::int32_t delay = io_controller->get_keyboard_repeat_delay();
            stream.write(delay);
        }
            break;

        case EIOSCommand::SET_KEYBOARD_REPEAT_DELAY:
        {
            std::int32_t delay = stream.read<std::int32_t>();
            io_controller->set_keyboard_repeat_delay(delay);
        }
            break;
            
        case EIOSCommand::STOP_ALL_PROCESSING:
        {
            io_controller->stop_all_processing();
        }
            break;

        case EIOSCommand::GET_UI_SCALING:
        {
            fprintf(stderr, "WHY ARE YOU REMOTELY CALLING THIS FUNCTION?!\n");
            std::exit(0);
        }
            break;

        case EIOSCommand::SET_UI_SCALING:
        {
            fprintf(stderr, "WHY ARE YOU REMOTELY CALLING THIS FUNCTION?!\n");
            std::exit(0);
        }
            break;

        case EIOSCommand::REFLECT_OBJECT:
        {
            ReflectionHook hook;
            stream >> hook;

            jobject result = reflector->getField<jobject>(hook.object, hook);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_COMPARE_OBJECTS:
        {
            jobject first = stream.read<jobject>();
            jobject second = stream.read<jobject>();
            bool result = reflector->getVM()->IsSameObject(first, second);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_INSTANCE_OF:
        {
            bool result = false;
            jobject object = stream.read<jobject>();
            std::string className = stream.read<std::string>();

            jclass cls = reflector->LoadClass(className.c_str());
            if (!cls)
            {
                result = reflector->IsDecendentOf(object, className.c_str());
            }
            else
            {
                result = reflector->getEnv()->IsInstanceOf(object, cls);
            }

            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_RELEASE_OBJECT:
        {
            jobject object = stream.read<jobject>();
            if (object)
            {
                reflector->getEnv()->DeleteGlobalRef(object);
            }
        }
            break;

        case EIOSCommand::REFLECT_RELEASE_OBJECTS:
        {
            std::size_t size = stream.read<std::size_t>();
            for (std::size_t i = 0; i < size; ++i)
            {
                jobject object = stream.read<jobject>();
                if (object)
                {
                    reflector->getEnv()->DeleteGlobalRef(object);
                }
            }
        }
            break;

        case EIOSCommand::REFLECT_CHAR:
        {
            ReflectionHook hook;
            stream >> hook;

            jchar utf16_char = reflector->getPrimitive<jchar>(hook.object, hook);
            char result = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().to_bytes(utf16_char)[0];
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_BYTE:
        {
            ReflectionHook hook;
            stream >> hook;

            std::uint8_t result = reflector->getPrimitive<jbyte>(hook.object, hook);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_BOOLEAN:
        {
            ReflectionHook hook;
            stream >> hook;

            bool result = reflector->getPrimitive<jboolean>(hook.object, hook);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_SHORT:
        {
            ReflectionHook hook;
            stream >> hook;

            std::int16_t result = reflector->getPrimitive<jshort>(hook.object, hook);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_INT:
        {
            ReflectionHook hook;
            stream >> hook;

            std::int32_t result = reflector->getPrimitive<jint>(hook.object, hook);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_LONG:
        {
            ReflectionHook hook;
            stream >> hook;

            std::int64_t result = reflector->getPrimitive<jlong>(hook.object, hook);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_FLOAT:
        {
            ReflectionHook hook;
            stream >> hook;

            float result = reflector->getPrimitive<jfloat>(hook.object, hook);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_DOUBLE:
        {
            ReflectionHook hook;
            stream >> hook;

            double result = reflector->getPrimitive<jdouble>(hook.object, hook);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_STRING:
        {
            ReflectionHook hook;
            stream >> hook;
            stream.write(reflector->getField<std::string>(hook.object, hook));
        }
            break;

        case EIOSCommand::REFLECT_ARRAY:
        {
            ReflectionHook hook;
            stream >> hook;

            jarray result = reflector->getField<jarray>(hook.object, hook);
            stream.write(result);
        }
            break;

        case EIOSCommand::REFLECT_ARRAY_SIZE:
        {
            jobjectArray array = stream.read<jobjectArray>();
            std::size_t length = reflector->getEnv()->GetArrayLength(array);
            stream.write(length);
        }
            break;

        case EIOSCommand::REFLECT_ARRAY_WITH_SIZE:
        {
            ReflectionHook hook;
            stream >> hook;

            jarray array = reflector->getField<jarray>(hook.object, hook);
            if (array)
            {
                std::size_t length = reflector->getEnv()->GetArrayLength(array);
                stream.write(length);
                stream.write(array);
            }
            else
            {
                stream.write(0);
                stream.write(nullptr);
            }
        }
            break;

        case EIOSCommand::REFLECT_ARRAY_ALL:
        {
            ReflectionHook hook;
            stream >> hook;
            jarray array = reflector->getField<jarray>(hook.object, hook);

            ReflectionType type = stream.read<ReflectionType>();
            std::size_t dimensions = stream.read<std::size_t>();
            process_reflect_array_all(stream, array, type, dimensions);
            reflector->getEnv()->DeleteGlobalRef(array);
        }
            break;

        case EIOSCommand::REFLECT_ARRAY_WITH_INDEX_SIZE:
        {
            jarray array = stream.read<jarray>();
            ReflectionType type = stream.read<ReflectionType>();
            std::size_t length = stream.read<std::size_t>();
            std::size_t dimensions = stream.read<std::size_t>();
            process_reflect_array_index_length(stream, array, type, length, dimensions);
        }
            break;

        case EIOSCommand::REFLECT_ARRAY_INDICES:
        {
            jarray array = stream.read<jarray>();
            process_reflect_array_indices(stream, array);
        }
            break;

        case EIOSCommand::REMOTE_VM_INSTRUCTION:
        {
            this->remote_vm->process_command(&image_data);
        }
            break;
    }
}

void ControlCenter::send_array_response_index_length(Stream &stream, jarray array, ReflectionType type, std::size_t index, std::size_t length) const noexcept
{
    if (!array)
    {
        stream << nullptr;
        return;
    }

    if (length == 0)
    {
        //length = reflector->getEnv()->GetArrayLength(array);
        stream << nullptr;
        return;
    }

    switch (type)
    {
        case ReflectionType::CHAR:
        {
            JNIEnv* env = reflector->getEnv();
            char16_t* result = static_cast<char16_t*>(env->GetPrimitiveArrayCritical(array, nullptr));
            std::string utf8_result = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().to_bytes(result);
            env->ReleasePrimitiveArrayCritical(array, result, JNI_ABORT);
            stream.write(utf8_result.c_str() + (index * sizeof(char)), length * sizeof(char));
        }
            break;

        case ReflectionType::BYTE:
        case ReflectionType::BOOL:
        case ReflectionType::SHORT:
        case ReflectionType::INT:
        case ReflectionType::LONG:
        case ReflectionType::FLOAT:
        case ReflectionType::DOUBLE:
        {
            JNIEnv* env = reflector->getEnv();
            std::size_t element_size = reflect_size_for_type(type);
            char* result = static_cast<char*>(env->GetPrimitiveArrayCritical(array, nullptr));
            stream.write(result + (index * element_size), length * element_size);
            env->ReleasePrimitiveArrayCritical(array, result, JNI_ABORT);
        }
            break;

        case ReflectionType::STRING:
        {
            JNIEnv* env = reflector->getEnv();
            for (std::size_t i = 0; i < length; ++i)
            {
                jobject element = env->GetObjectArrayElement(static_cast<jobjectArray>(array), index + i);
                stream.write(reflector->getField<std::string>(static_cast<jstring>(element)));
                env->DeleteLocalRef(element);
            }
        }
            break;

        case ReflectionType::OBJECT:
        {
            JNIEnv* env = reflector->getEnv();
            for (std::size_t i = 0; i < length; ++i)
            {
                jobject result = env->GetObjectArrayElement(static_cast<jobjectArray>(array), index + i);
                stream.write(result ? env->NewGlobalRef(result) : nullptr);
                env->DeleteLocalRef(result);
            }
        }
            break;
    }
}

void ControlCenter::send_array_response_indices(Stream &stream, jarray array, ReflectionType type, std::int32_t* indices, std::size_t length) const noexcept
{
    if (!array)
    {
        stream << nullptr;
        return;
    }

    if (length == 0)
    {
        //length = reflector->getEnv()->GetArrayLength(array);
        stream << nullptr;
        return;
    }

    switch (type)
    {
        case ReflectionType::CHAR:
        {
            JNIEnv* env = reflector->getEnv();
            char16_t* result = static_cast<char16_t*>(env->GetPrimitiveArrayCritical(array, nullptr));
            std::string utf8_result = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().to_bytes(result);
            env->ReleasePrimitiveArrayCritical(array, result, JNI_ABORT);

            for (std::size_t i = 0; i < length; ++i)
            {
                stream.write(utf8_result.c_str() + (indices[i] * sizeof(char)), sizeof(char));
            }
        }
            break;

        case ReflectionType::BYTE:
        case ReflectionType::BOOL:
        case ReflectionType::SHORT:
        case ReflectionType::INT:
        case ReflectionType::LONG:
        case ReflectionType::FLOAT:
        case ReflectionType::DOUBLE:
        {
            JNIEnv* env = reflector->getEnv();
            std::size_t element_size = reflect_size_for_type(type);

            char* result = static_cast<char*>(env->GetPrimitiveArrayCritical(array, nullptr));
            for (std::size_t i = 0; i < length; ++i)
            {
                stream.write(result + (indices[i] * element_size), element_size);
            }
            env->ReleasePrimitiveArrayCritical(array, result, JNI_ABORT);
        }
            break;

        case ReflectionType::STRING:
        {
            for (std::size_t i = 0; i < length; ++i)
            {
                jobject element = reflector->getEnv()->GetObjectArrayElement(static_cast<jobjectArray>(array), indices[i]);
                stream.write(reflector->getField<std::string>(static_cast<jstring>(element)));
                reflector->getEnv()->DeleteLocalRef(element);
            }
        }
            break;

        case ReflectionType::OBJECT:
        {
            for (std::size_t i = 0; i < length; ++i)
            {
                jobject result = reflector->getEnv()->GetObjectArrayElement(static_cast<jobjectArray>(array), indices[i]);
                stream.write(result ? reflector->getEnv()->NewGlobalRef(result) : nullptr);
                reflector->getEnv()->DeleteLocalRef(result);
            }
        }
            break;
    }
}

void ControlCenter::process_reflect_array_indices(Stream &stream, jarray array) const noexcept
{
    ReflectionType type = stream.read<ReflectionType>();
    std::vector<std::int32_t> indices = stream.read<std::vector<std::int32_t>>();
    stream.write(indices.size());
    send_array_response_indices(stream, array, type, &indices[0], indices.size());
}

void ControlCenter::process_reflect_array_index_length(Stream &stream, jarray array, ReflectionType type, std::size_t length, std::size_t dimensions) const noexcept
{
    if (!array)
    {
        stream.write(0);
        stream << nullptr;
        return;
    }

    if (dimensions == 1)
    {
        std::size_t index = stream.read<jsize>();
        stream.write(length);
        send_array_response_index_length(stream, array, type, index, length);
        return;
    }

    JNIEnv* env = reflector->getEnv();
    array = static_cast<jarray>(env->NewLocalRef(array));

    for (std::size_t i = 0; i < dimensions - 1; ++i)
    {
        std::size_t index = stream.read<std::size_t>();

        if (array)
        {
            jint length = env->GetArrayLength(array);
            if (index >= length)
            {
                fprintf(stderr, "Index out of bounds\n");
                stream.write(0);
                stream << nullptr;
                return;
            }

            env->DeleteLocalRef(std::exchange(array, static_cast<jarray>(env->GetObjectArrayElement(static_cast<jobjectArray>(array), index))));
        }
    }

    if (!array)
    {
        stream.write(0);
        stream << nullptr;
        return;
    }

    std::size_t real_length = env->GetArrayLength(array);
    std::size_t ideal_length = std::min(length, real_length);

    std::size_t index = stream.read<std::size_t>();
    stream.write(ideal_length);
    send_array_response_index_length(stream, array, type, index, ideal_length);
    env->DeleteLocalRef(array);
}

void ControlCenter::process_reflect_array_all(Stream &stream, jarray array, ReflectionType type, std::size_t dimensions) const noexcept
{
    auto write_array = [](const ControlCenter* this_, JNIEnv* env, Stream &stream, jarray array, ReflectionType type, std::size_t dimensions) {
        auto write_impl = [](const ControlCenter* this_, JNIEnv* env, Stream &stream, jarray array, ReflectionType type, std::size_t dimensions, auto& self) mutable {
            if (!array)
            {
                stream.write(0);
                stream << nullptr;
                return;
            }

            if (dimensions == 1)
            {
                std::size_t length = env->GetArrayLength(array);
                stream.write(length);
                this_->send_array_response_index_length(stream, array, type, 0, length);
                return;
            }

            std::size_t length = env->GetArrayLength(array);
            stream.write(length);

            for (std::size_t i = 0; i < dimensions; ++i)
            {
                self(this_, env, stream, array, type, dimensions - 1, self);
            }
        };

        return write_impl(this_, env, stream, array, type, dimensions, write_impl);
    };

    JNIEnv* env = reflector->getEnv();
    write_array(this, env, stream, array, type, dimensions);
    env->DeleteLocalRef(array);
}

bool ControlCenter::init_maps() noexcept
{
    char mapName[256] = {0};
    #if defined(_WIN32) || defined(_WIN64)
    snprintf(mapName, sizeof(mapName) - 1, "Local\\RemoteInput_%d", pid);
    #else
    snprintf(mapName, sizeof(mapName) - 1, "RemoteInput_%d", pid);
    #endif

    int width = 0;
    int height = 0;
    GetDesktopResolution(width, height); //TODO: Change to Target Window size..

    std::size_t image_size = width * height * 4 * sizeof(std::uint8_t);
    std::size_t debug_size = width * height * 4 * sizeof(std::uint8_t);
    std::size_t extra_size = (1024 * sizeof(std::int32_t));
    std::int32_t map_size = sizeof(EIOSData) + image_size + debug_size + extra_size;

    if (is_controller)
    {
        //Open only..
        memory_map = std::make_unique<MemoryMapStream>(mapName, map_size, MemoryMapStream::open_mode::read | MemoryMapStream::open_mode::write);
        return memory_map && memory_map->is_mapped();
    }

    //Create only..
    memory_map = std::make_unique<MemoryMapStream>(mapName, map_size, MemoryMapStream::open_mode::read | MemoryMapStream::open_mode::write | MemoryMapStream::open_mode::create);
    bool result = memory_map && memory_map->is_mapped();
    if (result)
    {
        update_dimensions(width, height);
    }
    return result;
}


bool ControlCenter::init_locks() noexcept
{
    #if defined(_WIN32) || defined(_WIN64)
    char lockName[256] = {0};
    snprintf(lockName, sizeof(lockName) - 1, "Local\\RemoteInput_Lock_%d", pid);
    #elif defined(__linux__)
    char lockName[256] = {0};
    snprintf(lockName, sizeof(lockName) - 1, "/RemoteInput_Lock_%d", pid);
    #else
    char lockName[31] = {0};  //PSHMNAMELEN from <sys/posix_shm.h>
    snprintf(lockName, sizeof(lockName) - 1, "/RemoteInput_Lock_%d", pid);
    #endif

    map_lock = std::make_unique<Mutex>(lockName);
    return map_lock != nullptr;
}

bool ControlCenter::init_wait() noexcept
{
    #if defined(_WIN32) || defined(_WIN64)
    char signalName[256] = {0};
    snprintf(signalName, sizeof(signalName) - 1, "Local\\RemoteInput_ControlCenter_SyncSignal_%d", pid);
    #elif defined(__linux__)
    char signalName[256] = {0};
    snprintf(signalName, sizeof(signalName) - 1, "/RemoteInput_ControlCenter_SyncSignal_%d", pid);
    #else
    char signalName[31] = {0};  //PSHMNAMELEN from <sys/posix_shm.h>
    snprintf(signalName, sizeof(signalName) - 1, "/RI_CC_SyncSignal_%d", pid);
    #endif

    sync_signal = std::make_unique<AtomicSignal>(signalName);
    return sync_signal != nullptr;
}

bool ControlCenter::init_signals() noexcept
{
    #if defined(_WIN32) || defined(_WIN64)
    char readName[256] = {0};
    char writeName[256] = {0};
    snprintf(readName, sizeof(readName) - 1, "Local\\RemoteInput_ControlCenter_EventRead_%d", pid);
    snprintf(writeName, sizeof(writeName) - 1, "Local\\RemoteInput_ControlCenter_EventWrite_%d", pid);
    #elif defined(__linux__)
    char readName[256] = {0};
    char writeName[256] = {0};
    snprintf(readName, sizeof(readName) - 1, "/RemoteInput_ControlCenter_EventRead_%d", pid);
    snprintf(writeName, sizeof(writeName) - 1, "/RemoteInput_ControlCenter_EventWrite_%d", pid);
    #else
    char readName[31] = {0};  //PSHMNAMELEN from <sys/posix_shm.h>
    char writeName[31] = {0};
    snprintf(readName, sizeof(readName) - 1, "/RI_CC_EventRead_%d", pid);
    snprintf(writeName, sizeof(writeName) - 1, "/RI_CC_EventWrite_%d", pid);
    #endif

    command_signal = std::make_unique<Signal>(readName);
    response_signal = std::make_unique<Signal>(writeName);
    return command_signal && response_signal;
}

void ControlCenter::kill_zombie_process(std::int32_t pid) noexcept
{
    #if defined(_WIN32) || defined(_WIN64)

    #elif defined(__linux__)
    char buffer[256] = {0};

    //Kill Memory Maps
    snprintf(buffer, sizeof(buffer) - 1, "RemoteInput_%d", pid);
    shm_unlink(buffer);

    //Kill Locks
    snprintf(buffer, sizeof(buffer) - 1, "/RemoteInput_Lock_%d", pid);
    shm_unlink(buffer);

    //Kill Signals
    snprintf(buffer, sizeof(buffer) - 1, "/RemoteInput_ControlCenter_EventRead_%d", pid);
    sem_unlink(buffer);

    snprintf(buffer, sizeof(buffer) - 1, "/RemoteInput_ControlCenter_EventWrite_%d", pid);
    sem_unlink(buffer);
    #else
    char buffer[256] = {0};

    //Kill Memory Maps
    snprintf(buffer, sizeof(buffer) - 1, "RemoteInput_%d", pid);
    shm_unlink(buffer);

    //Kill Locks
    snprintf(buffer, sizeof(buffer) - 1, "/RemoteInput_Lock_%d", pid);
    shm_unlink(buffer);

    //Kill Signals
    snprintf(buffer, sizeof(buffer) - 1, "/RI_CC_EventRead_%d", pid);
    sem_unlink(buffer);

    snprintf(buffer, sizeof(buffer) - 1, "/RI_CC_EventWrite_%d", pid);
    sem_unlink(buffer);
    #endif
}

bool ControlCenter::hasReflector() const noexcept
{
    return reflector != nullptr;
}

ImageData* ControlCenter::get_image_data() const noexcept
{
    return memory_map && memory_map->is_mapped() ? &memory_map->image_data() : nullptr;
}

std::int32_t ControlCenter::parent_process_id() const noexcept
{
    return memory_map && memory_map->is_mapped() ? get_image_data()->parent_process_id() : -1;
}

std::int32_t ControlCenter::parent_thread_id() const noexcept
{
    return memory_map && memory_map->is_mapped() ? get_image_data()->parent_thread_id() : -1;
}

std::int32_t ControlCenter::get_width() const noexcept
{
    return memory_map && memory_map->is_mapped() ? get_image_data()->width() : -1;
}

std::int32_t ControlCenter::get_height() const noexcept
{
    return memory_map && memory_map->is_mapped() ? get_image_data()->height() : -1;
}

std::uint8_t* ControlCenter::get_image() const noexcept
{
    ImageData* image_data = get_image_data();
    return image_data ? image_data->image_buffer() : nullptr;
}

std::uint8_t* ControlCenter::get_debug_image() const noexcept
{
    std::uint8_t* image = get_image();
    return image ? image + (get_width() * get_height() * 4) : nullptr;
}

bool ControlCenter::get_debug_graphics() const noexcept
{
    if (memory_map && memory_map->is_mapped())
    {
        return get_image_data()->debug_graphics();
    }
    return false;
}

void ControlCenter::set_debug_graphics(bool enabled) const noexcept
{
    if (memory_map && memory_map->is_mapped())
    {
        get_image_data()->set_debug_graphics(enabled);
    }
}

bool ControlCenter::get_graphics_scaling() const noexcept
{
    return memory_map && memory_map->is_mapped() ? get_image_data()->ui_scaling() : false;
}

void ControlCenter::set_graphics_scaling(bool enabled) const noexcept
{
    if (memory_map && memory_map->is_mapped())
    {
        get_image_data()->set_ui_scaling(enabled);
    }
}

void ControlCenter::set_parent_process_id(std::int32_t pid) const noexcept
{
    if (memory_map && memory_map->is_mapped())
    {
        get_image_data()->set_parent_process_id(pid);
    }
}

void ControlCenter::set_parent_thread_id(std::int32_t tid) const noexcept
{
    if (memory_map && memory_map->is_mapped())
    {
        get_image_data()->set_parent_thread_id(tid);
    }
}

void ControlCenter::signal_sync(std::int32_t pid) noexcept
{
    #if defined(_WIN32) || defined(_WIN64)
    char signalName[256] = {0};
    snprintf(signalName, sizeof(signalName) - 1, "Local\\RemoteInput_ControlCenter_SyncSignal_%d", pid);
    #elif defined(__linux__)
    char signalName[256] = {0};
    snprintf(signalName, sizeof(signalName) - 1, "/RemoteInput_ControlCenter_SyncSignal_%d", pid);
    #else
    char signalName[31] = {0};  //PSHMNAMELEN from <sys/posix_shm.h>
    snprintf(signalName, sizeof(signalName) - 1, "/RI_CC_SyncSignal_%d", pid);
    #endif

    std::unique_ptr<AtomicSignal> sync_signal = std::make_unique<AtomicSignal>(signalName);

    if (!sync_signal->is_signalled())
    {
        sync_signal->signal();
    }
}

void ControlCenter::wait_for_sync(std::int32_t pid) noexcept
{
    #if defined(_WIN32) || defined(_WIN64)
    char signalName[256] = {0};
    snprintf(signalName, sizeof(signalName) - 1, "Local\\RemoteInput_ControlCenter_SyncSignal_%d", pid);
    #elif defined(__linux__)
    char signalName[256] = {0};
    snprintf(signalName, sizeof(signalName) - 1, "/RemoteInput_ControlCenter_SyncSignal_%d", pid);
    #else
    char signalName[31] = {0};  //PSHMNAMELEN from <sys/posix_shm.h>
    snprintf(signalName, sizeof(signalName) - 1, "/RI_CC_SyncSignal_%d", pid);
    #endif

    std::unique_ptr<AtomicSignal> sync_signal = std::make_unique<AtomicSignal>(signalName);

    if (!sync_signal->is_signalled())
    {
        //We shall wait no longer than 5 seconds for initialization of sync signals.
        sync_signal->timed_wait(5000);
    }
}

bool ControlCenter::controller_exists(std::int32_t pid) noexcept
{
    if (pid == getpid())
    {
        return false;
    }

    char mapName[256] = {0};
    #if defined(_WIN32) || defined(_WIN64)
    snprintf(mapName, sizeof(mapName) - 1, "Local\\RemoteInput_%d", pid);
    #else
    snprintf(mapName, sizeof(mapName) - 1, "RemoteInput_%d", pid);
    #endif

    return MemoryMapStream(mapName, 0, MemoryMapStream::open_mode::read).is_mapped();
}

bool ControlCenter::controller_is_paired(std::int32_t pid, bool* exists) noexcept
{
    if (pid == getpid())
    {
        *exists = false;
        return true;
    }

    char mapName[256] = {0};
    #if defined(_WIN32) || defined(_WIN64)
    snprintf(mapName, sizeof(mapName) - 1, "Local\\RemoteInput_%d", pid);
    #else
    snprintf(mapName, sizeof(mapName) - 1, "RemoteInput_%d", pid);
    #endif

    *exists = false;
    auto memory_map = MemoryMapStream(mapName, sizeof(EIOSData), MemoryMapStream::open_mode::read | MemoryMapStream::open_mode::write);
    if (memory_map.is_mapped())
    {
        *exists = true;
        ImageData &data = memory_map.image_data();

        if (data.parent_process_id() != -1 && !IsProcessAlive(data.parent_process_id()))
        {
            data.set_parent_process_id(-1);
            data.set_parent_thread_id(-1);
        }

        if (data.parent_thread_id() != -1 && !IsThreadAlive(data.parent_thread_id()))
        {
            data.set_parent_thread_id(-1);
        }

        return data.parent_thread_id() != -1 && data.parent_thread_id() != GetCurrentThreadID();
    }

    return false;
}

void ControlCenter::kill_process(std::int32_t pid) const noexcept
{
    send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::KILL_APPLICATION);
    });
}

void ControlCenter::update_dimensions(std::int32_t width, std::int32_t height) const noexcept
{
    if (memory_map && memory_map->is_mapped())
    {
        ImageData* image_data = get_image_data();
        if (image_data->ui_scaling())
        {
            std::int32_t x = 0;
            std::int32_t y = 0;
            std::size_t w = 0;
            std::size_t h = 0;

            get_applet_dimensions(&x, &y, &w, &h);

            if (w > 0 && h > 0)
            {
                image_data->set_width(w);
                image_data->set_height(h);
            }
            else
            {
                image_data->set_width(width);
                image_data->set_height(height);
            }
        }
        else
        {
            image_data->set_width(width);
            image_data->set_height(height);
        }
    }
}

void ControlCenter::get_target_dimensions(std::int32_t* width, std::int32_t* height) const noexcept
{
    if (memory_map && memory_map->is_mapped())
    {
        *width = get_width();
        *height = get_height();
    }
    else
    {
        *width = -1;
        *height = -1;
    }
}

bool ControlCenter::has_focus() const noexcept
{
    bool result = send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::HAS_FOCUS);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<bool>();
    }
    return false;
}

void ControlCenter::gain_focus() const noexcept
{
    send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::GAIN_FOCUS);
    });
}

void ControlCenter::lose_focus() const noexcept
{
    send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::LOSE_FOCUS);
    });
}

bool ControlCenter::is_keyboard_input_enabled() const noexcept
{
    bool result = send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::IS_KEYBOARD_INPUT_ENABLED);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<bool>();
    }

    return false;
}

void ControlCenter::set_keyboard_input_enabled(bool enabled) const noexcept
{
    send_command([enabled](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::SET_KEYBOARD_INPUT_ENABLED);
        stream.write<bool>(enabled);
    });
}

bool ControlCenter::is_mouse_input_enabled() const noexcept
{
    bool result = send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::IS_MOUSE_INPUT_ENABLED);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<bool>();
    }

    return false;
}

void ControlCenter::set_mouse_input_enabled(bool enabled) const noexcept
{
    send_command([enabled](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::SET_MOUSE_INPUT_ENABLED);
        stream.write<bool>(enabled);
    });
}

void ControlCenter::get_mouse_position(std::int32_t* x, std::int32_t* y) const noexcept
{
    bool result = send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::GET_MOUSE);
    });

    if (result)
    {
        Stream& stream = get_image_data()->data_stream();
        *x = stream.read<std::int32_t>();
        *y = stream.read<std::int32_t>();
    }
}

void ControlCenter::get_real_mouse_position(std::int32_t* x, std::int32_t* y) const noexcept
{
    bool result = send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::GET_REAL_MOUSE);
    });

    if (result)
    {
        Stream& stream = get_image_data()->data_stream();
        *x = stream.read<std::int32_t>();
        *y = stream.read<std::int32_t>();
    }
}

void ControlCenter::move_mouse(std::int32_t x, std::int32_t y) const noexcept
{
    send_command([x, y](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::MOVE_MOUSE);
        stream.write<std::int32_t>(x);
        stream.write<std::int32_t>(y);
    });
}

void ControlCenter::hold_mouse(std::int32_t x, std::int32_t y, std::int32_t button) const noexcept
{
    send_command([x, y, button](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::HOLD_MOUSE);
        stream.write<std::int32_t>(x);
        stream.write<std::int32_t>(y);
        stream.write<std::int32_t>(button);
    });
}

void ControlCenter::release_mouse(std::int32_t x, std::int32_t y, std::int32_t button) const noexcept
{
    send_command([x, y, button](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::RELEASE_MOUSE);
        stream.write<std::int32_t>(x);
        stream.write<std::int32_t>(y);
        stream.write<std::int32_t>(button);
    });
}

void ControlCenter::scroll_mouse(std::int32_t x, std::int32_t y, std::int32_t lines) const noexcept
{
    send_command([x, y, lines](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::SCROLL_MOUSE);
        stream.write<std::int32_t>(x);
        stream.write<std::int32_t>(y);
        stream.write<std::int32_t>(lines);
    });
}

bool ControlCenter::is_mouse_held(std::int32_t button) const noexcept
{
    bool result = send_command([button](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::IS_MOUSE_HELD);
        stream.write<std::int32_t>(button);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<bool>();
    }
    return false;
}

void ControlCenter::send_string(const char* string, std::int32_t keywait, std::int32_t keymodwait) const noexcept
{
    send_command([string, keywait, keymodwait](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::SEND_STRING);
        image_data->data_stream() << string;
        stream.write<std::int32_t>(keywait);
        stream.write<std::int32_t>(keymodwait);
    });
}

void ControlCenter::hold_key(std::int32_t key) const noexcept
{
    send_command([key](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::HOLD_KEY);
        stream.write<std::int32_t>(key);
    });
}

void ControlCenter::release_key(std::int32_t key) const noexcept
{
    send_command([key](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::RELEASE_KEY);
        stream.write<std::int32_t>(key);
    });
}

bool ControlCenter::is_key_held(std::int32_t key) const noexcept
{
    bool result = send_command([key](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::IS_KEY_HELD);
        stream.write<std::int32_t>(key);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<bool>();
    }
    return false;
}

std::int32_t ControlCenter::get_keyboard_speed() const noexcept
{
    bool result = send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::GET_KEYBOARD_SPEED);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<std::int32_t>();
    }
    return -1;
}

void ControlCenter::set_keyboard_speed(std::int32_t speed) const noexcept
{
    send_command([speed](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::SET_KEYBOARD_SPEED);
        stream.write<std::int32_t>(speed);
    });
}

std::int32_t ControlCenter::get_keyboard_repeat_delay() const noexcept
{
    bool result = send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::GET_KEYBOARD_REPEAT_DELAY);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<std::int32_t>();
    }
    return -1;
}

void ControlCenter::set_keyboard_repeat_delay(std::int32_t delay) const noexcept
{
    send_command([delay](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::SET_KEYBOARD_REPEAT_DELAY);
        stream.write<std::int32_t>(delay);
    });
}

void ControlCenter::stop_all_processing() const noexcept
{
    send_command([](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::STOP_ALL_PROCESSING);
    });
}

bool ControlCenter::reflect_is_objects_equal(const jobject first, const jobject second) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_COMPARE_OBJECTS);
        stream.write<jobject>(first);
        stream.write<jobject>(second);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<bool>();
    }
    return false;
}

bool ControlCenter::reflect_instance_of(const jobject object, std::string cls) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_INSTANCE_OF);
        stream.write<jobject>(object);
        stream.write(cls);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<bool>();
    }
    return false;
}

jobject ControlCenter::reflect_object(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_OBJECT);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<jobject>();
    }
    return nullptr;
}

void ControlCenter::reflect_release_object(const jobject object) const noexcept
{
    send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_RELEASE_OBJECT);
        stream.write<jobject>(object);
    });
}

void ControlCenter::reflect_release_objects(const jobject* array, std::size_t length) const noexcept
{
    send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_RELEASE_OBJECTS);
        stream.write<std::size_t>(length);
        stream.write(array, length * sizeof(jobject));
    });
}

char ControlCenter::reflect_char(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_CHAR);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<char>();
    }
    return '\0';
}

std::uint8_t ControlCenter::reflect_byte(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_BYTE);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<std::uint8_t>();
    }
    return 0;
}

bool ControlCenter::reflect_boolean(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_BOOLEAN);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<bool>();
    }
    return false;
}

std::int16_t ControlCenter::reflect_short(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_SHORT);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<std::int16_t>();
    }
    return -1;
}

std::int32_t ControlCenter::reflect_int(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_INT);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<std::int32_t>();
    }
    return -1;
}

std::int64_t ControlCenter::reflect_long(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_LONG);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<std::int64_t>();
    }
    return -1;
}

float ControlCenter::reflect_float(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_FLOAT);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<float>();
    }
    return -1;
}

double ControlCenter::reflect_double(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_DOUBLE);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<double>();
    }
    return -1;
}

std::string ControlCenter::reflect_string(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_STRING);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<std::string>();
    }
    return "";
}

jarray ControlCenter::reflect_array(const ReflectionHook &hook) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_ARRAY);
        stream << hook;
    });

    if (result)
    {
        return get_image_data()->data_stream().read<jarray>();
    }
    return nullptr;
}

jarray ControlCenter::reflect_array(const ReflectionHook &hook, std::size_t* output_size) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_ARRAY_WITH_SIZE);
        stream << hook;
    });

    if (result)
    {
        Stream& stream = get_image_data()->data_stream();
        *output_size = stream.read<std::size_t>();
        return stream.read<jarray>();
    }
    return nullptr;
}

std::size_t ControlCenter::reflect_array_size(const jarray array) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_ARRAY_SIZE);
        stream.write(array);
    });

    if (result)
    {
        return get_image_data()->data_stream().read<std::size_t>();
    }
    return 0;
}

ImageData* ControlCenter::reflect_array_indices(const jarray array, ReflectionType type, std::int32_t* indices, std::size_t length) const noexcept
{
    bool result = send_command([&](Stream &stream, ImageData* image_data) {
        image_data->set_command(EIOSCommand::REFLECT_ARRAY_INDICES);
        stream.write(array);
        stream.write(type);
        stream.write(length);
        stream.write(indices, length * sizeof(std::int32_t));
    });

    if (result)
    {
        return get_image_data();
    }
    return nullptr;
}

std::size_t ControlCenter::reflect_size_for_type(ReflectionType type) noexcept
{
    static_assert(sizeof(jchar) == sizeof(char16_t) &&
                  sizeof(jboolean) == sizeof(bool) &&
                  sizeof(jshort) == sizeof(short) &&
                  sizeof(jint) == sizeof(std::int32_t) &&
                  sizeof(jlong) == sizeof(std::int64_t) &&
                  sizeof(jfloat) == sizeof(float) &&
                  sizeof(jdouble) == sizeof(double) &&
                  sizeof(jstring) == sizeof(void*) &&
                  sizeof(jobject) == sizeof(void*),
                  "Size of primitive types must be equal");

    static std::size_t mapping[] = {
        sizeof(jchar),
        sizeof(jbyte),
        sizeof(jboolean),
        sizeof(jshort),
        sizeof(jint),
        sizeof(jlong),
        sizeof(jfloat),
        sizeof(jdouble),
        sizeof(jstring),
        sizeof(jobject)
    };

    return mapping[static_cast<std::underlying_type<ReflectionType>::type>(type)];
}

java::Applet ControlCenter::reflect_applet() const noexcept
{
    return {reflector->getEnv(), reflector->getApplet(), false};
}

java::Component ControlCenter::reflect_canvas() const noexcept
{
    java::Applet applet{reflector->getEnv(), reflector->getApplet(), false};
    return applet.getComponent(0);
}

std::unique_ptr<RemoteVM> ControlCenter::create_remote_vm() noexcept
{
    return std::make_unique<RemoteVM>(nullptr,
                                      this,
                                      [this](std::function<void(Stream&, ImageData*)> &&fn) -> bool {
                                          return send_command(std::forward<std::function<void(Stream&, ImageData*)>>(fn));
                                      },
                                      &ControlCenter::get_image_data);
}

void ControlCenter::get_applet_dimensions(std::int32_t* x, std::int32_t* y, std::size_t* width, std::size_t* height) const noexcept
{
    if (io_controller)
    {
        io_controller->get_applet_dimensions(*x, *y, *width, *height);
    }
}

void ControlCenter::get_applet_mouse_position(std::int32_t* x, std::int32_t* y) const noexcept
{
    if (io_controller)
    {
        io_controller->get_applet_mouse_position(*x, *y);
    }
}
