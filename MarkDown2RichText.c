// Convert Markdown to RTF - 2022 by Thomas Fuhringer, released under the terms of the GPL
// Enhanced with lists, blockquotes, strikethrough, horizontal rules, escape characters, code blocks, and tables
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#include <winhttp.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")
#endif

static __declspec(thread) char* path;
static __declspec(thread) int top_of_page = 1;

// State variables for multi-line constructs
static __declspec(thread) int in_fenced_code = 0;
static __declspec(thread) char fence_char = 0;
static __declspec(thread) int fence_len = 0;
static __declspec(thread) int in_table = 0;
static __declspec(thread) int table_col_count = 0;
static __declspec(thread) int embed_images = 1;

typedef struct PendingImageTag {
	char* marker;
	char* path;
} PendingImage;

static __declspec(thread) PendingImage* pending_images = NULL;
static __declspec(thread) int pending_image_count = 0;
static __declspec(thread) int pending_image_capacity = 0;

static char*
get_line(char** input)
{
	char* pos = *input;
	size_t length = strlen(pos);

	if (length == 0)
		return NULL;

	char* line = NULL;
	char* eol = strchr(pos, '\n');

	if (!eol) {
		line = (char*)malloc(length + 1);
		memcpy(line, pos, length);
		line[length] = 0;
		*input = pos + length;
	}
	else {
		length = eol - pos;
		line = (char*)malloc(length + 1);
		memcpy(line, pos, length);
		if (length > 0 && line[length - 1] == '\r')
			line[length - 1] = 0;
		else
			line[length] = 0;

		*input = pos + length + 1;
	}

	return line;
}

static __declspec(thread) char* rtf;
static __declspec(thread) size_t buffer_size;
static __declspec(thread) size_t buffer_len;
const static char* hex_digits = "0123456789ABCDEF";

static int
get_image_format(const char* file_name);

char* markdown2rtf_ex(const char* md, const char* img_path, int enable_images);
void markdown_clear_pending_images(void);
int markdown_get_pending_image_count(void);
const char* markdown_get_pending_image_marker(int index);
const char* markdown_get_pending_image_path(int index);

static void
append_buffer(const char* str)
{
	size_t length = strlen(str);
	size_t required = buffer_len + length + 1; // +1 for terminator
	if (required > buffer_size) {
		size_t new_size = buffer_size ? buffer_size : 1024;
		while (new_size < required) {
			new_size = new_size * 2 + length;
		}
		char* new_rtf = realloc(rtf, new_size);
		if (new_rtf == NULL)
			return;
		rtf = new_rtf;
		buffer_size = new_size;
	}
	memcpy(rtf + buffer_len, str, length);
	buffer_len += length;
	rtf[buffer_len] = '\0';
}

static void
clear_pending_images_internal(void)
{
	int i;

	if (pending_images == NULL)
		return;

	for (i = 0; i < pending_image_count; ++i) {
		if (pending_images[i].marker != NULL)
			free(pending_images[i].marker);
		if (pending_images[i].path != NULL)
			free(pending_images[i].path);
	}
	free(pending_images);
	pending_images = NULL;
	pending_image_count = 0;
	pending_image_capacity = 0;
}

static int
append_pending_image(const char* marker, const char* path_utf8)
{
	PendingImage* expanded;

	if (marker == NULL || path_utf8 == NULL)
		return 0;

	if (pending_image_count >= pending_image_capacity) {
		int new_capacity = pending_image_capacity == 0 ? 8 : pending_image_capacity * 2;
		expanded = (PendingImage*)realloc(pending_images, sizeof(PendingImage) * new_capacity);
		if (expanded == NULL)
			return 0;
		memset(expanded + pending_image_capacity, 0, sizeof(PendingImage) * (new_capacity - pending_image_capacity));
		pending_images = expanded;
		pending_image_capacity = new_capacity;
	}

	pending_images[pending_image_count].marker = _strdup(marker);
	pending_images[pending_image_count].path = _strdup(path_utf8);
	if (pending_images[pending_image_count].marker == NULL || pending_images[pending_image_count].path == NULL) {
		if (pending_images[pending_image_count].marker != NULL) {
			free(pending_images[pending_image_count].marker);
			pending_images[pending_image_count].marker = NULL;
		}
		if (pending_images[pending_image_count].path != NULL) {
			free(pending_images[pending_image_count].path);
			pending_images[pending_image_count].path = NULL;
		}
		return 0;
	}

	pending_image_count++;
	return 1;
}

void
markdown_clear_pending_images(void)
{
	clear_pending_images_internal();
}

int
markdown_get_pending_image_count(void)
{
	return pending_image_count;
}

const char*
markdown_get_pending_image_marker(int index)
{
	if (index < 0 || index >= pending_image_count)
		return NULL;
	return pending_images[index].marker;
}

const char*
markdown_get_pending_image_path(int index)
{
	if (index < 0 || index >= pending_image_count)
		return NULL;
	return pending_images[index].path;
}

static void
append_hex_bytes(const unsigned char* data, size_t length)
{
	char hex[3] = "00";
	size_t i;

	for (i = 0; i < length; ++i) {
		hex[0] = hex_digits[data[i] >> 4 & 0xF];
		hex[1] = hex_digits[data[i] & 0xF];
		append_buffer(hex);
	}
}

static unsigned int
read_be32(const unsigned char* data)
{
	return ((unsigned int)data[0] << 24) |
		((unsigned int)data[1] << 16) |
		((unsigned int)data[2] << 8) |
		(unsigned int)data[3];
}

static int
read_jpeg_dimensions(FILE* file, int* width, int* height)
{
	int marker_prefix;
	int marker_type;

	if (file == NULL || width == NULL || height == NULL)
		return 0;

	if (fgetc(file) != 0xFF || fgetc(file) != 0xD8)
		return 0;

	for (;;) {
		int segment_len;
		int hi;
		int lo;

		do {
			marker_prefix = fgetc(file);
		} while (marker_prefix == 0xFF);

		if (marker_prefix == EOF)
			return 0;
		marker_type = marker_prefix;

		while (marker_type == 0xFF) {
			marker_type = fgetc(file);
			if (marker_type == EOF)
				return 0;
		}

		if (marker_type == 0xD9 || marker_type == 0xDA)
			return 0;
		if (marker_type >= 0xD0 && marker_type <= 0xD7)
			continue;

		hi = fgetc(file);
		lo = fgetc(file);
		if (hi == EOF || lo == EOF)
			return 0;

		segment_len = (hi << 8) | lo;
		if (segment_len < 2)
			return 0;

		if ((marker_type >= 0xC0 && marker_type <= 0xC3) ||
			(marker_type >= 0xC5 && marker_type <= 0xC7) ||
			(marker_type >= 0xC9 && marker_type <= 0xCB) ||
			(marker_type >= 0xCD && marker_type <= 0xCF)) {
			int precision = fgetc(file);
			int height_hi = fgetc(file);
			int height_lo = fgetc(file);
			int width_hi = fgetc(file);
			int width_lo = fgetc(file);
			(void)precision;

			if (height_hi == EOF || height_lo == EOF || width_hi == EOF || width_lo == EOF)
				return 0;

			*height = (height_hi << 8) | height_lo;
			*width = (width_hi << 8) | width_lo;
			return *width > 0 && *height > 0;
		}

		if (fseek(file, segment_len - 2, SEEK_CUR) != 0)
			return 0;
	}
}

