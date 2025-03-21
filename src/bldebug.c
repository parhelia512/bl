#include "bldebug.h"
#include "common.h"
#include <stdarg.h>

#if BL_PLATFORM_WIN
// clang-format off
#include <Windows.h>
#include <DbgHelp.h>
// clang-format on
#endif

#if BL_PLATFORM_MACOS || BL_PLATFORM_LINUX
#include <execinfo.h>
#endif

#define MAX_LOG_MSG_SIZE 2048

void log_impl(log_msg_kind_t t, const char *file, s32 line, const char *msg, ...) {
	char    buffer[MAX_LOG_MSG_SIZE];
	va_list args;
	va_start(args, msg);
	vsnprintf(buffer, MAX_LOG_MSG_SIZE, msg, args);

	switch (t) {
	case LOG_ASSERT:
		color_print(stderr, BL_RED, "assert [%s:%d]: %s\n", file, line, buffer);
		break;
	case LOG_ABORT_ISSUE:
		color_print(stderr, BL_RED, "internal compiler error [%s:%d]: %s\n", file, line, buffer);
		break;
	case LOG_ABORT:
		color_print(stderr,
		            BL_RED,
		            "internal compiler error [%s:%d]: %s (Please report the issue on "
		            "'https://github.com/biscuitlang/bl/issues')\n",
		            file,
		            line,
		            buffer);
		break;
	case LOG_WARNING:
		color_print(stderr, BL_YELLOW, "warning [%s:%d]: %s\n", file, line, buffer);
		break;
	case LOG_MSG:
		fprintf(stdout, "log [%s:%d]: %s\n", file, line, buffer);
		break;
	}

	va_end(args);
}

void print_trace_impl(void) {
#if BL_PLATFORM_MACOS || BL_PLATFORM_LINUX
	void  *tmp[128];
	usize  size    = backtrace(tmp, static_arrlenu(tmp));
	char **strings = backtrace_symbols(tmp, size);

	fprintf(stderr, "Obtained stack trace:\n");
	for (usize i = 1; i < size; i++)
		fprintf(stderr, "  %s\n", strings[i]);

	free(strings);
#elif BL_PLATFORM_WIN
	void  *stack[128];
	HANDLE process = GetCurrentProcess();

	SymSetOptions(SYMOPT_LOAD_LINES);
	SymInitialize(process, NULL, TRUE);

	unsigned short  frame_count = CaptureStackBackTrace(0, static_arrlenu(stack), stack, NULL);
	char            symbol_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
	PSYMBOL_INFO    symbol           = (PSYMBOL_INFO)symbol_buffer;
	IMAGEHLP_LINE64 line             = {0};
	DWORD           displacementLine = 0;
	symbol->MaxNameLen               = MAX_SYM_NAME;
	symbol->SizeOfStruct             = sizeof(SYMBOL_INFO);

	fprintf(stderr, "Obtained stack trace:\n");
	for (s32 i = 1; i < frame_count; i++) {
		SymGetLineFromAddr64(process, (DWORD64)(stack[i]), &displacementLine, &line);
		SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
		fprintf(stderr, "  %s:%lu: %s\n", line.FileName, line.LineNumber, symbol->Name);
	}
	SymCleanup(process);
#endif
}
