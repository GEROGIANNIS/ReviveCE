#include "feed.h"

namespace
{
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

int StringLength(const char* text)
{
    int length = 0;
    if (text != NULL)
        while (text[length] != '\0')
            ++length;
    return length;
}

bool StartsWithIgnoreCase(const char* text, const char* prefix)
{
    return text != NULL && prefix != NULL &&
           EqualsIgnoreCase(text, prefix, StringLength(prefix));
}

bool IsNameEnd(char value)
{
    return value == '>' || value == '/' || value == ' ' || value == '\t' ||
           value == '\r' || value == '\n' || value == '\0';
}

const char* FindOpening(const char* cursor, const char* end, const char* name,
                        const char** afterOpening)
{
    const int nameLength = StringLength(name);
    while (cursor != NULL && cursor < end && *cursor != '\0')
    {
        if (*cursor == '<' && cursor + 1 + nameLength < end &&
            EqualsIgnoreCase(cursor + 1, name, nameLength) &&
            IsNameEnd(cursor[1 + nameLength]))
        {
            const char* closing = cursor + 1 + nameLength;
            while (closing < end && *closing != '>')
                ++closing;
            if (closing >= end)
                return NULL;
            if (afterOpening != NULL)
                *afterOpening = closing + 1;
            return cursor;
        }
        ++cursor;
    }
    return NULL;
}

const char* FindClosing(const char* cursor, const char* end, const char* name)
{
    const int nameLength = StringLength(name);
    while (cursor != NULL && cursor < end && *cursor != '\0')
    {
        if (*cursor == '<' && cursor + 2 + nameLength <= end && cursor[1] == '/' &&
            EqualsIgnoreCase(cursor + 2, name, nameLength) &&
            IsNameEnd(cursor[2 + nameLength]))
            return cursor;
        ++cursor;
    }
    return NULL;
}

bool FindElement(const char* start, const char* end, const char* name,
                 const char** contentStart, const char** contentEnd)
{
    const char* openingEnd = NULL;
    const char* opening = FindOpening(start, end, name, &openingEnd);
    const char* closing;
    if (opening == NULL || openingEnd == NULL)
        return false;
    closing = FindClosing(openingEnd, end, name);
    if (closing == NULL)
        return false;
    *contentStart = openingEnd;
    *contentEnd = closing;
    return true;
}

bool AppendChar(char* destination, int capacity, int* length, char value)
{
    if (destination == NULL || length == NULL || *length >= capacity - 1)
        return false;
    destination[(*length)++] = value;
    destination[*length] = '\0';
    return true;
}

bool AppendEntity(char* destination, int capacity, int* length,
                  const char* value, const char* end, const char** next)
{
    if (value + 5 <= end && StartsWithIgnoreCase(value, "&amp;"))
    {
        *next = value + 5;
        return AppendChar(destination, capacity, length, '&');
    }
    if (value + 4 <= end && StartsWithIgnoreCase(value, "&lt;"))
    {
        *next = value + 4;
        return AppendChar(destination, capacity, length, '<');
    }
    if (value + 4 <= end && StartsWithIgnoreCase(value, "&gt;"))
    {
        *next = value + 4;
        return AppendChar(destination, capacity, length, '>');
    }
    if (value + 6 <= end && StartsWithIgnoreCase(value, "&quot;"))
    {
        *next = value + 6;
        return AppendChar(destination, capacity, length, '"');
    }
    if (value + 6 <= end && StartsWithIgnoreCase(value, "&apos;"))
    {
        *next = value + 6;
        return AppendChar(destination, capacity, length, '\'');
    }
    *next = value + 1;
    return AppendChar(destination, capacity, length, '&');
}

void CopyText(const char* start, const char* end, char* destination, int capacity)
{
    int length = 0;
    bool previousSpace = true;
    if (destination == NULL || capacity < 2)
        return;
    destination[0] = '\0';
    while (start != NULL && start < end)
    {
        if (start + 9 <= end && StartsWithIgnoreCase(start, "<![CDATA["))
        {
            start += 9;
            while (start + 3 <= end && !(start[0] == ']' && start[1] == ']' && start[2] == '>'))
            {
                if (!AppendChar(destination, capacity, &length, *start++))
                    return;
            }
            if (start + 3 <= end)
                start += 3;
            previousSpace = false;
            continue;
        }
        if (*start == '<')
        {
            while (start < end && *start != '>')
                ++start;
            if (start < end)
                ++start;
            continue;
        }
        if (*start == '&')
        {
            const char* next;
            if (!AppendEntity(destination, capacity, &length, start, end, &next))
                return;
            start = next;
            previousSpace = false;
            continue;
        }
        if (*start == '\r' || *start == '\n' || *start == '\t' || *start == ' ')
        {
            if (!previousSpace && !AppendChar(destination, capacity, &length, ' '))
                return;
            previousSpace = true;
            ++start;
            continue;
        }
        if (!AppendChar(destination, capacity, &length, *start++))
            return;
        previousSpace = false;
    }
    while (length > 0 && destination[length - 1] == ' ')
        destination[--length] = '\0';
}

bool CopyElementText(const char* start, const char* end, const char* name,
                     char* destination, int capacity)
{
    const char* contentStart;
    const char* contentEnd;
    if (!FindElement(start, end, name, &contentStart, &contentEnd))
        return false;
    CopyText(contentStart, contentEnd, destination, capacity);
    return true;
}

bool CopyAttribute(const char* opening, const char* openingEnd, const char* name,
                   char* destination, int capacity)
{
    const int nameLength = StringLength(name);
    const char* cursor = opening;
    destination[0] = '\0';
    while (cursor != NULL && cursor + nameLength + 2 < openingEnd)
    {
        if (EqualsIgnoreCase(cursor, name, nameLength) && cursor[nameLength] == '=')
        {
            const char quote = cursor[nameLength + 1];
            const char* value = cursor + nameLength + 2;
            const char* valueEnd = value;
            if (quote != '\'' && quote != '"')
                return false;
            while (valueEnd < openingEnd && *valueEnd != quote)
                ++valueEnd;
            if (valueEnd >= openingEnd)
                return false;
            CopyText(value, valueEnd, destination, capacity);
            return destination[0] != '\0';
        }
        ++cursor;
    }
    return false;
}

void ExtractItem(const char* start, const char* end, bool atom, ReviveFeedItem* item)
{
    const char* openingEnd = NULL;
    int index;
    for (index = 0; index < REVIVE_FEED_TITLE_CAPACITY; ++index)
        item->title[index] = '\0';
    for (index = 0; index < REVIVE_FEED_LINK_CAPACITY; ++index)
        item->link[index] = '\0';
    for (index = 0; index < REVIVE_FEED_DATE_CAPACITY; ++index)
        item->date[index] = '\0';
    CopyElementText(start, end, "title", item->title, sizeof(item->title));
    if (atom)
    {
        const char* opening = FindOpening(start, end, "link", &openingEnd);
        if (opening != NULL)
            CopyAttribute(opening, openingEnd, "href", item->link, sizeof(item->link));
        if (!CopyElementText(start, end, "published", item->date, sizeof(item->date)))
            CopyElementText(start, end, "updated", item->date, sizeof(item->date));
    }
    else
    {
        CopyElementText(start, end, "link", item->link, sizeof(item->link));
        if (!CopyElementText(start, end, "pubDate", item->date, sizeof(item->date)))
            CopyElementText(start, end, "date", item->date, sizeof(item->date));
    }
}
}

