#include "Platform.hxx"
#include <string>
#include <chrono>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif // defined

#if defined(_WIN32) || defined(_WIN64)
void GetDesktopResolution(int &width, int &height)
{
    RECT desktop = {0};
    const HWND hDesktop = GetDesktopWindow();
    GetWindowRect(hDesktop, &desktop);
    width = desktop.right;
    height = desktop.bottom;
}
#endif // defined

#if defined(_WIN32) || defined(_WIN64)
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && lParam)
    {
        char className[MAX_PATH] = {0};
        int result = GetClassName(hwnd, className, sizeof(className));
        if (result != 0 && std::string(className) == "SunAwtFrame")
        {
            *reinterpret_cast<HWND*>(lParam) = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

Reflection* GetNativeReflector()
{
    auto TimeOut = [&](std::uint32_t time, std::function<bool()> &&run) -> bool {
		auto start = std::chrono::high_resolution_clock::now();
		while(!run())
		{
			if (elapsed_time<std::chrono::seconds>(start) >= time)
			{
				return false;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		return true;
	};

	auto IsValidFrame = [&](Reflection* reflection, jobject object) -> bool {
		return reflection->GetClassType(object) == "java.awt.Frame";
    };

	auto GetMainWindow = [&] {
		HWND windowFrame = nullptr;
		EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windowFrame));
		return windowFrame;
	};

	HMODULE awt = nullptr;
	if (TimeOut(20, [&]{ return (awt = GetModuleHandle("awt.dll")); }))
	{
		return nullptr;
	}

	if (!TimeOut(5, [&]{ return JVM().getVM() != nullptr; }))
	{
		return nullptr;
	}

    auto DSGetComponent = reinterpret_cast<jobject __stdcall (*)(JNIEnv*, void*)>(GetProcAddress(awt, "DSGetComponent"));
    if (!DSGetComponent)
    {
        DSGetComponent = reinterpret_cast<jobject __stdcall (*)(JNIEnv*, void*)>(GetProcAddress(awt, "_DSGetComponent@8"));
    }

    if (!DSGetComponent)
    {
        int ordinal = 146;
        char* procName = reinterpret_cast<char*>(static_cast<std::uint64_t>(static_cast<std::uint16_t>(ordinal)));
        DSGetComponent = reinterpret_cast<jobject __stdcall (*)(JNIEnv*, void*)>(GetProcAddress(awt, procName));
    }

    if (!DSGetComponent)
    {
        return nullptr;
    }

    Reflection* reflection = new Reflection();

    if (reflection->Attach())
    {
		auto hasReflection = TimeOut(20, [&]{
			HWND windowFrame = GetMainWindow();
			if (windowFrame)
			{
				jobject object = DSGetComponent(reflection->getEnv(), windowFrame);  //java.awt.Frame
				return IsValidFrame(reflection, object) && reflection->Initialize(object);
			}
			return false;
		});

		if (hasReflection)
		{
			reflection->Detach();
			return reflection;
		}

        reflection->Detach();
    }

    delete reflection;
    return nullptr;
}
#endif // defined
