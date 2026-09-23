#include "log.h"

#include <windows.h>

namespace
{
const wchar_t* const kLogDirectory = L"\\Application Data\\ReviveCE";
const wchar_t* const kLogFile = L"\\Application Data\\ReviveCE\\revive.log";

void AppendAscii(HANDLE file, const char* text)
{
    DWORD written = 0;
    DWORD length = 0;

    if (text == NULL)
        return;

    while (text[length] != '\0')
        ++length;

    if (length != 0)
        WriteFile(file, text, length, &written, NULL);
}

void FormatInteger(int value, char* buffer, int capacity)
{
    char reversed[16];
    unsigned int magnitude;
    int count = 0;
    int output = 0;

    if (buffer == NULL || capacity < 2)
        return;

    if (value < 0)
    {
        buffer[output++] = '-';
        magnitude = 0U - static_cast<unsigned int>(value);
    }
    else
    {
        magnitude = static_cast<unsigned int>(value);
    }

    do
    {
        reversed[count++] = static_cast<char>('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0 && count < static_cast<int>(sizeof(reversed)));

    while (count > 0 && output < capacity - 1)
        buffer[output++] = reversed[--count];
    buffer[output] = '\0';
}

void FormatUnsigned(unsigned int value, char* buffer, int capacity)
{
    char reversed[16];
    int count = 0;
    int output = 0;

    if (buffer == NULL || capacity < 2)
        return;

    do
    {
        reversed[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < static_cast<int>(sizeof(reversed)));

    while (count > 0 && output < capacity - 1)
        buffer[output++] = reversed[--count];
    buffer[output] = '\0';
}
}

void ReviveLog(const char* component, const char* message, int nativeError)
{
    SYSTEMTIME now;
    wchar_t widePrefix[64];
    char prefix[128];
    char errorText[32];
    HANDLE file;

    CreateDirectory(kLogDirectory, NULL);
    file = CreateFile(kLogFile, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;

    SetFilePointer(file, 0, NULL, FILE_END);
    GetLocalTime(&now);
    wsprintf(widePrefix, L"%04u-%02u-%02u %02u:%02u:%02u ",
             now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond);
    prefix[0] = '\0';
    if (WideCharToMultiByte(CP_UTF8, 0, widePrefix, -1,
                            prefix, sizeof(prefix), NULL, NULL) == 0)
    {
        CloseHandle(file);
        return;
    }

    AppendAscii(file, prefix);
    AppendAscii(file, "[");
    AppendAscii(file, component);
    AppendAscii(file, "] ");
    AppendAscii(file, message);
    if (nativeError != 0)
    {
        AppendAscii(file, " (error ");
        FormatInteger(nativeError, errorText, sizeof(errorText));
        AppendAscii(file, errorText);
        AppendAscii(file, ")");
    }
    AppendAscii(file, "\r\n");
    CloseHandle(file);
}

void ReviveLogEndpoint(const char* component, const char* message,
                       const char* host, unsigned short port)
{
    char endpoint[128];
    char portText[16];
    int output = 0;
    int index;

    if (message == NULL)
        message = "";
    if (host == NULL)
        host = "";
    FormatUnsigned(port, portText, sizeof(portText));
    for (index = 0; message[index] != '\0' && output < static_cast<int>(sizeof(endpoint)) - 1; ++index)
        endpoint[output++] = message[index];
    if (output < static_cast<int>(sizeof(endpoint)) - 1)
        endpoint[output++] = ' ';
    for (index = 0; host[index] != '\0' && output < static_cast<int>(sizeof(endpoint)) - 1; ++index)
        endpoint[output++] = host[index];
    if (output < static_cast<int>(sizeof(endpoint)) - 1)
        endpoint[output++] = ':';
    for (index = 0; portText[index] != '\0' && output < static_cast<int>(sizeof(endpoint)) - 1; ++index)
        endpoint[output++] = portText[index];
    endpoint[output] = '\0';

    ReviveLog(component, endpoint, 0);
}
