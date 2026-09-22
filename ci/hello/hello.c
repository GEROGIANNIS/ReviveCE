#include <windows.h>

/*
 * A CRT-free entry point keeps the first toolchain proof focused on the CE
 * compiler, linker, SDK headers, import library, and ARM code generation.
 */
void WINAPI WinMainCRTStartup(void)
{
    MessageBoxW(NULL,
                L"ARMV4I build works on this device.",
                L"ReviveCE HelloWorld",
                MB_OK | MB_ICONINFORMATION);
    ExitProcess(0);
}