static int
read_png_dimensions(FILE* file, int* width, int* height)
{
	unsigned char header[24];
	static const unsigned char png_signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

	if (file == NULL || width == NULL || height == NULL)
		return 0;

	if (fread(header, 1, sizeof(header), file) != sizeof(header))
		return 0;
	if (memcmp(header, png_signature, sizeof(png_signature)) != 0)
		return 0;
	if (memcmp(header + 12, "IHDR", 4) != 0)
		return 0;

	*width = (int)read_be32(header + 16);
	*height = (int)read_be32(header + 20);
	return *width > 0 && *height > 0;
}

static int
read_image_dimensions(FILE* file, int img_format, int* width, int* height)
{
	int success = 0;

	if (file == NULL || width == NULL || height == NULL)
		return 0;

	*width = 0;
	*height = 0;

	if (fseek(file, 0, SEEK_SET) != 0)
		return 0;

	if (img_format == 1)
		success = read_png_dimensions(file, width, height);
	else if (img_format == 2)
		success = read_jpeg_dimensions(file, width, height);

	if (fseek(file, 0, SEEK_SET) != 0)
		return 0;

	return success;
}

static void
append_picture_header(int img_format, int width, int height)
{
	char buf[64];

	append_buffer("\\par\\qc{\\pict\\");
	if (img_format == 1)
		append_buffer("pngblip");
	else
		append_buffer("jpegblip");

	if (width > 0 && height > 0) {
		sprintf_s(buf, sizeof(buf), "\\picw%d\\pich%d\\picwgoal%d\\pichgoal%d ",
			width, height, width * 15, height * 15);
	}
	else {
		strcpy_s(buf, sizeof(buf), "\\picscalex100\\picscaley100\\picscaled1 ");
	}
	append_buffer(buf);
}

static int
is_http_url(const char* value)
{
	if (value == NULL)
		return 0;
	return _strnicmp(value, "http://", 7) == 0 || _strnicmp(value, "https://", 8) == 0;
}

static int
get_image_format_from_reference(const char* file_name)
{
	char reference[1024];
	size_t i = 0;

	if (file_name == NULL)
		return 0;

	while (file_name[i] != '\0' && file_name[i] != '?' && file_name[i] != '#' && i < sizeof(reference) - 1) {
		reference[i] = file_name[i];
		i++;
	}
	reference[i] = '\0';
	return get_image_format(reference);
}

static void
trim_image_reference(char* text)
{
	char* start;
	char* end;
	char* alias;

	if (text == NULL)
		return;

	start = text;
	while (*start == ' ' || *start == '\t')
		start++;
	if (start != text)
		memmove(text, start, strlen(start) + 1);

	alias = strchr(text, '|');
	if (alias != NULL)
		*alias = '\0';

	end = text + strlen(text);
	while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
		end--;
	*end = '\0';
}

static char*
resolve_local_image_path_utf8(const char* file_name)
{
#ifdef _WIN32
	WCHAR base_path_w[MAX_PATH * 4];
	WCHAR search_dir_w[MAX_PATH * 4];
	WCHAR file_name_w[MAX_PATH * 4];
	WCHAR full_path_w[MAX_PATH * 4];
	WCHAR canonical_path[MAX_PATH * 4];
	int size_needed;
	char* result;

	if (file_name == NULL || is_http_url(file_name))
		return NULL;

	if (path == NULL)
		return NULL;
	if (MultiByteToWideChar(CP_UTF8, 0, path, -1, base_path_w, _countof(base_path_w)) == 0)
		return NULL;
	if (MultiByteToWideChar(CP_UTF8, 0, file_name, -1, file_name_w, _countof(file_name_w)) == 0)
		return NULL;

	if (!PathIsRelativeW(file_name_w)) {
		wcscpy_s(full_path_w, _countof(full_path_w), file_name_w);
		if (!PathCanonicalizeW(canonical_path, full_path_w))
			return NULL;
	}
	else {
		wcscpy_s(search_dir_w, _countof(search_dir_w), base_path_w);
		for (;;) {
			if (swprintf_s(full_path_w, _countof(full_path_w), L"%s\\%s", search_dir_w, file_name_w) <= 0)
				return NULL;
			if (PathCanonicalizeW(canonical_path, full_path_w) && PathFileExistsW(canonical_path))
				break;
			if (!PathRemoveFileSpecW(search_dir_w))
				return NULL;
		}
	}

	size_needed = WideCharToMultiByte(CP_UTF8, 0, canonical_path, -1, NULL, 0, NULL, NULL);
	if (size_needed <= 0)
		return NULL;

	result = (char*)malloc(size_needed);
	if (result == NULL)
		return NULL;
	if (WideCharToMultiByte(CP_UTF8, 0, canonical_path, -1, result, size_needed, NULL, NULL) == 0) {
		free(result);
		return NULL;
	}
	return result;
#else
	char full_path[1024];
	char* result;

	if (file_name == NULL)
		return NULL;
	sprintf_s(full_path, sizeof(full_path), "%s/%s", path, file_name);
	result = _strdup(full_path);
	return result;
#endif
}

