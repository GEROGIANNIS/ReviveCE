#include "imap.h"

#include "../common/log.h"

#include <string.h>

namespace
{
const int kReadBufferCapacity = 512;
const int kLineCapacity = 2048;

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
    char line[kLineCapacity];
    unsigned long uids[REVIVE_IMAP_MAX_MESSAGES];
    int commandLength;
    int uidCount = 0;
    int index;
    bool tooLarge;
    ReviveImapResult result;

    if (messageCount != NULL)
        *messageCount = 0;
    if (connection == NULL || credentials == NULL || messages == NULL ||
        messageCount == NULL || capacity <= 0 ||
        capacity > REVIVE_IMAP_MAX_MESSAGES || credentials->email[0] == '\0' ||
        credentials->appPassword[0] == '\0')
        return REVIVE_IMAP_CONFIGURATION_ERROR;

    reader.connection = connection;
    reader.offset = 0;
    reader.length = 0;
    if (!ReadLine(&reader, line, sizeof(line), &tooLarge))
        return tooLarge ? REVIVE_IMAP_RESPONSE_TOO_LARGE : REVIVE_IMAP_IO_ERROR;
    if (!StartsWith(line, "* OK"))
        return REVIVE_IMAP_GREETING_ERROR;
    ReviveLog("IMAP", "encrypted server greeting accepted", 0);

    commandLength = 0;
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
    result = ReadTaggedCompletion(&reader, "A001", REVIVE_IMAP_AUTHENTICATION_ERROR);
    if (result != REVIVE_IMAP_OK)
        return result;
    ReviveLog("IMAP", "authenticated with app password", 0);

    if (!WriteAll(connection, "A002 SELECT INBOX\r\n"))
        return REVIVE_IMAP_IO_ERROR;
    result = ReadTaggedCompletion(&reader, "A002", REVIVE_IMAP_SELECT_ERROR);
    if (result != REVIVE_IMAP_OK)
        return result;
    ReviveLog("IMAP", "INBOX selected", 0);

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
