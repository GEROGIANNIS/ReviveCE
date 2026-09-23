#include "smtp.h"

#include <windows.h>

namespace
{
const int kReadBufferCapacity = 512;
const int kLineCapacity = 1024;
const int kDataCapacity = REVIVE_SMTP_BODY_CAPACITY * 2 + 1024;

struct SmtpReader
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

int StringLength(const char* text)
{
    int length = 0;
    if (text == NULL)
        return 0;
    while (text[length] != '\0')
        ++length;
    return length;
}

bool WriteBytes(ReviveTlsConnection* connection, const char* data, int length)
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

bool WriteLine(ReviveTlsConnection* connection, const char* text)
{
    return WriteBytes(connection, text, StringLength(text));
}

bool ReadByte(SmtpReader* reader, char* value)
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

bool ConsumeLine(SmtpReader* reader)
{
    char value;
    do
    {
        if (!ReadByte(reader, &value))
            return false;
    } while (value != '\n');
    return true;
}

bool ReadLine(SmtpReader* reader, char* line, int capacity, bool* tooLarge)
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

bool HasResponseCode(const char* line, int expected)
{
    return line != NULL && line[0] == static_cast<char>('0' + expected / 100) &&
           line[1] == static_cast<char>('0' + (expected / 10) % 10) &&
           line[2] == static_cast<char>('0' + expected % 10) &&
           (line[3] == ' ' || line[3] == '-');
}

ReviveSmtpResult ReadResponse(SmtpReader* reader, int expected,
                              ReviveSmtpResult failedResult)
{
    char line[kLineCapacity];
    bool tooLarge;
    for (;;)
    {
        if (!ReadLine(reader, line, sizeof(line), &tooLarge))
            return tooLarge ? REVIVE_SMTP_RESPONSE_TOO_LARGE : REVIVE_SMTP_IO_ERROR;
        if (tooLarge)
            return REVIVE_SMTP_RESPONSE_TOO_LARGE;
        if (!HasResponseCode(line, expected))
            return failedResult;
        if (line[3] == ' ')
            return REVIVE_SMTP_OK;
    }
}

bool IsSafeAddress(const char* value)
{
    bool hasAt = false;
    int index;
    if (value == NULL || value[0] == '\0')
        return false;
    for (index = 0; value[index] != '\0'; ++index)
    {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character <= 32 || character == '<' || character == '>' ||
            character == ':' || character == '\r' || character == '\n')
            return false;
        if (character == '@')
            hasAt = true;
    }
    return hasAt;
}

bool IsSafeHeader(const char* value)
{
    int index;
    if (value == NULL)
        return false;
    for (index = 0; value[index] != '\0'; ++index)
    {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character < 32 || character > 126 || character == '\r' || character == '\n')
            return false;
    }
    return true;
}

bool IsSafeBody(const char* value)
{
    int index;
    if (value == NULL)
        return false;
    for (index = 0; value[index] != '\0'; ++index)
    {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if ((character < 32 && character != '\r' && character != '\n' && character != '\t') ||
            character == 127)
            return false;
    }
    return true;
}

bool AppendByte(char* data, int capacity, int* length, char value)
{
    if (data == NULL || length == NULL || *length >= capacity - 1)
        return false;
    data[(*length)++] = value;
    data[*length] = '\0';
    return true;
}

bool AppendText(char* data, int capacity, int* length, const char* text)
{
    int index;
    if (text == NULL)
        return false;
    for (index = 0; text[index] != '\0'; ++index)
        if (!AppendByte(data, capacity, length, text[index]))
            return false;
    return true;
}

bool AppendCrLf(char* data, int capacity, int* length)
{
    return AppendByte(data, capacity, length, '\r') &&
           AppendByte(data, capacity, length, '\n');
}

bool BuildData(const ReviveImapCredentials* credentials,
               const ReviveSmtpMessage* message, char* data, int capacity)
{
    int length = 0;
    int index;
    bool atLineStart = true;
    if (!AppendText(data, capacity, &length, "From: <") ||
        !AppendText(data, capacity, &length, credentials->email) ||
        !AppendText(data, capacity, &length, ">\r\nTo: <") ||
        !AppendText(data, capacity, &length, message->recipient) ||
        !AppendText(data, capacity, &length, ">\r\nSubject: ") ||
        !AppendText(data, capacity, &length, message->subject) ||
        !AppendText(data, capacity, &length,
                    "\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8"
                    "\r\nContent-Transfer-Encoding: 8bit\r\n\r\n"))
        return false;
    for (index = 0; message->body[index] != '\0'; ++index)
    {
        const char character = message->body[index];
        if (character == '\r' || character == '\n')
        {
            if (character == '\r' && message->body[index + 1] == '\n')
                ++index;
            if (!AppendCrLf(data, capacity, &length))
                return false;
            atLineStart = true;
        }
        else
        {
            if (atLineStart && character == '.' && !AppendByte(data, capacity, &length, '.'))
                return false;
            if (!AppendByte(data, capacity, &length, character))
                return false;
            atLineStart = false;
        }
    }
    if (!atLineStart && !AppendCrLf(data, capacity, &length))
        return false;
    return AppendText(data, capacity, &length, ".\r\n");
}

