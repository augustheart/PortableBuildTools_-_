#include "app_config.h"
#include "base.h"

nat args_count;
const char* args[MAX_CMDLINE_LEN_UTF8 / 3];
bool invoked_from_console;
HINSTANCE instance;
bool com_initialized;

[[noreturn]]
void app_exit(int exit_code)
{
 if (com_initialized)
 {
  CoUninitialize();
  com_initialized = false;
 }
 ExitProcess(exit_code);
}

void check_privilege(void)
{
 HANDLE token = null;
 if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
 {
  echoln("check_privilege: OpenProcessToken failed, error ", itos(GetLastError()));
  return;
 }

 DWORD size = 0;
 GetTokenInformation(token, TokenPrivileges, null, 0, &size);
 if (size == 0)
 {
  echoln("check_privilege: GetTokenInformation size failed, error ", itos(GetLastError()));
  CloseHandle(token);
  return;
 }

 TOKEN_PRIVILEGES* privileges = cast(TOKEN_PRIVILEGES*, HeapAlloc(GetProcessHeap(), 0, size));
 if (!privileges)
 {
  echoln("check_privilege: HeapAlloc failed");
  CloseHandle(token);
  return;
 }

 if (!GetTokenInformation(token, TokenPrivileges, privileges, size, &size))
 {
  echoln("check_privilege: GetTokenInformation failed, error ", itos(GetLastError()));
  HeapFree(GetProcessHeap(), 0, privileges);
  CloseHandle(token);
  return;
 }

 nat enabled_count = 0;
 nat available_count = 0;
 for (DWORD i = 0; i < privileges->PrivilegeCount; i++)
 {
  DWORD attributes = privileges->Privileges[i].Attributes;
  if (attributes & SE_PRIVILEGE_ENABLED)
   enabled_count++;
  else if (!(attributes & SE_PRIVILEGE_REMOVED))
   available_count++;
 }

 echoln("token privileges: enabled=", itos(enabled_count), " available=", itos(available_count));
 for (DWORD i = 0; i < privileges->PrivilegeCount; i++)
 {
  DWORD attributes = privileges->Privileges[i].Attributes;
  const char* state;
  if (attributes & SE_PRIVILEGE_ENABLED)
   state = "[enabled] ";
  else if (!(attributes & SE_PRIVILEGE_REMOVED))
   state = "[can enable] ";
  else
   continue;

  WCHAR name[256];
  DWORD name_count = countof(name);
  if (!LookupPrivilegeNameW(null, &privileges->Privileges[i].Luid, name, &name_count))
   continue;

  char name_utf8[256 * 3];
  WideCharToMultiByte(CP_UTF8, 0, name, -1, name_utf8, countof(name_utf8), null, null);
  echoln(state, name_utf8);
 }

 HeapFree(GetProcessHeap(), 0, privileges);
 CloseHandle(token);
}

nat uchar_size_naive(uchar uc)
{
 byte b = bitcast(byte, uc);
 nat size = 0;
 size = ((b & 0b11111000) == 0b11110000) ? 4 : size;
 size = ((b & 0b11110000) == 0b11100000) ? 3 : size;
 size = ((b & 0b11100000) == 0b11000000) ? 2 : size;
 size = ((b & 0b10000000) == 0b00000000) ? 1 : size;
 size = (b == 0) ? 0 : size;
 return (size);
}

nat uchar_size(uchar uc)
{
 byte* bytes = cast(byte*, &uc);
 nat size = uchar_size_naive(uc);
 size = ((size == 2) & ((bytes[1] & 0b11000000) != 0b10000000)) ? 0 : size;
 size = ((size == 3) & ((bytes[1] & 0b11000000) != 0b10000000)) ? 0 : size;
 size = ((size == 3) & ((bytes[2] & 0b11000000) != 0b10000000)) ? 0 : size;
 size = ((size == 4) & ((bytes[1] & 0b11000000) != 0b10000000)) ? 0 : size;
 size = ((size == 4) & ((bytes[2] & 0b11000000) != 0b10000000)) ? 0 : size;
 size = ((size == 4) & ((bytes[3] & 0b11000000) != 0b10000000)) ? 0 : size;
 return (size);
}

uchar string_decode_uchar(const char* s)
{
 char c;
 char uc[4];
 nat size = uchar_size_naive(s[0]);
 c = s[0];
 uc[0] = (size < 1) ? 0 : c;
 s += c ? 1 : 0;
 c = s[0];
 uc[1] = (size < 2) ? 0 : c;
 s += c ? 1 : 0;
 c = s[0];
 uc[2] = (size < 3) ? 0 : c;
 s += c ? 1 : 0;
 c = s[0];
 uc[3] = (size < 4) ? 0 : c;
 return (bitcast(uchar, uc));
}

