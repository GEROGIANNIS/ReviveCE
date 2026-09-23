#ifndef REVIVECE_FEED_H
#define REVIVECE_FEED_H

enum
{
    REVIVE_FEED_MAX_ITEMS = 25,
    REVIVE_FEED_TITLE_CAPACITY = 192,
    REVIVE_FEED_LINK_CAPACITY = 384,
    REVIVE_FEED_DATE_CAPACITY = 80
};

enum ReviveFeedResult
{
    REVIVE_FEED_OK = 300,
    REVIVE_FEED_CONFIGURATION_ERROR,
    REVIVE_FEED_FORMAT_ERROR,
    REVIVE_FEED_NO_ITEMS
};

struct ReviveFeedItem
{
    char title[REVIVE_FEED_TITLE_CAPACITY];
    char link[REVIVE_FEED_LINK_CAPACITY];
    char date[REVIVE_FEED_DATE_CAPACITY];
};

// Extracts a bounded set of RSS 2.0 <item> or Atom 1.0 <entry> summaries.
// This is intentionally a compact display parser, not a general XML engine.
ReviveFeedResult ReviveFeedParse(const char* xml, ReviveFeedItem* items,
                                 int capacity, int* itemCount,
                                 bool* atomFormat);

#endif