bool Base64Encode(const char* input, char* output, int capacity)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int length = StringLength(input);
    int source = 0;
    int destination = 0;
    if (input == NULL || output == NULL || capacity < ((length + 2) / 3) * 4 + 1)
        return false;
    while (source < length)
    {
        const unsigned int first = static_cast<unsigned char>(input[source++]);
        const bool secondPresent = source < length;
        const unsigned int second = secondPresent ? static_cast<unsigned char>(input[source++]) : 0;
        const bool thirdPresent = source < length;
        const unsigned int third = thirdPresent ? static_cast<unsigned char>(input[source++]) : 0;
        output[destination++] = alphabet[first >> 2];
        output[destination++] = alphabet[((first & 3) << 4) | (second >> 4)];
        output[destination++] = secondPresent ? alphabet[((second & 15) << 2) | (third >> 6)] : '=';
        output[destination++] = thirdPresent ? alphabet[third & 63] : '=';
    }
    output[destination] = '\0';
    return true;
}
}

ReviveSmtpResult ReviveSmtpSendMessage(ReviveTlsConnection* connection,
                                       const ReviveImapCredentials* credentials,
                                       const ReviveSmtpMessage* message)
{
    SmtpReader reader;
    char encodedEmail[REVIVE_IMAP_EMAIL_CAPACITY * 2];
    char encodedPassword[REVIVE_IMAP_PASSWORD_CAPACITY * 2];
    char command[REVIVE_SMTP_RECIPIENT_CAPACITY + 32];
    char* data = NULL;
    ReviveSmtpResult result;
    if (connection == NULL || credentials == NULL || message == NULL ||
        !IsSafeAddress(credentials->email) || credentials->appPassword[0] == '\0' ||
        !IsSafeAddress(message->recipient) || !IsSafeHeader(message->subject) ||
        !IsSafeBody(message->body))
        return REVIVE_SMTP_CONFIGURATION_ERROR;

    reader.connection = connection;
    reader.offset = 0;
    reader.length = 0;
    result = ReadResponse(&reader, 220, REVIVE_SMTP_GREETING_ERROR);
    if (result != REVIVE_SMTP_OK)
        return result;
    if (!WriteLine(connection, "EHLO revivece\r\n"))
        return REVIVE_SMTP_IO_ERROR;
    result = ReadResponse(&reader, 250, REVIVE_SMTP_EHLO_ERROR);
    if (result != REVIVE_SMTP_OK)
        return result;
    if (!Base64Encode(credentials->email, encodedEmail, sizeof(encodedEmail)) ||
        !Base64Encode(credentials->appPassword, encodedPassword, sizeof(encodedPassword)))
        return REVIVE_SMTP_CONFIGURATION_ERROR;
    if (!WriteLine(connection, "AUTH LOGIN\r\n"))
        return REVIVE_SMTP_IO_ERROR;
    result = ReadResponse(&reader, 334, REVIVE_SMTP_AUTHENTICATION_ERROR);
    if (result != REVIVE_SMTP_OK || !WriteLine(connection, encodedEmail) ||
        !WriteLine(connection, "\r\n"))
        return result == REVIVE_SMTP_OK ? REVIVE_SMTP_IO_ERROR : result;
    result = ReadResponse(&reader, 334, REVIVE_SMTP_AUTHENTICATION_ERROR);
    if (result != REVIVE_SMTP_OK || !WriteLine(connection, encodedPassword) ||
        !WriteLine(connection, "\r\n"))
        return result == REVIVE_SMTP_OK ? REVIVE_SMTP_IO_ERROR : result;
    result = ReadResponse(&reader, 235, REVIVE_SMTP_AUTHENTICATION_ERROR);
    if (result != REVIVE_SMTP_OK)
        return result;
    wsprintfA(command, "MAIL FROM:<%s>\r\n", credentials->email);
    if (!WriteLine(connection, command))
        return REVIVE_SMTP_IO_ERROR;
    result = ReadResponse(&reader, 250, REVIVE_SMTP_SENDER_ERROR);
    if (result != REVIVE_SMTP_OK)
        return result;
    wsprintfA(command, "RCPT TO:<%s>\r\n", message->recipient);
    if (!WriteLine(connection, command))
        return REVIVE_SMTP_IO_ERROR;
    result = ReadResponse(&reader, 250, REVIVE_SMTP_RECIPIENT_ERROR);
    if (result != REVIVE_SMTP_OK)
        return result;
    if (!WriteLine(connection, "DATA\r\n"))
        return REVIVE_SMTP_IO_ERROR;
    result = ReadResponse(&reader, 354, REVIVE_SMTP_DATA_ERROR);
    if (result != REVIVE_SMTP_OK)
        return result;
    data = static_cast<char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, kDataCapacity));
    if (data == NULL)
        return REVIVE_SMTP_IO_ERROR;
    if (!BuildData(credentials, message, data, kDataCapacity) ||
        !WriteBytes(connection, data, StringLength(data)))
        result = REVIVE_SMTP_IO_ERROR;
    else
        result = ReadResponse(&reader, 250, REVIVE_SMTP_MESSAGE_ERROR);
    ClearBytes(data, kDataCapacity);
    HeapFree(GetProcessHeap(), 0, data);
    if (result == REVIVE_SMTP_OK)
        WriteLine(connection, "QUIT\r\n");
    ClearBytes(encodedEmail, sizeof(encodedEmail));
    ClearBytes(encodedPassword, sizeof(encodedPassword));
    ClearBytes(command, sizeof(command));
    return result;
}

void ReviveSmtpClearMessage(ReviveSmtpMessage* message)
{
    if (message != NULL)
        ClearBytes(message, sizeof(*message));
}