nat _string_copy(slice(char, buf), const char* s)
{
 nat count = 0;
 while (*s != 0)
 {
  nat size = uchar_size(string_decode_uchar(s));
  size = clamp_bot(size, 1);
  if (buf_count - count < size + 1)
   break;
  memcpy(&buf[count], s, size);
  s += size;
  count += size;
 }
 buf[count] = 0;
 return (count);
}

nat _string_append(slice(char, buf), const char* s)
{
 nat prev_count = strlen(buf);
 nat count = prev_count;
 while (*s != 0)
 {
  nat size = uchar_size(string_decode_uchar(s));
  size = clamp_bot(size, 1);
  if (buf_count - count < size + 1)
   break;
  memcpy(&buf[count], s, size);
  s += size;
  count += size;
 }
 buf[count] = 0;
 return (count - prev_count);
}

char* __string_join(char* buf, nat buf_count, std::initializer_list<const char*> strings)
{
 buf[0] = 0;
 for (const char* s : strings)
 {
  if (s == null)
   break;
  nat n = _string_append(buf, buf_count, s);
  if (n < strlen(s))
   break;
 }
 return (buf);
}

bool string_starts_with(const char* s, const char* x)
{
 bool match = true;
 while (*x != 0)
 {
  match = (*s != *x) ? false : match;
  if (*s == 0)
   break;
  s++;
  x++;
 }
 return (match);
}

bool string_ends_with(const char* s, const char* x)
{
 nat s_count = strlen(s);
 nat x_count = strlen(x);
 nat diff = (s_count > x_count) ? (s_count - x_count) : 0;
 return string_starts_with(s + diff, x);
}

bool string_trim_left(char* s, const char* x)
{
 nat s_count = strlen(s);
 nat x_count = strlen(x);
 bool trim = string_starts_with(s, x);
 nat copy_size = trim ? (s_count - x_count) + 1 : 0;
 memcpy(s, &s[x_count], copy_size);
 return (trim);
}

bool string_trim_right(char* s, const char* x)
{
 nat s_count = strlen(s);
 nat x_count = strlen(x);
 bool trim = string_ends_with(s, x);
 nat index = trim ? (s_count - x_count) : s_count;
 s[index] = 0;
 return (trim);
}

nat _console_read(slice(char, buf))
{
 HANDLE in_handle = GetStdHandle(STD_INPUT_HANDLE);
 WCHAR wbuf[256];
 DWORD n = 0;
 if (!ReadConsole(in_handle, wbuf, countof(wbuf), &n, null))
  n = 0;
 while ((n > 0) & ((wbuf[n - 1] == L'\r') | (wbuf[n - 1] == L'\n')))
  n--;
 if (buf_count == 0)
  return (0);
 int size = WideCharToMultiByte(CP_UTF8, 0, wbuf, n, buf, (int)buf_count - 1, null, null);
 if (size < 0)
  size = 0;
 buf[size] = 0;
 return (size);
}

void console_write(const char* s)
{
 HANDLE out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
 WCHAR ws[mem_page_size / sizeof(WCHAR)];
 DWORD written = 0;
 nat len = strlen(s);
 nat pos = 0;
 while (pos < len)
 {
  nat chunk = min(len - pos, countof(ws));
  while ((chunk > 0) & ((s[pos + chunk] & 0xC0) == 0x80))
   chunk--;
  if (chunk == 0)
   chunk = 1;
  int ws_count = MultiByteToWideChar(CP_UTF8, 0, &s[pos], (int)chunk, ws, countof(ws));
  if (ws_count > 0)
   WriteConsole(out_handle, ws, ws_count, &written, null);
  pos += chunk;
 }
}

void _echo(std::initializer_list<const char*> strings)
{
 for (const char* s : strings)
  console_write(s);
}

bool to_wide(const char* path, WCHAR* wpath, int wpath_count)
{
 return (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wpath, wpath_count) > 0);
}