#ifdef _WIN32
static BOOL
download_url_bytes(const char* url, unsigned char** data_out, size_t* length_out)
{
	BOOL success = FALSE;
	WCHAR url_w[2048];
	URL_COMPONENTS components;
	HINTERNET hSession = NULL;
	HINTERNET hConnect = NULL;
	HINTERNET hRequest = NULL;
	WCHAR host_name[256];
	WCHAR url_path[2048];
	DWORD flags = 0;
	unsigned char* buffer = NULL;
	size_t length = 0;

	*data_out = NULL;
	*length_out = 0;

	if (MultiByteToWideChar(CP_UTF8, 0, url, -1, url_w, (int)(sizeof(url_w) / sizeof(url_w[0]))) == 0)
		return FALSE;

	ZeroMemory(&components, sizeof(components));
	components.dwStructSize = sizeof(components);
	components.lpszHostName = host_name;
	components.dwHostNameLength = (DWORD)(sizeof(host_name) / sizeof(host_name[0]));
	components.lpszUrlPath = url_path;
	components.dwUrlPathLength = (DWORD)(sizeof(url_path) / sizeof(url_path[0]));
	components.dwSchemeLength = (DWORD)-1;

	if (!WinHttpCrackUrl(url_w, 0, 0, &components))
		goto cleanup;

	if (components.nScheme == INTERNET_SCHEME_HTTPS)
		flags |= WINHTTP_FLAG_SECURE;

	hSession = WinHttpOpen(L"mdview/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (hSession == NULL)
		goto cleanup;

	hConnect = WinHttpConnect(hSession, host_name, components.nPort, 0);
	if (hConnect == NULL)
		goto cleanup;

	hRequest = WinHttpOpenRequest(hConnect, L"GET", url_path, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (hRequest == NULL)
		goto cleanup;

	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
		goto cleanup;
	if (!WinHttpReceiveResponse(hRequest, NULL))
		goto cleanup;

	for (;;) {
		DWORD available = 0;
		unsigned char* new_buffer;

		if (!WinHttpQueryDataAvailable(hRequest, &available))
			goto cleanup;
		if (available == 0)
			break;

		new_buffer = (unsigned char*)realloc(buffer, length + available);
		if (new_buffer == NULL)
			goto cleanup;
		buffer = new_buffer;

		if (!WinHttpReadData(hRequest, buffer + length, available, &available))
			goto cleanup;
		length += available;
	}

	*data_out = buffer;
	*length_out = length;
	buffer = NULL;
	success = TRUE;

cleanup:
	if (buffer != NULL)
		free(buffer);
	if (hRequest != NULL)
		WinHttpCloseHandle(hRequest);
	if (hConnect != NULL)
		WinHttpCloseHandle(hConnect);
	if (hSession != NULL)
		WinHttpCloseHandle(hSession);
	return success;
}
#endif

static void
append_char(char c)
{
	char str[2] = { c, 0 };
	append_buffer(str);
}

static void
append_rtf_char(char c)
{
	if (c == '\\')
		append_buffer("\\\\");
	else if (c == '{')
		append_buffer("\\{");
	else if (c == '}')
		append_buffer("\\}");
	else
		append_char(c);
}

#ifdef _WIN32
LPWSTR toW(const char* strTextUTF8);
#endif

// Get image format from file extension
// Returns: 1 for PNG, 2 for JPEG, 0 for unknown
static int
get_image_format(const char* file_name)
{
	const char* ext = strrchr(file_name, '.');
	if (ext == NULL)
		return 0;
	
	// Convert to lowercase for comparison
	char lower_ext[8] = {0};
	int i = 0;
	while (ext[i] && i < 7) {
		lower_ext[i] = (char)tolower((unsigned char)ext[i]);
		i++;
	}
	
	if (strcmp(lower_ext, ".png") == 0)
		return 1;  // PNG
	else if (strcmp(lower_ext, ".jpg") == 0 || strcmp(lower_ext, ".jpeg") == 0)
		return 2;  // JPEG
	else if (strcmp(lower_ext, ".webp") == 0)
		return 3;  // WebP
	return 0;  // Unknown
}

static void
append_image(const char* file_name)
{
	char reference[1024];

	if (!embed_images)
		return;
	if (file_name == NULL)
		return;

	strncpy_s(reference, sizeof(reference), file_name, _TRUNCATE);
	trim_image_reference(reference);
	if (reference[0] == '\0')
		return;

	{
		char marker[64];
		char* resolved_path;

		if (is_http_url(reference))
			resolved_path = _strdup(reference);
		else {
			int img_format = get_image_format_from_reference(reference);
			if (img_format == 0)
				return;  // Unsupported local format
			resolved_path = resolve_local_image_path_utf8(reference);
		}

		if (resolved_path == NULL)
			return;

		sprintf_s(marker, sizeof(marker), "MDVIEWIMG_%04d", pending_image_count + 1);
		if (!append_pending_image(marker, resolved_path)) {
			free(resolved_path);
			return;
		}
		free(resolved_path);
		append_buffer(marker);
		return;
	}
}

static int
is_horizontal_rule(const char* line)
{
	const char* p = line;
	char c = 0;
	int count = 0;

	int spaces = 0;
	while (*p == ' ' && spaces < 3) {
		p++;
		spaces++;
	}

	if (*p != '-' && *p != '*' && *p != '_')
		return 0;

	c = *p;
	while (*p) {
		if (*p == c)
			count++;
		else if (*p != ' ')
			return 0;
		p++;
	}

	return count >= 3;
}

static int
is_fenced_code_start(const char* line, char* fc, int* fl)
{
	const char* p = line;
	int count = 0;

	int spaces = 0;
	while (*p == ' ' && spaces < 3) {
		p++;
		spaces++;
	}

	if (*p != '`' && *p != '~')
		return 0;

	char c = *p;
	while (*p == c) {
		count++;
		p++;
	}

	if (count >= 3) {
		*fc = c;
		*fl = count;
		return 1;
	}
	return 0;
}

static int
is_fenced_code_end(const char* line, char fc, int fl)
{
	const char* p = line;
	int count = 0;

	int spaces = 0;
	while (*p == ' ' && spaces < 3) {
		p++;
		spaces++;
	}

	while (*p == fc) {
		count++;
		p++;
	}

	while (*p) {
		if (*p != ' ' && *p != '\t')
			return 0;
		p++;
	}

	return count >= fl;
}

static int
is_indented_code(const char* line)
{
	if (line[0] == '\t')
		return 1;
	if (strncmp(line, "    ", 4) == 0)
		return 1;
	return 0;
}

static int
count_indent(const char* line)
{
	int indent = 0;
	const char* p = line;
	while (*p == ' ' || *p == '\t') {
		if (*p == '\t')
			indent += 4;
		else
			indent++;
		p++;
	}
	return indent / 2;
}

static int
is_unordered_list(const char* line)
{
	const char* p = line;

	while (*p == ' ' || *p == '\t')
		p++;

	if ((*p == '*' || *p == '+' || *p == '-') && *(p + 1) == ' ')
		return (int)(p - line);

	return -1;
}

// Check if line is a task list item and return checkbox state
// Returns: 0 = unchecked, 1 = checked, -1 = not a task list
static int
is_task_list(const char* line, int* marker_pos)
{
	const char* p = line;

	while (*p == ' ' || *p == '\t')
		p++;

	if ((*p == '*' || *p == '+' || *p == '-') && *(p + 1) == ' ') {
		*marker_pos = (int)(p - line);
		const char* check = p + 2;
		
		// Check for [ ] or [x] or [X]
		if (*check == '[' && *(check + 2) == ']') {
			if (*(check + 1) == ' ')
				return 0; // Unchecked
			else if (*(check + 1) == 'x' || *(check + 1) == 'X')
				return 1; // Checked
		}
	}

	return -1;
}

static int
is_ordered_list(const char* line)
{
	const char* p = line;

	while (*p == ' ' || *p == '\t')
		p++;

	if (!isdigit((unsigned char)*p))
		return -1;

	while (isdigit((unsigned char)*p))
		p++;

	if (*p == '.' && *(p + 1) == ' ')
		return (int)(p - line + 1);

	return -1;
}

static int
is_blockquote(const char* line)
{
	const char* p = line;

	int spaces = 0;
	while (*p == ' ' && spaces < 3) {
		p++;
		spaces++;
	}

	return *p == '>';
}

static int
get_blockquote_depth(const char* line, int* content_start)
{
	const char* p = line;
	int depth = 0;

	while (1) {
		while (*p == ' ')
			p++;

		if (*p == '>') {
			depth++;
			p++;
			if (*p == ' ')
				p++;
		}
		else {
			break;
		}
	}

	*content_start = (int)(p - line);
	return depth;
}

static int
is_table_row(const char* line)
{
	const char* p = line;

	while (*p == ' ' || *p == '\t')
		p++;

	if (*p != '|')
		return 0;

	if (strchr(p + 1, '|') == NULL)
		return 0;

	return 1;
}

static int
is_table_separator(const char* line)
{
	const char* p = line;

	while (*p == ' ' || *p == '\t')
		p++;

	if (*p != '|')
		return 0;

	p++;
	int has_dash = 0;

	while (*p) {
		if (*p == '-' || *p == ':')
			has_dash = 1;
		else if (*p != ' ' && *p != '\t' && *p != '|')
			return 0;
		p++;
	}

	return has_dash;
}

static int
count_table_columns(const char* line)
{
	int count = 0;
	const char* p = line;

	while (*p == ' ' || *p == '\t')
		p++;

	const char* start = p;

	while (*p) {
		if (*p == '|')
			count++;
		p++;
	}

	if (count > 0 && *(p - 1) == '|')
		count--;

	if (count > 0 && *start == '|')
		count--;

	return count > 0 ? count : 1;
}

// Trim trailing # characters and spaces from heading text
static char*
trim_trailing_hashes(char* text)
{
	if (!text || *text == 0)
		return text;
	
	int len = (int)strlen(text);
	int end = len - 1;
	
	// Trim trailing spaces and # characters
	while (end >= 0 && (text[end] == ' ' || text[end] == '#')) {
		end--;
	}
	
	// Null-terminate at the new end
	text[end + 1] = 0;
	return text;
}

// Check if the string at p starts with a valid Setext underline
// Returns length of line to skip if match, otherwise 0
// type: 1 for '=', 2 for '-'
static int
get_setext_underline(const char* p, int* type)
{
	const char* start = p;
	int spaces = 0;
	while (*p == ' ' && spaces < 4) {
		p++;
		spaces++;
	}

	char c = *p;
	if (c != '=' && c != '-')
		return 0;

	*type = (c == '=') ? 1 : 2;

	while (*p == c)
		p++;
	
	while (*p == ' ' || *p == '\t')
		p++;
		
	if (*p == '\r') p++;
	if (*p == '\n') p++;
	else if (*p != 0) return 0; // Must span entire line (or end of string)

	return (int)(p - start);
}

static __declspec(thread) int bold_state = 0;
static __declspec(thread) int italic_state = 0;
static __declspec(thread) int strike_state = 0;
static __declspec(thread) int code_state = 0;
static __declspec(thread) int sup_state = 0;
static __declspec(thread) int sub_state = 0;
static __declspec(thread) int html_u_state = 0;

static int
has_closing_single_emphasis(const char* p, char marker)
{
	const char* q;

	if (!p || *p != marker || *(p + 1) == marker)
		return 0;

	q = p + 1;
	while (*q) {
		if (*q == '\\' && *(q + 1) != 0) {
			q += 2;
			continue;
		}
		if (*q == marker && *(q + 1) == marker) {
			q += 2;
			continue;
		}
		if (*q == marker)
			return 1;
		q++;
	}
	return 0;
}

static int
has_closing_double_emphasis(const char* p, char marker)
{
	const char* q;

	if (!p || p[0] != marker || p[1] != marker || p[2] == marker)
		return 0;

	q = p + 2;
	while (*q) {
		if (*q == '\\' && *(q + 1) != 0) {
			q += 2;
			continue;
		}
		if (q[0] == marker && q[1] == marker && q[2] != marker)
			return 1;
		q++;
	}
	return 0;
}

// Only treat a single '~' as subscript delimiter if it can be closed later in the same line.
// This prevents common range expressions like "50°~115°" from enabling subscript until EOF.
static int
has_closing_single_tilde(const char* p)
{
	if (!p || *p != '~')
		return 0;
	
	const char* q = p + 1;
	while (*q) {
		// Skip escaped characters (e.g., \~)
		if (*q == '\\' && *(q + 1) != 0) {
			q += 2;
			continue;
		}
		
		// Skip strikethrough marker '~~'
		if (*q == '~' && *(q + 1) == '~') {
			q += 2;
			continue;
		}
		
		if (*q == '~')
			return 1;
		
		q++;
	}
	return 0;
}

static int
append_html_entity(const char* entity)
{
	if (strncmp(entity, "&nbsp;", 6) == 0) {
		append_char(' ');
		return 1;
	}
	else if (strncmp(entity, "&amp;", 5) == 0) {
		append_char('&');
		return 1;
	}
	else if (strncmp(entity, "&lt;", 4) == 0) {
		append_char('<');
		return 1;
	}
	else if (strncmp(entity, "&gt;", 4) == 0) {
		append_char('>');
		return 1;
	}
	else if (strncmp(entity, "&quot;", 6) == 0) {
		append_char('"');
		return 1;
	}
	else if (strncmp(entity, "&apos;", 6) == 0) {
		append_char('\'');
		return 1;
	}
	else if (strncmp(entity, "&copy;", 6) == 0) {
		append_buffer("(c)");
		return 1;
	}
	else if (strncmp(entity, "&reg;", 5) == 0) {
		append_buffer("(R)");
		return 1;
	}
	else if (strncmp(entity, "&trade;", 7) == 0) {
		append_buffer("(TM)");
		return 1;
	}
	else if (strncmp(entity, "&mdash;", 7) == 0) {
		append_buffer("--");
		return 1;
	}
	else if (strncmp(entity, "&ndash;", 7) == 0) {
		append_buffer("-");
		return 1;
	}
	else if (strncmp(entity, "&bull;", 6) == 0) {
		append_buffer("*");
		return 1;
	}
	else if (entity[0] == '&' && entity[strlen(entity) - 1] == ';') {
		return 1;
	}
	return 0;
}

static int
process_html_tag(char** pos_ptr)
{
	char* pos = *pos_ptr;
	
	if (*pos != '<') {
		return 0;
	}
	
	char* tag_close = strchr(pos, '>');
	if (!tag_close) {
		return 0;
	}
	
	size_t tag_len = (size_t)(tag_close - pos + 1);
	
	char html_tag_name[32];
	char* p = pos + 1;
	
	int is_closing = 0;
	if (*p == '/') {
		is_closing = 1;
		p++;
	}
	
	char* name_start = p;
	while (*p && *p != ' ' && *p != '\t' && *p != '/' && *p != '>') {
		p++;
	}
	
	int name_len = (int)(p - name_start);
	if (name_len <= 0 || name_len >= 32) {
		return 0;
	}
	
	memcpy(html_tag_name, name_start, (size_t)name_len);
	html_tag_name[name_len] = 0;
	
	for (int i = 0; html_tag_name[i]; i++) {
		html_tag_name[i] = (char)tolower((unsigned char)html_tag_name[i]);
	}
	
	if (strcmp(html_tag_name, "b") == 0 || strcmp(html_tag_name, "strong") == 0) {
		if (is_closing) {
			if (bold_state > 0) {
				append_buffer("\\b0 ");
				bold_state--;
			}
		}
		else {
			append_buffer("\\b1 ");
			bold_state++;
		}
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "i") == 0 || strcmp(html_tag_name, "em") == 0) {
		if (is_closing) {
			if (italic_state > 0) {
				append_buffer("\\i0 ");
				italic_state--;
			}
		}
		else {
			append_buffer("\\i1 ");
			italic_state++;
		}
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "u") == 0) {
		if (is_closing) {
			if (html_u_state > 0) {
				append_buffer("\\ul0 ");
				html_u_state--;
			}
		}
		else {
			append_buffer("\\ul ");
			html_u_state++;
		}
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "s") == 0 || strcmp(html_tag_name, "strike") == 0 || strcmp(html_tag_name, "del") == 0) {
		if (is_closing) {
			if (strike_state > 0) {
				append_buffer("\\strike0 ");
				strike_state--;
			}
		}
		else {
			append_buffer("\\strike ");
			strike_state++;
		}
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "code") == 0) {
		if (is_closing) {
			if (code_state > 0) {
				append_buffer("}");
				code_state--;
			}
		}
		else {
			append_buffer("{\\f1\\highlight2 ");
			code_state++;
		}
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "sub") == 0) {
		if (is_closing) {
			append_buffer("\\nosupersub ");
		}
		else {
			append_buffer("\\sub ");
		}
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "sup") == 0) {
		if (is_closing) {
			append_buffer("\\nosupersub ");
		}
		else {
			append_buffer("\\super ");
		}
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "br") == 0) {
		append_buffer("\\par\n");
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "hr") == 0) {
		append_buffer("\\pard\\sa150\\sl0\\slmult0 {\\strike \\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab} \\par\\pard\n");
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "a") == 0) {
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "/a") == 0) {
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "blockquote") == 0) {
		if (is_closing) {
			append_buffer("}\\pard\n");
		}
		else {
			append_buffer("{\\pard\\li360\\ri360\\cf3\\highlight2 ");
		}
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	if (strcmp(html_tag_name, "p") == 0 || strcmp(html_tag_name, "div") == 0 ||
		 strcmp(html_tag_name, "h1") == 0 || strcmp(html_tag_name, "h2") == 0 ||
		 strcmp(html_tag_name, "h3") == 0 || strcmp(html_tag_name, "h4") == 0 ||
		 strcmp(html_tag_name, "h5") == 0 || strcmp(html_tag_name, "h6") == 0 ||
		 strcmp(html_tag_name, "li") == 0) {
		if (is_closing) {
			append_buffer("\\par\n");
		}
		*pos_ptr = pos + tag_len;
		return 1;
	}
	
	return 0;
}

