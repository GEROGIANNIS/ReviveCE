#ifndef REVIVECE_SMTP_H
#define REVIVECE_SMTP_H

#include "imap.h"

enum
{
    REVIVE_SMTP_RECIPIENT_CAPACITY = 256,
    REVIVE_SMTP_SUBJECT_CAPACITY = 256,
    REVIVE_SMTP_BODY_CAPACITY = 8192
};

enum ReviveSmtpResult
{
    // Keep SMTP failures distinct from the IMAP result values because both
    // are reported through the shared MAIL status row.
    REVIVE_SMTP_OK = 100,
    REVIVE_SMTP_CONFIGURATION_ERROR,
    REVIVE_SMTP_IO_ERROR,
    REVIVE_SMTP_GREETING_ERROR,
    REVIVE_SMTP_EHLO_ERROR,
    REVIVE_SMTP_AUTHENTICATION_ERROR,
    REVIVE_SMTP_SENDER_ERROR,
    REVIVE_SMTP_RECIPIENT_ERROR,
    REVIVE_SMTP_DATA_ERROR,
    REVIVE_SMTP_MESSAGE_ERROR,
    REVIVE_SMTP_RESPONSE_TOO_LARGE
};

struct ReviveSmtpMessage
{
    char recipient[REVIVE_SMTP_RECIPIENT_CAPACITY];
    char subject[REVIVE_SMTP_SUBJECT_CAPACITY];
    char body[REVIVE_SMTP_BODY_CAPACITY];
};

// Sends one UTF-8 plain-text message through an already verified implicit-TLS
// SMTP connection. Credentials and message text stay in memory and are never
// written to the diagnostic log.
ReviveSmtpResult ReviveSmtpSendMessage(ReviveTlsConnection* connection,
                                       const ReviveImapCredentials* credentials,
                                       const ReviveSmtpMessage* message);

void ReviveSmtpClearMessage(ReviveSmtpMessage* message);

#endif