ReviveFeedResult ReviveFeedParse(const char* xml, ReviveFeedItem* items,
                                 int capacity, int* itemCount,
                                 bool* atomFormat)
{
    const char* cursor;
    const char* end;
    const char* itemName;
    bool atom;
    int count = 0;
    if (xml == NULL || items == NULL || capacity <= 0 || itemCount == NULL || atomFormat == NULL)
        return REVIVE_FEED_CONFIGURATION_ERROR;
    *itemCount = 0;
    *atomFormat = false;
    end = xml + StringLength(xml);
    atom = FindOpening(xml, end, "feed", NULL) != NULL;
    if (!atom && FindOpening(xml, end, "rss", NULL) == NULL)
        return REVIVE_FEED_FORMAT_ERROR;
    itemName = atom ? "entry" : "item";
    cursor = xml;
    while (count < capacity && count < REVIVE_FEED_MAX_ITEMS)
    {
        const char* contentStart = NULL;
        const char* closing;
        const char* opening = FindOpening(cursor, end, itemName, &contentStart);
        if (opening == NULL || contentStart == NULL)
            break;
        closing = FindClosing(contentStart, end, itemName);
        if (closing == NULL)
            return count > 0 ? REVIVE_FEED_OK : REVIVE_FEED_FORMAT_ERROR;
        ExtractItem(contentStart, closing, atom, &items[count]);
        if (items[count].title[0] != '\0')
            ++count;
        cursor = closing + 1;
    }
    *itemCount = count;
    *atomFormat = atom;
    return count > 0 ? REVIVE_FEED_OK : REVIVE_FEED_NO_ITEMS;
}