static void
append_buffer_line(char* line)
{
	char* pos = line;
	while (*pos != 0)
	{
		// Handle HTML entities first
		if (*pos == '&' && strchr(pos, ';') != NULL) {
			const char* entity_end = strchr(pos, ';');
			size_t entity_len = (size_t)(entity_end - pos + 1);
			
			if (entity_len < 32) {
				char saved_char = *(pos + entity_len);
				*(pos + entity_len) = 0;
				if (append_html_entity(pos)) {
					*(pos + entity_len) = saved_char;
					pos += entity_len;
					continue;
				}
				*(pos + entity_len) = saved_char;
			}
		}
		
		if (process_html_tag(&pos)) {
			continue;
		}
		
		if (*pos == '\\' && *(pos + 1) != 0) {
			char next = *(pos + 1);
			if (next == '\\' || next == '`' || next == '*' || next == '_' ||
				next == '{' || next == '}' || next == '[' || next == ']' ||
				next == '(' || next == ')' || next == '#' || next == '+' ||
				next == '-' || next == '.' || next == '!' || next == '|' ||
				next == '-' || next == '.' || next == '!' || next == '|' ||
				next == '~' || next == '>' || next == '^') {
				append_rtf_char(next);
				pos += 2;
				continue;
			}
		}

		if (*pos == '`' && !code_state) {
			int backticks = 1;
			if (*(pos + 1) == '`') backticks = 2;

			char* end = pos + backticks;
			int found = 0;
			while (*end) {
				if (*end == '`') {
					int end_ticks = 1;
					if (backticks == 2 && *(end + 1) == '`') end_ticks = 2;
					if (end_ticks == backticks) {
						*pos = 0;
						append_buffer("{\\f1\\highlight2 ");
						pos += backticks;
						*end = 0;
						while (*pos) {
							append_rtf_char(*pos);
							pos++;
						}
						append_buffer("}");
						pos = end + backticks;
						found = 1;
						break;
					}
				}
				end++;
			}
			if (!found) {
				append_char('`');
				pos++;
			}
			continue;
		}

		if (strncmp(pos, "~~", 2) == 0 && *(pos + 2) != '~') {
			*pos = 0;
			*(pos + 1) = 0;
			if (strike_state) {
				append_buffer("\\strike0 ");
				strike_state = 0;
			}
			else {
				append_buffer("\\strike ");
				strike_state = 1;
			}
			pos += 2;
			continue;
		}

		// Subscript using single tildes: ~text~
		// Important: do not toggle on a lone '~' (e.g., numeric ranges "50°~115°").
		if (*pos == '~' && *(pos + 1) != '~') {
			if (sub_state) {
				*pos = 0;
				append_buffer("\\nosupersub ");
				sub_state = 0;
				pos += 1;
				continue;
			}
			else {
				if (!has_closing_single_tilde(pos)) {
					append_rtf_char('~');
					pos += 1;
					continue;
				}
				*pos = 0;
				append_buffer("\\sub ");
				sub_state = 1;
				pos += 1;
				continue;
			}
		}

		if (strncmp(pos, "^", 1) == 0) {
			*pos = 0;
			if (sup_state) {
				append_buffer("\\nosupersub ");
				sup_state = 0;
			}
			else {
				append_buffer("\\super ");
				sup_state = 1;
			}
			pos += 1;
			continue;
		}

		if ((strncmp(pos, "**", 2) == 0 && strncmp(pos + 2, "*", 1) != 0) ||
			(strncmp(pos, "__", 2) == 0 && strncmp(pos + 2, "_", 1) != 0)) {
			if (!bold_state && !has_closing_double_emphasis(pos, *pos)) {
				append_rtf_char(*pos);
				pos++;
				continue;
			}
			*pos = 0;
			if (bold_state) {
				append_buffer("\\b0 ");
				bold_state = 0;
			}
			else {
				append_buffer("\\b1 ");
				bold_state = 1;
			}
			pos += 2;
			continue;
		}

		if ((strncmp(pos, "*", 1) == 0 && strncmp(pos + 1, "*", 1) != 0) ||
			(strncmp(pos, "_", 1) == 0 && strncmp(pos + 1, "_", 1) != 0)) {
			if (!italic_state && !has_closing_single_emphasis(pos, *pos)) {
				append_rtf_char(*pos);
				pos++;
				continue;
			}
			*pos = 0;
			if (italic_state) {
				append_buffer("\\i0 ");
				italic_state = 0;
			}
			else {
				append_buffer("\\i1 ");
				italic_state = 1;
			}
			pos += 1;
			continue;
		}

		// Autolinks: <http://url> or <email@domain>
		if (strncmp(pos, "<", 1) == 0) {
			char* end = strstr(pos + 1, ">");
			if (end) {
				char* url_start = pos + 1;
				int url_len = (int)(end - url_start);
				
				// Check if it's a URL or email
				if (url_len > 0 && (strstr(url_start, "://") || strstr(url_start, "@"))) {
					*end = 0;
					append_buffer("\\ul {\\field{\\*\\fldinst {HYPERLINK \"");
					
					// Add mailto: prefix for emails if not present
					if (strchr(url_start, '@') && !strstr(url_start, "://")) {
						append_buffer("mailto:");
					}
					
					append_buffer(url_start);
					append_buffer("\" }}{\\fldrslt {");
					append_buffer(url_start);
					append_buffer("}}}\\ul0 ");
					pos = end + 1;
					continue;
				}
			}
		}

		if (strncmp(pos, "![[", 3) == 0) {
			char* end = strstr(pos + 3, "]]");
			if (end) {
				*end = 0;
				append_image(pos + 3);
				pos = end + 2;
				continue;
			}
		}

		if (strncmp(pos, "![", 2) == 0) {
			char* middle = strstr(pos + 2, "](");
			if (middle) {
				middle += 2;
				char* end = strstr(middle, ")");
				if (end) {
					*end = 0;
					append_image(middle);
					pos = end + 1;
					continue;
				}
			}
		}

		if (strncmp(pos, "[", 1) == 0) {
			char* text_end = strstr(pos + 1, "](");
			if (text_end) {
				char* url_start = text_end + 2;
				char* url_end = strstr(url_start, ")");
				if (url_end) {
					*text_end = 0;
					*url_end = 0;
					append_buffer("\\ul {\\field{\\*\\fldinst {HYPERLINK \"");
					append_buffer(url_start);
					append_buffer("\" }}{\\fldrslt {");
					append_buffer(pos + 1);
					append_buffer("}}}\\ul0 ");
					pos = url_end + 1;
					continue;
				}
			}
		}

		append_rtf_char(*pos);
		pos++;
	}
	
	// Safety: prevent unmatched ~ / ^ from leaking formatting into following lines.
	if (sub_state) {
		append_buffer("\\nosupersub ");
		sub_state = 0;
	}
	if (sup_state) {
		append_buffer("\\nosupersub ");
		sup_state = 0;
	}
}

