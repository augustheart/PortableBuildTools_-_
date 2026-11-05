#include "app_config.h"
#include "base.h"
#include <atomic>
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_MALLOC
#include "miniz/miniz.h"
#include "dialog.h"
#include <shobjidl.h>
#include <shlwapi.h>
#include <wininet.h>

// Defined locally instead of linking mingw's uuid static library: its uuid.o
// contains every GUID and pulls ~64 KB of unused data into .rdata.
static const GUID pbt_CLSID_FileOpenDialog = {0xdc1c5a9c, 0xe88a, 0x4dde, {0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7}};
static const GUID pbt_IID_IFileOpenDialog = {0xd57c7288, 0xd4ad, 0x4768, {0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60}};
#define CLSID_FileOpenDialog pbt_CLSID_FileOpenDialog
#define IID_IFileOpenDialog pbt_IID_IFileOpenDialog

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "wininet.lib")
#define utf8_to_utf16(str, str_count, wbuf, wbuf_count) \
 MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, str_count, wbuf, wbuf_count)
#define utf16_to_utf8(wstr, wstr_count, buf, buf_count) \
 WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr, wstr_count, buf, buf_count, null, null)
#define MAX_ENV_LEN 32767
#define MAX_CMDLINE_LEN 32767

#pragma region strings

void string_lower(char* s)
{
 while (*s != 0)
 {
  char lower = *s + ('a' - 'A');
  *s = ((*s >= 'A') & (*s <= 'Z')) ? lower : *s;
  s++;
 }
}

bool string_in_list(const char* s, const char* const* list, nat count)
{
 bool found = false;
 for (nat i = 0; i < count; i++)
  found = string_is(s, list[i]) ? true : found;
 return (found);
}

bool string_in_versions(const char* s, const char* versions, nat count, nat stride)
{
 bool found = false;
 for (nat i = 0; i < count; i++)
  found = string_is(s, versions + (i * stride)) ? true : found;
 return (found);
}

#pragma endregion

#pragma region json parser

typedef uchar (*json_peek_proc)(void* context, bool advance);
typedef struct {
 json_peek_proc peek;
 void* context;
} json_parser;

json_parser json_parser_begin(json_peek_proc peek, void* context)
{
 json_parser p;
 p.peek = peek;
 p.context = context;
 return (p);
}

typedef enum {
 json_none,
 json_word,
 json_number,
 json_string,
 json_object,
 json_array,
} json_value;

json_value json_value_skip(json_parser* p);

json_value json_value_next(json_parser* p)
{
 json_value out = json_none;
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  bool skip = ((symbol == ' ') | (symbol == '\t') | (symbol == '\n') | (symbol == '\r') | (symbol == ','));
  out = (((symbol >= 'a') & (symbol <= 'z')) | ((symbol >= 'A') & (symbol <= 'Z'))) ? json_word : out;
  out = (((symbol >= '0') & (symbol <= '9')) | (symbol == '-') | (symbol == '.')) ? json_number : out;
  out = (symbol == '"') ? json_string : out;
  out = (symbol == '{') ? json_object : out;
  out = (symbol == '[') ? json_array : out;
  if (skip)
   continue;
  break;
 }
 return (out);
}

void json_word_skip(json_parser* p)
{
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if (((symbol >= 'a') & (symbol <= 'z')) | ((symbol >= 'A') & (symbol <= 'Z')))
   continue;
  break;
 }
}

void json_number_skip(json_parser* p)
{
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if (((symbol >= '0') & (symbol <= '9')) | (symbol == '-') | (symbol == '.'))
   continue;
  break;
 }
}

#define json_string_extract(p, b) \
 _json_string_extract(p, b, countof(b))
void _json_string_extract(json_parser* p, slice(char, buf))
{
 buf_count--;
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if (symbol == '"')
  {
   p->peek(p->context, true);
   break;
  }
 }
 i64 pos = 0;
 bool buf_filled = false;
 bool escaping = false;
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if ((symbol == '\\') & !escaping)
  {
   escaping = true;
   continue;
  }
  if ((symbol == '"') & !escaping)
  {
   p->peek(p->context, true);
   break;
  }
  escaping = false;
  buf_filled = (pos >= cast(i64, buf_count)) ? true : buf_filled;
  if (!buf_filled)
  {
   buf[pos] = symbol;
   pos++;
  }
 }
 buf[pos] = 0;
}

bool json_string_is(json_parser* p, const char* str)
{
 i64 str_count = strlen(str);
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if (symbol == '"')
  {
   p->peek(p->context, true);
   break;
  }
 }
 i64 pos = 0;
 bool match = true;
 bool escaping = false;
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if (symbol == '\\')
  {
   if (!escaping)
   {
    escaping = true;
    continue;
   }
  }
  if (symbol == '"')
  {
   if (!escaping)
   {
    p->peek(p->context, true);
    break;
   }
  }
  escaping = false;
  if (pos < str_count)
   match = (str[pos] != symbol) ? false : match;
  pos++;
 }
 return ((pos == str_count) & (match));
}

void json_string_skip(json_parser* p)
{
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if (symbol == '"')
  {
   p->peek(p->context, true);
   break;
  }
 }
 bool escaping = false;
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if (symbol == '\\')
  {
   if (!escaping)
   {
    escaping = true;
    continue;
   }
  }
  if (symbol == '"')
  {
   if (!escaping)
   {
    p->peek(p->context, true);
    break;
   }
  }
  escaping = false;
 }
}

bool json_array_next(json_parser* p)
{
 bool match = false;
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if ((symbol == '[') | (symbol == ' ') | (symbol == '\t') | (symbol == '\n') | (symbol == '\r') | (symbol == ','))
  {
   continue;
  }
  if (symbol == ']')
  {
   p->peek(p->context, true);
   break;
  }
  match = true;
  break;
 }
 return (match);
}

void json_array_skip(json_parser* p)
{
 while (json_array_next(p))
  json_value_skip(p);
}

bool json_object_next(json_parser* p)
{
 bool match = false;
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if ((symbol == '{') | (symbol == ' ') | (symbol == '\t') | (symbol == '\n') | (symbol == '\r') | (symbol == ','))
   continue;
  if (symbol == '}')
  {
   p->peek(p->context, true);
   break;
  }
  if (symbol == '"')
   match = true;
  break;
 }
 return (match);
}

#define json_object_key_extract(p, b) \
 _json_object_key_extract(p, b, countof(b))
void _json_object_key_extract(json_parser* p, slice(char, buf))
{
 _json_string_extract(p, buf, buf_count);
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if (symbol == ':')
  {
   p->peek(p->context, true);
   break;
  }
 }
}

void json_object_key_skip(json_parser* p)
{
 json_string_skip(p);
 for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
 {
  char symbol = *cast(char*, &c8);
  if (symbol == ':')
  {
   p->peek(p->context, true);
   break;
  }
 }
}

bool json_object_key_find(json_parser* p, const char* key)
{
 bool match = false;
 while (json_object_next(p) & !match)
 {
  match = json_string_is(p, key);
  for (uchar c8 = p->peek(p->context, false); c8 != 0; c8 = p->peek(p->context, true))
  {
   char symbol = *cast(char*, &c8);
   if (symbol == ':')
   {
    p->peek(p->context, true);
    break;
   }
  }
  if (!match)
   json_value_skip(p);
 }
 return (match);
}

void json_object_skip(json_parser* p)
{
 while (json_object_next(p))
 {
  json_object_key_skip(p);
  json_value_skip(p);
 }
}

json_value json_value_skip(json_parser* p)
{
 json_value v = json_value_next(p);
 if (v == json_word)
  json_word_skip(p);
 if (v == json_number)
  json_number_skip(p);
 if (v == json_string)
  json_string_skip(p);
 if (v == json_object)
  json_object_skip(p);
 if (v == json_array)
  json_array_skip(p);
 return (v);
}

#pragma endregion

#pragma region json file parser

typedef struct {
 char cache[mem_page_size];
 uchar c8;
 i64 cache_size;
 i64 cache_pos;
 struct file f;
} json_context;

uchar json_file_peek(void* context, bool advance)
{
 json_context* jc = cast(json_context*, context);
 // advance if beginning of the file, or if asked to
 if ((!advance) & (jc->f.pos != 0))
  return (jc->c8);
 // if beginning of the file, reset all values
 if (jc->f.pos == 0)
 {
  jc->c8 = 0;
  jc->cache_size = 0;
  jc->cache_pos = 0;
 }
 // chunk end
 if (jc->cache_pos >= jc->cache_size)
 {
  jc->cache_size = file_read(&jc->f, string_slice(jc->cache));
  jc->cache_pos = 0;
 }
 // file end
 if (jc->cache_size <= 0)
 {
  jc->cache_pos = 0;
  jc->c8 = 0;
  return (jc->c8);
 }
 jc->c8 = cast(uchar, jc->cache[jc->cache_pos]);
 jc->cache_pos++;
 return (jc->c8);
}

void json_file_context_restore(json_context* jc, json_context context)
{
 *jc = context;
 file_seek(&jc->f, jc->f.pos);
}

#pragma endregion

#pragma region miniz helpers

void* miniz_malloc(void* opaque, size_t items, size_t size)
{
 return HeapAlloc(GetProcessHeap(), 0, items * size);
}

void miniz_free(void* opaque, void* address)
{
 HeapFree(GetProcessHeap(), 0, address);
}

void* miniz_realloc(void* opaque, void* address, size_t items, size_t size)
{
 if (!address)
 {
  return HeapAlloc(GetProcessHeap(), 0, items * size);
 }
 else if ((items == 0) | (size == 0))
 {
  HeapFree(GetProcessHeap(), 0, address);
  return (null);
 }
 else
 {
  return HeapReAlloc(GetProcessHeap(), 0, address, items * size);
 }
}

size_t miniz_file_read(void* context, mz_uint64 file_ofs, void* buf, size_t buf_count)
{
 struct file* file = cast(struct file*, context);
 file_seek(file, file_ofs);
 return file_read(file, buf, buf_count);
}

size_t mz_file_write(void* context, mz_uint64 file_ofs, const void* buf, size_t buf_count)
{
 struct file* file = cast(struct file*, context);
 return (file_write(file, buf, (n32)buf_count));
}

#pragma endregion

enum download_status
{
 download_status_unknown,
 download_status_downloading,
 download_status_finished,
 download_status_error_open_url,
 download_status_error_read_file,
 download_status_error_open_file,
 download_status_error_write_file,
};

struct download_service
{
 HWND handle;
 HANDLE thread;
 std::atomic<nat> content_size;
 std::atomic<nat> downloaded_size;
 std::atomic<n32> last_error;
 std::atomic<enum download_status> status;
};
struct download_service download_service;
HINTERNET internet_session;

static inline void set_download_status(enum download_status status)
{
 download_service.status.store(status, std::memory_order_release);
}
static inline enum download_status get_download_status(void)
{
 return download_service.status.load(std::memory_order_acquire);
}

struct download_info
{
 const char* file_url;
 const char* file_path;
};

const char* release_vsmanifest_url = "https://aka.ms/vs/17/release/channel";
const char* preview_mode_vsmanifest_url = "https://aka.ms/vs/17/pre/channel";

const char* targets[] = {"x64", "x86", "arm", "arm64",};
const char* hosts[] = {"x64", "x86", "arm64"};

enum class EnvMode : unsigned char
{
 none,
 user,
 global,
};
const size_t VERSION_COUNT = 32;
const size_t VERSION_LENGTH = 16;
bool install_start;
bool subprocess_mode;
bool gui_mode;
bool preview_mode;
bool atlmfc;
bool mbcs;
bool spectre;
EnvMode env_mode;

