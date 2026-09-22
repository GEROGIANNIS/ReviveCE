#include "imap.h"

#include "../common/log.h"

#include <string.h>

namespace
{
const int kReadBufferCapacity = 512;
const int kLineCapacity = 2048;
const int kRawMessageCapacity = 32768;

struct ImapReader
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
    {
        if (*text++ != *prefix++)
            return false;
    }
    return true;
}

bool Contains(const char* text, const char* token)
{
    if (text == NULL || token == NULL || *token == '\0')
        return false;
    for (; *text != '\0'; ++text)
        if (StartsWith(text, token))
            return true;
    return false;
}

const char* FindText(const char* text, const char* token)
{
    if (text == NULL || token == NULL || *token == '\0')
        return NULL;
    for (; *text != '\0'; ++text)
        if (StartsWith(text, token))
            return text;
    return NULL;
}

bool IsAsciiSpace(char value)
{
    return value == ' ' || value == '\t';
}

bool EqualsIgnoreCase(const char* left, const char* right, int length)
{
    int index;
    for (index = 0; index < length; ++index)
    {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return right[length] == '\0';
}

void CopyValue(char* destination, int capacity, const char* source)
{
    int output = 0;
    if (destination == NULL || capacity <= 0)
        return;
    destination[0] = '\0';
    if (source == NULL)
        return;

    while (IsAsciiSpace(*source))
        ++source;
    while (*source != '\0' && *source != '\r' && *source != '\n' &&
           output < capacity - 1)
        destination[output++] = *source++;
    while (output > 0 && IsAsciiSpace(destination[output - 1]))
        --output;
    destination[output] = '\0';
}

bool ReadByte(ImapReader* reader, char* value)
{
    int received;
    if (reader == NULL || value == NULL)
        return false;
    if (reader->offset == reader->length)
    {
        received = ReviveTLSRead(reader->connection, reader->buffer,
                                 kReadBufferCapacity);
        if (received <= 0)
            return false;
        reader->offset = 0;
        reader->length = received;
    }
    *value = reader->buffer[reader->offset++];
    return true;
}

bool ConsumeLine(ImapReader* reader)
{
    char value;
    do
    {
        if (!ReadByte(reader, &value))
            return false;
    } while (value != '\n');
    return true;
}

bool ReadLine(ImapReader* reader, char* line, int capacity, bool* tooLarge)
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

bool WriteAll(ReviveTlsConnection* connection, const char* command)
{
    int length = 0;
    int written = 0;
    if (connection == NULL || command == NULL)
        return false;
    while (command[length] != '\0')
        ++length;
    while (written < length)
    {
        const int result = ReviveTLSWrite(connection, command + written,
                                          length - written);
        if (result <= 0)
            return false;
        written += result;
    }
    return true;
}

bool IsTaggedCompletion(const char* line, const char* tag, bool* succeeded)
{
    int tagLength = 0;
    if (line == NULL || tag == NULL || succeeded == NULL)
        return false;
    while (tag[tagLength] != '\0')
        ++tagLength;
    if (!StartsWith(line, tag) || !IsAsciiSpace(line[tagLength]))
        return false;
    while (IsAsciiSpace(line[tagLength]))
        ++tagLength;
    *succeeded = StartsWith(line + tagLength, "OK") &&
                 (line[tagLength + 2] == '\0' ||
                  IsAsciiSpace(line[tagLength + 2]));
    return true;
}

ReviveImapResult ReadTaggedCompletion(ImapReader* reader, const char* tag,
                                      ReviveImapResult failedResult)
{
    char line[kLineCapacity];
    bool tooLarge;
    bool succeeded;
    for (;;)
    {
        if (!ReadLine(reader, line, sizeof(line), &tooLarge))
            return tooLarge ? REVIVE_IMAP_RESPONSE_TOO_LARGE : REVIVE_IMAP_IO_ERROR;
        if (tooLarge)
            return REVIVE_IMAP_RESPONSE_TOO_LARGE;
        if (IsTaggedCompletion(line, tag, &succeeded))
            return succeeded ? REVIVE_IMAP_OK : failedResult;
    }
}

bool AppendChar(char* destination, int capacity, int* length, char value)
{
    if (*length >= capacity - 1)
        return false;
    destination[(*length)++] = value;
    destination[*length] = '\0';
    return true;
}

bool AppendText(char* destination, int capacity, int* length, const char* text)
{
    while (text != NULL && *text != '\0')
        if (!AppendChar(destination, capacity, length, *text++))
            return false;
    return true;
}

bool AppendUnsigned(char* destination, int capacity, int* length,
                    unsigned long value)
{
    char reverse[12];
    int count = 0;
    do
    {
        reverse[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < static_cast<int>(sizeof(reverse)));
    while (count > 0)
        if (!AppendChar(destination, capacity, length, reverse[--count]))
            return false;
    return true;
}

bool AppendQuoted(char* destination, int capacity, int* length, const char* value)
{
    if (value == NULL || !AppendChar(destination, capacity, length, '"'))
        return false;
    while (*value != '\0')
    {
        const unsigned char byte = static_cast<unsigned char>(*value++);
        if (byte < 0x20 || byte == 0x7f)
            return false;
        if (byte == '"' || byte == '\\')
            if (!AppendChar(destination, capacity, length, '\\'))
                return false;
        if (!AppendChar(destination, capacity, length, static_cast<char>(byte)))
            return false;
    }
    return AppendChar(destination, capacity, length, '"');
}

bool ParseUnsignedAfter(const char* line, const char* token, unsigned long* value)
{
    const char* cursor = FindText(line, token);
    unsigned long parsed = 0;
    bool found = false;
    if (cursor == NULL || value == NULL)
        return false;
    cursor += strlen(token);
    while (*cursor >= '0' && *cursor <= '9')
    {
        found = true;
        parsed = parsed * 10 + static_cast<unsigned long>(*cursor - '0');
        ++cursor;
    }
    if (found)
        *value = parsed;
    return found;
}

void KeepNewestUid(unsigned long* values, int capacity, int* count,
                   unsigned long value)
{
    int index;
    if (*count < capacity)
        values[(*count)++] = value;
    else
    {
        for (index = 1; index < capacity; ++index)
            values[index - 1] = values[index];
        values[capacity - 1] = value;
    }
}

ReviveImapResult ReadSearchCompletion(ImapReader* reader, const char* tag,
                                      unsigned long* values, int capacity,
                                      int* count)
{
    char first;
    char prefix[8];
    char line[96];
    bool succeeded;
    int index;

    *count = 0;
    for (;;)
    {
        if (!ReadByte(reader, &first))
            return REVIVE_IMAP_IO_ERROR;
        if (first == '*')
        {
            for (index = 0; index < 7; ++index)
                if (!ReadByte(reader, &prefix[index]))
                    return REVIVE_IMAP_IO_ERROR;
            prefix[7] = '\0';
            if (strcmp(prefix, " SEARCH") == 0)
            {
                unsigned long value = 0;
                bool inNumber = false;
                char character;
                for (;;)
                {
                    if (!ReadByte(reader, &character))
                        return REVIVE_IMAP_IO_ERROR;
                    if (character >= '0' && character <= '9')
                    {
                        value = value * 10 +
                            static_cast<unsigned long>(character - '0');
                        inNumber = true;
                    }
                    else
                    {
                        if (inNumber)
                            KeepNewestUid(values, capacity, count, value);
                        value = 0;
                        inNumber = false;
                        if (character == '\n')
                            break;
                    }
                }
            }
            else if (!ConsumeLine(reader))
                return REVIVE_IMAP_IO_ERROR;
        }
        else
        {
            line[0] = first;
            if (!ReadLine(reader, line + 1, sizeof(line) - 1, NULL))
                return REVIVE_IMAP_IO_ERROR;
            if (IsTaggedCompletion(line, tag, &succeeded))
                return succeeded ? REVIVE_IMAP_OK : REVIVE_IMAP_SEARCH_ERROR;
        }
    }
}

void ParseHeaderLine(ReviveImapMessage* message, const char* line)
{
    const char* colon;
    int nameLength = 0;
    if (message == NULL || line == NULL)
        return;
    colon = line;
    while (*colon != '\0' && *colon != ':')
        ++colon;
    if (*colon != ':')
        return;
    nameLength = static_cast<int>(colon - line);
    if (EqualsIgnoreCase(line, "From", nameLength))
        CopyValue(message->sender, sizeof(message->sender), colon + 1);
    else if (EqualsIgnoreCase(line, "Subject", nameLength))
        CopyValue(message->subject, sizeof(message->subject), colon + 1);
    else if (EqualsIgnoreCase(line, "Date", nameLength))
        CopyValue(message->date, sizeof(message->date), colon + 1);
}

ReviveImapResult ReadFetchCompletion(ImapReader* reader, const char* tag,
                                     ReviveImapMessage* messages,
                                     int capacity, int* count)
{
    char line[kLineCapacity];
    bool tooLarge;
    bool succeeded;
    int current = -1;

    *count = 0;
    for (;;)
    {
        if (!ReadLine(reader, line, sizeof(line), &tooLarge))
            return tooLarge ? REVIVE_IMAP_RESPONSE_TOO_LARGE : REVIVE_IMAP_IO_ERROR;
        if (tooLarge)
            return REVIVE_IMAP_RESPONSE_TOO_LARGE;
        if (IsTaggedCompletion(line, tag, &succeeded))
            return succeeded ? REVIVE_IMAP_OK : REVIVE_IMAP_FETCH_ERROR;

        if (StartsWith(line, "* ") && Contains(line, " FETCH ("))
        {
            unsigned long uid;
            current = -1;
            if (*count < capacity && ParseUnsignedAfter(line, "UID ", &uid))
            {
                ReviveImapMessage* message = &messages[*count];
                ClearBytes(message, sizeof(*message));
                message->uid = uid;
                message->unread = !Contains(line, "\\Seen");
                current = (*count)++;
            }
        }
        else if (current >= 0)
        {
            if (line[0] == ')')
                current = -1;
            else
                ParseHeaderLine(&messages[current], line);
        }
    }
}

ReviveImapResult InitializeAuthenticatedInbox(ImapReader* reader,
                                              ReviveTlsConnection* connection,
                                              const ReviveImapCredentials* credentials)
{
    char command[768];
    char line[kLineCapacity];
    int commandLength = 0;
    bool tooLarge;
    ReviveImapResult result;

    reader->connection = connection;
    reader->offset = 0;
    reader->length = 0;
    if (!ReadLine(reader, line, sizeof(line), &tooLarge))
        return tooLarge ? REVIVE_IMAP_RESPONSE_TOO_LARGE : REVIVE_IMAP_IO_ERROR;
    if (tooLarge)
        return REVIVE_IMAP_RESPONSE_TOO_LARGE;
    if (!StartsWith(line, "* OK"))
        return REVIVE_IMAP_GREETING_ERROR;
    ReviveLog("IMAP", "encrypted server greeting accepted", 0);

    command[0] = '\0';
    if (!AppendText(command, sizeof(command), &commandLength, "A001 LOGIN ") ||
        !AppendQuoted(command, sizeof(command), &commandLength, credentials->email) ||
        !AppendChar(command, sizeof(command), &commandLength, ' ') ||
        !AppendQuoted(command, sizeof(command), &commandLength,
                      credentials->appPassword) ||
        !AppendText(command, sizeof(command), &commandLength, "\r\n"))
    {
        ClearBytes(command, sizeof(command));
        return REVIVE_IMAP_CONFIGURATION_ERROR;
    }
    if (!WriteAll(connection, command))
    {
        ClearBytes(command, sizeof(command));
        return REVIVE_IMAP_IO_ERROR;
    }
    ClearBytes(command, sizeof(command));
    result = ReadTaggedCompletion(reader, "A001", REVIVE_IMAP_AUTHENTICATION_ERROR);
    if (result != REVIVE_IMAP_OK)
        return result;
    ReviveLog("IMAP", "authenticated with app password", 0);

    if (!WriteAll(connection, "A002 SELECT INBOX\r\n"))
        return REVIVE_IMAP_IO_ERROR;
    result = ReadTaggedCompletion(reader, "A002", REVIVE_IMAP_SELECT_ERROR);
    if (result == REVIVE_IMAP_OK)
        ReviveLog("IMAP", "INBOX selected", 0);
    return result;
}

bool ParseLiteralLength(const char* line, unsigned long* length)
{
    const char* opening = NULL;
    const char* cursor;
    unsigned long value = 0;
    bool hasDigit = false;
    if (line == NULL || length == NULL)
        return false;
    for (cursor = line; *cursor != '\0'; ++cursor)
        if (*cursor == '{')
            opening = cursor;
    if (opening == NULL)
        return false;
    for (cursor = opening + 1; *cursor >= '0' && *cursor <= '9'; ++cursor)
    {
        hasDigit = true;
        value = value * 10 + static_cast<unsigned long>(*cursor - '0');
    }
    if (!hasDigit || *cursor != '}')
        return false;
    *length = value;
    return true;
}

bool ReadExact(ImapReader* reader, char* destination, unsigned long length)
{
    unsigned long copied = 0;
    while (copied < length)
    {
        char value;
        if (!ReadByte(reader, &value))
            return false;
        destination[copied++] = value;
    }
    return true;
}

const char* FindHeader(const char* headers, const char* name)
{
    const char* line = headers;
    int nameLength = 0;
    while (name[nameLength] != '\0')
        ++nameLength;
    while (line != NULL && *line != '\0')
    {
        const char* colon = line;
        while (*colon != '\0' && *colon != ':' && *colon != '\r' && *colon != '\n')
            ++colon;
        if (*colon == ':' && EqualsIgnoreCase(line, name, static_cast<int>(colon - line)))
        {
            const char* value = colon + 1;
            while (IsAsciiSpace(*value))
                ++value;
            return value;
        }
        line = strstr(line, "\n");
        if (line != NULL)
            ++line;
    }
    return NULL;
}

void CopyHeaderValue(const char* headers, const char* name,
                     char* destination, int capacity)
{
    const char* value = FindHeader(headers, name);
    if (value == NULL)
    {
        if (destination != NULL && capacity > 0)
            destination[0] = '\0';
        return;
    }
    CopyValue(destination, capacity, value);
}

int HexValue(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

int Base64Value(char value)
{
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

bool DecodeContent(const char* source, int length, const char* transferEncoding,
                   char* destination, int capacity)
{
    int input = 0;
    int output = 0;
    if (destination == NULL || capacity < 1)
        return false;
    destination[0] = '\0';
    if (source == NULL || length < 0)
        return false;
    if (transferEncoding != NULL && Contains(transferEncoding, "base64"))
    {
        int bits = 0;
        int bitCount = 0;
        for (; input < length; ++input)
        {
            const int value = Base64Value(source[input]);
            if (value < 0)
                continue;
            bits = (bits << 6) | value;
            bitCount += 6;
            if (bitCount >= 8)
            {
                bitCount -= 8;
                if (output >= capacity - 1)
                    return false;
                destination[output++] = static_cast<char>((bits >> bitCount) & 0xff);
            }
        }
    }
    else
    {
        const bool quotedPrintable = transferEncoding != NULL &&
                                     Contains(transferEncoding, "quoted-printable");
        while (input < length)
        {
            char value = source[input++];
            if (quotedPrintable && value == '=' && input + 1 < length)
            {
                if (source[input] == '\r' && source[input + 1] == '\n')
                {
                    input += 2;
                    continue;
                }
                const int high = HexValue(source[input]);
                const int low = HexValue(source[input + 1]);
                if (high >= 0 && low >= 0)
                {
                    value = static_cast<char>((high << 4) | low);
                    input += 2;
                }
            }
            if (output >= capacity - 1)
                return false;
            destination[output++] = value;
        }
    }
    destination[output] = '\0';
    return true;
}

bool HtmlToText(const char* html, char* text, int capacity)
{
    int input = 0;
    int output = 0;
    bool inTag = false;
    if (html == NULL || text == NULL || capacity < 1)
        return false;
    while (html[input] != '\0')
    {
        if (html[input] == '<')
        {
            if (StartsWith(html + input, "<br") || StartsWith(html + input, "<BR") ||
                StartsWith(html + input, "</p") || StartsWith(html + input, "</P"))
            {
                if (output < capacity - 1 && (output == 0 || text[output - 1] != '\n'))
                    text[output++] = '\n';
            }
            inTag = true;
        }
        else if (html[input] == '>')
            inTag = false;
        else if (!inTag)
        {
            char value = html[input];
            if (html[input] == '&')
            {
                if (StartsWith(html + input, "&amp;")) { value = '&'; input += 4; }
                else if (StartsWith(html + input, "&lt;")) { value = '<'; input += 3; }
                else if (StartsWith(html + input, "&gt;")) { value = '>'; input += 3; }
                else if (StartsWith(html + input, "&quot;")) { value = '"'; input += 5; }
            }
            if (output >= capacity - 1)
                return false;
            text[output++] = value;
        }
        ++input;
    }
    text[output] = '\0';
    return true;
}

bool ExtractBoundary(const char* contentType, char* boundary, int capacity)
{
    const char* value = FindText(contentType, "boundary=");
    int output = 0;
    char quote = '\0';
    if (value == NULL || boundary == NULL || capacity < 2)
        return false;
    value += 9;
    if (*value == '"' || *value == '\'')
        quote = *value++;
    while (*value != '\0' && output < capacity - 1)
    {
        if ((quote != '\0' && *value == quote) ||
            (quote == '\0' && (IsAsciiSpace(*value) || *value == ';' || *value == '\r')))
            break;
        boundary[output++] = *value++;
    }
    boundary[output] = '\0';
    return output != 0;
}

bool DecodeCandidate(const char* headers, const char* content, int contentLength,
                     char* body, int capacity, bool html)
{
    char transfer[80];
    char* decoded;
    bool succeeded = false;
    int length = 0;
    if (capacity < 2)
        return false;
    decoded = static_cast<char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           capacity));
    if (decoded == NULL)
        return false;
    CopyHeaderValue(headers, "Content-Transfer-Encoding", transfer, sizeof(transfer));
    if (!DecodeContent(content, contentLength, transfer, decoded, capacity))
    {
        ClearBytes(decoded, capacity);
        HeapFree(GetProcessHeap(), 0, decoded);
        return false;
    }
    if (html)
        succeeded = HtmlToText(decoded, body, capacity);
    else
    {
        while (decoded[length] != '\0')
        {
            if (length >= capacity - 1)
                break;
            body[length] = decoded[length];
            ++length;
        }
        if (decoded[length] == '\0')
        {
            body[length] = '\0';
            succeeded = true;
        }
    }
    ClearBytes(decoded, capacity);
    HeapFree(GetProcessHeap(), 0, decoded);
    return succeeded;
}

bool ExtractMessageText(char* raw, ReviveImapMessage* header,
                        char* body, int bodyCapacity, bool* usedHtmlFallback)
{
    char* bodyStart;
    char contentType[256];
    char boundary[160];
    if (raw == NULL || header == NULL || body == NULL || usedHtmlFallback == NULL)
        return false;
    *usedHtmlFallback = false;
    ClearBytes(header, sizeof(*header));
    body[0] = '\0';
    bodyStart = strstr(raw, "\r\n\r\n");
    if (bodyStart == NULL)
        bodyStart = strstr(raw, "\n\n");
    if (bodyStart == NULL)
        return false;
    if (bodyStart[0] == '\r')
    {
        bodyStart[0] = '\0';
        bodyStart += 4;
    }
    else
    {
        bodyStart[0] = '\0';
        bodyStart += 2;
    }
    CopyHeaderValue(raw, "From", header->sender, sizeof(header->sender));
    CopyHeaderValue(raw, "Subject", header->subject, sizeof(header->subject));
    CopyHeaderValue(raw, "Date", header->date, sizeof(header->date));
    CopyHeaderValue(raw, "Content-Type", contentType, sizeof(contentType));

    if (!Contains(contentType, "multipart/") || !ExtractBoundary(contentType, boundary, sizeof(boundary)))
    {
        const bool html = Contains(contentType, "text/html");
        if (!DecodeCandidate(raw, bodyStart, static_cast<int>(strlen(bodyStart)), body,
                             bodyCapacity, html))
            return false;
        *usedHtmlFallback = html;
        return true;
    }

    char marker[170];
    int markerLength = 0;
    if (!AppendText(marker, sizeof(marker), &markerLength, "--") ||
        !AppendText(marker, sizeof(marker), &markerLength, boundary))
        return false;
    char* part = strstr(bodyStart, marker);
    char* htmlHeaders = NULL;
    char* htmlContent = NULL;
    int htmlLength = 0;
    while (part != NULL)
    {
        char* next;
        char* partHeaders;
        char* partContent;
        char partType[256];
        part += markerLength;
        if (StartsWith(part, "--"))
            break;
        if (StartsWith(part, "\r\n")) part += 2;
        else if (*part == '\n') ++part;
        partHeaders = part;
        partContent = strstr(partHeaders, "\r\n\r\n");
        if (partContent == NULL)
            partContent = strstr(partHeaders, "\n\n");
        if (partContent == NULL)
            break;
        if (partContent[0] == '\r')
        {
            partContent[0] = '\0';
            partContent += 4;
        }
        else
        {
            partContent[0] = '\0';
            partContent += 2;
        }
        next = strstr(partContent, marker);
        if (next == NULL)
            break;
        CopyHeaderValue(partHeaders, "Content-Type", partType, sizeof(partType));
        if (Contains(partType, "text/plain"))
            return DecodeCandidate(partHeaders, partContent,
                                   static_cast<int>(next - partContent), body,
                                   bodyCapacity, false);
        if (Contains(partType, "text/html") && htmlHeaders == NULL)
        {
            htmlHeaders = partHeaders;
            htmlContent = partContent;
            htmlLength = static_cast<int>(next - partContent);
        }
        part = next;
    }
    if (htmlHeaders != NULL && DecodeCandidate(htmlHeaders, htmlContent, htmlLength,
                                                body, bodyCapacity, true))
    {
        *usedHtmlFallback = true;
        return true;
    }
    return false;
}

ReviveImapResult ReadMessageFetchCompletion(ImapReader* reader, const char* tag,
                                            ReviveImapMessage* header, char* body,
                                            int bodyCapacity, bool* usedHtmlFallback)
{
    char line[kLineCapacity];
    char raw[kRawMessageCapacity];
    bool tooLarge;
    bool succeeded;
    bool receivedLiteral = false;
    for (;;)
    {
        unsigned long literalLength;
        if (!ReadLine(reader, line, sizeof(line), &tooLarge))
            return tooLarge ? REVIVE_IMAP_RESPONSE_TOO_LARGE : REVIVE_IMAP_IO_ERROR;
        if (tooLarge)
            return REVIVE_IMAP_RESPONSE_TOO_LARGE;
        if (IsTaggedCompletion(line, tag, &succeeded))
        {
            if (!succeeded)
                return REVIVE_IMAP_FETCH_ERROR;
            return receivedLiteral ? REVIVE_IMAP_OK : REVIVE_IMAP_FETCH_ERROR;
        }
        if (!ParseLiteralLength(line, &literalLength))
            continue;
        if (literalLength >= static_cast<unsigned long>(sizeof(raw)))
            return REVIVE_IMAP_BODY_TOO_LARGE;
        if (!ReadExact(reader, raw, literalLength))
            return REVIVE_IMAP_IO_ERROR;
        raw[literalLength] = '\0';
        if (!ExtractMessageText(raw, header, body, bodyCapacity, usedHtmlFallback))
        {
            ClearBytes(raw, sizeof(raw));
            return REVIVE_IMAP_UNSUPPORTED_MESSAGE;
        }
        ClearBytes(raw, sizeof(raw));
        receivedLiteral = true;
    }
}

ReviveImapResult ReadSectionFetchCompletion(ImapReader* reader, const char* tag,
                                            char* headers, int headerCapacity,
                                            char* content, int contentCapacity,
                                            bool captureMimeHeaders,
                                            unsigned long* contentLength)
{
    char line[kLineCapacity];
    bool tooLarge;
    bool succeeded;
    bool receivedContent = false;
    bool receivedHeader = false;
    if (contentLength != NULL)
        *contentLength = 0;
    if (headers != NULL && headerCapacity > 0)
        headers[0] = '\0';
    if (content != NULL && contentCapacity > 0)
        content[0] = '\0';
    for (;;)
    {
        unsigned long literalLength;
        char* destination = content;
        int capacity = contentCapacity;
        if (!ReadLine(reader, line, sizeof(line), &tooLarge))
            return tooLarge ? REVIVE_IMAP_RESPONSE_TOO_LARGE : REVIVE_IMAP_IO_ERROR;
        if (tooLarge)
            return REVIVE_IMAP_RESPONSE_TOO_LARGE;
        if (IsTaggedCompletion(line, tag, &succeeded))
        {
            if (!succeeded)
                return REVIVE_IMAP_FETCH_ERROR;
            if (content == NULL)
                return receivedHeader ? REVIVE_IMAP_OK : REVIVE_IMAP_FETCH_ERROR;
            return receivedContent ? REVIVE_IMAP_OK : REVIVE_IMAP_FETCH_ERROR;
        }
        if (!ParseLiteralLength(line, &literalLength))
            continue;
        if ((content == NULL || captureMimeHeaders) && headers != NULL &&
            (!captureMimeHeaders || Contains(line, "MIME]")))
        {
            destination = headers;
            capacity = headerCapacity;
            receivedHeader = true;
        }
        else
            receivedContent = true;
        if (destination == NULL || capacity < 2 ||
            literalLength >= static_cast<unsigned long>(capacity))
            return REVIVE_IMAP_BODY_TOO_LARGE;
        if (!ReadExact(reader, destination, literalLength))
            return REVIVE_IMAP_IO_ERROR;
        destination[literalLength] = '\0';
        if (destination == content && contentLength != NULL)
            *contentLength = literalLength;
    }
}
}

void ReviveImapClearCredentials(ReviveImapCredentials* credentials)
{
    if (credentials != NULL)
        ClearBytes(credentials, sizeof(*credentials));
}

ReviveImapResult ReviveImapFetchInbox(ReviveTlsConnection* connection,
                                      const ReviveImapCredentials* credentials,
                                      ReviveImapMessage* messages,
                                      int capacity,
                                      int* messageCount)
{
    ImapReader reader;
    char command[768];
    unsigned long uids[REVIVE_IMAP_MAX_MESSAGES];
    int commandLength;
    int uidCount = 0;
    int index;
    ReviveImapResult result;

    if (messageCount != NULL)
        *messageCount = 0;
    if (connection == NULL || credentials == NULL || messages == NULL ||
        messageCount == NULL || capacity <= 0 ||
        capacity > REVIVE_IMAP_MAX_MESSAGES || credentials->email[0] == '\0' ||
        credentials->appPassword[0] == '\0')
        return REVIVE_IMAP_CONFIGURATION_ERROR;

    result = InitializeAuthenticatedInbox(&reader, connection, credentials);
    if (result != REVIVE_IMAP_OK)
        return result;

    if (!WriteAll(connection, "A003 UID SEARCH ALL\r\n"))
        return REVIVE_IMAP_IO_ERROR;
    result = ReadSearchCompletion(&reader, "A003", uids, capacity, &uidCount);
    if (result != REVIVE_IMAP_OK)
        return result;
    if (uidCount == 0)
        return REVIVE_IMAP_OK;

    commandLength = 0;
    command[0] = '\0';
    if (!AppendText(command, sizeof(command), &commandLength, "A004 UID FETCH "))
        return REVIVE_IMAP_CONFIGURATION_ERROR;
    for (index = 0; index < uidCount; ++index)
    {
        if ((index != 0 && !AppendChar(command, sizeof(command), &commandLength, ',')) ||
            !AppendUnsigned(command, sizeof(command), &commandLength, uids[index]))
        {
            ClearBytes(command, sizeof(command));
            return REVIVE_IMAP_CONFIGURATION_ERROR;
        }
    }
    if (!AppendText(command, sizeof(command), &commandLength,
                    " (UID FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])\r\n"))
    {
        ClearBytes(command, sizeof(command));
        return REVIVE_IMAP_CONFIGURATION_ERROR;
    }
    if (!WriteAll(connection, command))
    {
        ClearBytes(command, sizeof(command));
        return REVIVE_IMAP_IO_ERROR;
    }
    result = ReadFetchCompletion(&reader, "A004", messages, capacity, messageCount);
    ClearBytes(command, sizeof(command));
    if (result == REVIVE_IMAP_OK)
        ReviveLog("IMAP", "inbox headers received", *messageCount);
    return result;
}

ReviveImapResult ReviveImapFetchMessage(ReviveTlsConnection* connection,
                                        const ReviveImapCredentials* credentials,
                                        unsigned long uid,
                                        unsigned long requestedBodyBytes,
                                        ReviveImapMessage* header,
                                        char* body,
                                        int bodyCapacity,
                                        bool* usedHtmlFallback,
                                        bool* hasMore)
{
    ImapReader reader;
    char command[256];
    char messageHeaders[4096];
    char partHeaders[4096];
    char contentType[256];
    unsigned long literalBytes = 0;
    unsigned long requestedWireBytes;
    int commandLength = 0;
    ReviveImapResult result;

    if (connection == NULL || credentials == NULL || header == NULL ||
        body == NULL || bodyCapacity < 2 || usedHtmlFallback == NULL ||
        hasMore == NULL || requestedBodyBytes == 0 ||
        requestedBodyBytes > REVIVE_IMAP_BODY_CAPACITY ||
        bodyCapacity < static_cast<int>(requestedBodyBytes + 2) ||
        uid == 0 || credentials->email[0] == '\0' ||
        credentials->appPassword[0] == '\0')
        return REVIVE_IMAP_CONFIGURATION_ERROR;

    body[0] = '\0';
    *usedHtmlFallback = false;
    *hasMore = false;
    requestedWireBytes = requestedBodyBytes + 1;
    result = InitializeAuthenticatedInbox(&reader, connection, credentials);
    if (result != REVIVE_IMAP_OK)
        return result;

    command[0] = '\0';
    if (!AppendText(command, sizeof(command), &commandLength, "A003 UID FETCH ") ||
        !AppendUnsigned(command, sizeof(command), &commandLength, uid) ||
        !AppendText(command, sizeof(command), &commandLength,
                    " (UID BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE CONTENT-TYPE CONTENT-TRANSFER-ENCODING)])\r\n"))
        return REVIVE_IMAP_CONFIGURATION_ERROR;
    if (!WriteAll(connection, command))
        return REVIVE_IMAP_IO_ERROR;
    result = ReadSectionFetchCompletion(&reader, "A003", messageHeaders,
                                        sizeof(messageHeaders), NULL, 0, false, NULL);
    if (result != REVIVE_IMAP_OK)
    {
        ClearBytes(command, sizeof(command));
        ClearBytes(messageHeaders, sizeof(messageHeaders));
        return result;
    }
    ClearBytes(header, sizeof(*header));
    CopyHeaderValue(messageHeaders, "From", header->sender, sizeof(header->sender));
    CopyHeaderValue(messageHeaders, "Subject", header->subject, sizeof(header->subject));
    CopyHeaderValue(messageHeaders, "Date", header->date, sizeof(header->date));
    CopyHeaderValue(messageHeaders, "Content-Type", contentType, sizeof(contentType));

    commandLength = 0;
    command[0] = '\0';
    if (!AppendText(command, sizeof(command), &commandLength, "A004 UID FETCH ") ||
        !AppendUnsigned(command, sizeof(command), &commandLength, uid))
    {
        ClearBytes(command, sizeof(command));
        ClearBytes(messageHeaders, sizeof(messageHeaders));
        return REVIVE_IMAP_CONFIGURATION_ERROR;
    }
    const bool multipart = Contains(contentType, "multipart/");
    if (multipart)
    {
        if (!AppendText(command, sizeof(command), &commandLength,
                        " (BODY.PEEK[1.MIME] BODY.PEEK[1]<0.") ||
            !AppendUnsigned(command, sizeof(command), &commandLength,
                            requestedWireBytes) ||
            !AppendText(command, sizeof(command), &commandLength, ">)\r\n"))
        {
            ClearBytes(command, sizeof(command));
            ClearBytes(messageHeaders, sizeof(messageHeaders));
            return REVIVE_IMAP_CONFIGURATION_ERROR;
        }
    }
    else if (!AppendText(command, sizeof(command), &commandLength,
                         " (BODY.PEEK[TEXT]<0.") ||
             !AppendUnsigned(command, sizeof(command), &commandLength,
                             requestedWireBytes) ||
             !AppendText(command, sizeof(command), &commandLength, ">)\r\n"))
    {
        ClearBytes(command, sizeof(command));
        ClearBytes(messageHeaders, sizeof(messageHeaders));
        return REVIVE_IMAP_CONFIGURATION_ERROR;
    }
    if (!WriteAll(connection, command))
    {
        ClearBytes(command, sizeof(command));
        ClearBytes(messageHeaders, sizeof(messageHeaders));
        return REVIVE_IMAP_IO_ERROR;
    }
    result = ReadSectionFetchCompletion(&reader, "A004", partHeaders,
                                        sizeof(partHeaders), body, bodyCapacity,
                                        multipart, &literalBytes);
    ClearBytes(command, sizeof(command));
    if (result == REVIVE_IMAP_OK)
    {
        const char* partTypeHeaders = multipart && partHeaders[0] != '\0' ?
                                      partHeaders : messageHeaders;
        char selectedContentType[256];
        CopyHeaderValue(partTypeHeaders, "Content-Type", selectedContentType,
                        sizeof(selectedContentType));
        *usedHtmlFallback = Contains(selectedContentType, "text/html");
        if (!DecodeCandidate(partTypeHeaders, body, static_cast<int>(strlen(body)),
                             body, bodyCapacity, *usedHtmlFallback))
            result = REVIVE_IMAP_UNSUPPORTED_MESSAGE;
        *hasMore = literalBytes > requestedBodyBytes;
        ClearBytes(selectedContentType, sizeof(selectedContentType));
        header->uid = uid;
        if (result == REVIVE_IMAP_OK)
            ReviveLog("IMAP", "message text received", 0);
    }
    ClearBytes(messageHeaders, sizeof(messageHeaders));
    ClearBytes(partHeaders, sizeof(partHeaders));
    return result;
}
