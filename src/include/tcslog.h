
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2004-2006
 *
 */


#ifndef _TCSLOG_H_
#define _TCSLOG_H_

#include <stdio.h>
#include <syslog.h>

extern int foreground;

/* log to syslog -- change your syslog destination here */
#define TSS_SYSLOG_LVL	LOG_LOCAL5

#define LogMessage(dest, priority, layer, fmt, ...) \
        do { \
		if (foreground) { \
			fprintf(dest, "%s " fmt "\n", layer, ## __VA_ARGS__); \
		} else { \
			openlog(layer, LOG_NDELAY|LOG_PID, TSS_SYSLOG_LVL); \
			syslog(priority, "TrouSerS " fmt "\n", ## __VA_ARGS__); \
		} \
        } while (0)

/* Debug logging */
#ifdef TSS_DEBUG
#define LogDebug(fmt, ...)	LogMessage(stdout, LOG_DEBUG, APPID, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LogDebugFn(fmt, ...)	LogMessage(stdout, LOG_DEBUG, APPID, "%s:%d %s: " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define LogBlob(sz,blb)		LogBlobData(APPID, sz, blb)
#define LogDebugKey(k) \
	do { \
		LogDebugFn("Version: %hhu.%hhu.%hhu.%hhu", \
			   k.ver.major, k.ver.minor, \
			   k.ver.revMajor, k.ver.revMinor); \
		LogDebugFn("keyUsage: 0x%hx", k.keyUsage); \
		LogDebugFn("keyFlags: 0x%x", k.keyFlags); \
		LogDebugFn("authDatausage: %hhu", k.authDataUsage); \
		LogDebugFn("pcrInfosize: %u", k.PCRInfoSize); \
		LogDebugFn("encDataSize: %u", k.encSize); \
	} while (0)
#define LogDebugUnrollKey(b) \
	do { \
			TCPA_KEY tmpkey; \
			UINT16 offset = 0; \
			if (!UnloadBlob_KEY(&offset, b, &tmpkey)) { \
				LogDebugKey(tmpkey); \
				destroy_key_refs(&tmpkey); \
			} else { \
				LogDebugFn("*** ERROR UNLOADING DEBUGGING KEY BLOB ***"); \
			} \
	} while (0)

#define LogError(fmt, ...)	LogMessage(stderr, LOG_ERR, APPID, "ERROR: %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LogWarn(fmt, ...)	LogMessage(stdout, LOG_WARNING, APPID, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LogInfo(fmt, ...)	LogMessage(stdout, LOG_INFO, APPID, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define LogDebug(fmt, ...)
#define LogDebugFn(fmt, ...)
#define LogBlob(sz,blb)
#define LogDebugKey(s)
#define LogDebugUnrollKey(b)

/* Error logging */
#define LogError(fmt, ...)	LogMessage(stderr, LOG_ERR, APPID, "ERROR: " fmt, ##__VA_ARGS__)

/* Warn logging */
#define LogWarn(fmt, ...)	LogMessage(stdout, LOG_WARNING, APPID, fmt, ##__VA_ARGS__)

/* Info Logging */
#define LogInfo(fmt, ...)	LogMessage(stdout, LOG_INFO, APPID, fmt, ##__VA_ARGS__)
#endif

void LogBlobData(char *appid, unsigned long sizeOfBlob, unsigned char *blob);

#endif