struct file file_open(const char* path, bool write_access)
{
 struct file file = {};
 WCHAR wpath[MAX_PATH];
 if (!to_wide(path, wpath, countof(wpath)))
  return (file);
 DWORD access = write_access ? FILE_GENERIC_WRITE : FILE_GENERIC_READ;
 DWORD attrs = write_access ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_READONLY;
 DWORD disposition = write_access ? CREATE_ALWAYS : OPEN_EXISTING;
 file.handle = CreateFile(wpath, access, FILE_SHARE_READ, null, disposition, attrs, null);
 if (file.handle == INVALID_HANDLE_VALUE)
 {
  file.handle = null;
  return (file);
 }
 GetFileSizeEx(file.handle, cast(LARGE_INTEGER*, &file.size));
 file.pos = 0;
 SetFilePointerEx(file.handle, bitcast(LARGE_INTEGER, file.pos), null, FILE_BEGIN);
 return (file);
}

void file_close(struct file* file)
{
 CloseHandle(file->handle);
 memset(file, 0, sizeof(struct file));
}

n32 file_read(struct file* file, void* buf, n32 buf_count)
{
 DWORD n = 0;
 ReadFile(file->handle, buf, buf_count, &n, null);
 file->pos += n;
 return (n);
}

n32 file_write(struct file* file, const void* data, n32 data_count)
{
 DWORD n = 0;
 WriteFile(file->handle, data, data_count, &n, null);
 file->pos += n;
 return (n);
}

void file_seek(struct file* file, n64 pos)
{
 SetFilePointerEx(file->handle, bitcast(LARGE_INTEGER, pos), cast(LARGE_INTEGER*, &file->pos), FILE_BEGIN);
}

bool file_delete(const char* path)
{
 WCHAR wpath[MAX_PATH];
 if (!to_wide(path, wpath, countof(wpath)))
  return (false);
 if (DeleteFile(wpath))
  return (true);
 bool ok = (GetLastError() == ERROR_FILE_NOT_FOUND);
 return (ok);
}

void file_copy(const char* out_path, const char* in_path)
{
 WCHAR wout[MAX_PATH];
 WCHAR win[MAX_PATH];
 if (!to_wide(out_path, wout, countof(wout)))
  return;
 if (!to_wide(in_path, win, countof(win)))
  return;
 CopyFile(win, wout, false);
}

bool folder_exists(const char* path)
{
 WCHAR wpath[MAX_PATH];
 if (!to_wide(path, wpath, countof(wpath)))
  return (false);
 DWORD attr = GetFileAttributes(wpath);
 return ((attr != INVALID_FILE_ATTRIBUTES) & ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0));
}

struct folder folder_open(const char* path)
{
 struct folder folder = {};
 WCHAR wpath[MAX_PATH];
 char path_fixed[MAX_PATH * 3];
 string_join_buf(path_fixed, path, "\\*");
 if (!to_wide(path_fixed, wpath, countof(wpath)))
  return (folder);
 // skip .
 folder.handle = FindFirstFile(wpath, &folder.data);
 if (folder.handle == INVALID_HANDLE_VALUE)
 {
  folder.handle = null;
  return (folder);
 }
 // skip ..
 FindNextFile(folder.handle, &folder.data);
 return (folder);
}

void folder_close(struct folder* folder)
{
 FindClose(folder->handle);
 memset(folder, 0, sizeof(struct folder));
}

bool folder_next(struct folder* folder)
{
 if (folder->handle == null)
  return (false);
 if (!FindNextFile(folder->handle, &folder->data))
  return (false);
 WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, folder->data.cFileName, -1, folder->file_name, countof(folder->file_name), null, null);
 return (true);
}

bool folder_create(const char* path)
{
 if (path[0] == 0)
  return (false);
 WCHAR wpath[MAX_PATH];
 WCHAR full[MAX_PATH];
 if (!to_wide(path, wpath, countof(wpath)))
  return (false);
 DWORD n = GetFullPathName(wpath, countof(full), full, null);
 if ((n == 0) | (n >= countof(full)))
  return (false);
 int result = SHCreateDirectoryEx(null, full, null);
 return ((result == ERROR_SUCCESS) | (result == ERROR_ALREADY_EXISTS));
}

