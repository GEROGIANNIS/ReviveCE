#ifndef REVIVECE_LOG_H
#define REVIVECE_LOG_H

void ReviveLog(const char* component, const char* message, int nativeError);
void ReviveLogEndpoint(const char* component, const char* message,
					   const char* host, unsigned short port);

#endif
