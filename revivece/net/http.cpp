#include "http.h"

namespace
{
const int kReadBufferCapacity = 512;
const int kLineCapacity = 2048;
const int kHeaderCapacity = 8192;
const int kRequestCapacity = REVIVE_HTTP_HOST_CAPACITY + REVIVE_HTTP_PATH_CAPACITY + 256;

struct HttpReader
{
    ReviveTlsConnection* connection;
    char buffer[kReadBufferCapacity];
    int offset;
    int length;
};

void ClearBytes(void* value, unsigned int length)
{
    volatile unsigned char* cursor = static_cast<volatile unsigned char*>(value);
    while (length-- != 0)
        *cursor++ = 0;
}

bool StartsWith(const char* text, const char* prefix)
{
    if (text == NULL || prefix == NULL)
        return false;
    while (*prefix != '\0')
        if (*text++ != *prefix++)
            return false;
    return true;
}

bool EqualsIgnoreCase(const char* left, const char* right, int length)
{
    int index;
    if (left == NULL || right == NULL)
        return false;
    for (index = 0; index < length; ++index)
    {
        char a = left[index];
        char b = right[index];
        if (a == '\0')
            return false;
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return right[length] == '\0';
}

bool IsSpace(char value)
{
    return value == ' ' || value == '\t';
}

bool IsHostCharacter(char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '.' || value == '-';
}

bool AppendChar(char* destination, int capacity, int* length, char value)
{
    if (destination == NULL || length == NULL || *length >= capacity - 1)
        return false;
    destination[(*length)++] = value;
    destination[*length] = '\0';
    return true;
}

bool AppendText(char* destination, int capacity, int* length, const char* text)
{
    int index;
    if (text == NULL)
        return false;
    for (index = 0; text[index] != '\0'; ++index)
        if (!AppendChar(destination, capacity, length, text[index]))
            return false;
    return true;
}

bool AppendDecimal(char* destination, int capacity, int* length, unsigned long value)
{
    char reversed[12];
    int digits = 0;
    do
    {
        reversed[digits++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (digits > 0)
    {
        --digits;
        if (!AppendChar(destination, capacity, length, reversed[digits]))
            return false;
    }
    return true;
}

int StringLength(const char* text)
{
    int length = 0;
    if (text != NULL)
        while (text[length] != '\0')
            ++length;
    return length;
}

bool WriteAll(ReviveTlsConnection* connection, const char* data, int length)
{
    int written = 0;
    if (connection == NULL || data == NULL || length < 0)
        return false;
    while (written < length)
    {
        const int result = ReviveTLSWrite(connection, data + written, length - written);
        if (result <= 0)
            return false;
        written += result;
    }
    return true;
}

bool ReadByte(HttpReader* reader, char* value)
{
    int received;
    if (reader == NULL || value == NULL)
        return false;
    if (reader->offset == reader->length)
    {
        received = ReviveTLSRead(reader->connection, reader->buffer, kReadBufferCapacity);
        if (received <= 0)
            return false;
        reader->offset = 0;
        reader->length = received;
    }
    *value = reader->buffer[reader->offset++];
    return true;
}

bool ConsumeLine(HttpReader* reader)
{
    char value;
    do
    {
        if (!ReadByte(reader, &value))
            return false;
    } while (value != '\n');
    return true;
}

bool ReadLine(HttpReader* reader, char* line, int capacity, bool* tooLarge)
{
    int output = 0;
    char value;
    if (tooLarge != NULL)
        *tooLarge = false;
    if (reader == NULL || line == NULL || capacity < 2)
        return false;
    for (;;)
    {
        if (!ReadByte(reader, &value))
            return false;
        if (value == '\n')
        {
            if (output > 0 && line[output - 1] == '\r')
                --output;
            line[output] = '\0';
            return true;
        }
        if (output >= capacity - 1)
        {
            line[0] = '\0';
            if (tooLarge != NULL)
                *tooLarge = true;
            return ConsumeLine(reader);
        }
        line[output++] = value;
    }
}

bool ParseStatus(const char* line, int* status)
{
    if (line == NULL || status == NULL || !StartsWith(line, "HTTP/1.") ||
        (line[7] != '0' && line[7] != '1') || !IsSpace(line[8]) ||
        line[9] < '0' || line[9] > '9' ||
        line[10] < '0' || line[10] > '9' || line[11] < '0' || line[11] > '9')
        return false;
    *status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + (line[11] - '0');
    return true;
}

bool HeaderValue(const char* line, const char* name, const char** value)
{
    int length = 0;
    if (line == NULL || name == NULL || value == NULL)
        return false;
    while (name[length] != '\0')
        ++length;
    if (!EqualsIgnoreCase(line, name, length) || line[length] != ':')
        return false;
    *value = line + length + 1;
    while (IsSpace(**value))
        ++*value;
    return true;
}

bool ParseDecimal(const char* value, unsigned long* parsed)
{
    unsigned long result = 0;
    bool haveDigit = false;
    if (value == NULL || parsed == NULL)
        return false;
    while (*value >= '0' && *value <= '9')
    {
        const unsigned long digit = static_cast<unsigned long>(*value++ - '0');
        if (result > (0xffffffffUL - digit) / 10)
            return false;
        result = result * 10 + digit;
        haveDigit = true;
    }
    while (IsSpace(*value))
        ++value;
    if (!haveDigit || *value != '\0')
        return false;
    *parsed = result;
    return true;
}

bool ParseChunkLength(const char* line, unsigned long* parsed)
{
    unsigned long result = 0;
    bool haveDigit = false;
    if (line == NULL || parsed == NULL)
        return false;
    while (*line != '\0' && *line != ';' && !IsSpace(*line))
    {
        unsigned long digit;
        if (*line >= '0' && *line <= '9')
            digit = static_cast<unsigned long>(*line - '0');
        else if (*line >= 'a' && *line <= 'f')
            digit = static_cast<unsigned long>(*line - 'a' + 10);
        else if (*line >= 'A' && *line <= 'F')
            digit = static_cast<unsigned long>(*line - 'A' + 10);
        else
            return false;
        if (result > (0xffffffffUL - digit) / 16)
            return false;
        result = result * 16 + digit;
        haveDigit = true;
        ++line;
    }
    if (!haveDigit)
        return false;
    *parsed = result;
    return true;
}

ReviveHttpResult ReadBodyBytes(HttpReader* reader, unsigned long bytes,
                               char* response, int capacity, int* output,
                               bool* truncated)
{
    unsigned long index;
    char value;
    for (index = 0; index < bytes; ++index)
    {
        if (!ReadByte(reader, &value))
            return REVIVE_HTTP_IO_ERROR;
        if (*output >= capacity - 1)
        {
            response[*output] = '\0';
            *truncated = true;
            return REVIVE_HTTP_OK;
        }
        response[(*output)++] = value;
    }
    response[*output] = '\0';
    return REVIVE_HTTP_OK;
}

ReviveHttpResult ReadChunkedBody(HttpReader* reader, char* response, int capacity,
                                 int* output, bool* truncated)
{
    char line[kLineCapacity];
    bool tooLarge;
    for (;;)
    {
        unsigned long chunkLength;
        ReviveHttpResult result;
        if (!ReadLine(reader, line, sizeof(line), &tooLarge))
            return tooLarge ? REVIVE_HTTP_HEADER_TOO_LARGE : REVIVE_HTTP_IO_ERROR;
        if (tooLarge || !ParseChunkLength(line, &chunkLength))
            return REVIVE_HTTP_RESPONSE_ERROR;
        if (chunkLength == 0)
        {
            do
            {
                if (!ReadLine(reader, line, sizeof(line), &tooLarge))
                    return tooLarge ? REVIVE_HTTP_HEADER_TOO_LARGE : REVIVE_HTTP_IO_ERROR;
                if (tooLarge)
                    return REVIVE_HTTP_HEADER_TOO_LARGE;
            } while (line[0] != '\0');
            return REVIVE_HTTP_OK;
        }
        result = ReadBodyBytes(reader, chunkLength, response, capacity, output, truncated);
        if (result != REVIVE_HTTP_OK || *truncated)
            return result;
        if (!ReadLine(reader, line, sizeof(line), &tooLarge) || tooLarge || line[0] != '\0')
            return REVIVE_HTTP_RESPONSE_ERROR;
    }
}

ReviveHttpResult ReadCloseBody(HttpReader* reader, char* response, int capacity,
                               int* output, bool* truncated)
{
    char value;
    while (ReadByte(reader, &value))
    {
        if (*output >= capacity - 1)
        {
            response[*output] = '\0';
            *truncated = true;
            return REVIVE_HTTP_OK;
        }
        response[(*output)++] = value;
    }
    response[*output] = '\0';
    return REVIVE_HTTP_OK;
}
}

bool ReviveHttpParseUrl(const char* text, ReviveHttpUrl* url)
{
    const char* cursor;
    int hostLength = 0;
    int pathLength = 0;
    unsigned long port = 443;
    if (text == NULL || url == NULL || !StartsWith(text, "https://"))
        return false;
    ClearBytes(url, sizeof(*url));
    cursor = text + 8;
    while (*cursor != '\0' && *cursor != '/' && *cursor != '?' && *cursor != ':')
    {
        if (!IsHostCharacter(*cursor) || hostLength >= REVIVE_HTTP_HOST_CAPACITY - 1)
            return false;
        url->host[hostLength++] = *cursor++;
    }
    if (hostLength == 0 || url->host[hostLength - 1] == '.')
        return false;
    url->host[hostLength] = '\0';
    if (*cursor == ':')
    {
        bool haveDigit = false;
        port = 0;
        ++cursor;
        while (*cursor >= '0' && *cursor <= '9')
        {
            if (port > 6553)
                return false;
            port = port * 10 + static_cast<unsigned long>(*cursor++ - '0');
            haveDigit = true;
        }
        if (!haveDigit || port == 0 || port > 65535)
            return false;
    }
    if (*cursor == '\0')
        url->path[pathLength++] = '/';
    else
    {
        if (*cursor == '?')
            url->path[pathLength++] = '/';
        while (*cursor != '\0')
        {
            const unsigned char value = static_cast<unsigned char>(*cursor++);
            if (value <= 32 || value == 127 || value == '#')
                return false;
            if (pathLength >= REVIVE_HTTP_PATH_CAPACITY - 1)
                return false;
            url->path[pathLength++] = static_cast<char>(value);
        }
    }
    url->path[pathLength] = '\0';
    url->port = static_cast<unsigned short>(port);
    return true;
}

ReviveHttpResult ReviveHttpGet(ReviveTlsConnection* connection,
                               const ReviveHttpUrl* url,
                               char* response,
                               int responseCapacity,
                               int* httpStatus,
                               bool* truncated)
{
    HttpReader reader;
    char request[kRequestCapacity];
    char line[kLineCapacity];
    int requestLength = 0;
    int headerBytes = 0;
    int output = 0;
    bool tooLarge;
    bool chunked = false;
    bool contentLengthKnown = false;
    unsigned long contentLength = 0;
    if (connection == NULL || url == NULL || response == NULL || responseCapacity < 2 ||
        httpStatus == NULL || truncated == NULL || url->host[0] == '\0' || url->path[0] != '/')
        return REVIVE_HTTP_URL_ERROR;
    response[0] = '\0';
    *httpStatus = 0;
    *truncated = false;
    if (!AppendText(request, sizeof(request), &requestLength, "GET ") ||
        !AppendText(request, sizeof(request), &requestLength, url->path) ||
        !AppendText(request, sizeof(request), &requestLength, " HTTP/1.1\r\nHost: ") ||
        !AppendText(request, sizeof(request), &requestLength, url->host) ||
        (url->port != 443 && (!AppendChar(request, sizeof(request), &requestLength, ':') ||
                              !AppendDecimal(request, sizeof(request), &requestLength, url->port))) ||
        !AppendText(request, sizeof(request), &requestLength,
                    "\r\nUser-Agent: ReviveCE/1.0\r\nAccept: text/plain, text/html, "
                    "application/xml, application/rss+xml, application/atom+xml\r\n"
                    "Accept-Encoding: identity\r\nConnection: close\r\n\r\n"))
        return REVIVE_HTTP_URL_ERROR;
    if (!WriteAll(connection, request, requestLength))
        return REVIVE_HTTP_IO_ERROR;
    reader.connection = connection;
    reader.offset = 0;
    reader.length = 0;
    if (!ReadLine(&reader, line, sizeof(line), &tooLarge))
        return tooLarge ? REVIVE_HTTP_HEADER_TOO_LARGE : REVIVE_HTTP_IO_ERROR;
    if (tooLarge || !ParseStatus(line, httpStatus))
        return REVIVE_HTTP_RESPONSE_ERROR;
    for (;;)
    {
        const char* value;
        if (!ReadLine(&reader, line, sizeof(line), &tooLarge))
            return tooLarge ? REVIVE_HTTP_HEADER_TOO_LARGE : REVIVE_HTTP_IO_ERROR;
        if (tooLarge)
            return REVIVE_HTTP_HEADER_TOO_LARGE;
        headerBytes += StringLength(line) + 2;
        if (headerBytes > kHeaderCapacity)
            return REVIVE_HTTP_HEADER_TOO_LARGE;
        if (line[0] == '\0')
            break;
        if (HeaderValue(line, "Transfer-Encoding", &value) &&
            EqualsIgnoreCase(value, "chunked", 7))
            chunked = true;
        else if (HeaderValue(line, "Content-Length", &value))
        {
            if (!ParseDecimal(value, &contentLength))
                return REVIVE_HTTP_RESPONSE_ERROR;
            contentLengthKnown = true;
        }
        else if (HeaderValue(line, "Content-Encoding", &value) &&
                 !EqualsIgnoreCase(value, "identity", 8) && value[0] != '\0')
            return REVIVE_HTTP_ENCODING_ERROR;
    }
    if (*httpStatus < 200 || *httpStatus >= 300)
        return REVIVE_HTTP_STATUS_ERROR;
    if (chunked)
        return ReadChunkedBody(&reader, response, responseCapacity, &output, truncated);
    if (contentLengthKnown)
        return ReadBodyBytes(&reader, contentLength, response, responseCapacity, &output, truncated);
    return ReadCloseBody(&reader, response, responseCapacity, &output, truncated);
}
