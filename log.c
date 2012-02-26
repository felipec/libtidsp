/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define SYSLOG

#ifdef SYSLOG
#include <syslog.h>
#endif

#ifdef SYSLOG
static inline int
log_level_to_syslog(unsigned int level)
{
	switch (level) {
	case 0: return LOG_ERR;
	case 1: return LOG_WARNING;
	case 2:
	case 3: return LOG_INFO;
	default: return LOG_DEBUG;
	}
}
#endif

void pr_helper(unsigned int level,
		void *object,
		const char *file,
		const char *function,
		unsigned int line,
		const char *fmt,
		...)
{
	char *tmp;
	va_list args;

	va_start(args, fmt);

	if (vasprintf(&tmp, fmt, args) < 0)
		goto leave;

	if (level <= 1) {
#ifdef SYSLOG
		syslog(log_level_to_syslog(level), "%s", tmp);
#endif
		if (level == 0)
			fprintf(stderr, "%s: %s\n", function, tmp);
		else
			fprintf(stdout, "%s: %s\n", function, tmp);
	}
	else if (level == 2)
		fprintf(stdout, "%s:%s(%u): %s\n", file, function, line, tmp);
#if defined(DEVEL) || defined(DEBUG)
	else if (level == 3)
		fprintf(stdout, "%s: %s\n", function, tmp);
#endif
#ifdef DEBUG
	else if (level == 4)
		fprintf(stdout, "%s:%s(%u): %s\n", file, function, line, tmp);
#endif

	free(tmp);

leave:
	va_end(args);
}