bool folder_delete(const char* path)
{
 WCHAR wpath[MAX_PATH];
 if (!to_wide(path, wpath, countof(wpath)))
  return (false);
 struct folder folder = folder_open(path);
 while (folder_next(&folder))
 {
  char file_path[MAX_PATH * 3];
  string_join_buf(file_path, path, "\\", folder.file_name);
  if (folder.data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
   folder_delete(file_path);
  else
   file_delete(file_path);
 }
 folder_close(&folder);
 bool ok = RemoveDirectory(wpath);
 if (!ok)
  ok = (GetLastError() == ERROR_FILE_NOT_FOUND);
 return (ok);
}

void sleep(n64 duration)
{
 Sleep(cast(DWORD, clamp_bot(duration / millisecond, 1)));
}

struct _thread_data
{
 thread_callback callback;
 void* data;
 HANDLE signal;
};

DWORD WINAPI _thread(void* data)
{
 struct _thread_data thread_data = {};
 memcpy(&thread_data, data, sizeof(struct _thread_data));
 SetEvent(thread_data.signal);
 thread_data.callback(thread_data.data);
 return (0);
}

HANDLE thread_start(thread_callback callback, void* data)
{
 struct _thread_data thread_data = {};
 thread_data.callback = callback;
 thread_data.data = data;
 thread_data.signal = CreateEvent(null, false, false, null);
 if (!thread_data.signal)
  return (null);
 HANDLE thread = CreateThread(null, 0, _thread, &thread_data, 0, null);
 if (!thread)
 {
  CloseHandle(thread_data.signal);
  return (null);
 }
 WaitForSingleObject(thread_data.signal, INFINITE);
 CloseHandle(thread_data.signal);
 return (thread);
}

void _console_parse_arguments(void)
{
 static char args_global[MAX_CMDLINE_LEN_UTF8];
 int args_position = 0;
 int wargs_count = 0;
 LPCWSTR* wargs = (LPCWSTR*)CommandLineToArgvW(GetCommandLine(), &wargs_count);
 for (int i = 0; i < wargs_count; i++)
 {
  if (args_count >= countof(args))
   break;
  int remain = (int)sizeof(args_global) - args_position;
  if (remain <= 1)
   break;
  int arg_count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wargs[i], -1, &args_global[args_position], remain, null, null);
  if (arg_count <= 0)
   continue;
  args[args_count++] = &args_global[args_position];
  args_position += arg_count;
 }
 LocalFree(wargs);
}

LRESULT CALLBACK _service_proc(HWND service, UINT message, WPARAM wparam, LPARAM lparam)
{
 if (message == WM_APP)
 {
  HWND window = cast(HWND, GetWindowLongPtr(service, GWLP_USERDATA));
  if (window == null)
   window = GetConsoleWindow();
  SetForegroundWindow(window);
  return (0);
 }

 if (message == WM_DESTROY)
 {
  PostQuitMessage(0);
  return (0);
 }

 return DefWindowProc(service, message, wparam, lparam);
}

void message_loop(void)
{
 MSG msg = {};
 while (GetMessage(&msg, null, 0, 0))
  DispatchMessage(&msg);
}

void register_service_class(WNDPROC proc, const WCHAR* name)
{
 WNDCLASS wc = {};
 wc.lpfnWndProc = proc;
 wc.hInstance = instance;
 wc.lpszClassName = name;
 RegisterClass(&wc);
}

void _main_thread(HWND service)
{
 _console_parse_arguments();
 DWORD console_processes[2];
 DWORD console_process_count = GetConsoleProcessList(console_processes, countof(console_processes));
 invoked_from_console = (console_process_count > 1);
 echoln(invoked_from_console ? "console mode" : "gui mode" );
 start();
 SendMessage(service, WM_CLOSE, 0, 0);
}

extern "C"
void _start(void)
{
 instance = GetModuleHandle(null);
 register_service_class(_service_proc, TEXT(appid) L":service");
 HWND service = CreateWindow(TEXT(appid) L":service", null, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, HWND_MESSAGE, null, instance, null);
 if (!service)
  app_exit(1);
 HANDLE main_thread = thread_start((thread_callback)_main_thread, service);
 if (!main_thread)
  app_exit(1);
 CloseHandle(main_thread);
 message_loop();
 app_exit(0);
}

HWND find_service(bool other_process, DWORD* pid_out)
{
 HWND service = null;
 DWORD pid = GetCurrentProcessId();
 while (true)
 {
  service = FindWindowEx(HWND_MESSAGE, service, TEXT(appid) L":service", null);
  if (service == null)
   break;
  DWORD service_pid = 0;
  GetWindowThreadProcessId(service, &service_pid);
  if ((pid == service_pid) != (!other_process))
   continue;
  if (pid_out != null)
   *pid_out = service_pid;
  break;
 }
 return (service);
}

HWND find_app_service(void)
{
 return find_service(false, null);
}

void appid_instance_guard(void)
{
#if defined(__cplusplus)
 if constexpr (appid_name == "0")
  return;
#else
 if (string_is(appid, "0"))
  return;
#endif
 DWORD service_pid = 0;
 HWND service = find_service(true, &service_pid);
 if (service != null)
 {
  AllowSetForegroundWindow(service_pid);
  PostMessage(service, WM_APP, 0, 0);
  app_exit(0);
 }
}
