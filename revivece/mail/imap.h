#ifndef REVIVECE_IMAP_H
#define REVIVECE_IMAP_H

#include "../net/tls.h"

// M4 deliberately keeps account credentials in RAM only.  Persistent,
// encrypted account settings are a later storage milestone.
enum
{
    REVIVE_IMAP_EMAIL_CAPACITY = 128,
    REVIVE_IMAP_PASSWORD_CAPACITY = 128,
    REVIVE_IMAP_SENDER_CAPACITY = 160,
    REVIVE_IMAP_SUBJECT_CAPACITY = 192,
    REVIVE_IMAP_DATE_CAPACITY = 80,
    REVIVE_IMAP_INITIAL_BODY_BYTES = 8192,
    REVIVE_IMAP_BODY_PAGE_BYTES = 8192,
    REVIVE_IMAP_BODY_CAPACITY = 32768,
    REVIVE_IMAP_MAX_MESSAGES = 25
};

enum ReviveImapResult
{
    REVIVE_IMAP_OK = 0,
    REVIVE_IMAP_CONFIGURATION_ERROR,
    REVIVE_IMAP_IO_ERROR,
    REVIVE_IMAP_GREETING_ERROR,
    REVIVE_IMAP_AUTHENTICATION_ERROR,
    REVIVE_IMAP_SELECT_ERROR,
    REVIVE_IMAP_SEARCH_ERROR,
    REVIVE_IMAP_FETCH_ERROR,
    REVIVE_IMAP_RESPONSE_TOO_LARGE,
    REVIVE_IMAP_BODY_TOO_LARGE,
    REVIVE_IMAP_UNSUPPORTED_MESSAGE
};

struct ReviveImapCredentials
{
    char email[REVIVE_IMAP_EMAIL_CAPACITY];
    char appPassword[REVIVE_IMAP_PASSWORD_CAPACITY];
};

struct ReviveImapMessage
{
    unsigned long uid;
    bool unread;
    char sender[REVIVE_IMAP_SENDER_CAPACITY];
    char subject[REVIVE_IMAP_SUBJECT_CAPACITY];
    char date[REVIVE_IMAP_DATE_CAPACITY];
};

// Authenticates with an IMAP app password, selects INBOX, and obtains header
// fields for the newest messages.  It never writes credentials or mail data
// to the diagnostic log.  Call ReviveImapClearCredentials as soon as the
// caller no longer needs the supplied credentials.
ReviveImapResult ReviveImapFetchInbox(ReviveTlsConnection* connection,
                                      const ReviveImapCredentials* credentials,
                                      ReviveImapMessage* messages,
                                      int capacity,
                                      int* messageCount);

// Fetches the first requestedBodyBytes of one message's text section over a
// fresh, verified IMAP session.  The caller can request a larger prefix after
// a successful page to implement bounded "load more" behavior. Attachments
// are never downloaded.
ReviveImapResult ReviveImapFetchMessage(ReviveTlsConnection* connection,
                                        const ReviveImapCredentials* credentials,
                                        unsigned long uid,
                                        unsigned long requestedBodyBytes,
                                        ReviveImapMessage* header,
                                        char* body,
                                        int bodyCapacity,
                                        bool* usedHtmlFallback,
                                        bool* hasMore);

void ReviveImapClearCredentials(ReviveImapCredentials* credentials);

#endif