char msvc_version[VERSION_COUNT];

char sdk_version[VERSION_COUNT];
char target_arch[VERSION_COUNT] = "x64";
char host_arch[VERSION_COUNT] = "x64";
char install_path[(MAX_PATH * 3) + 8] = "C:\\BuildTools"; // +8 extra padding just in case
bool license_accepted;
bool is_admin;
char temp_path[MAX_PATH * 3];

char release_manifest_url[L_MAX_URL_LENGTH * 3];
char release_license_url[L_MAX_URL_LENGTH * 3];
char preview_mode_manifest_url[L_MAX_URL_LENGTH * 3];
char preview_mode_license_url[L_MAX_URL_LENGTH * 3];

// 14/32
char release_msvc_versions[VERSION_COUNT][VERSION_LENGTH];
i64 release_msvc_versions_count;
// 14/32
char preview_mode_msvc_versions[VERSION_COUNT][VERSION_LENGTH];
i64 preview_mode_msvc_versions_count;

// 6/16
char release_sdk_versions[VERSION_COUNT][VERSION_LENGTH];
i64 release_sdk_versions_count;
// 6/16
char preview_mode_sdk_versions[VERSION_COUNT][VERSION_LENGTH];
i64 preview_mode_sdk_versions_count;

void env_set(HKEY location, LPCWSTR subkey, LPCWSTR key, const char* env)
{
 WCHAR wenv[MAX_ENV_LEN];
 wenv[0] = 0;
 int n = utf8_to_utf16(env, -1, wenv, countof(wenv));
 if (n <= 0)
  return;
 RegSetKeyValue(location, subkey, key, REG_SZ, wenv, n * sizeof(WCHAR));
}

bool is_folder_empty(const char* path)
{
 struct folder folder = folder_open(path);
 bool has_files = folder_next(&folder);
 folder_close(&folder);
 return (!has_files);
}

LRESULT CALLBACK download_service_proc(HWND service, UINT message, WPARAM wparam, LPARAM lparam)
{
 if (message == WM_APP)
 {
  struct download_info* info = cast(struct download_info*, wparam);
  WCHAR wurl[L_MAX_URL_LENGTH];
  wurl[0] = 0;
  if (utf8_to_utf16(info->file_url, -1, wurl, countof(wurl)) <= 0)
  {
   download_service.last_error.store(0, std::memory_order_relaxed);
   set_download_status(download_status_error_open_url);
   return (1);
  }
  DWORD flags = INTERNET_FLAG_DONT_CACHE;
  if (string_starts_with(info->file_url, "https://"))
   flags |= INTERNET_FLAG_SECURE;
  HINTERNET connection = InternetOpenUrl(internet_session, wurl, null, 0, flags, 0);
  if (!connection)
  {
   download_service.last_error.store(GetLastError(), std::memory_order_relaxed);
   set_download_status(download_status_error_open_url);
   return (1);
  }
  DWORD content_size = 0;
  DWORD content_size_arg = sizeof(DWORD);
  if (!HttpQueryInfo(connection, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &content_size, &content_size_arg, null))
   content_size = 0;
  download_service.content_size.store(content_size, std::memory_order_relaxed);
  set_download_status(download_status_downloading);
  struct file file = {0};
  DWORD n = 0;
  char chunk[16 * mem_page_size];
  bool success = false;
  file = file_open(info->file_path, true);
  if (!file.handle)
  {
   download_service.last_error.store(GetLastError(), std::memory_order_relaxed);
   set_download_status(download_status_error_open_file);
   goto cleanup;
  }
  while (true)
  {
   if (!InternetReadFile(connection, chunk, countof(chunk), &n))
   {
    download_service.last_error.store(GetLastError(), std::memory_order_relaxed);
    set_download_status(download_status_error_read_file);
    goto cleanup;
   }
   if (n == 0)
    break;
   if (file_write(&file, chunk, n) != n)
   {
    download_service.last_error.store(GetLastError(), std::memory_order_relaxed);
    set_download_status(download_status_error_write_file);
    goto cleanup;
   }
   download_service.downloaded_size.fetch_add(n, std::memory_order_relaxed);
  }
  success = true;
cleanup:
  file_close(&file);
  InternetCloseHandle(connection);
  if (success)
   set_download_status(download_status_finished);
  return (success ? 0 : 1);
 }

 if (message == WM_DESTROY)
 {
  PostQuitMessage(0);
  return (0);
 }

 return DefWindowProc(service, message, wparam, lparam);
}

void download_service_thread(HANDLE signal)
{
 download_service.handle = CreateWindow(TEXT(appid) L":download_service", null, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, HWND_MESSAGE, null, instance, null);
 SetEvent(signal);
 if (!download_service.handle)
 {
  if (internet_session != null)
  {
   InternetCloseHandle(internet_session);
   internet_session = null;
  }
  return;
 }
 message_loop();
 if (internet_session != null)
 {
  InternetCloseHandle(internet_session);
  internet_session = null;
 }
}

bool download_service_start(void)
{
 if (download_service.thread || download_service.handle)
  return (false);
 internet_session = InternetOpen(L"PortableBuildTools/2.10", INTERNET_OPEN_TYPE_PRECONFIG, null, null, 0);
 if (!internet_session)
  return (false);
 HANDLE signal = CreateEvent(null, false, false, null);
 if (!signal)
 {
  InternetCloseHandle(internet_session);
  internet_session = null;
  return (false);
 }
 HANDLE thread = thread_start(download_service_thread, signal);
 if (!thread)
 {
  CloseHandle(signal);
  InternetCloseHandle(internet_session);
  internet_session = null;
  return (false);
 }
 download_service.thread = thread;
 WaitForSingleObject(signal, INFINITE);
 CloseHandle(signal);
 if (!download_service.handle)
 {
  WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
  download_service.thread = null;
  if (internet_session != null)
  {
   InternetCloseHandle(internet_session);
   internet_session = null;
  }
  return (false);
 }
 return (true);
}

void download_service_stop(void)
{
 if (download_service.handle)
 {
  SendMessage(download_service.handle, WM_CLOSE, 0, 0);
  download_service.handle = null;
 }
 if (download_service.thread)
 {
  WaitForSingleObject(download_service.thread, INFINITE);
  CloseHandle(download_service.thread);
  download_service.thread = null;
 }
 if (internet_session != null)
 {
  InternetCloseHandle(internet_session);
  internet_session = null;
 }
}

void download_file(const char* file_url, const char* file_path, const char* display_name)
{
 struct download_info info = {0};
 info.file_url = file_url;
 info.file_path = file_path;
 while (true)
 {
  nat old_p = 0;
  nat old_kb = 0;
  download_service.content_size.store(0, std::memory_order_relaxed);
  download_service.downloaded_size.store(0, std::memory_order_relaxed);
  download_service.last_error.store(0, std::memory_order_relaxed);
  set_download_status(download_status_unknown);
  echo("[?] ", display_name);
  PostMessage(download_service.handle, WM_APP, cast(WPARAM, &info), 0);
  while (true)
  {
   enum download_status status = get_download_status();
   if (status >= download_status_downloading)
   {
    nat content_size = download_service.content_size.load(std::memory_order_relaxed);
    nat downloaded_size = download_service.downloaded_size.load(std::memory_order_relaxed);
    if (content_size > 0)
    {
     nat p = clamp_top((downloaded_size * 100) / content_size, 100);
     if (p != old_p)
      echo("\r[", itos(downloaded_size), "/",
          itos(content_size), ",", itos(p), "%] ", display_name);
     old_p = p;
    }
    else
    {
     nat kb = (downloaded_size / mem_kilobyte);
     if (kb != old_kb)
      echo("\r[", itos(kb), "kb] ", display_name);
     old_kb = kb;
    }
   }
   if (status >= download_status_finished)
   {
    echoln("");
    break;
   }
   sleep(1 * millisecond);
  }
  if (get_download_status() == download_status_finished)
   break;
  sleep(3 * second);
  enum download_status status = get_download_status();
  n32 error = download_service.last_error.load(std::memory_order_relaxed);
  if (status == download_status_error_open_url)
   echoln("[ERROR] InternetOpenUrl failed, error ", itos(error), ", retrying...");
  if (status == download_status_error_read_file)
   echoln("[ERROR] InternetReadFile failed, error ", itos(error), ", retrying...");
  if (status == download_status_error_open_file)
   echoln("[ERROR] File open failed, error ", itos(error), ", path: ", file_path, ", retrying...");
  if (status == download_status_error_write_file)
   echoln("[ERROR] File write failed, error ", itos(error), ", path: ", file_path, ", retrying...");
 }
}

#define hope(cond, err) \
 do { if (!cond) die(err, TEXT(err)); } while(0)
void die(const char* err, LPCWSTR werr)
{
 download_service_stop();
 if (temp_path[0] != 0)
 {
  folder_delete(temp_path);
  temp_path[0] = 0;
 }
 if (gui_mode || subprocess_mode)
 {
  MessageBox(GetConsoleWindow(), werr, null, MB_TOPMOST | MB_ICONERROR);
 }
 else
 {
  echoln(err);
 }
 app_exit(1);
}

void dialog_fill_versions(HWND dlg, int id, const char* versions, i64 count, nat stride, char* dest, nat dest_size)
{
 HWND item = GetDlgItem(dlg, id);
 SendMessage(item, CB_RESETCONTENT, 0, 0);
 for (i64 i = count - 1; i >= 0; i--)
 {
  WCHAR wver[64];
  wver[0] = 0;
  if (utf8_to_utf16(versions + (i * stride), -1, wver, countof(wver)) <= 0)
   continue;
  SendMessage(item, CB_ADDSTRING, 0, cast(LPARAM, wver));
 }
 SendMessage(item, CB_SETCURSEL, 0, 0);
 if (count > 0)
  _string_copy(dest, dest_size, versions + ((count - 1) * stride));
 else
  dest[0] = 0;
}

void dialog_refill(HWND dlg)
{
 // iterating backwards, assuming versions are sorted backwards in the JSON
 char (*msvc_versions)[16] = preview_mode ? preview_mode_msvc_versions : release_msvc_versions;
 i64 msvc_versions_count = preview_mode ? preview_mode_msvc_versions_count : release_msvc_versions_count;
 dialog_fill_versions(dlg, ID_COMBO_MSVC, cast(const char*, msvc_versions), msvc_versions_count, sizeof(msvc_versions[0]), msvc_version, countof(msvc_version));
 char (*sdk_versions)[16] = preview_mode ? preview_mode_sdk_versions : release_sdk_versions;
 i64 sdk_versions_count = preview_mode ? preview_mode_sdk_versions_count : release_sdk_versions_count;
 dialog_fill_versions(dlg, ID_COMBO_SDK, cast(const char*, sdk_versions), sdk_versions_count, sizeof(sdk_versions[0]), sdk_version, countof(sdk_version));
 {
  char* license_url = preview_mode ? preview_mode_license_url : release_license_url;
  char link_url[64 + L_MAX_URL_LENGTH * 3];
  string_join(link_url, "I accept the <a href=\"", license_url, "\">License Agreement</a>");
  WCHAR wlink_url[64 + L_MAX_URL_LENGTH];
  wlink_url[0] = 0;
  if (utf8_to_utf16(link_url, -1, wlink_url, countof(wlink_url)) <= 0)
   wlink_url[0] = 0;
  SetDlgItemText(dlg, ID_SYSLINK_LICENSE, wlink_url);
 }
}

