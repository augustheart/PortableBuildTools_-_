#pragma once
// libc-free base layer

#if !defined(appid)
 #define appid "0"
#endif

#define _CRT_DECLARE_NONSTDC_NAMES 0
#define NOMINMAX
#define UNICODE
#if defined(__cplusplus)
#define CINTERFACE
#endif
#include <windows.h>
#include <shlobj.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
#if defined(__cplusplus)
#include <string_view>
constexpr std::string_view appid_name = appid;
#endif

#pragma region types

typedef unsigned char n8;
typedef unsigned short n16;
typedef unsigned int n32;
typedef unsigned long long n64;
typedef char i8;
typedef short i16;
typedef int i32;
typedef long long i64;
typedef n8 b8;
typedef n16 b16;
typedef n32 b32;
typedef n64 b64;
typedef n8 byte;
typedef n32 uchar;
typedef n64 nat;

#pragma endregion

#pragma region constants

const n8 n8_min = 0x00;
const n8 n8_max = 0xFF;
const n16 n16_min = 0x0000;
const n16 n16_max = 0xFFFF;
const n32 n32_min = 0x00000000;
const n32 n32_max = 0xFFFFFFFF;
const n64 n64_min = 0x0000000000000000;
const n64 n64_max = 0xFFFFFFFFFFFFFFFF;
const i8 i8_min = 0x80;
const i8 i8_max = 0x7F;
const i16 i16_min = 0x8000;
const i16 i16_max = 0x7FFF;
const i32 i32_min = 0x80000000;
const i32 i32_max = 0x7FFFFFFF;
const i64 i64_min = 0x8000000000000000;
const i64 i64_max = 0x7FFFFFFFFFFFFFFF;

enum
{
 mem_byte = 1ull,
 mem_kilobyte = 1024 * mem_byte,
 mem_megabyte = 1024 * mem_kilobyte,
 mem_gigabyte = 1024 * mem_megabyte,
 mem_page_size = 4 * mem_kilobyte
};

#pragma endregion

#pragma region macros

#if defined(__cplusplus)
#define null nullptr
#else
#define null ((void*)0)
#endif
#define cast(t, m) \
 ((t)(m))
#define bitcast(t, m) \
 *cast(t*, &(m))
#define countof(a) \
 (sizeof(a) / sizeof((a)[0]))
#define min(x, y) \
 ((x) < (y) ? (x) : (y))
#define max(x, y) \
 ((x) > (y) ? (x) : (y))
#define clamp_top(x, y) \
 min(x, y)
#define clamp_bot(x, y) \
 max(x, y)
#define clamp(x, low, high) \
 clamp_top(clamp_bot(x, low), high)
#define slice(type, name) \
 type name[], nat name##_count
#define string_slice(s) \
 s, countof(s) - 1
#define vcall(v, c, ...) \
 (v)->c(v, ##__VA_ARGS__)

#pragma endregion


struct file
{
	void*	handle;
	n64	size;
	n64	pos;
};

struct folder
{
	HANDLE handle;
	WIN32_FIND_DATA data;
	char file_name[MAX_PATH * 3];
};

typedef void (*thread_callback)(void*);

#define MAX_CMDLINE_LEN_UTF8 (32767 * 3)
#define nanosecond 1ull
#define microsecond (1000 * nanosecond)
#define millisecond (1000 * microsecond)
#define second (1000 * millisecond)
#define minute (60 * second)
#define hour (60 * minute)
extern nat args_count;
extern const char* args[MAX_CMDLINE_LEN_UTF8 / 3];
extern bool invoked_from_console;
extern HINSTANCE instance;
extern bool com_initialized;
void start(void);
void check_privilege(void);

#define string_copy(b, s) \
	_string_copy(b, countof(b), s)
#define string_append(b, s) \
	_string_append(b, countof(b), s)
#define string_join_buf(b, ...) \
	__string_join(b, countof(b), {__VA_ARGS__})
#define string_join(b, ...) \
	(memset((b), 0, sizeof(b)), __string_join(b, countof(b), {__VA_ARGS__}))
#define _string_join(b, c, ...) \
	__string_join(b, c, {__VA_ARGS__})
#define string_is(s, x) \
	(strcmp((s), (x)) == 0)
#define itos(x) \
	_ui64toa((n64)(x), (char[66]){0}, 10)
#define console_read(b) \
	_console_read(b, countof(b))
#define echo(...) \
	_echo({__VA_ARGS__})
#define echoln(...) \
	_echo({__VA_ARGS__, "\n"})

#pragma region declarations
[[noreturn]] void app_exit(int exit_code);
nat _string_copy(slice(char, buf), const char* s);
nat _string_append(slice(char, buf), const char* s);
char* __string_join(char* buf, nat buf_count, std::initializer_list<const char*> strings);
bool string_starts_with(const char* s, const char* x);
bool string_ends_with(const char* s, const char* x);
bool string_trim_left(char* s, const char* x);
bool string_trim_right(char* s, const char* x);
nat _console_read(slice(char, buf));
void console_write(const char* s);
void _echo(std::initializer_list<const char*> strings);
bool to_wide(const char* path, WCHAR* wpath, int wpath_count);
struct file file_open(const char* path, bool write_access);
void file_close(struct file* file);
n32 file_read(struct file* file, void* buf, n32 buf_count);
n32 file_write(struct file* file, const void* data, n32 data_count);
void file_seek(struct file* file, n64 pos);
bool file_delete(const char* path);
void file_copy(const char* out_path, const char* in_path);
bool folder_exists(const char* path);
struct folder folder_open(const char* path);
void folder_close(struct folder* folder);
bool folder_next(struct folder* folder);
bool folder_create(const char* path);
bool folder_delete(const char* path);
void sleep(n64 duration);
DWORD WINAPI _thread(void* data);
HANDLE thread_start(thread_callback callback, void* data);
void _console_parse_arguments(void);
LRESULT CALLBACK _service_proc(HWND service, UINT message, WPARAM wparam, LPARAM lparam);
void message_loop(void);
void register_service_class(WNDPROC proc, const WCHAR* name);
void _main_thread(HWND service);
extern "C" void _start(void);
HWND find_service(bool other_process, DWORD* pid_out);
HWND find_app_service(void);
void appid_instance_guard(void);
#pragma endregion
