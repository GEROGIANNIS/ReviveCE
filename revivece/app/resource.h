#ifndef REVIVECE_RESOURCE_H
#define REVIVECE_RESOURCE_H

#define IDS_APP_TITLE                   1
#define IDR_MAINFRAME                   128

#define IDC_STATUS_DNS                  1001
#define IDC_STATUS_TCP                  1002
#define IDC_STATUS_TLS                  1003
#define IDC_STATUS_CERTIFICATE          1004
#define IDC_STATUS_HOSTNAME             1005
#define IDC_STATUS_IMAP                 1006
#define IDC_RUN_TEST                    1010
#define IDC_EMAIL                       1011
#define IDC_APP_PASSWORD                1012
#define IDC_REFRESH_INBOX               1013
#define IDC_INBOX                       1014
#define IDC_OPEN_MESSAGE                 1015
#define IDC_COMPOSE                      1016

#define WM_REVIVE_STATUS                (WM_APP + 1)
#define WM_REVIVE_TEST_COMPLETE         (WM_APP + 2)
#define WM_REVIVE_INBOX_MESSAGE          (WM_APP + 3)
#define WM_REVIVE_MESSAGE_BODY            (WM_APP + 4)
#define WM_REVIVE_SEND_RESULT             (WM_APP + 5)

#endif