// update install_path, add shield to install button if needed
void dialog_path_update(HWND dlg)
{
 WCHAR wpath[MAX_PATH] = {0};
 GetDlgItemText(dlg, ID_EDIT_PATH, wpath, MAX_PATH);
 utf16_to_utf8(wpath, -1, install_path, countof(install_path));
 // check if path is accessible by creating temporary path/~ file
 if (folder_exists(install_path))
 {
  nat n = 0;
  while (wpath[n] != 0)
   n++;
  wpath[n] = L'\\';
  n++;
  wpath[n] = L'~';
  n++;
  wpath[n] = 0;
  HANDLE tfile = CreateFile(wpath, GENERIC_WRITE, 0, null, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, null);
  if (tfile != INVALID_HANDLE_VALUE)
  {
   CloseHandle(tfile);
   is_admin = false;
   SendMessage(GetDlgItem(dlg, ID_BUTTON_INSTALL), BCM_SETSHIELD, 0, false);
   return;
  }
 }
 else
 {
  bool ok = folder_create(install_path);
  if (ok)
  {
   folder_delete(install_path);
   is_admin = false;
   SendMessage(GetDlgItem(dlg, ID_BUTTON_INSTALL), BCM_SETSHIELD, 0, false);
   return;
  }
 }
 is_admin = (GetLastError() == ERROR_ACCESS_DENIED) ? true : false;
 SendMessage(GetDlgItem(dlg, ID_BUTTON_INSTALL), BCM_SETSHIELD, 0, is_admin);
}

void dialog_add_items(HWND dlg, int id, const WCHAR* const* items, nat count)
{
 HWND item = GetDlgItem(dlg, id);
 for (nat i = 0; i < count; i++)
  SendMessage(item, CB_ADDSTRING, 0, cast(LPARAM, items[i]));
 SendMessage(item, CB_SETCURSEL, 0, 0);
}

void dialog_select_version(HWND dlg, int id, const char* versions, i64 count, nat stride, char* dest, nat dest_size)
{
 i64 index = SendMessage(GetDlgItem(dlg, id), CB_GETCURSEL, 0, 0);
 if ((index < 0) | (index >= count))
  return;
 _string_copy(dest, dest_size, versions + ((count - index - 1) * stride));
}

void dialog_select_string(HWND dlg, int id, const char* const* list, i64 count, char* dest, nat dest_size)
{
 i64 index = SendMessage(GetDlgItem(dlg, id), CB_GETCURSEL, 0, 0);
 if ((index < 0) | (index >= count))
  return;
 _string_copy(dest, dest_size, list[index]);
}

INT_PTR CALLBACK dialog_proc(HWND dlg, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch(message)
    {
    case WM_INITDIALOG:
    {
        HWND service = find_app_service();
        if (service != null)
         SetWindowLongPtr(service, GWLP_USERDATA, cast(LONG_PTR, dlg));
        SetForegroundWindow(dlg);
        {
         HICON icon = LoadIcon(instance, L"icon");
         SendMessage(dlg, WM_SETICON, ICON_SMALL, cast(LPARAM, icon));
         SendMessage(dlg, WM_SETICON, ICON_BIG, cast(LPARAM, icon));
        }
        {
         static const WCHAR* items[] = {
          L"Create environment setup scripts",
          L"Create the scripts & add to user environment",
          L"Create the scripts & add to global environment",
         };
         dialog_add_items(dlg, ID_COMBO_ENV, items, countof(items));
        }
        {
         static const WCHAR* items[] = {L"x64", L"x86", L"arm", L"arm64"};
         dialog_add_items(dlg, ID_COMBO_TARGET, items, countof(items));
        }
        {
         static const WCHAR* items[] = {L"x64", L"x86", L"arm64"};
         dialog_add_items(dlg, ID_COMBO_HOST, items, countof(items));
        }
        {
         HWND item = GetDlgItem(dlg, ID_EDIT_PATH);
         SendMessage(item, WM_SETTEXT, 0, cast(LPARAM, L"C:\\BuildTools"));
         SHAutoComplete(item, SHACF_FILESYSTEM);
        }
        dialog_refill(dlg);
        dialog_path_update(dlg);
        EnableWindow(GetDlgItem(dlg, ID_BUTTON_INSTALL), false);
        return (true);
    }
    case WM_CLOSE:
    {
        HWND service = find_app_service();
        if (service != null)
         SetWindowLongPtr(service, GWLP_USERDATA, 0);
        EndDialog(dlg, 0);
        return (true);
    }
    case WM_NOTIFY:
    {
        NMHDR* param = cast(NMHDR*, lparam);
        if ((param->code == NM_CLICK) || (param->code == NM_RETURN))
        {
         NMLINK* link = cast(NMLINK*, lparam);
         if (link->hdr.idFrom == ID_SYSLINK_LICENSE)
          ShellExecute(null, L"open", link->item.szUrl, null, null, SW_NORMAL);
        }
        break;
    }
    case WM_COMMAND:
    {
        switch(LOWORD(wparam))
        {
            case ID_CHECK_PREVIEW:
        {
            preview_mode = (IsDlgButtonChecked(dlg, ID_CHECK_PREVIEW) == BST_CHECKED);
            dialog_refill(dlg);
            break;
        }
            case ID_COMBO_ENV:
        {
            env_mode = cast(EnvMode, SendMessage(GetDlgItem(dlg, ID_COMBO_ENV), CB_GETCURSEL, 0, 0));
            break;
        }
        case ID_COMBO_MSVC:
        {
            char (*versions)[16] = preview_mode ? preview_mode_msvc_versions : release_msvc_versions;
            i64 count = preview_mode ? preview_mode_msvc_versions_count : release_msvc_versions_count;
            dialog_select_version(dlg, ID_COMBO_MSVC, cast(const char*, versions), count, sizeof(versions[0]), msvc_version, countof(msvc_version));
           break;
        }
        case ID_COMBO_SDK:
        {
            char (*versions)[16] = preview_mode ? preview_mode_sdk_versions : release_sdk_versions;
            i64 count = preview_mode ? preview_mode_sdk_versions_count : release_sdk_versions_count;
            dialog_select_version(dlg, ID_COMBO_SDK, cast(const char*, versions), count, sizeof(versions[0]), sdk_version, countof(sdk_version));
            break;
        }
        case ID_COMBO_TARGET:
        {
            dialog_select_string(dlg, ID_COMBO_TARGET, cast(const char* const*, targets), countof(targets), target_arch, countof(target_arch));
            break;
        }
        case ID_COMBO_HOST:
        {
            dialog_select_string(dlg, ID_COMBO_HOST, cast(const char* const*, hosts), countof(hosts), host_arch, countof(host_arch));
            break;
        }
        case ID_EDIT_PATH:
        {
            if (HIWORD(wparam) == EN_CHANGE)
             dialog_path_update(dlg);
            break;
        }
        case ID_BUTTON_BROWSE:
        {
            IFileOpenDialog* fd = null;
            HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, null, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, cast(void**, &fd));
            if (SUCCEEDED(hr) && fd)
            {
             GUID client_guid = {0x65CDF695, 0x0A2C, 0x46DB, {0x9F, 0xE8, 0x73, 0xCF, 0xA7, 0xF7, 0x1E, 0x65}};
             vcall(fd, lpVtbl->SetClientGuid, client_guid);
             DWORD options = 0;
             if (SUCCEEDED(vcall(fd, lpVtbl->GetOptions, &options)))
              vcall(fd, lpVtbl->SetOptions, options | FOS_PICKFOLDERS);
             if (SUCCEEDED(vcall(fd, lpVtbl->Show, null)))
             {
              IShellItem* psi = null;
              if (SUCCEEDED(vcall(fd, lpVtbl->GetResult, &psi)) && psi)
              {
               LPWSTR wpath = null;
               if (SUCCEEDED(vcall(psi, lpVtbl->GetDisplayName, SIGDN_DESKTOPABSOLUTEPARSING, &wpath)) && wpath)
               {
                SetDlgItemText(dlg, ID_EDIT_PATH, wpath);
                CoTaskMemFree(wpath);
               }
               vcall(psi, lpVtbl->Release);
              }
             }
             vcall(fd, lpVtbl->Release);
            }
            break;
        }
        case ID_CHECK_LICENSE:
        {
            license_accepted = (IsDlgButtonChecked(dlg, ID_CHECK_LICENSE) == BST_CHECKED);
            EnableWindow(GetDlgItem(dlg, ID_BUTTON_INSTALL), license_accepted);
            break;
        }
        case ID_CHECK_ATL:
        {
            atlmfc = (IsDlgButtonChecked(dlg, ID_CHECK_ATL) == BST_CHECKED);
            break;
        }
        case ID_CHECK_SPECTRE:
        {
            spectre = (IsDlgButtonChecked(dlg,ID_CHECK_SPECTRE) == BST_CHECKED);
            break;
        }
        case ID_BUTTON_INSTALL:
        {
            if (folder_exists(install_path))
            {
             if (is_folder_empty(install_path))
             {
                 echoln("install_start=true");
              install_start = true;
             }
             else
             {
              MessageBox(dlg, L"Destination folder is not empty.", null, MB_TOPMOST | MB_ICONERROR);
             }
            }
            else
            {
             // creating a folder to check if it possible to create one
             if (folder_create(install_path))
             {
              folder_delete(install_path);
              install_start = true;
             }
             else if (GetLastError() != ERROR_ACCESS_DENIED)
             {
              MessageBox(dlg, L"Destination folder cannot be made.", null, MB_TOPMOST | MB_ICONERROR);
             }
            }
            if (install_start)
             SendMessage(dlg, WM_CLOSE, 0, 0);
            break;
        }
        default:
            break;
        }
    }
    default:
        break;
    }
 return (false);
}

void parse_vsmanifest(const char* path, bool preview_mode)
{
 echoln("Parsing ", preview_mode ? "Preview" : "Release", " Visual Studio Manifest...");
 char manifest_url[countof(release_manifest_url)] = {0};
 char license_url[countof(release_license_url)] = {0};
 json_context jc;
 jc.c8 = 0;
 jc.cache_size = 0;
 jc.cache_pos = 0;
 jc.f = file_open(path, false);
 json_parser p = json_parser_begin(json_file_peek, &jc);
 hope(json_object_key_find(&p, "channelItems"), "channelItems not found");
 while (json_array_next(&p))
 {
  json_context obj_state = jc;
  if (!json_object_key_find(&p, "id"))
  {
   json_object_skip(&p);
   continue;
  }
  char id[64];
  json_string_extract(&p, id);
  if (string_is(id, preview_mode ? "Microsoft.VisualStudio.Manifests.VisualStudioPreview" : "Microsoft.VisualStudio.Manifests.VisualStudio"))
  {
   json_file_context_restore(&jc, obj_state);
   hope(json_object_key_find(&p, "payloads"), "payloads not found");
   json_array_next(&p);
   hope(json_object_key_find(&p, "url"), "manifest url not found");
   json_string_extract(&p, manifest_url);
   json_object_skip(&p);
   json_array_next(&p);
  }
  if (string_is(id, "Microsoft.VisualStudio.Product.BuildTools"))
  {
   json_file_context_restore(&jc, obj_state);
   hope(json_object_key_find(&p, "localizedResources"), "localizedResources not found");
   while (json_array_next(&p))
   {
    json_context res_state = jc;
    hope(json_object_key_find(&p, "language"), "license language not found");
    char language[8];
    json_string_extract(&p, language);
    if (string_is(language, "en-us"))
    {
     json_file_context_restore(&jc, res_state);
     hope(json_object_key_find(&p, "license"), "license url not found");
     json_string_extract(&p, license_url);
     json_object_skip(&p);
     break;
    }
    json_object_skip(&p);
   }
  }
  json_object_skip(&p);
 }
 file_close(&jc.f);
 if (manifest_url[0] == 0)
  die("manifest url not found", L"Manifest URL not found");
 if (license_url[0] == 0)
  die("license url not found", L"License URL not found");
 memcpy(preview_mode ? preview_mode_manifest_url : release_manifest_url, manifest_url, sizeof(manifest_url));
 memcpy(preview_mode ? preview_mode_license_url : release_license_url, license_url, sizeof(license_url));
}

