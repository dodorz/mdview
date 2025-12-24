// Convert Markdown to RTF - 2022 by Thomas Fuhringer, released under the terms of the GPL
// Enhanced with lists, blockquotes, strikethrough, horizontal rules, escape characters, code blocks, and tables
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#endif

static char* path;
static int top_of_page = 1;

// State variables for multi-line constructs
static int in_fenced_code = 0;
static char fence_char = 0;
static int fence_len = 0;
static int in_table = 0;
static int table_col_count = 0;

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

static char* rtf;
static size_t buffer_size;
static size_t buffer_left;

static void
append_buffer(const char* str)
{
	size_t length = strlen(str);
	if (buffer_left < length) {
		buffer_left += buffer_size;
		buffer_size = buffer_size * 2 + length;
		rtf = realloc(rtf, buffer_size);
	}
	strcat(rtf, str);
	buffer_left -= strlen(str);
}

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

static char hex[] = "00";
const static char* hex_digits = "0123456789ABCDEF";
static void
append_image(const char* file_name)
{
	FILE* file;
	char full_path[1024];
#ifdef _WIN32
	char full_path_[1024];
	sprintf(full_path, "%s\\%s", path, file_name);
	LPWSTR path_w = toW(full_path);
	file = _wfopen(path_w, L"rb");
	free(path_w);
#else
	sprintf(full_path, "%s/%s", path, file_name);
	file = fopen(full_path, "rb");
#endif

	if (file == NULL)
		return;
	int c;
	append_buffer("\\par\\qc{\\pict\\pngblip\\picscalex100\\picscaley100\\picscaled1 ");

	while ((c = getc(file)) != EOF) {
		*hex = hex_digits[c >> 4 & 0xF];
		*(hex + 1) = hex_digits[c & 0xF];
		append_buffer(hex);
	}
	append_buffer("\n}\\par\\ql\n");

	fclose(file);
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

static int
is_ordered_list(const char* line)
{
	const char* p = line;

	while (*p == ' ' || *p == '\t')
		p++;

	if (!isdigit(*p))
		return -1;

	while (isdigit(*p))
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

static int bold_state = 0;
static int italic_state = 0;
static int strike_state = 0;
static int code_state = 0;

static void
append_buffer_line(char* line)
{
	char* pos = line;
	while (*pos != 0)
	{
		if (*pos == '\\' && *(pos + 1) != 0) {
			char next = *(pos + 1);
			if (next == '\\' || next == '`' || next == '*' || next == '_' ||
				next == '{' || next == '}' || next == '[' || next == ']' ||
				next == '(' || next == ')' || next == '#' || next == '+' ||
				next == '-' || next == '.' || next == '!' || next == '|' ||
				next == '~' || next == '>') {
				append_rtf_char(next);
				pos += 2;
				continue;
			}
		}

		if (*pos == '`' && !code_state) {
			int backticks = 1;
			if (*(pos + 1) == '`') backticks = 2;

			char* end = pos + backticks;
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
						break;
					}
				}
				end++;
			}
			if (*end == 0) {
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

		if ((strncmp(pos, "**", 2) == 0 && strncmp(pos + 2, "*", 1) != 0) ||
			(strncmp(pos, "__", 2) == 0 && strncmp(pos + 2, "_", 1) != 0)) {
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
}

static void
output_table_cell(const char* cell, int is_header)
{
	if (is_header)
		append_buffer("\\b ");

	while (*cell == ' ')
		cell++;

	char* trimmed = (char*)malloc(strlen(cell) + 1);
	strcpy(trimmed, cell);

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
		sprintf(cellx, "\\clbrdrt\\brdrw10\\brdrs\\clbrdrl\\brdrw10\\brdrs\\clbrdrb\\brdrw10\\brdrs\\clbrdrr\\brdrw10\\brdrs\\cellx%d ", i * col_width);
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
	buffer_size = strlen(md) * 6;
	buffer_left = buffer_size - 1;
	rtf = malloc(buffer_size);
	if (rtf == NULL)
		return "";
	rtf[0] = 0;
	char* pos = (char*)md;
	char* line;
	path = (char*)img_path;

	in_fenced_code = 0;
	in_table = 0;
	bold_state = 0;
	italic_state = 0;
	strike_state = 0;
	code_state = 0;

	append_buffer("{\\rtf\\ansi\\f0\\fnil \\sl300 {\\fonttbl {\\f0 Arial;}{\\f1 Courier New;}{\\f2 Symbol;}}");
	append_buffer("{\\colortbl;\\red5\\green10\\blue221;\\red235\\green235\\blue235;\\red102\\green102\\blue102;}");
	append_buffer("\\fs22\n");

	int prev_list_depth = 0;
	int in_blockquote = 0;
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
			append_buffer("\\pard\\brdrb\\brdrs\\brdrw10\\brsp20 \\par\\pard\n");
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

			if (!in_blockquote) {
				append_buffer("{\\pard\\li360\\ri360\\cf3\\highlight2 ");
				in_blockquote = 1;
			}

			append_buffer_line(line + content_start);
			append_buffer("\\par\n");

			free(line);
			continue;
		}
		else if (in_blockquote) {
			append_buffer("}\\pard\n");
			in_blockquote = 0;
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

		if (ul_pos >= 0) {
			int depth = count_indent(line);
			int indent = (depth + 1) * 360;

			char list_fmt[128];
			sprintf(list_fmt, "{\\pard\\fi-180\\li%d ", indent);
			append_buffer(list_fmt);
			append_buffer("{\\f2\\'B7}\\tab ");

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
			while (isdigit(*p) && ni < 14) {
				num[ni++] = *p++;
			}
			num[ni] = 0;

			char list_fmt[128];
			sprintf(list_fmt, "{\\pard\\fi-180\\li%d ", indent);
			append_buffer(list_fmt);
			append_buffer(num);
			append_buffer(".\\tab ");

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
			append_buffer_line(line + 2);
			append_buffer("\\par\\pard}\n");
		}
		else if (strncmp(line, "## ", 3) == 0) {
			append_buffer("{\\par\\fs26\\sb170\\sa100\\b1 ");
			append_buffer_line(line + 3);
			append_buffer("\\par\\pard}\n");
		}
		else if (strncmp(line, "### ", 4) == 0) {
			append_buffer("{\\par\\pard\\fs24\\sb150\\sa50\\b1 ");
			append_buffer_line(line + 4);
			append_buffer("\\par}\n");
		}
		else if (strncmp(line, "#### ", 5) == 0) {
			append_buffer("{\\par\\pard\\fs23\\sb120\\sa50\\b1 ");
			append_buffer_line(line + 5);
			append_buffer("\\par}\n");
		}
		else if (strncmp(line, "##### ", 6) == 0) {
			append_buffer("{\\par\\pard\\fs22\\sb100\\sa50\\b1 ");
			append_buffer_line(line + 6);
			append_buffer("\\par}\n");
		}
		else {
			int len = (int)strlen(line);
			if (len >= 2 && strncmp(line + len - 2, "  ", 2) == 0) {
				line[len - 2] = 0;
				append_buffer_line(line);
				append_buffer("\\par\n");
			}
			else if (len > 0) {
				append_buffer_line(line);
				append_buffer("\n");
			}
			else {
				append_buffer("\\par\n");
			}
		}
		free(line);
	}

	if (in_blockquote) {
		append_buffer("}\\pard\n");
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
	return rtf;
}