static void
output_table_cell(const char* cell, int is_header)
{
	if (is_header)
		append_buffer("\\b ");

	while (*cell == ' ')
		cell++;

	char* trimmed = (char*)malloc(strlen(cell) + 1);
	strcpy_s(trimmed, strlen(cell) + 1, cell);

	int len = (int)strlen(trimmed);
	while (len > 0 && trimmed[len - 1] == ' ') {
		trimmed[len - 1] = 0;
		len--;
	}

	append_buffer_line(trimmed);
	free(trimmed);

	if (is_header)
		append_buffer("\\b0 ");

	append_buffer("\\cell ");
}

static void
process_table_row(const char* line, int is_header, int col_count)
{
	int col_width = 8000 / col_count;

	append_buffer("\\trowd\\trqc ");

	for (int i = 1; i <= col_count; i++) {
		char cellx[128];
		sprintf_s(cellx, sizeof(cellx), "\\clbrdrt\\brdrw10\\brdrs\\clbrdrl\\brdrw10\\brdrs\\clbrdrb\\brdrw10\\brdrs\\clbrdrr\\brdrw10\\brdrs\\cellx%d ", i * col_width);
		append_buffer(cellx);
	}

	append_buffer("\\intbl ");

	const char* p = line;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '|')
		p++;

	char cell[512];
	int cell_idx = 0;
	int col = 0;

	while (*p && col < col_count) {
		if (*p == '|' || *p == 0) {
			cell[cell_idx] = 0;
			output_table_cell(cell, is_header);
			cell_idx = 0;
			col++;
			if (*p == '|')
				p++;
		}
		else {
			if (cell_idx < 510)
				cell[cell_idx++] = *p;
			p++;
		}
	}

	while (col < col_count) {
		cell[cell_idx] = 0;
		output_table_cell(cell, is_header);
		cell_idx = 0;
		col++;
	}

	append_buffer("\\row\n");
}