void parse_manifest(const char* path, bool preview_mode)
{
 echoln("Parsing ", preview_mode ? "Preview" : "Release", " Build Tool Manifest...");
 char msvc_versions[VERSION_COUNT][VERSION_LENGTH] = {0};
 i64 msvc_versions_count = 0;
 char sdk_versions[VERSION_COUNT][VERSION_LENGTH] = {0};
 i64 sdk_versions_count = 0;
 json_context jc;
 jc.c8 = 0;
 jc.cache_size = 0;
 jc.cache_pos = 0;
 jc.f = file_open(path, false);
 json_parser p = json_parser_begin(json_file_peek, &jc);
 hope(json_object_key_find(&p, "packages"), "packages not found");
 while (json_array_next(&p))
 {
  hope(json_object_key_find(&p, "id"), "id not found");
  char id[64];
  json_string_extract(&p, id);
  char id_lower[countof(id)];
  string_copy(id_lower, id);
  string_lower(id_lower);
  bool is_msvc_version = true;
  is_msvc_version = string_trim_left(id_lower, "microsoft.vc.") ? is_msvc_version : false;
  is_msvc_version = string_trim_right(id_lower, ".tools.hostx64.targetx64.base") ? is_msvc_version : false;
  bool is_sdk_version = false;
  is_sdk_version = string_trim_left(id_lower, "microsoft.visualstudio.component.windows10sdk.") ? true : is_sdk_version;
  is_sdk_version = string_trim_left(id_lower, "microsoft.visualstudio.component.windows11sdk.") ? true : is_sdk_version;
  bool is_digit = (strspn(id_lower, ".0123456789") == strlen(id_lower));
  if (is_msvc_version & is_digit)
   _string_copy(msvc_versions[msvc_versions_count++], sizeof(msvc_versions[0]), id_lower);
  if (is_sdk_version & is_digit)
   _string_copy(sdk_versions[sdk_versions_count++], sizeof(sdk_versions[0]), id_lower);
  json_object_skip(&p);
  if(msvc_versions_count >= VERSION_COUNT || sdk_versions_count >= VERSION_COUNT){
      break;
  }
 }
 file_close(&jc.f);
 memcpy(preview_mode ? preview_mode_sdk_versions : release_sdk_versions, sdk_versions, sizeof(sdk_versions));
 memcpy(preview_mode ? preview_mode_msvc_versions : release_msvc_versions, msvc_versions, sizeof(msvc_versions));
 release_msvc_versions_count = preview_mode ? release_msvc_versions_count : msvc_versions_count;
 preview_mode_msvc_versions_count = preview_mode ? msvc_versions_count : preview_mode_msvc_versions_count;
 release_sdk_versions_count = preview_mode ? release_sdk_versions_count : sdk_versions_count;
 preview_mode_sdk_versions_count = preview_mode ? sdk_versions_count : preview_mode_sdk_versions_count;
}

void extract_payload_info(json_context* jc, json_parser* p, char (*file_name)[MAX_PATH * 3], char (*url)[L_MAX_URL_LENGTH * 3])
{
 // TODO: extract and validate sha256 (given sha256 doesn't seem to correspond to the file's actual sha256)
 json_context payload_state = *jc;
 hope(json_object_key_find(p, "fileName"), "fileName not found");
 json_string_extract(p, *file_name);
 json_file_context_restore(jc, payload_state);
 hope(json_object_key_find(p, "url"), "url not found");
 json_string_extract(p, *url);
 json_file_context_restore(jc, payload_state);
}

void print_versions(const char* title, const char* versions, i64 count, nat stride)
{
 echoln(title);
 for (i64 i = count - 1; i >= 0; i--)
  echoln(versions + (i * stride));
}

void delete_children_except(const char* dir, const char* keep, bool suffix)
{
 struct folder folder = folder_open(dir);
 while (folder_next(&folder))
 {
  bool match = suffix ? string_ends_with(folder.file_name, keep) : string_is(folder.file_name, keep);
  if (match)
   continue;
  char path[MAX_PATH * 3];
  string_join(path, dir, "\\", folder.file_name);
  folder_delete(path);
 }
 folder_close(&folder);
}

bool parse_arg_binding(const char* arg)
{
 static const struct
 {
  const char* prefix;
  char* dest;
  nat size;
 } bindings[] = {
  {"msvc=", msvc_version, countof(msvc_version)},
  {"sdk=", sdk_version, countof(sdk_version)},
  {"target=", target_arch, countof(target_arch)},
  {"host=", host_arch, countof(host_arch)},
 };
 for (nat i = 0; i < countof(bindings); i++)
 {
  if (!string_starts_with(arg, bindings[i].prefix))
   continue;
  _string_copy(bindings[i].dest, bindings[i].size, arg);
  string_trim_left(bindings[i].dest, bindings[i].prefix);
  return (true);
 }
 return (false);
}

void parse_args(bool* list_versions, bool* gui_mode)
{
 for (nat i = 0; i < args_count; i++)
 {
  if (i == 0)
   continue;
  const char* arg = args[i];
  if (string_is(arg, "!"))
  {
   subprocess_mode = true;
  }
  else if (string_is(arg, "gui"))
  {
   *gui_mode = true;
  }
  else if (string_is(arg, "list"))
  {
   *list_versions = true;
  }
  else if (string_is(arg, "accept_license"))
  {
   license_accepted = true;
  }
  else if (string_is(arg, "preview_mode"))
  {
   preview_mode = true;
  }
  else if(string_is(arg, "atlfmc"))
  {
      atlmfc = true;
  }
  else if(string_is(arg, "spectre"))
  {
      spectre = true;
  }
  else if (parse_arg_binding(arg))
  {
  }
  else if (string_starts_with(arg, "env="))
  {
   env_mode = EnvMode::none;
   env_mode = string_is(arg, "env=global") ? EnvMode::global : env_mode;
   env_mode = string_is(arg, "env=user") ? EnvMode::user : env_mode;
  }
  else if (string_starts_with(arg, "path="))
  {
   string_copy(install_path, arg);
   string_trim_left(install_path, "path=");
   string_trim_left(install_path, "\"");
   string_trim_right(install_path, "\"");
  }
  else if (string_starts_with(arg, "temp="))
  {
   string_copy(temp_path, arg);
   string_trim_left(temp_path, "temp=");
   string_trim_left(temp_path, "\"");
   string_trim_right(temp_path, "\"");
  }
  else
  {
   echo(
   "usage: PortableBuildTools.exe [gui] [list] [accept_license] [preview_mode] [msvc=MSVC version] [sdk=SDK version] [target=x64/x86/arm/arm64] [host=x64/x86/arm64] [env=none/user/global] [path=\"C:\\BuildTools\"]\n"
    "\n"
    "*gui: run PortableBuildTools in GUI mode\n"
    "*list: show all available Build Tools versions\n"
    "*accept_license: auto-accept the license [default: ask]\n"
    "*preview_mode: install from the Preview channel, instead of the Release channel [default: Release channel]\n"
    "*msvc: which MSVC toolchain version to install [default: whatever is latest]\n"
    "*sdk: which Windows SDK version to install [default: whatever is latest]\n"
    "*target: target architecture [default: x64]\n"
    "*host: host architecture [default: x64]\n"
    "*env: if supplied, then the installed path will be added to PATH environment variable, for the current user or for all users [default: none]\n"
    "*path: installation path [default: C:\\BuildTools]\n"
   );
   app_exit(1);
  }
 }
}

void download_manifests(void)
{
 if (!download_service_start())
  die("download service start failed", L"Download service start failed");
 echoln("Downloading manifest files...");
 char manifest_path_release[MAX_PATH * 3]{0};
 char manifest_path_preview[MAX_PATH * 3]{0};
 char buildtools_path_release[MAX_PATH * 3]{0};
 char buildtools_path_preview[MAX_PATH * 3]{0};
 string_join_buf(manifest_path_release, temp_path, "\\Manifest.Release.json");
 string_join_buf(manifest_path_preview, temp_path, "\\Manifest.Preview.json");
 string_join_buf(buildtools_path_release, temp_path, "\\BuildToolsManifest.Release.json");
 string_join_buf(buildtools_path_preview, temp_path, "\\BuildToolsManifest.Preview.json");
 download_file(release_vsmanifest_url, manifest_path_release, "Manifest.Release.json");
 download_file(preview_mode_vsmanifest_url, manifest_path_preview, "Manifest.Preview.json");
 echoln("Parsing manifest files...");
 parse_vsmanifest(manifest_path_release, false);
 parse_vsmanifest(manifest_path_preview, true);
 echoln("Downloading Build Tools manifest files...");
 download_file(release_manifest_url, buildtools_path_release, "BuildToolsManifest.Release.json");
 download_file(preview_mode_manifest_url, buildtools_path_preview, "BuildToolsManifest.Preview.json");
 echoln("Parsing Build Tools manifest files...");
 parse_manifest(buildtools_path_release, false);
 parse_manifest(buildtools_path_preview, true);
 echoln("");
 download_service_stop();
 echoln("finish download manifest files");
}

void run_gui(void)
{
 echoln("enter gui mode!");
 FreeConsole();
 INITCOMMONCONTROLSEX controls = {};
 controls.dwSize = sizeof(INITCOMMONCONTROLSEX);
 controls.dwICC = ICC_STANDARD_CLASSES | ICC_LINK_CLASS;
 InitCommonControlsEx(&controls);
 int ret = DialogBox(instance, MAKEINTRESOURCEW(ID_DIALOG), null, dialog_proc);
 if (ret < 0)
  return;
 if (!install_start)
  app_exit(1);
 const char* env_mode_str = "none";
 env_mode_str = (env_mode == EnvMode::user) ? "user" : env_mode_str;
 env_mode_str = (env_mode == EnvMode::global) ? "global" : env_mode_str;
 char params[MAX_CMDLINE_LEN * 3];
 string_join(params, "! ", preview_mode ? "preview_mode" : "", " msvc=", msvc_version, " sdk=", sdk_version, " target=", target_arch, " host=", host_arch, " env=", env_mode_str, " temp=\"", temp_path, "\"", " path=\"", install_path, "\"");
 WCHAR wprogram[MAX_PATH];
 const char* program = args[0];
 wprogram[0] = 0;
 if (utf8_to_utf16(program, -1, wprogram, countof(wprogram)) <= 0)
  app_exit(1);
 WCHAR wparams[MAX_CMDLINE_LEN];
 wparams[0] = 0;
 if (utf8_to_utf16(params, -1, wparams, countof(wparams)) <= 0)
  app_exit(1);
 SHELLEXECUTEINFO info = {};
 info.cbSize = sizeof(SHELLEXECUTEINFO);
 info.fMask = SEE_MASK_NOCLOSEPROCESS;
 info.lpVerb = is_admin ? L"runas" : L"open";
 info.lpFile = wprogram;
 info.lpParameters = wparams;
 info.nShow = SW_NORMAL;
 if (!ShellExecuteEx(&info))
 {
  echoln("shellexecute fail");
  app_exit(1);
 }
 WaitForSingleObject(info.hProcess, INFINITE);
 CloseHandle(info.hProcess);
}
template<size_t N, typename O, typename Sep, typename Head, typename... Tail>
void string_join_2(O (&buffer)[N], const Sep& sep, const Head& first, const Tail&... rest)
{
    strcat_s(buffer, first);
    if constexpr (sizeof...(rest) > 0)
    {
        string_join_2(buffer, sep, rest...);      // 注意函数名拼写
    }
}
void build_list(const char* ver, const char* arch, bool spectre)
{
    echoln(arch, spectre ? "true" : "false");
    char msvc_packages[32][128]={0};
    size_t idx = 0;
    for(const auto& comp1 : {"ATL","MFC"})
    {
        for(const auto& comp2 : {"Headers","Source"})
        {
            string_join_2(msvc_packages[idx++],".", "Microsoft","VC",ver,comp1,comp2,"base");
        }
    }
    string_join_2(msvc_packages[idx++], ".","Microsoft","VC",ver,arch,"base");
    if(spectre)
    {
        string_join_2(msvc_packages[idx++], ".","Microsoft","VC",ver,arch,"Spectre","base");
    }
    for(const auto& is_debug : {"",".Debug"})
    {
        string_join_2(msvc_packages[idx++], ".","Microsoft","VC","MFC",arch,is_debug,"base");
        if(spectre)
        {
            string_join_2(msvc_packages[idx++], ".","Microsoft","VC","MFC",arch,is_debug,"Spectre", "base");
        }
    }
    string_join_2(msvc_packages[idx++], ".","Microsoft","VC","MFC","Redist",arch,"base");
    if(spectre)
    {
        string_join_2(msvc_packages[idx++], ".","Microsoft","VC","MFC","Redist",arch,"Spectre", "base");
    }
    for(const auto& is_debug : {"","Debug"})
    {
        string_join_2(msvc_packages[idx++], ".","Microsoft","VC","MFC","MBCS",strcmp(arch,"X86")==0?"":arch,is_debug,"base");
        if(spectre)
        {
            string_join_2(msvc_packages[idx++], ".","Microsoft","VC","MFC","MBCS",strcmp(arch,"X86")==0?"":arch,is_debug,"Spectre","base");
        }
    }
    for(auto n(0); n < idx; ++n)
    {
        echoln(msvc_packages[n]);
    }
}