char*
markdown2rtf(const char* md, const char* img_path)
{
	return markdown2rtf_ex(md, img_path, 1);
}

char*
markdown2rtf_ex(const char* md, const char* img_path, int enable_images)
{
	size_t md_len = strlen(md);
	buffer_size = (md_len * 4) + 4096; // heuristic
	buffer_len = 0;
	rtf = malloc(buffer_size);
	if (rtf == NULL)
		return NULL;
	rtf[0] = 0;
	char* pos = (char*)md;
	char* line;
	path = (char*)img_path;
	embed_images = enable_images ? 1 : 0;
	clear_pending_images_internal();

	in_fenced_code = 0;
	in_table = 0;
	bold_state = 0;
	italic_state = 0;
	strike_state = 0;
	code_state = 0;
	sup_state = 0;
	sub_state = 0;

	append_buffer("{\\rtf\\ansi\\f0\\fnil \\sl300 {\\fonttbl {\\f0 Arial;}{\\f1 Courier New;}{\\f2 Symbol;}}");
	append_buffer("{\\colortbl;\\red5\\green10\\blue221;\\red235\\green235\\blue235;\\red102\\green102\\blue102;\\red200\\green200\\blue200;}");
	append_buffer("\\fs22\n");

	// Skip YAML front matter if present
	if (strncmp(md, "---\n", 4) == 0 || strncmp(md, "---\r\n", 5) == 0) {
		char* yaml_line = get_line(&pos);
		if (yaml_line != NULL) {
			free(yaml_line); // Skip the opening "---" line
			
			// Skip all YAML content until closing marker
			while ((yaml_line = get_line(&pos)) != NULL) {
				// Note: get_line() strips trailing newlines, so compare without \n
				if (strcmp(yaml_line, "---") == 0 || strcmp(yaml_line, "...") == 0) {
					break; // Found closing marker, stop skipping
				}
				free(yaml_line); // Skip this YAML line
			}
			
			if (yaml_line != NULL) {
				free(yaml_line); // Free the closing marker line
			}
		}
	}

	int prev_list_depth = 0;
	int in_blockquote = 0;
	int blockquote_first_line = 0;
	int prev_was_indented_code = 0;

	while ((line = get_line(&pos)) != NULL)
	{
		int line_len = (int)strlen(line);

		if (in_fenced_code) {
			if (is_fenced_code_end(line, fence_char, fence_len)) {
				append_buffer("}\\par\\pard\n");
				in_fenced_code = 0;
			}
			else {
				for (int i = 0; i < line_len; i++) {
					append_rtf_char(line[i]);
				}
				append_buffer("\\line\n");
			}
			free(line);
			continue;
		}

		char fc;
		int fl;
		if (is_fenced_code_start(line, &fc, &fl)) {
			in_fenced_code = 1;
			fence_char = fc;
			fence_len = fl;
			append_buffer("{\\f1\\fs20\\highlight2\\par ");
			free(line);
			continue;
		}

		if (is_horizontal_rule(line)) {
			if (prev_list_depth > 0) {
				append_buffer("\\par\\pard\n");
				prev_list_depth = 0;
			}
			// Use strikethrough spaces to simulate a horizontal line (widely compatible)
			// Using 20 tabs to cover most window widths
			append_buffer("\\pard\\sa150\\sl0\\slmult0 {\\strike \\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab\\tab} \\par\\pard\n");
			free(line);
			continue;
		}

		if (is_table_row(line)) {
			if (!in_table) {
				in_table = 1;
				table_col_count = count_table_columns(line);
				process_table_row(line, 1, table_col_count);
			}
			else if (is_table_separator(line)) {
			}
			else {
				process_table_row(line, 0, table_col_count);
			}
			free(line);
			continue;
		}
		else if (in_table) {
			in_table = 0;
			append_buffer("\\pard\\par\n");
		}

		if (is_blockquote(line)) {
			int content_start;
			int depth = get_blockquote_depth(line, &content_start);

			if (depth != in_blockquote) {
				if (in_blockquote > 0) {
					append_buffer("\\cell\\row\\pard\\par}\n");
				}
				in_blockquote = depth;
				blockquote_first_line = 1;

				{
					char rowfmt[512];
					int bar_width = 80 * (depth > 0 ? depth : 1);
					int content_width = 10800;
					int cell1 = bar_width;
					int cell2 = cell1 + content_width;
					sprintf_s(
						rowfmt,
						sizeof(rowfmt),
						"{\\trowd\\trgaph0\\trleft0\\trwWidth0\\trftsWidth3"
						"\\trbrdrt\\brdrnil\\trbrdrl\\brdrnil\\trbrdrb\\brdrnil\\trbrdrr\\brdrnil"
						"\\trbrdrh\\brdrnil\\trbrdrv\\brdrnil"
						"\\clbrdrt\\brdrnil\\clbrdrl\\brdrnil\\clbrdrb\\brdrnil\\clbrdrr\\brdrnil"
						"\\clcbpat4\\cellx%d"
						"\\clbrdrt\\brdrnil\\clbrdrl\\brdrnil\\clbrdrb\\brdrnil\\clbrdrr\\brdrnil"
						"\\clcbpat2\\cellx%d\n",
						cell1,
						cell2
					);
					append_buffer(rowfmt);
					append_buffer("\\pard\\intbl\\cell\n");
					append_buffer("\\pard\\intbl\\cf3 ");
				}
			}
			if (!blockquote_first_line) {
				append_buffer("\\line ");
			}
			append_buffer_line(line + content_start);
			blockquote_first_line = 0;

			free(line);
			continue;
		}
		else if (in_blockquote > 0) {
			append_buffer("\\cell\\row\\pard\\par}\n");
			in_blockquote = 0;
			blockquote_first_line = 0;
		}

		if (is_indented_code(line) && prev_list_depth == 0) {
			if (!prev_was_indented_code) {
				append_buffer("{\\f1\\fs20\\highlight2 ");
			}
			const char* code_start = line;
			if (line[0] == '\t')
				code_start = line + 1;
			else
				code_start = line + 4;

			append_buffer_line((char*)code_start);
			append_buffer("\\line\n");
			prev_was_indented_code = 1;
			free(line);
			continue;
		}
		else if (prev_was_indented_code) {
			append_buffer("}\\par\\pard\n");
			prev_was_indented_code = 0;
		}

		int ul_pos = is_unordered_list(line);
		int ol_pos = is_ordered_list(line);
		int task_marker_pos = 0;
		int task_state = is_task_list(line, &task_marker_pos);

		if (task_state >= 0) {
			// Task list item
			int depth = count_indent(line);
			int indent = (depth + 1) * 360;

			char list_fmt[128];
			sprintf_s(list_fmt, sizeof(list_fmt), "{\\pard\\fi-180\\li%d ", indent);
			append_buffer(list_fmt);
			
			// Render checkbox using Unicode code points (U+2610/U+2611)
			if (task_state == 0)
				append_buffer("{\\u9744?} "); // unchecked
			else
				append_buffer("{\\u9745?} "); // checked

			// Skip past "- [ ] " or "- [x] " (marker + space + checkbox + space)
			append_buffer_line(line + task_marker_pos + 6);
			append_buffer("\\par}\n");

			prev_list_depth = depth + 1;
			free(line);
			continue;
		}
		else if (ul_pos >= 0) {
			// Regular unordered list
			int depth = count_indent(line);
			int indent = (depth + 1) * 360;

			char list_fmt[128];
			sprintf_s(list_fmt, sizeof(list_fmt), "{\\pard\\fi-180\\li%d ", indent);
			append_buffer(list_fmt);
			append_buffer("\\bullet\\tab ");

			append_buffer_line(line + ul_pos + 2);
			append_buffer("\\par}\n");

			prev_list_depth = depth + 1;
			free(line);
			continue;
		}

		if (ol_pos >= 0) {
			int depth = count_indent(line);
			int indent = (depth + 1) * 360;

			const char* p = line;
			while (*p == ' ' || *p == '\t')
				p++;
			char num[16];
			int ni = 0;
			while (isdigit((unsigned char)*p) && ni < 14) {
				num[ni++] = *p++;
			}
			num[ni] = 0;

			char list_fmt[128];
			sprintf_s(list_fmt, sizeof(list_fmt), "{\\pard\\fi-180\\li%d ", indent);
			append_buffer(list_fmt);
			append_buffer(num);
			append_buffer(". ");

			append_buffer_line(line + ol_pos + 1);
			append_buffer("\\par}\n");

			prev_list_depth = depth + 1;
			free(line);
			continue;
		}

		if (prev_list_depth > 0 && ul_pos < 0 && ol_pos < 0) {
			prev_list_depth = 0;
		}

		if (strncmp(line, "# ", 2) == 0) {
			if (top_of_page) {
				append_buffer("{\\fs32\\sb0\\sa100\\b1 ");
				top_of_page = 0;
			}
			else
				append_buffer("{\\par\\fs32\\sb100\\sa100\\b1 ");
			char* heading_text = trim_trailing_hashes(line + 2);
			append_buffer_line(heading_text);
			append_buffer("\\par\\pard}\n");
		}
		else if (strncmp(line, "## ", 3) == 0) {
			append_buffer("{\\par\\fs26\\sb170\\sa100\\b1 ");
			char* heading_text = trim_trailing_hashes(line + 3);
			append_buffer_line(heading_text);
			append_buffer("\\par\\pard}\n");
		}
		else if (strncmp(line, "### ", 4) == 0) {
			append_buffer("{\\par\\pard\\fs24\\sb150\\sa50\\b1 ");
			char* heading_text = trim_trailing_hashes(line + 4);
			append_buffer_line(heading_text);
			append_buffer("\\par}\n");
		}
		else if (strncmp(line, "#### ", 5) == 0) {
			append_buffer("{\\par\\pard\\fs23\\sb120\\sa50\\b1 ");
			char* heading_text = trim_trailing_hashes(line + 5);
			append_buffer_line(heading_text);
			append_buffer("\\par}\n");
		}
		else if (strncmp(line, "##### ", 6) == 0) {
			append_buffer("{\\par\\pard\\fs22\\sb100\\sa50\\b1 ");
			char* heading_text = trim_trailing_hashes(line + 6);
			append_buffer_line(heading_text);
			append_buffer("\\par}\n");
		}
		else if (strncmp(line, "###### ", 7) == 0) {
			append_buffer("{\\par\\pard\\fs21\\sb80\\sa50\\b1 ");
			char* heading_text = trim_trailing_hashes(line + 7);
			append_buffer_line(heading_text);
			append_buffer("\\par}\n");
		}
		else {
			// Check for Setext lookahead
			int setext_type = 0;
			int has_content = 0;
			const char* check = line;
			while (*check) { if (*check != ' ' && *check != '\t') { has_content = 1; break; } check++; }

			int skip_len = (has_content) ? get_setext_underline(pos, &setext_type) : 0;
			
			if (skip_len > 0) {
				// Render as Setext Heading
				if (setext_type == 1) { // H1 ===
					if (top_of_page) {
						append_buffer("{\\fs32\\sb0\\sa100\\b1 ");
						top_of_page = 0;
					}
					else
						append_buffer("{\\par\\fs32\\sb100\\sa100\\b1 ");
				}
				else { // H2 ---
					append_buffer("{\\par\\fs26\\sb170\\sa100\\b1 ");
				}

				append_buffer_line(line);
				append_buffer("\\par\\pard}\n");

				// Skip the underline line
				pos += skip_len;
				
				free(line);
				continue;
			}

			int len = (int)strlen(line);
			if (len >= 2 && strncmp(line + len - 2, "  ", 2) == 0) {
				line[len - 2] = 0;
				append_buffer_line(line);
				append_buffer("\\par\n");
			}
			else if (len > 0) {
				append_buffer_line(line);
				append_buffer("\\par\n");
			}
			else {
				append_buffer("\\par\n");
			}
		}
		free(line);
	}

	if (in_blockquote) {
		append_buffer("\\cell\\row\\pard\\par}\n");
	}
	if (prev_was_indented_code) {
		append_buffer("}\\par\\pard\n");
	}
	if (in_fenced_code) {
		append_buffer("}\\par\\pard\n");
	}
	if (in_table) {
		append_buffer("\\pard\\par\n");
	}

	append_buffer("}\n\0");
	top_of_page = 1;
	embed_images = 1;
	return rtf;
}