void build_batch(const char* install, const char* msvcv, const char* sdkv, bool use_atlmfc, bool use_spectre)
{
    char bat_path[MAX_PATH * 3];
    string_join(bat_path, install, "\\devcmd.bat");
    struct file f = file_open(bat_path, true);
    char buf[mem_page_size];
    file_write(&f, string_slice(
     "@echo off\n"
     "set BUILD_TOOLS_ROOT=%~dp0\n"
     "set WindowsSDKDir=%BUILD_TOOLS_ROOT%Windows Kits\\10\n"));
    string_join_buf(buf,
     "set VCToolsInstallDir=%BUILD_TOOLS_ROOT%VC\\Tools\\MSVC\\", msvcv,
     "\nset WindowsSDKVersion=", sdkv,
     "\nset VSCMD_ARG_TGT_ARCH=", target_arch,
     "\nset VSCMD_ARG_HOST_ARCH=", host_arch, "\n");
    file_write(&f, buf, strlen(buf));
    file_write(&f, string_slice(
     "set INCLUDE=%VCToolsInstallDir%\\include;"));
    if(use_atlmfc)
    {
        file_write(&f, string_slice("%VCToolsInstallDir%\\atlmfc\\include;"));
    }
    file_write(&f, string_slice(
     "%WindowsSDKDir%\\Include\\%WindowsSDKVersion%\\ucrt;"
     "%WindowsSDKDir%\\Include\\%WindowsSDKVersion%\\shared;"
     "%WindowsSDKDir%\\Include\\%WindowsSDKVersion%\\um;"
     "%WindowsSDKDir%\\Include\\%WindowsSDKVersion%\\winrt;"
     "%WindowsSDKDir%\\Include\\%WindowsSDKVersion%\\cppwinrt;\n"));
    file_write(&f, string_slice("set LIB="));
    if(use_spectre)
    {
        file_write(&f, string_slice("%VCToolsInstallDir%\\lib\\spectre\\%VSCMD_ARG_TGT_ARCH%;"));
    }
    file_write(&f, string_slice("%VCToolsInstallDir%\\lib\\%VSCMD_ARG_TGT_ARCH%;"));
    if(use_atlmfc)
    {
        if(use_spectre)
        {
            file_write(&f, string_slice("%VCToolsInstallDir%\\atlmfc\\lib\\spectre\\%VSCMD_ARG_TGT_ARCH%;"));
        }
        file_write(&f, string_slice("%VCToolsInstallDir%\\atlmfc\\lib\\%VSCMD_ARG_TGT_ARCH%;"));
    }
    file_write(&f, string_slice("%WindowsSDKDir%\\Lib\\%WindowsSDKVersion%\\ucrt\\%VSCMD_ARG_TGT_ARCH%;"
     "%WindowsSDKDir%\\Lib\\%WindowsSDKVersion%\\um\\%VSCMD_ARG_TGT_ARCH%\n"));
    file_write(&f, string_slice(
     "set BUILD_TOOLS_BIN=%VCToolsInstallDir%\\bin\\Host%VSCMD_ARG_HOST_ARCH%\\%VSCMD_ARG_TGT_ARCH%;"
     "%WindowsSDKDir%\\bin\\%WindowsSDKVersion%\\%VSCMD_ARG_TGT_ARCH%;"
     "%WindowsSDKDir%\\bin\\%WindowsSDKVersion%\\%VSCMD_ARG_TGT_ARCH%\\ucrt\n"));
    file_write(&f, string_slice("set PATH=%BUILD_TOOLS_BIN%;%PATH%\n"));
    file_close(&f);
}
void build_powershell(const char* install, const char* msvcv, const char* sdkv, bool use_atlmfc, bool use_spectre)
{
    char ps_path[MAX_PATH * 3];
    string_join(ps_path, install, "\\devcmd.ps1");
    struct file f = file_open(ps_path, true);
    char buf[mem_page_size];
    file_write(&f, string_slice(
     "#Requires -Version 5\n"
     "param([string]$InstallPath = $PSScriptRoot)\n"
     "$env:BUILD_TOOLS_ROOT = $InstallPath\n"
     "$env:WindowsSDKDir = (Join-Path $InstallPath '\\Windows Kits\\10')\n"
     "$VCToolsVersion = (Get-ChildItem -Directory (Join-Path $InstallPath '\\VC\\Tools\\MSVC' | Sort-Object -Descending LastWriteTime | Select-Object -First 1) -ErrorAction SilentlyContinue).Name\n"
     "if (!$VCToolsVersion) { throw 'VCToolsVersion cannot be determined.' }\n"
     "$env:VCToolsInstallDir = Join-Path (Join-Path $InstallPath '\\VC\\Tools\\MSVC') $VCToolsVersion\n"
     "$env:WindowsSDKVersion = (Get-ChildItem -Directory (Join-Path $env:WindowsSDKDir 'bin') -ErrorAction SilentlyContinue | Sort-Object -Descending LastWriteTime | Select-Object -First 1).Name\n"
     "if (!$env:WindowsSDKVersion ) { throw 'WindowsSDKVersion cannot be determined.' }\n"));
    string_join_buf(buf,
     "$env:VSCMD_ARG_TGT_ARCH = '", target_arch,
     "'\n$env:VSCMD_ARG_HOST_ARCH = '", host_arch, "'\n");
    file_write(&f, buf, strlen(buf));
    file_write(&f, string_slice(
     "'Portable Build Tools environment started.'\n"
     "'* BUILD_TOOLS_ROOT   : {0}' -f $env:BUILD_TOOLS_ROOT\n"
     "'* WindowsSDKDir      : {0}' -f $env:WindowsSDKDir\n"
     "'* WindowsSDKVersion  : {0}' -f $env:WindowsSDKVersion\n"
     "'* VCToolsInstallDir  : {0}' -f $env:VCToolsInstallDir\n"
     "'* VSCMD_ARG_TGT_ARCH : {0}' -f $env:VSCMD_ARG_TGT_ARCH\n"
     "'* VSCMD_ARG_HOST_ARCH: {0}' -f $env:VSCMD_ARG_HOST_ARCH\n"));
    file_write(&f, string_slice("$env:INCLUDE =\"$env:VCToolsInstallDir\\include;"));
    if(use_atlmfc)
    {
        file_write(&f, string_slice("$env:VCToolsInstallDir\\atlmfc\\include;"));
    }
    file_write(&f, string_slice("$env:WindowsSDKDir\\Include\\$env:WindowsSDKVersion\\ucrt;"
     "$env:WindowsSDKDir\\Include\\$env:WindowsSDKVersion\\shared;"
     "$env:WindowsSDKDir\\Include\\$env:WindowsSDKVersion\\um;"
     "$env:WindowsSDKDir\\Include\\$env:WindowsSDKVersion\\winrt;"
     "$env:WindowsSDKDir\\Include\\$env:WindowsSDKVersion\\cppwinrt\"\n"));
    file_write(&f, string_slice("$env:LIB = "));
    if(use_spectre)
    {
        file_write(&f, string_slice("$env:VCToolsInstallDir\\lib\\spectre\\$env:VSCMD_ARG_TGT_ARCH;"));
    }
    if(use_atlmfc)
    {
        if(use_spectre)
        {
            file_write(&f, string_slice("$env:VCToolsInstallDir\\atlmfc\\lib\\spectre\\$env:VSCMD_ARG_TGT_ARCH;"));
        }
        file_write(&f, string_slice("$env:VCToolsInstallDir\\atlmfc\\lib\\$env:VSCMD_ARG_TGT_ARCH;"));
    }
    file_write(&f, string_slice("\"$env:VCToolsInstallDir\\lib\\$env:VSCMD_ARG_TGT_ARCH;"
     "$env:WindowsSDKDir\\Lib\\$env:WindowsSDKVersion\\ucrt\\$env:VSCMD_ARG_TGT_ARCH;"
     "$env:WindowsSDKDir\\Lib\\$env:WindowsSDKVersion\\um\\$env:VSCMD_ARG_TGT_ARCH\"\n"));
    file_write(&f, string_slice(
     "$env:BUILD_TOOLS_BIN = \"$env:VCToolsInstallDir\\bin\\Host$env:VSCMD_ARG_HOST_ARCH\\$env:VSCMD_ARG_TGT_ARCH;"
     "$env:WindowsSDKDir\\bin\\$env:WindowsSDKVersion\\$env:VSCMD_ARG_TGT_ARCH;"
     "$env:WindowsSDKDir\\bin\\$env:WindowsSDKVersion\\$env:VSCMD_ARG_TGT_ARCH\\ucrt\"\n"));
    file_write(&f, string_slice("$env:PATH = \"$env:BUILD_TOOLS_BIN;$env:PATH\"\n"));
    file_close(&f);
}
template<size_t N>
void build_include_env(char (&dest)[N], const char* sdkv, bool use_atlmfc)
{
    string_join_2(dest, "", "%VCToolsInstallDir%", "\\include;");
    if(use_atlmfc)
    {
        string_join_2(dest,"","%VCToolsInstallDir%", "\\atlmfc\\include;");
    }
    string_join_2(dest, "",
     "%WindowsSDKDir%", "\\Include\\", sdkv, "\\ucrt;",
     "%WindowsSDKDir%", "\\Include\\", sdkv, "\\shared;",
     "%WindowsSDKDir%", "\\Include\\", sdkv, "\\um;",
     "%WindowsSDKDir%", "\\Include\\", sdkv, "\\winrt;",
     "%WindowsSDKDir%", "\\Include\\", sdkv, "\\cppwinrt"
    );
}
template<size_t N>
void build_lib_env(char (&buf)[N], const char* sdkv, bool use_atlmfc, bool use_spectre)
{
    if(use_spectre)
    {
        string_join_2(buf,"","%VCToolsInstallDir%", "\\lib\\spectre\\", target_arch, ";");
    }
    string_join_2(buf,"","%VCToolsInstallDir%", "\\lib\\", target_arch, ";");
    if(use_atlmfc)
    {
        if(use_spectre)
        {
            string_join_2(buf,"","%VCToolsInstallDir%", "\\atlmfc\\lib\\spectre\\", target_arch, ";");
        }
        string_join_2(buf,"","%VCToolsInstallDir%", "\\atlmfc\\lib\\", target_arch, ";");
    }
    string_join_2(buf,"","%WindowsSDKDir%", "\\Lib\\", sdkv, "\\ucrt\\", target_arch, ";",
    "%WindowsSDKDir%", "\\Lib\\", sdkv, "\\um\\", target_arch);
}
void start(void)
{
 bool gui_mode = !invoked_from_console;
 bool list_versions = false;
 parse_args(&list_versions, &gui_mode);
 if (temp_path[0] == 0)
 {
  WCHAR wtemp_path[MAX_PATH] = {0};
  char temp_root[MAX_PATH * 3];
  temp_root[0] = 0;
  DWORD temp_len = GetTempPath(countof(wtemp_path), wtemp_path);
  if ((temp_len == 0) || (temp_len >= countof(wtemp_path)))
   string_copy(temp_root, ".\\");
  else if (utf16_to_utf8(wtemp_path, -1, temp_root, countof(temp_root)) <= 0)
   string_copy(temp_root, ".\\");
  string_join_buf(temp_path, temp_root, "BuildTools-", itos(GetCurrentProcessId()));
 }
 com_initialized = SUCCEEDED(CoInitializeEx(null, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
 register_service_class(download_service_proc, TEXT(appid) L":download_service");
 if (!subprocess_mode)
 {
  appid_instance_guard();
  if (!folder_create(temp_path))
  {
   char cwd[MAX_PATH * 3];
   DWORD cwd_len = GetCurrentDirectoryA(countof(cwd), cwd);
   if ((cwd_len == 0) || (cwd_len >= countof(cwd)))
    die("failed to create temp directory", L"Failed to create temp directory");
   string_join_buf(temp_path, cwd, "\\BuildTools-", itos(GetCurrentProcessId()));
   if (!folder_create(temp_path))
    die("failed to create temp directory", L"Failed to create temp directory");
  }
  download_manifests();
  if (gui_mode)
  {
   run_gui();
   //return;
  }
  if (list_versions)
  {
   print_versions("MSVC Versions (Release): ", cast(const char*, release_msvc_versions), release_msvc_versions_count, sizeof(release_msvc_versions[0]));
   print_versions("SDK Versions (Release): ", cast(const char*, release_sdk_versions), release_sdk_versions_count, sizeof(release_sdk_versions[0]));
   print_versions("MSVC Versions (Preview): ", cast(const char*, preview_mode_msvc_versions), preview_mode_msvc_versions_count, sizeof(preview_mode_msvc_versions[0]));
   print_versions("SDK Versions (Preview): ", cast(const char*, preview_mode_sdk_versions), preview_mode_sdk_versions_count, sizeof(preview_mode_sdk_versions[0]));
   //return;
  }
  // cli mode
  if (!string_in_list(target_arch, cast(const char* const*, targets), countof(targets)))
  {
   echoln("Invalid target architecture: ", target_arch);
   app_exit(1);
  }
  if (!string_in_list(host_arch, cast(const char* const*, hosts), countof(hosts)))
  {
   echoln("Invalid host architecture: ", host_arch);
   app_exit(1);
  }
  {
   char (*msvc_versions)[16] = preview_mode ? preview_mode_msvc_versions : release_msvc_versions;
   i64 msvc_versions_count = preview_mode ? preview_mode_msvc_versions_count : release_msvc_versions_count;
   if (msvc_version[0] == 0)
    string_copy(msvc_version, msvc_versions[msvc_versions_count - 1]);
   if (!string_in_versions(msvc_version, cast(const char*, msvc_versions), msvc_versions_count, sizeof(msvc_versions[0])))
   {
    echoln("Invalid MSVC version: ", msvc_version);
    app_exit(1);
   }
  }
  {
   char (*sdk_versions)[16] = preview_mode ? preview_mode_sdk_versions : release_sdk_versions;
   i64 sdk_versions_count = preview_mode ? preview_mode_sdk_versions_count : release_sdk_versions_count;
   if (sdk_version[0] == 0)
    string_copy(sdk_version, sdk_versions[sdk_versions_count - 1]);
   if (!string_in_versions(sdk_version, cast(const char*, sdk_versions), sdk_versions_count, sizeof(sdk_versions[0])))
   {
    echoln("Invalid SDK version: ", sdk_version);
    app_exit(1);
   }
  }
  echoln("Preview channel: ", preview_mode ? "true" : "false");
  echoln("MSVC version: ", msvc_version);
  echoln("SDK version: ", sdk_version);
  echoln("Target arch: ", target_arch);
  echoln("Host arch: ", host_arch);
  echoln("Install path: ", install_path);
  if (folder_exists(install_path))
  {
   if (!is_folder_empty(install_path))
   {
    echoln("Destination folder is not empty.");
    app_exit(1);
   }
  }
  else
  {
   WCHAR wpath[MAX_PATH];
   wpath[0] = 0;
   if (utf8_to_utf16(install_path, -1, wpath, MAX_PATH) <= 0)
    app_exit(1);
   if (!CreateDirectory(wpath, null))
   {
    const char* error = (GetLastError() == ERROR_ACCESS_DENIED) ? "No access to the destination folder, try running this program as administrator." : "Destination folder cannot be made.";
    echoln(error);
    app_exit(1);
   }
  }
  if (license_accepted)
  {
   install_start = true;
   echoln("");
  }
  else
  {
   echoln("Do you accept the license agreement? [y]/n ", preview_mode ? preview_mode_license_url : release_license_url);
   char answer[4];
   console_read(answer);
   string_lower(answer);
   install_start = string_is(answer, "") | string_is(answer, "y") | string_is(answer, "yes");
  }
  if (!install_start)
  {
   echoln("License was not accepted, cannot proceed further.");
   app_exit(1);
  }
 }
 // download install files
 if (!download_service_start())
  die("download service start failed", L"Download service start failed");
 char msvc_path[MAX_PATH * 3];
 string_join(msvc_path, temp_path, "\\msvc");
 char sdk_path[MAX_PATH * 3];
 string_join(sdk_path, temp_path, "\\sdk");
 folder_create(msvc_path);
 folder_create(sdk_path);
 {
  char msvc_packages[32][128];
  i64 msvc_packages_count = 0;
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.visualcpp.dia.sdk");
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.crt.headers.base", msvc_version);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.crt.source.base", msvc_version);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.asan.headers.base", msvc_version);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.pgo.headers.base", msvc_version);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.tools.host%s.target%s.base", msvc_version, host_arch, target_arch);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.tools.host%s.target%s.res.base", msvc_version, host_arch, target_arch);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.crt.%s.desktop.base", msvc_version, target_arch);
  if(spectre)
  {
      wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.crt.%s.desktop.spectre.base", msvc_version, target_arch);
  }
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.crt.%s.store.base", msvc_version, target_arch);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.premium.tools.host%s.target%s.base", msvc_version, host_arch, target_arch);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.pgo.%s.base", msvc_version, target_arch);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.asan.x86.base", msvc_version);
  wsprintfA(msvc_packages[msvc_packages_count++], "microsoft.vc.%s.asan.x64.base", msvc_version);
  if(atlmfc)
  {
      [&](){
          for(const auto& comp1 : {"ATL","MFC"})
          {
              for(const auto& comp2 : {"Headers","Source"})
              {
                  string_join_2(msvc_packages[msvc_packages_count++],".", "Microsoft.VC",msvc_version,comp1,comp2,"base");
              }
          }
          string_join_2(msvc_packages[msvc_packages_count++], ".","Microsoft.VC",msvc_version,target_arch,"base");
          if(spectre)
          {
              string_join_2(msvc_packages[msvc_packages_count++], ".","Microsoft.VC",msvc_version,target_arch,"Spectre","base");
          }
          for(const auto& is_debug : {"",".Debug"})
          {
              string_join_2(msvc_packages[msvc_packages_count++], ".","Microsoft.VC",msvc_version,"MFC",target_arch,is_debug,"base");
              if(spectre)
              {
                  string_join_2(msvc_packages[msvc_packages_count++], ".","Microsoft.VC",msvc_version,"MFC",target_arch,is_debug,"Spectre", "base");
              }
          }
          string_join_2(msvc_packages[msvc_packages_count++], ".","Microsoft.VC",msvc_version,"MFC","Redist",target_arch,"base");
          if(spectre)
          {
              string_join_2(msvc_packages[msvc_packages_count++], ".","Microsoft.VC",msvc_version,"MFC","Redist",target_arch,"Spectre", "base");
          }
          for(const auto& is_debug : {"","Debug"})
          {
              string_join_2(msvc_packages[msvc_packages_count++], ".","Microsoft.VC",msvc_version,"MFC","MBCS",strcmp(target_arch,"X86")==0?"":target_arch,is_debug,"base");
              if(spectre)
              {
                  string_join_2(msvc_packages[msvc_packages_count++], ".","Microsoft.VC",msvc_version,"MFC","MBCS",strcmp(target_arch,"X86")==0?"":target_arch,is_debug,"Spectre","base");
              }
          }
      }();
  }
  char sdk_packages[32][128];
  i64 sdk_packages_count = 0;
  _string_copy(sdk_packages[sdk_packages_count++], sizeof(sdk_packages[0]), "Windows SDK for Windows Store Apps Tools-x86_en-us.msi");
  _string_copy(sdk_packages[sdk_packages_count++], sizeof(sdk_packages[0]), "Windows SDK for Windows Store Apps Headers-x86_en-us.msi");
  _string_copy(sdk_packages[sdk_packages_count++], sizeof(sdk_packages[0]), "Windows SDK for Windows Store Apps Headers OnecoreUap-x86_en-us.msi");
  _string_copy(sdk_packages[sdk_packages_count++], sizeof(sdk_packages[0]), "Windows SDK for Windows Store Apps Libs-x86_en-us.msi");
  _string_copy(sdk_packages[sdk_packages_count++], sizeof(sdk_packages[0]), "Universal CRT Headers Libraries and Sources-x86_en-us.msi");
  wsprintfA(sdk_packages[sdk_packages_count++], "Windows SDK Desktop Libs %s-x86_en-us.msi", target_arch);
  for (i64 i = 0; i < countof(targets); i++)
  {
   const char* t = targets[i];
   wsprintfA(sdk_packages[sdk_packages_count++], "Windows SDK Desktop Headers %s-x86_en-us.msi", t);
   wsprintfA(sdk_packages[sdk_packages_count++], "Windows SDK OnecoreUap Headers %s-x86_en-us.msi", t);
  }
  char sdk_package[32] = {0};
  json_context jc;
  jc.c8 = 0;
  jc.cache_size = 0;
  jc.cache_pos = 0;
  char manifest_path[MAX_PATH * 3];
  string_join(manifest_path, temp_path, "\\", preview_mode ? "BuildToolsManifest.Preview.json" : "BuildToolsManifest.Release.json");
  echoln("Parsing ", preview_mode ? "Preview" : "Release", " Build Tool Manifest...");
  jc.f = file_open(manifest_path, false);
  json_parser p = json_parser_begin(json_file_peek, &jc);
  hope(json_object_key_find(&p, "packages"), "packages not found");
  json_context packages_state = jc;
  while (json_array_next(&p))
  {
   hope(json_object_key_find(&p, "id"), "id not found");
   char id[64];
   json_string_extract(&p, id);
   char id_lower[countof(id)];
   string_copy(id_lower, id);
   string_lower(id_lower);
   bool is_sdk = false;
   is_sdk = string_starts_with(id_lower, "microsoft.visualstudio.component.windows10sdk.") ? true : is_sdk;
   is_sdk = string_starts_with(id_lower, "microsoft.visualstudio.component.windows11sdk.") ? true : is_sdk;
   if (is_sdk & string_ends_with(id_lower, sdk_version))
   {
    hope(json_object_key_find(&p, "dependencies"), "dependencies not found");
    hope(json_object_next(&p), "dependencies is empty");
    json_object_key_extract(&p, sdk_package);
    break;
   }
   json_object_skip(&p);
  }
  json_file_context_restore(&jc, packages_state);
  bool redist_found = false;
  const char* redist_suffix = string_is(target_arch, "arm") ? ".onecore.desktop" : "";
  char redist_name[128];
  wsprintfA(redist_name, "microsoft.vc.%s.crt.redist.%s%s.base", msvc_version, target_arch, redist_suffix);
  while (json_array_next(&p))
  {
   hope(json_object_key_find(&p, "id"), "id not found");
   char id[64];
   json_string_extract(&p, id);
   char id_lower[countof(id)];
   string_copy(id_lower, id);
   string_lower(id_lower);
   if (string_is(id_lower, redist_name))
   {
    redist_found = true;
    _string_copy(msvc_packages[msvc_packages_count++], sizeof(msvc_packages[0]), redist_name);
    break;
   }
   json_object_skip(&p);
  }
  json_file_context_restore(&jc, packages_state);
  if (!redist_found)
  {
   wsprintfA(redist_name, "microsoft.visualcpp.crt.redist.%s%s", target_arch, redist_suffix);
   while (json_array_next(&p))
   {
    hope(json_object_key_find(&p, "id"), "id not found");
    char id[64];
    json_string_extract(&p, id);
    char id_lower[countof(id)];
    string_copy(id_lower, id);
    string_lower(id_lower);
    if (string_is(id_lower, redist_name))
    {
     redist_found = true;
     hope(json_object_key_find(&p, "dependencies"), "dependencies not found");
     while (json_object_next(&p))
     {
      char redist_package[128];
      json_object_key_extract(&p, redist_package);
      if (string_ends_with(redist_package, ".base"))
      {
       string_lower(redist_package);
       _string_join(msvc_packages[msvc_packages_count++], sizeof(msvc_packages[0]), redist_package);
       break;
      }
      json_object_skip(&p);
     }
     break;
    }
    json_object_skip(&p);
   }
  }
  json_file_context_restore(&jc, packages_state);
  for(auto& row : msvc_packages)
  {
      for(auto& c : row)
      {
          c = tolower(c);
      }
  }
  echoln("Downloading the packages...");
  while (json_array_next(&p))
  {
   json_context pkg_state = jc;
   hope(json_object_key_find(&p, "id"), "id not found");
   char id[64];
   json_string_extract(&p, id);
   json_file_context_restore(&jc, pkg_state);
   char lang[8] = {0};
   if (json_object_key_find(&p, "language"))
    json_string_extract(&p, lang);
   json_file_context_restore(&jc, pkg_state);
   char id_lower[countof(id)];
   string_copy(id_lower, id);
   string_lower(id_lower);
   bool msvc_pkg = false;
   for (i64 i = 0; i < msvc_packages_count; i++)
    msvc_pkg = string_is(id_lower, msvc_packages[i]) ? true : msvc_pkg;
   msvc_pkg = (string_is(lang, "en-US") | string_is(lang, "")) ? msvc_pkg : false;
   if (msvc_pkg)
   {
    hope(json_object_key_find(&p, "payloads"), "payloads not found");
    while (json_array_next(&p))
    {
     char file_name[MAX_PATH * 3];
     char url[L_MAX_URL_LENGTH * 3];
     extract_payload_info(&jc, &p, &file_name, &url);
     json_object_skip(&p);
     char path[MAX_PATH * 3];
     string_join(path, msvc_path, "\\", file_name);
     download_file(url, path, file_name);
    }
   }
   if (string_is(id, sdk_package))
   {
    char msi_cabs[64][37];
    i64 msi_cabs_count = 0;
    hope(json_object_key_find(&p, "payloads"), "payloads not found");
    json_context payloads_state = jc;
    // download SDK payloads
    while (json_array_next(&p))
    {
     char file_name[MAX_PATH * 3];
     char url[L_MAX_URL_LENGTH * 3];
     extract_payload_info(&jc, &p, &file_name, &url);
     json_object_skip(&p);
     if (string_trim_left(file_name, "Installers\\"))
     {
      bool sdk_pkg = false;
      for (i64 i = 0; i < sdk_packages_count; i++)
       sdk_pkg = string_is(file_name, sdk_packages[i]) ? true : sdk_pkg;
      if (sdk_pkg)
      {
       char path[MAX_PATH * 3];
       string_join(path, sdk_path, "\\", file_name);
       download_file(url, path, file_name);
       // extract .cab file info
       struct file msi = file_open(path, false);
       char chunk[mem_page_size];
       while (true)
       {
        i64 n = file_read(&msi, chunk, countof(chunk));
        if (n <= 0)
         break;
        for (i64 i = 0; i < n; i++)
        {
         char str[4];
         str[0] = chunk[i];
         str[1] = ((i + 1) < n) ? chunk[i + 1] : 0;
         str[2] = ((i + 2) < n) ? chunk[i + 2] : 0;
         str[3] = ((i + 3) < n) ? chunk[i + 3] : 0;
         bool match = (str[0] == '.') & (str[1] == 'c') & (str[2] == 'a') & (str[3] == 'b');
         if (match && msi_cabs_count < countof(msi_cabs))
         {
          i64 index = msi.pos - countof(chunk) + i;
          struct file cabs_reader = file_open(path, false);
          if (cabs_reader.handle)
          {
           file_seek(&cabs_reader, index - 32);
           if (file_read(&cabs_reader, msi_cabs[msi_cabs_count], 36) == 36)
           {
            msi_cabs[msi_cabs_count][36] = 0;
            msi_cabs_count++;
           }
           file_close(&cabs_reader);
          }
         }
        }
        if (msi.pos == msi.size)
         break;
        file_seek(&msi, msi.pos - 3);
       }
       file_close(&msi);
      }
     }
    }
    json_file_context_restore(&jc, payloads_state);
    // download .cab packages
    while (json_array_next(&p))
    {
     char file_name[MAX_PATH * 3];
     char url[L_MAX_URL_LENGTH * 3];
     extract_payload_info(&jc, &p, &file_name, &url);
     json_object_skip(&p);
     if (string_trim_left(file_name, "Installers\\"))
     {
      bool cab_pkg = false;
      for (i64 i = 0; i < msi_cabs_count; i++)
       cab_pkg = string_is(file_name, msi_cabs[i]) ? true : cab_pkg;
      if (cab_pkg)
      {
       char path[MAX_PATH * 3];
       string_join(path, sdk_path, "\\", file_name);
       download_file(url, path, file_name);
      }
     }
    }
   }
   json_object_skip(&p);
  }
  file_close(&jc.f);
 }
 download_service_stop();
 // install the files
 folder_create(install_path);
 echoln("Installing MSVC...");
 {
  struct folder msvc_folder = folder_open(msvc_path);
  while (folder_next(&msvc_folder))
  {
   char file_path[MAX_PATH * 3];
   string_join(file_path, msvc_path, "\\", msvc_folder.file_name);
   struct file msvc_file = file_open(file_path, false);
   if (!msvc_file.handle)
    continue;
   mz_zip_archive zip_file = {};
   zip_file.m_pAlloc = miniz_malloc;
   zip_file.m_pFree = miniz_free;
   zip_file.m_pRealloc = miniz_realloc;
   zip_file.m_pRead = miniz_file_read;
   zip_file.m_pIO_opaque = &msvc_file;
   if (!mz_zip_reader_init(&zip_file, msvc_file.size, 0))
   {
    file_close(&msvc_file);
    continue;
   }
   i64 n = mz_zip_reader_get_num_files(&zip_file);
   for (i64 i = 0; i < n; i++)
   {
    char zipped_file_name[MAX_PATH * 3];
    if (mz_zip_reader_is_file_a_directory(&zip_file, i))
     continue;
    mz_zip_reader_get_filename(&zip_file, i, zipped_file_name, countof(zipped_file_name));
    if (!string_trim_left(zipped_file_name, "Contents/"))
     continue;
    char zipped_file_path[MAX_PATH * 3];
    string_join(zipped_file_path, install_path, "\\", zipped_file_name);
    // walk the path, creating directories as needed, and converting forward slashes into backslashes
    char* s = zipped_file_path;
    while (*s != 0)
    {
     if (*s == '/')
     {
      *s = 0;
      if (!folder_exists(zipped_file_path))
       folder_create(zipped_file_path);
      *s = '\\';
     }
     s++;
    }
    struct file zf = file_open(zipped_file_path, true);
    if (!zf.handle)
     continue;
    if (!mz_zip_reader_extract_to_callback(&zip_file, i, mz_file_write, &zf, 0))
    {
     file_close(&zf);
     break;
    }
    file_close(&zf);
   }
   mz_zip_end(&zip_file);
   file_close(&msvc_file);
  }
  folder_close(&msvc_folder);
 }
 echoln("Installing SDK...");
 {
  struct folder sdk_folder = folder_open(sdk_path);
  while (folder_next(&sdk_folder))
  {
   if (!string_ends_with(sdk_folder.file_name, ".msi"))
    continue;
   char file_path[MAX_PATH * 3];
   string_join(file_path, sdk_path, "\\", sdk_folder.file_name);
   char params[MAX_CMDLINE_LEN * 3];
   string_join(params, "/a \"", file_path, "\" /quiet TARGETDIR=\"", install_path, "\"");
   WCHAR wparams[MAX_CMDLINE_LEN];
   wparams[0] = 0;
   if (utf8_to_utf16(params, -1, wparams, countof(wparams)) <= 0)
    app_exit(1);
   SHELLEXECUTEINFO info = {};
   info.cbSize = sizeof(SHELLEXECUTEINFO);
   info.fMask = SEE_MASK_NOCLOSEPROCESS;
   info.lpVerb = L"open";
   info.lpFile = L"msiexec.exe";
   info.lpParameters = wparams;
   info.nShow = SW_NORMAL;
   if (!ShellExecuteEx(&info))
    die("msiexec failed", L"msiexec failed");
   WaitForSingleObject(info.hProcess, INFINITE);
   CloseHandle(info.hProcess);
  }
  folder_close(&sdk_folder);
 }
 char msvcv[MAX_PATH * 3];
 {
  string_join_buf(msvcv, install_path, "\\VC\\Tools\\MSVC");
  struct folder msvcv_folder = folder_open(msvcv);
  folder_next(&msvcv_folder);
  memcpy(msvcv, msvcv_folder.file_name, sizeof(msvcv_folder.file_name));
  folder_close(&msvcv_folder);
 }
 char sdkv[MAX_PATH * 3];
 {
  string_join_buf(sdkv, install_path, "\\Windows Kits\\10\\bin");
  struct folder sdkv_folder = folder_open(sdkv);
  folder_next(&sdkv_folder);
  memcpy(sdkv, sdkv_folder.file_name, sizeof(sdkv_folder.file_name));
  folder_close(&sdkv_folder);
 }
 echoln("Installing debug info...");
 char redist_path[MAX_PATH * 3];
 string_join(redist_path, install_path, "\\VC\\Redist");
 if (folder_exists(redist_path))
 {
  char debug_version_path[MAX_PATH * 3];
  string_join(debug_version_path, redist_path, "\\MSVC");
  struct folder debug_version_folder = folder_open(debug_version_path);
  folder_next(&debug_version_folder);
  char debug_path[MAX_PATH * 3];
  string_join(debug_path, debug_version_path, "\\", debug_version_folder.file_name, "\\debug_nonredist");
  folder_close(&debug_version_folder);
  struct folder debug_folder = folder_open(debug_path);
  while (folder_next(&debug_folder))
  {
   char debug_target_path[MAX_PATH * 3];
   string_join(debug_target_path, debug_path, "\\", debug_folder.file_name);
   struct folder debug_target_folder = folder_open(debug_target_path);
   while (folder_next(&debug_target_folder))
   {
    char debug_package_path[MAX_PATH * 3];
    string_join(debug_package_path, debug_target_path, "\\", debug_target_folder.file_name);
    struct folder debug_package_folder = folder_open(debug_package_path);
    while (folder_next(&debug_package_folder))
    {
     char file_path[MAX_PATH * 3];
     string_join(file_path, debug_package_path, "\\", debug_package_folder.file_name);
     char dst_file_path[MAX_PATH * 3];
     string_join(dst_file_path, install_path, "\\VC\\Tools\\MSVC\\", msvcv, "\\bin\\Host", host_arch, "\\", debug_folder.file_name, "\\", debug_package_folder.file_name);
     file_copy(dst_file_path, file_path);
    }
    folder_close(&debug_package_folder);
   }
   folder_close(&debug_target_folder);
  }
  folder_close(&debug_folder);
 }
 echoln("Installing DIA...");
 {
  char msdia[64] = {0};
  if (string_is(host_arch, "x86"))
   string_copy(msdia, "msdia140.dll");
  else
   wsprintfA(msdia, "%s\\msdia140.dll", string_is(host_arch, "x64") ? "amd64" : host_arch);
  char msdia_path[MAX_PATH * 3];
  string_join(msdia_path, install_path, "\\DIA%20SDK\\bin\\", msdia);
  char dst_path[MAX_PATH * 3];
  string_join(dst_path, install_path, "\\VC\\Tools\\MSVC\\", msvcv, "\\bin\\Host", host_arch);
  struct folder dst_folder = folder_open(dst_path);
  while (folder_next(&dst_folder))
  {
   char dst_file_path[MAX_PATH * 3];
   string_join(dst_file_path, dst_path, "\\", dst_folder.file_name, "\\msdia140.dll");
   file_copy(dst_file_path, msdia_path);
  }
  folder_close(&dst_folder);
 }
 echoln("Creating a batch setup script...");
 {
     build_batch(install_path, msvcv, sdkv, atlmfc, spectre);
 }
 echoln("Creating a PowerShell setup script...");
 {
     build_powershell(install_path, msvcv, sdkv, atlmfc, spectre);
 }
 echoln("Cleanup...");
 {
  folder_delete(redist_path);
  struct folder install_folder = folder_open(install_path);
  while (folder_next(&install_folder))
  {
   char file_path[MAX_PATH * 3];
   string_join(file_path, install_path, "\\", install_folder.file_name);
   if (string_ends_with(install_folder.file_name, ".msi"))
    file_delete(file_path);
   if (string_starts_with(install_folder.file_name, "DIA%20SDK"))
    folder_delete(file_path);
   if (string_is(install_folder.file_name, "Common7"))
    folder_delete(file_path);
  }
  folder_close(&install_folder);
  char folder_to_remove[MAX_PATH * 4];
  char msvc_base[MAX_PATH * 3];
  char sdk_base[MAX_PATH * 3];
  string_join_buf(msvc_base, install_path, "\\VC\\Tools\\MSVC\\", msvcv);
  string_join_buf(sdk_base, install_path, "\\Windows Kits\\10");
  wsprintfA(folder_to_remove, "%s\\Auxiliary", msvc_base);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\bin\\Host%s\\%s\\onecore", msvc_base, target_arch, host_arch);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\lib\\%s\\store", msvc_base, target_arch);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\lib\\%s\\uwp", msvc_base, target_arch);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\lib\\%s\\enclave", msvc_base, target_arch);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\lib\\%s\\onecore", msvc_base, target_arch);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\Catalogs", sdk_base);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\DesignTime", sdk_base);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\bin\\%s\\chpe", sdk_base, sdkv);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\Lib\\%s\\ucrt_enclave", sdk_base, sdkv);
  folder_delete(folder_to_remove);
  wsprintfA(folder_to_remove, "%s\\Lib\\%s\\ucrt", sdk_base, sdkv);
  delete_children_except(folder_to_remove, target_arch, false);
  wsprintfA(folder_to_remove, "%s\\Lib\\%s\\um", sdk_base, sdkv);
  delete_children_except(folder_to_remove, target_arch, false);
  wsprintfA(folder_to_remove, "%s\\bin", msvc_base);
  delete_children_except(folder_to_remove, host_arch, true);
  wsprintfA(folder_to_remove, "%s\\bin\\%s", sdk_base, sdkv);
  delete_children_except(folder_to_remove, host_arch, false);
  {
   char telemetry_path[MAX_PATH * 4];
   wsprintfA(telemetry_path, "%s\\bin\\Host%s\\%s\\vctip.exe", msvc_base, host_arch, target_arch);
   file_delete(telemetry_path);
   folder_delete(temp_path);
  }
  { // nvcc bodge
   char vcvars_path[MAX_PATH * 3];
   string_join(vcvars_path, install_path, "\\VC\\Auxiliary\\Build");
   folder_create(vcvars_path);
   char vcvarsall_path[MAX_PATH * 3];
   string_join(vcvarsall_path, vcvars_path, "\\vcvarsall.bat");
   struct file f = file_open(vcvarsall_path, true);
   file_close(&f);
   char vcvars64_path[MAX_PATH * 3];
   string_join(vcvars64_path, vcvars_path, "\\vcvars64.bat");
   f = file_open(vcvars64_path, true);
   file_close(&f);
  }
 }
 if (env_mode != EnvMode::none)
 {
  echoln("Setting up the environment...");
  HKEY location = (env_mode == EnvMode::global) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  LPCWSTR subkey = (env_mode == EnvMode::global) ? L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment" : L"Environment";
  char buf[MAX_ENV_LEN * 3];
  char msvc_base[MAX_PATH * 3];
  char sdk_base[MAX_PATH * 3];
  string_join_buf(msvc_base, install_path, "\\VC\\Tools\\MSVC\\", msvcv);
  string_join_buf(sdk_base, install_path, "\\Windows Kits\\10");
  env_set(location, subkey, L"VCToolsInstallDir", msvc_base);
  env_set(location, subkey, L"WindowsSDKDir", sdk_base);
  env_set(location, subkey, L"WindowsSDKVersion", sdkv);
  env_set(location, subkey, L"VSCMD_ARG_HOST_ARCH", host_arch);
  env_set(location, subkey, L"VSCMD_ARG_TGT_ARCH", target_arch);
  build_include_env(buf, sdkv, atlmfc);
  env_set(location, subkey, L"INCLUDE", buf);
  memset(buf, 0 ,sizeof(buf));
  build_lib_env(buf, sdkv, atlmfc, spectre);
  env_set(location, subkey, L"LIB", buf);
  memset(buf, 0 ,sizeof(buf));
  string_join_buf(buf,
   "%VCToolsInstallDir%", "\\bin\\Host", host_arch, "\\", target_arch, ";",
   "%WindowsSDKDir%", "\\bin\\", sdkv, "\\", target_arch, ";",
  "%WindowsSDKDir%", "\\bin\\", sdkv, "\\", target_arch, "\\ucrt\n"
  );
  env_set(location, subkey, L"BUILD_TOOLS_BIN", buf);

  WCHAR wpath[MAX_ENV_LEN];
  wpath[0] = 0;
  char path[countof(wpath) * 3];
  path[0] = 0;
  DWORD wpath_size = sizeof(wpath);
  LSTATUS err = RegGetValue(location, subkey, L"Path", RRF_RT_REG_SZ | RRF_NOEXPAND, null, wpath, &wpath_size);
  err = (err == ERROR_UNSUPPORTED_TYPE) ? 0 : err;
  i64 wpath_count = (err != 0) ? 0 : (wpath_size / sizeof(WCHAR));
  if (utf16_to_utf8(wpath, wpath_count, path, countof(path)) <= 0)
   path[0] = 0;
  // split path into strings by ;
  i64 path_count = strlen(path);
  for (i64 i = 0; i < path_count; i++)
   path[i] = (path[i] == ';') ? 0 : path[i];
  // check if %BUILD_TOOLS_BIN% is among them
  bool has_build_tools_bin = false;
  for (i64 i = 0; i < path_count; i++)
  {
   char* entry = &path[i];
   has_build_tools_bin = string_is(entry, "%BUILD_TOOLS_BIN%") ? true : has_build_tools_bin;
   i += strlen(entry);
  }
  // join the strings back
  for (i64 i = 0; i < path_count; i++)
   path[i] = (path[i] == 0) ? ';' : path[i];
  // add %BUILD_TOOLS_BIN% if not present
  string_append(path, has_build_tools_bin ? "" : ";%BUILD_TOOLS_BIN%");
  string_trim_right(path, ";");
  string_trim_left(path, ";");
  {
   WCHAR wpath[MAX_ENV_LEN];
   wpath[0] = 0;
   int n = utf8_to_utf16(path, -1, wpath, countof(wpath));
   if (n > 0)
    RegSetKeyValue(location, subkey, L"Path", REG_EXPAND_SZ, wpath, n * sizeof(WCHAR));
  }
 }
 echoln("Done!");
 if (subprocess_mode)
 {
  const WCHAR* wmsg = L"PortableBuildTools was installed successfully.";
  wmsg = (env_mode == EnvMode::user) ? L"Please log out and back in to finish installation." : wmsg;
  wmsg = (env_mode == EnvMode::global) ? L"Please reboot the machine to finish installation." : wmsg;
  MessageBox(GetConsoleWindow(), wmsg, L"Success!", MB_TOPMOST | MB_ICONINFORMATION);
 }
 else
 {
  const char* msg = "PortableBuildTools was installed successfully.";
  msg = (env_mode == EnvMode::user) ? "Please log out and back in to finish installation." : msg;
  msg = (env_mode == EnvMode::global) ? "Please reboot the machine to finish installation." : msg;
  echoln(msg);
 }
}
