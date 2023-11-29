#if defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#define NL "\r\n"
#else
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#define NL "\n"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>

#define DEFAULT_PREFIX "sqlgen"
#define DEFAULT_MALLOC "malloc"
#define DEFAULT_FREE "free"
#define DEFAULT_LOG_DBG "printf"
#define DEFAULT_LOG_ERR "printf"
#define DEFAULT_LOG_SQL_ERR "sqlgen_error"
#define PREFIX(sv, data) \
        (sv).len ? (sv) : str_view(DEFAULT_PREFIX), (sv).len ? (data) : DEFAULT_PREFIX
#define MALLOC(sv, data) \
        (sv).len ? (sv) : str_view(DEFAULT_MALLOC), (sv).len ? (data) : DEFAULT_MALLOC
#define FREE(sv, data) \
        (sv).len ? (sv) : str_view(DEFAULT_FREE), (sv).len ? (data) : DEFAULT_FREE
#define LOG_DBG(sv, data) \
        (sv).len ? (sv) : str_view(DEFAULT_LOG_DBG), (sv).len ? (data) : DEFAULT_LOG_DBG
#define LOG_ERR(sv, data) \
        (sv).len ? (sv) : str_view(DEFAULT_LOG_ERR), (sv).len ? (data) : DEFAULT_LOG_ERR
#define LOG_SQL_ERR(sv, data) \
        (sv).len ? (sv) : str_view(DEFAULT_LOG_SQL_ERR), (sv).len ? (data) : DEFAULT_LOG_SQL_ERR

/* ----------------------------------------------------------------------------
 * Platform abstractions & Utilities
 * ------------------------------------------------------------------------- */

struct mfile
{
    void* address;
    int size;
};

#if defined(WIN32)
static void
print_last_win32_error(void)
{
    LPSTR msg;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        GetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg,
        0, NULL);
    fprintf(stderr, "Error: %.*s", (int)size, msg);
    LocalFree(msg);
}
static wchar_t*
utf8_to_utf16(const char* utf8, int utf8_bytes)
{
    int utf16_bytes = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_bytes, NULL, 0);
    if (utf16_bytes == 0)
        return NULL;

    wchar_t* utf16 = malloc((sizeof(wchar_t) + 1) * utf16_bytes);
    if (utf16 == NULL)
        return NULL;

    if (MultiByteToWideChar(CP_UTF8, 0, utf8, utf8_bytes, utf16, utf16_bytes) == 0)
    {
        free(utf16);
        return NULL;
    }

    utf16[utf16_bytes] = 0;

    return utf16;
}
static void
utf_free(void* utf)
{
    free(utf);
}
#endif

static int
mfile_map_read(struct mfile* mf, const char* file_name)
{
#if defined(WIN32)
    HANDLE hFile;
    LARGE_INTEGER liFileSize;
    HANDLE mapping;
    wchar_t* utf16_filename;

    utf16_filename = utf8_to_utf16(file_name, (int)strlen(file_name));
    if (utf16_filename == NULL)
        goto utf16_conv_failed;

    /* Try to open the file */
    hFile = CreateFileW(
        utf16_filename,         /* File name */
        GENERIC_READ,           /* Read only */
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,                   /* Default security */
        OPEN_EXISTING,          /* File must exist */
        FILE_ATTRIBUTE_NORMAL,  /* Default attributes */
        NULL);                  /* No attribute template */
    if (hFile == INVALID_HANDLE_VALUE)
        goto open_failed;

    /* Determine file size in bytes */
    if (!GetFileSizeEx(hFile, &liFileSize))
        goto get_file_size_failed;
    if (liFileSize.QuadPart > (1ULL << 32) - 1)  /* mf->size is an int */
        goto get_file_size_failed;

    mapping = CreateFileMapping(
        hFile,                 /* File handle */
        NULL,                  /* Default security attributes */
        PAGE_READONLY,         /* Read only (or copy on write, but we don't write) */
        0, 0,                  /* High/Low size of mapping. Zero means entire file */
        NULL);                 /* Don't name the mapping */
    if (mapping == NULL)
        goto create_file_mapping_failed;

    mf->address = MapViewOfFile(
        mapping,               /* File mapping handle */
        FILE_MAP_READ,         /* Read-only view of file */
        0, 0,                  /* High/Low offset of where the mapping should begin in the file */
        0);                    /* Length of mapping. Zero means entire file */
    if (mf->address == NULL)
        goto map_view_failed;

    /* The file mapping isn't required anymore */
    CloseHandle(mapping);
    CloseHandle(hFile);
    utf_free(utf16_filename);

    mf->size = (int)liFileSize.QuadPart;

    return 0;

    map_view_failed            : CloseHandle(mapping);
    create_file_mapping_failed :
    get_file_size_failed       : CloseHandle(hFile);
    open_failed                : utf_free(utf16_filename);
    utf16_conv_failed          : return -1;
#else
    struct stat stbuf;
    int fd;

    fd = open(file_name, O_RDONLY);
    if (fd < 0)
        goto open_failed;

    if (fstat(fd, &stbuf) != 0)
        goto fstat_failed;
    if (!S_ISREG(stbuf.st_mode))
        goto fstat_failed;
    mf->address = mmap(NULL, (size_t)stbuf.st_size, PROT_READ, MAP_PRIVATE | MAP_NORESERVE, fd, 0);
    if (mf->address == MAP_FAILED)
        goto mmap_failed;

    /* file descriptor no longer required */
    close(fd);

    mf->size = (int)stbuf.st_size;
    return 0;

    mmap_failed    :
    fstat_failed   : close(fd);
    open_failed    : return -1;
#endif
}

static int
mfile_map_write(struct mfile* mf, const char* file_name, int size)
{
#if defined(WIN32)
    HANDLE hFile;
    HANDLE mapping;
    wchar_t* utf16_filename;

    utf16_filename = utf8_to_utf16(file_name, (int)strlen(file_name));
    if (utf16_filename == NULL)
        goto utf16_conv_failed;

    /* Try to open the file */
    hFile = CreateFileW(
        utf16_filename,         /* File name */
        GENERIC_READ | GENERIC_WRITE, /* Read/write */
        0,
        NULL,                   /* Default security */
        CREATE_ALWAYS,          /* Overwrite any existing, otherwise create */
        FILE_ATTRIBUTE_NORMAL,  /* Default attributes */
        NULL);                  /* No attribute template */
    if (hFile == INVALID_HANDLE_VALUE)
    {
        print_last_win32_error();
        goto open_failed;
    }

    mapping = CreateFileMappingW(
        hFile,                 /* File handle */
        NULL,                  /* Default security attributes */
        PAGE_READWRITE,        /* Read + Write */
        0, size,               /* High/Low size of mapping */
        NULL);                 /* Don't name the mapping */
    if (mapping == NULL)
        goto create_file_mapping_failed;

    mf->address = MapViewOfFile(
        mapping,               /* File mapping handle */
        FILE_MAP_READ | FILE_MAP_WRITE, /* Read + Write */
        0, 0,                  /* High/Low offset of where the mapping should begin in the file */
        0);                    /* Length of mapping. Zero means entire file */
    if (mf->address == NULL)
        goto map_view_failed;

    /* The file mapping isn't required anymore */
    CloseHandle(mapping);
    CloseHandle(hFile);
    utf_free(utf16_filename);

    mf->size = size;

    return 0;

    map_view_failed            : CloseHandle(mapping);
    create_file_mapping_failed : CloseHandle(hFile);
    open_failed                : utf_free(utf16_filename);
    utf16_conv_failed          : return -1;
#else
    int fd = open(file_name, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (fd < 0)
        goto open_failed;

    /* When truncating the file, it must be expanded again, otherwise writes to
     * the memory will cause SIGBUS.
     * NOTE: If this ever gets ported to non-Linux, see posix_fallocate() */
    if (fallocate(fd, 0, 0, size) != 0)
        goto mmap_failed;
    mf->address = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mf->address == MAP_FAILED)
        goto mmap_failed;

    /* file descriptor no longer required */
    close(fd);

    mf->size = size;
    return 0;

    mmap_failed    : close(fd);
    open_failed    : return -1;
#endif
}

void
mfile_unmap(struct mfile* mf)
{
#if defined(WIN32)
    UnmapViewOfFile(mf->address);
#else
    munmap(mf->address, (size_t)mf->size);
#endif
}

struct str_view
{
    int off, len;
};

static struct str_view str_view(const char* cstr)
{
    struct str_view str;
    str.off = 0;
    str.len = strlen(cstr);
    return str;
}
static int str_eq_cstr(const char* s1, struct str_view s2, const char* data)
{
    int len = (int)strlen(s1);
    return len == s2.len && memcmp(data + s2.off, s1, len) == 0;
}
static int str_eq_str(struct str_view s1, struct str_view s2, const char* data)
{
    return s1.len == s2.len && memcmp(data + s1.off, data + s2.off, s1.len) == 0;
}
int str_dec_to_int(struct str_view str, const char* data)
{
    int i;
    int value = 0;
    for (i = 0; i != str.len; ++i)
    {
        char b = data[str.off + i];
        if (b >= '0' && b <= '9')
            value = value * 10 + (b - '0');
        else
            return 0;
    }

    return value;
}

struct mstream
{
    void* address;
    int size;
    int idx;
};

static struct mstream mstream_init_writeable(void)
{
    struct mstream ms;
    ms.address = NULL;
    ms.size = 0;
    ms.idx = 0;
    return ms;
}

static inline void mstream_pad(struct mstream* ms, int additional_size)
{
    while (ms->size < ms->idx + additional_size)
    {
        ms->size = ms->size == 0 ? 32 : ms->size * 2;
        ms->address = realloc(ms->address, ms->size);
    }
}

static inline void mstream_putc(struct mstream* ms, char c)
{
    mstream_pad(ms, 1);
    ((char*)ms->address)[ms->idx++] = c;
}

static inline void mstream_write_int(struct mstream* ms, int value)
{
    char* dst;
    int digit = 1000000000;
    mstream_pad(ms, sizeof("-2147483648") - 1);
    if (value < 0)
    {
        ((char*)ms->address)[ms->idx++] = '-';
        value = -value;
    }
    if (value == 0)
    {
        ((char*)ms->address)[ms->idx++] = '0';
        return;
    }
    while (digit)
    {
        ((char*)ms->address)[ms->idx] = '0';
        if (value >= digit)
            ms->idx++;
        while (value >= digit)
        {
            value -= digit;
            ((char*)ms->address)[ms->idx-1]++;
        }
        digit /= 10;
    }
}

static inline void mstream_cstr(struct mstream* ms, const char* cstr)
{
    int len = (int)strlen(cstr);
    mstream_pad(ms, len);
    memcpy((char*)ms->address + ms->idx, cstr, len);
    ms->idx += len;
}

static inline void mstream_str(struct mstream* ms, struct str_view str, const char* data)
{
    mstream_pad(ms, str.len);
    memcpy((char*)ms->address + ms->idx, data + str.off, str.len);
    ms->idx += str.len;
}

static inline void mstream_fmt(struct mstream* ms, const char* fmt, ...)
{
    int i;
    va_list va;
    va_start(va, fmt);
    for (i = 0; fmt[i]; ++i)
    {
        if (fmt[i] == '%')
            switch (fmt[++i])
            {
                case 's': mstream_cstr(ms, va_arg(va, const char*)); continue;
                case 'i':
                case 'd': mstream_write_int(ms, va_arg(va, int)); continue;
                case 'S': {
                    struct str_view str = va_arg(va, struct str_view);
                    const char* data = va_arg(va, const char*);
                    mstream_str(ms, str, data);
                } continue;
            }
        mstream_putc(ms, fmt[i]);
    }
    va_end(va);
}

/* ----------------------------------------------------------------------------
 * Settings & Command Line
 * ------------------------------------------------------------------------- */

enum backend
{
    BACKEND_SQLITE3 = 0x01
};

struct cfg
{
    const char* input_file;
    const char* output_header;
    const char* output_source;
    enum backend backends;
    unsigned debug_layer        : 1;
    unsigned custom_init        : 1;
    unsigned custom_init_decl   : 1;
    unsigned custom_deinit      : 1;
    unsigned custom_deinit_decl : 1;
    unsigned custom_api         : 1;
    unsigned custom_api_decl    : 1;
    unsigned no_forwards_compat : 1;
};

static int
parse_cmdline(int argc, char** argv, struct cfg* cfg)
{
    int i;
    for (i = 1; i != argc; ++i)
    {
        if (strcmp(argv[i], "-i") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: Missing argument to option -i\n");
                return -1;
            }

            cfg->input_file = argv[++i];
        }
        else if (strcmp(argv[i], "--header") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: Missing argument to option --header\n");
                return -1;
            }

            cfg->output_header = argv[++i];
        }
        else if (strcmp(argv[i], "--source") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: Missing argument to option --source\n");
                return -1;
            }

            cfg->output_source = argv[++i];
        }
        else if (strcmp(argv[i], "-b") == 0)
        {
            char* backend;
            char* p;
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: Missing argument to option -b\n");
                return -1;
            }

            backend = argv[++i];
            p = backend;
            while (1)
            {
                while (*p && *p != ',')
                    ++p;

                if (memcmp(backend, "sqlite3", sizeof("sqlite3") - 1) == 0)
                    cfg->backends |= BACKEND_SQLITE3;
                else
                {
                    *p = 0;
                    fprintf(stderr, "Error: Unknown backend \"%s\"\n", backend);
                    return -1;
                }

                if (!*p)
                    break;

                ++p;
                if (!*p)
                    break;
                backend = p;
            }
        }
        else if (strcmp(argv[i], "--debug-layer") == 0)
            cfg->debug_layer = 1;
        else
        {
            fprintf(stderr, "Error: Unknown option \"%s\"\n", argv[i]);
            return -1;
        }
    }

    if (cfg->backends == 0)
    {
        fprintf(stderr, "Error: No backends were specified. Use -b <backend1,backend2,...>. Supported backends are: sqlite3\n");
        return -1;
    }

    if (cfg->output_header == NULL || !*cfg->output_header)
    {
        fprintf(stderr, "Error: No output header file was specified. Use --header\n");
        return -1;
    }
    if (cfg->output_source == NULL || !*cfg->output_source)
    {
        fprintf(stderr, "Error: No output source file was specified. Use --source\n");
        return -1;
    }

    if (cfg->input_file == NULL || !*cfg->input_file)
    {
        fprintf(stderr, "Error: No input file name was specified. Use -i\n");
        return -1;
    }

    return 0;
}

/* ----------------------------------------------------------------------------
 * Parser
 * ------------------------------------------------------------------------- */

struct parser
{
    const char* data;
    int tail;
    int head;
    int len;
    union {
        struct str_view str;
        int integer;
    } value;
};

static void
parser_init(struct parser* p, struct mfile* mf)
{
    p->data = (char*)mf->address;
    p->head = 0;
    p->tail = 0;
    p->len = mf->size;
}

enum token
{
    TOK_ERROR = -1,
    TOK_END = 0,
    TOK_OPTION = 256,
    TOK_DOXYGEN,
    TOK_STRING,
    TOK_LABEL,
    TOK_INTEGER,
    TOK_HEADER_PREAMBLE,
    TOK_HEADER_POSTAMBLE,
    TOK_SOURCE_INCLUDES,
    TOK_SOURCE_PREAMBLE,
    TOK_SOURCE_POSTAMBLE,
    TOK_UPGRADE,
    TOK_DOWNGRADE,
    TOK_QUERY,
    TOK_PRIVATE_QUERY,
    TOK_FUNCTION,
    TOK_TYPE,
    TOK_TABLE,
    TOK_STMT,
    TOK_BIND,
    TOK_CALLBACK,
    TOK_RETURN
};

static int
print_error(struct parser* p, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);

    fprintf(stderr, "%.*s\n", p->head - p->tail, p->data + p->tail);
    return -1;
}

static enum token
scan_comment_block(struct parser* p)
{
    while (p->head != p->len)
    {
        if (p->data[p->head] == '*' && p->data[p->head+1] == '/')
        {
            p->head += 2;
            return TOK_END;
        }

        p->head++;
    }

    return print_error(p, "Error: Missing \"*/\" closing block comment\n");
}

static void
scan_comment_line(struct parser* p)
{
    while (p->head != p->len)
    {
        if (p->data[p->head] == '\n')
        {
            p->head++;
            break;
        }
        p->head++;
    }
}

static enum token
scan_next_token(struct parser* p)
{
    p->tail = p->head;
    while (p->head != p->len)
    {
        if (p->data[p->head] == '/' && p->data[p->head+1] == '*' && p->data[p->head+2] == '!')
        {
            /* We want to preserve indentation, so scan backwards again as long
             * as there is whitespace */
            while (p->head > 0 && isblank(p->data[p->head-1]))
                p->head--;
            p->value.str.off = p->head;
            if (scan_comment_block(p) < 0)
                return TOK_ERROR;
            p->value.str.len = p->head - p->value.str.off;
            return TOK_DOXYGEN;
        }
        if (p->data[p->head] == '/' && p->data[p->head+1] == '*')
        {
            p->head += 2;
            if (scan_comment_block(p) < 0)
                return TOK_ERROR;
            continue;
        }
        if (p->data[p->head] == '/' && p->data[p->head+1] == '/')
        {
            p->head += 2;
            scan_comment_line(p);
            continue;
        }
        if (isspace(p->data[p->head]))
        {
            p->head++;
            continue;
        }
        /* ".*?" */
        if (p->data[p->head] == '"')
        {
            p->value.str.off = ++p->head;
            for (; p->head != p->len; ++p->head)
                if (p->data[p->head] == '"')
                    break;
            if (p->head == p->len)
                return print_error(p, "Error: Missing closing quote on string\n");
            p->value.str.len = p->head++ - p->value.str.off;
            return TOK_STRING;
        }
        if (memcmp(p->data + p->head, "%option", sizeof("%option") - 1) == 0)
        {
            p->head += sizeof("%option") - 1;
            return TOK_OPTION;
        }
        if (memcmp(p->data + p->head, "%header-preamble", sizeof("%header-preamble") - 1) == 0)
        {
            p->head += sizeof("%header-preamble") - 1;
            return TOK_HEADER_PREAMBLE;
        }
        if (memcmp(p->data + p->head, "%header-postamble", sizeof("%header-postamble") - 1) == 0)
        {
            p->head += sizeof("%header-postamble") - 1;
            return TOK_HEADER_POSTAMBLE;
        }
        if (memcmp(p->data + p->head, "%source-includes", sizeof("%source-includes") - 1) == 0)
        {
            p->head += sizeof("%source-includes") - 1;
            return TOK_SOURCE_INCLUDES;
        }
        if (memcmp(p->data + p->head, "%source-preamble", sizeof("%source-preamble") - 1) == 0)
        {
            p->head += sizeof("%source-preamble") - 1;
            return TOK_SOURCE_PREAMBLE;
        }
        if (memcmp(p->data + p->head, "%source-postamble", sizeof("%source-postamble") - 1) == 0)
        {
            p->head += sizeof("%source-postamble") - 1;
            return TOK_SOURCE_POSTAMBLE;
        }
        if (memcmp(p->data + p->head, "%upgrade", sizeof("%upgrade") - 1) == 0)
        {
            p->head += sizeof("%upgrade") - 1;
            return TOK_UPGRADE;
        }
        if (memcmp(p->data + p->head, "%downgrade", sizeof("%downgrade") - 1) == 0)
        {
            p->head += sizeof("%downgrade") - 1;
            return TOK_DOWNGRADE;
        }
        if (memcmp(p->data + p->head, "%query", sizeof("%query") - 1) == 0)
        {
            p->head += sizeof("%query") - 1;
            return TOK_QUERY;
        }
        if (memcmp(p->data + p->head, "%private-query", sizeof("%private-query") - 1) == 0)
        {
            p->head += sizeof("%private-query") - 1;
            return TOK_PRIVATE_QUERY;
        }
        if (memcmp(p->data + p->head, "%function", sizeof("%function") - 1) == 0)
        {
            p->head += sizeof("%function") - 1;
            return TOK_FUNCTION;
        }
        if (memcmp(p->data + p->head, "type", sizeof("type") - 1) == 0)
        {
            p->head += sizeof("type") - 1;
            return TOK_TYPE;
        }
        if (memcmp(p->data + p->head, "table", sizeof("table") - 1) == 0)
        {
            p->head += sizeof("table") - 1;
            return TOK_TABLE;
        }
        if (memcmp(p->data + p->head, "stmt", sizeof("stmt") - 1) == 0)
        {
            p->head += sizeof("stmt") - 1;
            return TOK_STMT;
        }
        if (memcmp(p->data + p->head, "bind", sizeof("bind") - 1) == 0)
        {
            p->head += sizeof("bind") - 1;
            return TOK_BIND;
        }
        if (memcmp(p->data + p->head, "callback", sizeof("callback") - 1) == 0)
        {
            p->head += sizeof("callback") - 1;
            return TOK_CALLBACK;
        }
        if (memcmp(p->data + p->head, "return", sizeof("return") - 1) == 0)
        {
            p->head += sizeof("return") - 1;
            return TOK_RETURN;
        }
        if (isalpha(p->data[p->head]) || p->data[p->head] == '_')
        {
            p->value.str.off = p->head++;
            while (p->head != p->len && (isalnum(p->data[p->head]) ||
                p->data[p->head] == '-' || p->data[p->head] == '_' || p->data[p->head] == '*'))
            {
                p->head++;
            }
            p->value.str.len = p->head - p->value.str.off;
            return TOK_LABEL;
        }
        if (isdigit(p->data[p->head]))
        {
            struct str_view num = { p->head++, 1 };
            while (isdigit(p->data[p->head]))
            {
                p->head++;
                num.len++;
            }
            p->value.integer = str_dec_to_int(num, p->data);
            return TOK_INTEGER;
        }

        return p->data[p->head++];
    }

    return TOK_END;
}

struct arg
{
    struct arg* next;
    struct str_view type;
    struct str_view name;
    char nullable;
    char update;
};

static struct arg*
arg_alloc(void)
{
    struct arg* a = malloc(sizeof *a);
    memset(a, 0, sizeof *a);
    return a;
}

struct function
{
    struct function* next;
    struct str_view name;
    struct str_view body;
    struct arg* args;
};

static struct function*
function_alloc(void)
{
    struct function* f = malloc(sizeof *f);
    memset(f, 0, sizeof *f);
    return f;
}

struct migration
{
    struct migration* next;
    struct str_view sql;
    int version;
};

static struct migration*
migration_alloc(int version)
{
    struct migration* m = malloc(sizeof * m);
    m->next = NULL;
    m->version = version;
    m->sql.len = 0;
    return m;
}

enum query_type
{
    QUERY_NONE,
    QUERY_INSERT,
    QUERY_UPDATE,
    QUERY_UPSERT,
    QUERY_DELETE,
    QUERY_EXISTS,
    QUERY_SELECT_FIRST,
    QUERY_SELECT_ALL,
};

struct query
{
    struct query* next;
    struct str_view name;
    struct str_view stmt;
    struct str_view table_name;
    struct str_view return_name;
    struct str_view doxygen;
    struct arg* in_args;
    struct arg* cb_args;
    struct arg* bind_args;
    enum query_type type;
};

static struct query*
query_alloc(void)
{
    struct query* q = malloc(sizeof *q);
    memset(q, 0, sizeof *q);
    return q;
}

struct query_group
{
    struct query_group* next;
    struct query* queries;
    struct function* functions;
    struct str_view name;
};

static struct query_group*
query_group_alloc(void)
{
    struct query_group* g = malloc(sizeof *g);
    memset(g, 0, sizeof *g);
    return g;
}

struct root
{
    struct str_view prefix;
    struct str_view malloc;
    struct str_view free;
    struct str_view log_dbg;
    struct str_view log_err;
    struct str_view log_sql_err;
    struct str_view header_preamble;
    struct str_view header_postamble;
    struct str_view source_includes;
    struct str_view source_preamble;
    struct str_view source_postamble;
    struct query_group* query_groups;
    struct query* queries;
    struct function* functions;
    struct migration* upgrade;
    struct migration* downgrade;
};

static void
root_init(struct root* root)
{
    memset(root, 0, sizeof *root);
}

static enum token
scan_block(struct parser* p, int expect_opening_brace)
{
    int depth = 1;
    if (expect_opening_brace)
        if (scan_next_token(p) != '{')
            return print_error(p, "Error: Expecting '{'\n");

    /* Trim any preceeding whitespace/newlines */
    for (; p->head != p->len; p->head++)
        if (!isspace(p->data[p->head]))
            break;
    if (p->head == p->len)
        return print_error(p, "Error: Missing closing \"}\"\n");

    p->value.str.off = p->head;
    while (p->head != p->len)
    {
        if (p->data[p->head] == '{')
            depth++;
        else if (p->data[p->head] == '}')
            if (--depth == 0)
            {
                p->value.str.len = p->head++ - p->value.str.off;

                /* Trim off any trailing whitespace/newlines */
                while (p->value.str.len > 0)
                {
                    char c = p->data[p->value.str.off + p->value.str.len - 1];
                    if (!isspace(c))
                        break;
                    p->value.str.len--;
                }

                return TOK_STRING;
            }

        p->head++;
    }

    return print_error(p, "Error: Missing closing \"}\"\n");
}

static int
parse(struct parser* p, struct root* root, struct cfg* cfg)
{
    enum token tok;
    do {
        switch ((tok = scan_next_token(p))) {
            case TOK_DOXYGEN: break;  /* ignore comment blocks outside of queries */
            case TOK_OPTION: {
                struct str_view option;
                if (scan_next_token(p) != TOK_LABEL)
                    return print_error(p, "Error: Expected option name after %%option\n");
                option = p->value.str;

                /* Options with no arguments */
                if (str_eq_cstr("debug-layer", option, p->data))
                    { cfg->debug_layer = 1; break; }
                else if (str_eq_cstr("custom-init", option, p->data))
                    { cfg->custom_init = 1; cfg->custom_init_decl = 1; break; }
                else if (str_eq_cstr("custom-init-decl", option, p->data))
                    { cfg->custom_init_decl = 1; break; }
                else if (str_eq_cstr("custom-deinit", option, p->data))
                    { cfg->custom_deinit = 1; cfg->custom_deinit_decl = 1; break; }
                else if (str_eq_cstr("custom-deinit-decl", option, p->data))
                    { cfg->custom_deinit_decl = 1; break; }
                else if (str_eq_cstr("custom-api", option, p->data))
                    { cfg->custom_api = 1; cfg->custom_api_decl = 1; break; }
                else if (str_eq_cstr("custom-api-decl", option, p->data))
                    { cfg->custom_api_decl = 1; break; }
                else if (str_eq_cstr("no-forwards-compat", option, p->data))
                    { cfg->no_forwards_compat = 1; break; }

                if (scan_next_token(p) != '=')
                    return print_error(p, "Error: Expecting '='\n");
                if (scan_next_token(p) != TOK_STRING)
                    return print_error(p, "Error: Expected string for %%option\n");

                if (str_eq_cstr("prefix", option, p->data))
                    root->prefix = p->value.str;
                else if (str_eq_cstr("malloc", option, p->data))
                    root->malloc = p->value.str;
                else if (str_eq_cstr("free", option, p->data))
                    root->free = p->value.str;
                else if (str_eq_cstr("log-dbg", option, p->data))
                    root->log_dbg = p->value.str;
                else if (str_eq_cstr("log-error", option, p->data))
                    root->log_err = p->value.str;
                else if (str_eq_cstr("log-sql-error", option, p->data))
                    root->log_sql_err = p->value.str;
                else
                    return print_error(p, "Unknown option \"%.*s\"\n", option.len, p->data + option.off);
            } break;

            case TOK_HEADER_PREAMBLE: {
                if (scan_block(p, 1) != TOK_STRING)
                    return -1;
                root->header_preamble = p->value.str;
            } break;

            case TOK_HEADER_POSTAMBLE: {
                if (scan_block(p, 1) != TOK_STRING)
                    return -1;
                root->header_postamble = p->value.str;
            } break;

            case TOK_SOURCE_INCLUDES: {
                if (scan_block(p, 1) != TOK_STRING)
                    return -1;
                root->source_includes = p->value.str;
            } break;

            case TOK_SOURCE_PREAMBLE: {
                if (scan_block(p, 1) != TOK_STRING)
                    return -1;
                root->source_preamble = p->value.str;
            } break;

            case TOK_SOURCE_POSTAMBLE: {
                if (scan_block(p, 1) != TOK_STRING)
                    return -1;
                root->source_postamble = p->value.str;
            } break;

            case TOK_UPGRADE:
            case TOK_DOWNGRADE: {
                struct migration* m;
                struct migration** l;
                if (scan_next_token(p) != TOK_INTEGER)
                    return print_error(p, "Error: Expected migration version number after \"%s\"\n",
                        tok == TOK_UPGRADE ? "%upgrade" : "%downgrade");

                /* Insert node sorted based on version */
                m = migration_alloc(p->value.integer);
                l = tok == TOK_UPGRADE ? &root->upgrade : &root->downgrade;
                if (*l != NULL)
                {
                    if (tok == TOK_UPGRADE)
                        while ((*l) && (*l)->version < m->version)
                            l = &(*l)->next;
                    else
                        while ((*l) && (*l)->version > m->version)
                            l = &(*l)->next;
                    m->next = (*l);
                }
                *l = m;

                if (scan_block(p, 1) != TOK_STRING)
                    return print_error(p, "Error: Missing body for %s\n",
                        tok == TOK_UPGRADE ? "%upgrade" : "%downgrade");
                m->sql = p->value.str;
            } break;

            case TOK_PRIVATE_QUERY:
            case TOK_QUERY: {
                struct str_view group_name = {0};
                struct query* query = query_alloc();

                /* Parse "group,name" or "name"*/
                if (scan_next_token(p) != TOK_LABEL)
                    return print_error(p, "Error: Expected label or group for %%query\n");
                query->name = p->value.str;
                switch (scan_next_token(p)) {
                    case '(': break;
                    case ',':
                        if (scan_next_token(p) != TOK_LABEL)
                            return print_error(p, "Error: Expected label for %%query\n");
                        group_name = query->name;
                        query->name = p->value.str;

                        if (scan_next_token(p) == '(')
                            break;
                        /* fallthrough */
                    default:
                        return print_error(p, "Error: Expected \"(\"\n");
                }

                /* Parse parameter list */
            expect_next_param: tok = scan_next_token(p);
            switch_next_param:
                switch (tok)
                {
                    case ')': break;
                    case ',':
                        if (query->in_args == NULL)
                            return print_error(p, "Error: Expected parameter after \"(\"\n");
                        if (scan_next_token(p) != TOK_LABEL)
                            return print_error(p, "Error: Expected parameter after \",\"\n");
                        /* fallthrough */
                    case TOK_LABEL: {
                        struct arg* arg = arg_alloc();
                        arg->type = p->value.str;

                        if (query->in_args == NULL)
                            query->in_args = arg;
                        else
                        {
                            struct arg* args = query->in_args;
                            while (args->next)
                                args = args->next;
                            args->next = arg;
                        }

                        /* Special case, struct -> expect another label */
                        if (str_eq_cstr("struct", arg->type, p->data))
                        {
                            if (scan_next_token(p) != TOK_LABEL)
                                return print_error(p, "Error: Missing struct name after \"struct\"\n");
                            arg->type.len = p->value.str.off + p->value.str.len - arg->type.off;
                        }
                        /* Special case, const -> expect another label */
                        if (str_eq_cstr("const", arg->type, p->data))
                        {
                            if (scan_next_token(p) != TOK_LABEL)
                                return print_error(p, "Error: const qualifier without type\n");
                            arg->type.len = p->value.str.off + p->value.str.len - arg->type.off;
                        }

                        if (scan_next_token(p) != TOK_LABEL)
                            return print_error(p, "Error: Missing parameter name\n");
                        arg->name = p->value.str;

                        /* Param can have a "null" qualifier on the end */
                        if ((tok = scan_next_token(p)) == TOK_LABEL)
                        {
                            if (str_eq_cstr("null", p->value.str, p->data))
                                arg->nullable = 1;
                            else
                                return print_error(p, "Error: Unknown parameter qualifier \"%.*s\"\n",
                                    p->value.str.len, p->data + p->value.str.off);
                            goto expect_next_param;
                        }
                    } goto switch_next_param;

                    default:
                        return print_error(p, "Error: Expected parameter list\n");
                }

                if (scan_next_token(p) != '{')
                    return print_error(p, "Error: Expected \"{\"\n");

            expect_next_stmt: tok = scan_next_token(p);
            switch_next_stmt:
                switch (tok) {
                    case TOK_DOXYGEN: {
                        query->doxygen = p->value.str;
                    } goto expect_next_stmt;

                    case TOK_TYPE: {
                        struct str_view t;
                        if (scan_next_token(p) != TOK_LABEL)
                            return print_error(p, "Error: Expected query type after \"type\"\n");
                        t = p->value.str;
                        if (str_eq_cstr("insert", t, p->data))
                            query->type = QUERY_INSERT;
                        else if (str_eq_cstr("update", t, p->data))
                            query->type = QUERY_UPDATE;
                        else if (str_eq_cstr("upsert", t, p->data))
                            query->type = QUERY_UPSERT;
                        else if (str_eq_cstr("delete", t, p->data))
                            query->type = QUERY_DELETE;
                        else if (str_eq_cstr("exists", t, p->data))
                            query->type = QUERY_EXISTS;
                        else if (str_eq_cstr("select-first", t, p->data))
                            query->type = QUERY_SELECT_FIRST;
                        else if (str_eq_cstr("select-all", t, p->data))
                            query->type = QUERY_SELECT_ALL;
                        else
                            return print_error(p, "Error: Unknown query type \"%.*s\"\n", t.len, p->data + t.off);

                        if (query->type == QUERY_UPDATE || query->type == QUERY_UPSERT)
                        {
                            do
                            {
                                struct arg* a;
                                struct str_view find;
                                tok = scan_next_token(p);
                                if (tok != TOK_LABEL)
                                    return print_error(p, "Error: Expected column name after \"%s\"\n",
                                            query->type == QUERY_UPDATE ? "update" : "upsert");
                                find = p->value.str;

                                a = query->in_args;
                                while (a) {
                                    if (str_eq_str(a->name, find, p->data))
                                    {
                                        a->update = 1;
                                        break;
                                    }
                                    a = a->next;
                                }
                                if (a == NULL)
                                    return print_error(p, "Error: \"update %.*s\" specified, but no argument with this name exists in the function's parameter list\n",
                                            find.len, p->data + find.off);
                            } while ((tok = scan_next_token(p)) == ',');
                            goto switch_next_stmt;
                        }
                    } goto expect_next_stmt;

                    case TOK_TABLE: {
                        if (scan_next_token(p) != TOK_LABEL)
                            return print_error(p, "Error: Expected query type after \"type\"\n");
                        query->table_name = p->value.str;
                    } goto expect_next_stmt;

                    case TOK_STMT: {
                        switch (scan_next_token(p)) {
                            case TOK_LABEL:
                                query->stmt = p->value.str;
                                break;
                            case '{':
                                if (scan_block(p, 0) != TOK_STRING)
                                    return -1;
                                query->stmt = p->value.str;
                                break;
                            default:
                                return print_error(p, "Error: Expected query statement after \"stmt\"\n");
                        }
                    } goto expect_next_stmt;

                    case TOK_BIND: {
                    expect_next_bind_param: tok = scan_next_token(p);
                    switch_next_bind_param:
                        switch (tok)
                        {
                            case ',':
                                if (query->bind_args == NULL)
                                    return print_error(p, "Error: Expected parameter after \"bind\"\n");
                                if (scan_next_token(p) != TOK_LABEL)
                                    return print_error(p, "Error: Expected parameter after \",\"\n");
                                /* fallthrough */
                            case TOK_LABEL: {
                                struct arg* a;
                                struct arg* arg = arg_alloc();
                                arg->type = p->value.str;

                                if (query->bind_args == NULL)
                                    query->bind_args = arg;
                                else
                                {
                                    struct arg* args = query->bind_args;
                                    while (args->next)
                                        args = args->next;
                                    args->next = arg;
                                }

                                /* Get the type information and other data from the function's parameter list */
                                a = query->in_args;
                                while (a) {
                                    if (str_eq_str(a->name, p->value.str, p->data))
                                    {
                                        arg->name = a->name;
                                        arg->type = a->type;
                                        arg->nullable = a->nullable;
                                        goto expect_next_bind_param;
                                    }
                                    a = a->next;
                                }

                                return print_error(p, "Bind argument \"%.*s\" does not exist in function's parameter list\n",
                                        p->value.str.len, p->data + p->value.str.off);
                            } break;

                            default: goto switch_next_stmt;
                        }
                    }

                    case TOK_RETURN: {
                        if (scan_next_token(p) != TOK_LABEL)
                            return print_error(p, "Error: Expected return value after \"return\"\n");
                        query->return_name = p->value.str;
                    } goto expect_next_stmt;

                    case TOK_CALLBACK: {
                    expect_next_cb_param: tok = scan_next_token(p);
                    switch_next_cb_param:
                        switch (tok)
                        {
                            case ',':
                                if (query->cb_args == NULL)
                                    return print_error(p, "Error: Expected parameter after \"select\"\n");
                                if (scan_next_token(p) != TOK_LABEL)
                                    return print_error(p, "Error: Expected parameter after \",\"\n");
                                /* fallthrough */
                            case TOK_LABEL: {
                                struct arg* arg = arg_alloc();
                                arg->type = p->value.str;

                                if (query->cb_args == NULL)
                                    query->cb_args = arg;
                                else
                                {
                                    struct arg* args = query->cb_args;
                                    while (args->next)
                                        args = args->next;
                                    args->next = arg;
                                }

                                /* Special case, struct -> expect another label */
                                if (str_eq_cstr("struct", arg->type, p->data))
                                {
                                    if (scan_next_token(p) != TOK_LABEL)
                                        return print_error(p, "Error: Missing struct name after \"struct\"\n");
                                    arg->type.len = p->value.str.off + p->value.str.len - arg->type.off;
                                }
                                /* Special case, const -> expect another label */
                                if (str_eq_cstr("const", arg->type, p->data))
                                {
                                    if (scan_next_token(p) != TOK_LABEL)
                                        return print_error(p, "Error: const qualifier without type\n");
                                    arg->type.len = p->value.str.off + p->value.str.len - arg->type.off;
                                }

                                if (scan_next_token(p) != TOK_LABEL)
                                    return print_error(p, "Error: Missing parameter name\n");
                                arg->name = p->value.str;

                                /* Param can have a "null" qualifier on the end */
                                if ((tok = scan_next_token(p)) == TOK_LABEL)
                                {
                                    if (str_eq_cstr("null", p->value.str, p->data))
                                        arg->nullable = 1;
                                    else
                                        return print_error(p, "Error: Unknown parameter qualifier \"%.*s\"\n",
                                            p->value.str.len, p->data + p->value.str.off);
                                    goto expect_next_cb_param;
                                }
                            } goto switch_next_cb_param;

                            default: goto switch_next_stmt;
                        }
                    }

                    case '}': break;
                    default:
                        return print_error(p, "Error: Expecting \"type\", \"table\", \"stmt\" or \"return\"\n");
                }

                if (group_name.len)
                {
                    struct query_group* g = root->query_groups;
                    while (g)
                    {
                        if (str_eq_str(g->name, group_name, p->data))
                            break;
                        g = g->next;
                    }
                    if (g == NULL)
                    {
                        g = query_group_alloc();
                        g->name = group_name;
                        if (root->query_groups == NULL)
                            root->query_groups = g;
                        else
                        {
                            struct query_group* node = root->query_groups;
                            while (node->next)
                                node = node->next;
                            node->next = g;
                        }
                    }

                    if (g->queries == NULL)
                        g->queries = query;
                    else
                    {
                        struct query* q = g->queries;
                        while (q->next)
                            q = q->next;
                        q->next = query;
                    }
                }
                else
                {
                    if (root->queries == NULL)
                        root->queries = query;
                    else
                    {
                        struct query* q = root->queries;
                        while (q->next)
                            q = q->next;
                        q->next = query;
                    }
                }
            } break;

            case TOK_FUNCTION: {
                struct str_view group_name = {0};
                struct function* func = function_alloc();

                /* Parse "group,name" or "name"*/
                if (scan_next_token(p) != TOK_LABEL)
                    return print_error(p, "Error: Expected label or group for %%function\n");
                func->name = p->value.str;
                switch (scan_next_token(p)) {
                    case '(': break;
                    case ',':
                        if (scan_next_token(p) != TOK_LABEL)
                            return print_error(p, "Error: Expected label for %%function\n");
                        group_name = func->name;
                        func->name = p->value.str;

                        if (scan_next_token(p) == '(')
                            break;
                        /* fallthrough */
                    default:
                        return print_error(p, "Error: Expected \"(\"\n");
                }

                /* Parse parameter list */
            expect_next_func_param:
                switch (scan_next_token(p))
                {
                    case ')': break;
                    case ',':
                        if (func->args == NULL)
                            return print_error(p, "Error: Expected parameter after \"(\"\n");
                        if (scan_next_token(p) != TOK_LABEL)
                            return print_error(p, "Error: Expected parameter after \",\"\n");
                        /* fallthrough */
                    case TOK_LABEL: {
                        struct arg* arg = arg = arg_alloc();
                        arg->type = p->value.str;

                        if (func->args == NULL)
                            func->args = arg;
                        else
                        {
                            struct arg* args = func->args;
                            while (args->next)
                                args = args->next;
                            args->next = arg;
                        }

                        /* Special case, struct -> expect another label */
                        if (str_eq_cstr("struct", arg->type, p->data))
                        {
                            if (scan_next_token(p) != TOK_LABEL)
                                return print_error(p, "Error: Missing struct name after \"struct\"\n");
                            arg->type.len = p->value.str.off + p->value.str.len - arg->type.off;
                        }
                        /* Special case, const -> expect another label */
                        if (str_eq_cstr("const", arg->type, p->data))
                        {
                            if (scan_next_token(p) != TOK_LABEL)
                                return print_error(p, "Error: const qualifier without type\n");
                            arg->type.len = p->value.str.off + p->value.str.len - arg->type.off;
                        }

                        if (scan_next_token(p) != TOK_LABEL)
                            return print_error(p, "Error: struct without name\n");
                        arg->name = p->value.str;

                        goto expect_next_func_param;
                    } break;

                    default:
                        return print_error(p, "Error: Expected parameter list\n");
                }

                if (scan_block(p, 1) != TOK_STRING)
                    return -1;
                func->body = p->value.str;

                if (group_name.len)
                {
                    struct query_group* g = root->query_groups;
                    while (g)
                    {
                        if (str_eq_str(g->name, group_name, p->data))
                            break;
                        g = g->next;
                    }
                    if (g == NULL)
                    {
                        g = query_group_alloc();
                        g->name = group_name;
                        if (root->query_groups == NULL)
                            root->query_groups = g;
                        else
                        {
                            struct query_group* node = root->query_groups;
                            while (node->next)
                                node = node->next;
                            node->next = g;
                        }
                    }

                    if (g->functions == NULL)
                        g->functions = func;
                    else
                    {
                        struct function* f = g->functions;
                        while (f->next)
                            f = f->next;
                        f->next = func;
                    }
                }
                else
                {
                    if (root->functions == NULL)
                        root->functions = func;
                    else
                    {
                        struct function* f = root->functions;
                        while (f->next)
                            f = f->next;
                        f->next = func;
                    }
                }
            } break;

            case TOK_END: return 0;
            default: return -1;
        }
    } while(1);
}

/* ----------------------------------------------------------------------------
 * Validation & Post-Processing
 * ------------------------------------------------------------------------- */

static int
post_parse(struct root* root, const char* data)
{
    struct query_group* g;
    struct query* q;

    /* TODO validate some things */

    /* If "bind" was not specified, use the function arguments list instead */
    q = root->queries;
    while (q) {
        if (q->bind_args == NULL)
            q->bind_args = q->in_args;
        q = q->next;
    }
    g = root->query_groups;
    while (g) {
        q = g->queries;
        while (q) {
            if (q->bind_args == NULL)
                q->bind_args = q->in_args;
            q = q->next;
        }
        g = g->next;
    }

    return 0;
}

/* ----------------------------------------------------------------------------
 * Generate
 * ------------------------------------------------------------------------- */

static void
write_func_name(struct mstream* ms, const struct query_group* g, const struct query* q, const char* data)
{
    if (g)
    {
        mstream_str(ms, g->name, data);
        mstream_putc(ms, '_');
    }
    mstream_str(ms, q->name, data);
}

static void
write_func_param_list(struct mstream* ms, const struct root* root, const struct query_group* g, const struct query* q, const char* data)
{
    struct arg* a;

    mstream_fmt(ms, "struct %S* ctx", PREFIX(root->prefix, data));

    a = q->in_args;
    while (a)
    {
        mstream_fmt(ms, ", %S %S", a->type, data, a->name, data);
        a = a->next;
    }

    if (q->cb_args)
        mstream_cstr(ms, ", int (*on_row)(");
    a = q->cb_args;
    while (a)
    {
        if (a != q->cb_args) mstream_cstr(ms, ", ");
        mstream_fmt(ms, "%S %S", a->type, data, a->name, data);
        a = a->next;
    }
    if (q->cb_args)
        mstream_cstr(ms, ", void* user_data), void* user_data");
}

static void
write_func_decl(struct mstream* ms, const struct root* root, const struct query_group* g, const struct query* q, const char* data)
{
    mstream_cstr(ms, "static int" NL);
    write_func_name(ms, g, q, data);
    mstream_putc(ms, '(');
    write_func_param_list(ms, root, g, q, data);
    mstream_putc(ms, ')');
}

static void
write_dbg_func_decl(struct mstream* ms, const struct root* root, const struct query_group* g, const struct query* q, const char* data)
{
    mstream_cstr(ms, "static int" NL "dbg_");
    write_func_name(ms, g, q, data);
    mstream_putc(ms, '(');
    write_func_param_list(ms, root, g, q, data);
    mstream_putc(ms, ')');
}

static void
write_func_ptr_decl(struct mstream* ms, const struct root* root, const struct query_group* g, const struct query* q, const char* data)
{
    mstream_cstr(ms, "int (*");
    write_func_name(ms, g, q, data);
    mstream_cstr(ms, ")(");
    write_func_param_list(ms, root, g, q, data);
    mstream_putc(ms, ')');
}

static void
write_sqlite_prepare_stmt(struct mstream* ms, const struct root* root, const struct query_group* g, const struct query* q, const char* data)
{
    struct arg* a;

    mstream_cstr(ms, "    if (ctx->");
    write_func_name(ms, g, q, data);
    mstream_cstr(ms, " == NULL)" NL);
    mstream_cstr(ms, "        if ((ret = sqlite3_prepare_v2(ctx->db," NL);

    if (q->stmt.len)
    {
        int p = 0;
        for (; p != q->stmt.len; ++p)
            if (!isspace(data[q->stmt.off + p]))
                break;

        mstream_cstr(ms, "            \"");
        for (; p != q->stmt.len; ++p)
        {
            if (data[q->stmt.off + p] == '\n')
            {
                for (; p != q->stmt.len; ++p)
                    if (!isspace(data[q->stmt.off + p + 1]))
                        break;
                if (p + 1 < q->stmt.len)
                    mstream_cstr(ms, " \"" NL "            \"");
            }
            else if (data[q->stmt.off + p] != '\r')
            {
                char c = data[q->stmt.off + p];
                if (c == '"')
                    mstream_putc(ms, '\\');
                mstream_putc(ms, c);
            }
        }
        mstream_cstr(ms, "\"," NL);
    }
    else switch (q->type)
    {
        case QUERY_UPSERT:
            mstream_fmt(ms, "            \"INSERT INTO %S (", q->table_name, data);

            a = q->in_args;
            while (a) {
                if (a != q->in_args) mstream_cstr(ms, ", ");
                mstream_fmt(ms, "%S", a->name, data);
                a = a->next;
            }
            mstream_cstr(ms, ") VALUES (");
            a = q->in_args;
            while (a) {
                if (a != q->in_args) mstream_cstr(ms, ", ");
                mstream_cstr(ms, "?");
                a = a->next;
            }
            mstream_cstr(ms, ")");

            mstream_cstr(ms, " \"" NL "            \"ON CONFLICT DO UPDATE SET ");
            a = q->in_args;
            while (a) {
                if (a != q->in_args) mstream_cstr(ms, ", ");
                mstream_fmt(ms, "%S=excluded.%S", a->name, data, a->name, data);
                a = a->next;
            }

            if (q->return_name.len || q->cb_args)
            {
                /*
                 * Have to re-insert a value to trigger the RETURNING statement.
                 * Caution here: We DON'T want to reinsert "id" or "rowid" because
                 * it will cause the id to auto-increment.
                 */
                struct str_view reinsert = q->in_args ? q->in_args->name : q->return_name;
                mstream_cstr(ms, " \"" NL);
                mstream_fmt(ms, "            \"RETURNING ",
                    reinsert, data, reinsert, data);
                if (q->return_name.len)
                    mstream_fmt(ms, "%S", q->return_name, data);
                a = q->cb_args;
                while (a) {
                    if (a != q->cb_args || q->return_name.len) mstream_cstr(ms, ", ");
                    mstream_fmt(ms, "%S", a->name, data);
                    a = a->next;
                }
                mstream_cstr(ms, ";\"," NL);
            }
            else
                mstream_cstr(ms, ";\"," NL);

            break;

        case QUERY_INSERT:
            if (q->return_name.len || q->cb_args)
                mstream_fmt(ms, "            \"INSERT INTO %S (", q->table_name, data);
            else
                mstream_fmt(ms, "            \"INSERT OR IGNORE INTO %S (", q->table_name, data);

            a = q->in_args;
            while (a) {
                if (a != q->in_args) mstream_cstr(ms, ", ");
                mstream_fmt(ms, "%S", a->name, data);
                a = a->next;
            }
            mstream_cstr(ms, ") VALUES (");
            a = q->in_args;
            while (a) {
                if (a != q->in_args) mstream_cstr(ms, ", ");
                mstream_cstr(ms, "?");
                a = a->next;
            }
            mstream_cstr(ms, ")");

            if (q->return_name.len || q->cb_args)
            {
                /*
                 * Have to re-insert a value to trigger the RETURNING statement.
                 * Caution here: We DON'T want to reinsert "id" or "rowid" because
                 * it will cause the id to auto-increment.
                 */
                struct str_view reinsert = q->in_args ? q->in_args->name : q->return_name;
                mstream_cstr(ms, " \"" NL);
                mstream_fmt(ms, "            \"ON CONFLICT DO UPDATE SET %S=excluded.%S RETURNING ",
                    reinsert, data, reinsert, data);
                if (q->return_name.len)
                    mstream_fmt(ms, "%S", q->return_name, data);
                a = q->cb_args;
                while (a) {
                    if (a != q->cb_args || q->return_name.len) mstream_cstr(ms, ", ");
                    mstream_fmt(ms, "%S", a->name, data);
                    a = a->next;
                }
                mstream_cstr(ms, ";\"," NL);
            }
            else
                mstream_cstr(ms, ";\"," NL);

            break;

        case QUERY_DELETE:
        case QUERY_UPDATE: {
            char first;
            mstream_cstr(ms, "            \"");
            mstream_cstr(ms, q->type == QUERY_UPDATE ? "UPDATE" : "DELETE FROM");

            mstream_fmt(ms, " %S ",
                    q->table_name, data);
            if (q->type == QUERY_UPDATE)
                mstream_cstr(ms, "SET ");

            /* SET ... */
            first = 1;
            a = q->in_args;
            while (a) {
                if (a->update)
                {
                    if (!first) mstream_cstr(ms, ", ");
                    mstream_fmt(ms, "%S=?", a->name, data);
                    first = 0;
                }
                a = a->next;
            }

            /* WHERE ... */
            first = 1;
            a = q->in_args;
            while (a) {
                if (!a->update)
                {
                    if (first) mstream_cstr(ms, " WHERE ");
                    else       mstream_cstr(ms, " AND ");
                    mstream_fmt(ms, "%S=?", a->name, data);
                    first = 0;
                }
                a = a->next;
            }
            mstream_cstr(ms, ";\"," NL);
        } break;

        case QUERY_EXISTS:
            mstream_fmt(ms, "            \"SELECT 1 FROM %S", q->table_name, data);
            a = q->in_args;
            while (a) {
                if (a == q->in_args) mstream_cstr(ms, " \"" NL "            \"WHERE ");
                else                 mstream_cstr(ms, " AND ");
                mstream_fmt(ms, "%S=?", a->name, data);
                a = a->next;
            }
            mstream_cstr(ms, " LIMIT 1;\"," NL);
            break;

        case QUERY_SELECT_FIRST:
        case QUERY_SELECT_ALL:
            mstream_cstr(ms, "            \"SELECT ");
            if (q->return_name.len)
                mstream_fmt(ms, "%S", q->return_name, data);
            a = q->cb_args;
            while (a) {
                if (a != q->cb_args || q->return_name.len) mstream_cstr(ms, ", ");
                mstream_fmt(ms, "%S", a->name, data);
                a = a->next;
            }
            mstream_fmt(ms, " FROM %S", q->table_name, data);

            a = q->in_args;
            while (a) {
                if (a == q->in_args) mstream_cstr(ms, " \"" NL "            \"WHERE ");
                else                 mstream_cstr(ms, " AND ");
                mstream_fmt(ms, "%S=?", a->name, data);
                a = a->next;
            }

            if (q->type == QUERY_SELECT_FIRST)
                mstream_cstr(ms, " LIMIT 1");

            mstream_cstr(ms, ";\"," NL);
            break;
    }

    mstream_cstr(ms, "            -1, &ctx->");
    write_func_name(ms, g, q, data);
    mstream_cstr(ms, ", NULL)) != SQLITE_OK)" NL);
    mstream_cstr(ms, "        {" NL);
    mstream_fmt(ms, "            %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(ctx->db));" NL,
                LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "            return -1;" NL);
    mstream_cstr(ms, "        }" NL NL);
}

static void
write_sqlite_bind_args(struct mstream* ms, const struct root* root, const struct query_group* g, const struct query* q, const char* data)
{
    struct arg* a;
    int i = 1;
    int update_pass = 1;
    int first = 1;

    if (q->bind_args == NULL)
        return;

again:
    a = q->bind_args;
    for (; a; a = a->next)
    {
        const char* sqlite_type = "";
        const char* cast = "";
        const char* null_cmp = "";

        if (update_pass != a->update)
            continue;

        if (first) mstream_cstr(ms, "    if ((ret = ");
        else mstream_cstr(ms, " ||" NL "        (ret = ");
        first = 0;

        if (str_eq_cstr("uint64_t", a->type, data))
            { sqlite_type = "int64"; cast = "(int64_t)"; null_cmp = "(uint64_t)-1"; }
        else if (str_eq_cstr("int64_t", a->type, data))
            { sqlite_type = "int64"; null_cmp = "< 0"; }
        else if (str_eq_cstr("int", a->type, data))
            { sqlite_type = "int"; null_cmp = "< 0"; }
        else if (str_eq_cstr("uint32_t", a->type, data))
            { sqlite_type = "int"; cast = "(int)"; null_cmp = "== (uint32_t)-1"; }
        else if (str_eq_cstr("uint16_t", a->type, data))
            { sqlite_type = "int"; cast = "(int)"; null_cmp = "== (uint16_t)-1"; }
        else if (str_eq_cstr("struct str_view", a->type, data))
            { sqlite_type = "text"; null_cmp = "== NULL"; }
        else if (str_eq_cstr("const char*", a->type, data))
            { sqlite_type = "text"; null_cmp = "== NULL"; }

        if (a->nullable)
        {
            mstream_fmt(ms, "%S %s ? sqlite3_bind_null(ctx->", a->name, data, null_cmp);
            write_func_name(ms, g, q, data);
            mstream_fmt(ms, ", %d) : ", i);
        }
        mstream_fmt(ms, "sqlite3_bind_%s(ctx->", sqlite_type);
        write_func_name(ms, g, q, data);
        mstream_fmt(ms, ", %d, %s%S", i, cast, a->name, data);

        if (str_eq_cstr("struct str_view", a->type, data))
            mstream_fmt(ms, ".data, %S.len, SQLITE_STATIC", a->name, data);
        else if (str_eq_cstr("const char*", a->type, data))
            mstream_cstr(ms, ", -1, SQLITE_STATIC");
        mstream_cstr(ms, ")) != SQLITE_OK");

        i++;
    }

    if (update_pass--)
        goto again;

    mstream_cstr(ms, ")" NL "    {" NL);
    mstream_fmt(ms, "        %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(ctx->db));" NL,
                LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "        return -1;" NL "    }" NL NL);
}

static void
write_sqlite_exec_callback(struct mstream* ms, const struct query_group* g, const struct query* q, const char* data)
{
    struct arg* a = q->cb_args;
    int i = q->return_name.len ? 1 : 0;

    mstream_cstr(ms, "            ret = on_row(" NL);
    for (; a; a = a->next, i++)
    {
        const char* sqlite_type = "";
        const char* cast = "";
        const char* null_value = "";

        if (str_eq_cstr("uint64_t", a->type, data))
            { sqlite_type = "int64"; cast = "(uint64_t)"; null_value = "(uint64_t)-1"; }
        else if (str_eq_cstr("int64_t", a->type, data))
            { sqlite_type = "int64"; null_value = "-1"; }
        else if (str_eq_cstr("int", a->type, data))
            { sqlite_type = "int"; null_value = "-1"; }
        else if (str_eq_cstr("uint32_t", a->type, data))
            { sqlite_type = "int"; cast = "(uint32_t)"; null_value = "(uint32_t)-1"; }
        else if (str_eq_cstr("uint16_t", a->type, data))
            { sqlite_type = "int"; cast = "(uint16_t)"; null_value = "(uint16_t)-1"; }
        else if (str_eq_cstr("struct str_view", a->type, data))
            { sqlite_type = "text"; cast = "(const char*)"; null_value = "NULL"; }
        else if (str_eq_cstr("const char*", a->type, data))
            { sqlite_type = "text"; cast = "(const char*)"; null_value = "NULL"; }

        mstream_cstr(ms, "                ");
        if (a->nullable)
        {
            mstream_cstr(ms, "sqlite3_column_type(ctx->");
            write_func_name(ms, g, q, data);
            mstream_fmt(ms, ", %d) == SQLITE_NULL ? ", i);
            mstream_cstr(ms, null_value);
            mstream_cstr(ms, " : ");
        }

        mstream_fmt(ms, "%ssqlite3_column_%s(ctx->", cast, sqlite_type);
        write_func_name(ms, g, q, data);
        mstream_fmt(ms, ", %d)," NL, i);
    }
    mstream_cstr(ms, "                user_data);" NL);
}

static void
write_sqlite_exec(struct mstream* ms, const struct root* root, const struct query_group* g, const struct query* q, const char* data)
{
    switch (q->type)
    {
        /*
         * Exists should return:
         *   1 if a row was found.
         *   0 if no rows were found.
         *   -1 if an error occurs.
         */
        case QUERY_EXISTS:
            mstream_cstr(ms, "next_step:" NL);
            mstream_cstr(ms, "    ret = sqlite3_step(ctx->");
            write_func_name(ms, g, q, data);
            mstream_cstr(ms, ");" NL);
            mstream_cstr(ms, "    switch (ret)" NL "    {" NL);
            mstream_cstr(ms, "        case SQLITE_BUSY: goto next_step;" NL);
            mstream_cstr(ms, "        case SQLITE_ROW:" NL);
            mstream_cstr(ms, "            sqlite3_reset(ctx->");
            write_func_name(ms, g, q, data);
            mstream_cstr(ms, "); " NL);
            mstream_cstr(ms, "            return 1;" NL);
            mstream_cstr(ms, "        case SQLITE_DONE:" NL);
            mstream_cstr(ms, "            sqlite3_reset(ctx->");
            write_func_name(ms, g, q, data);
            mstream_cstr(ms, ");" NL);
            mstream_cstr(ms, "            return 0;" NL);
            mstream_cstr(ms, "    }" NL NL);
            mstream_fmt(ms, "    %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(ctx->db));" NL,
                        LOG_SQL_ERR(root->log_sql_err, data));
            mstream_cstr(ms, "    sqlite3_reset(ctx->");
            write_func_name(ms, g, q, data);
            mstream_cstr(ms, ");" NL);
            mstream_cstr(ms, "    return -1;" NL);
            break;

        /*
         * All of these query types either return a single value, or return
         * nothing. Both "callback" and "return" statements can be used.
         *
         * If both "return" and "callback" are specified, then "return" has
         * a higher priority this value is returned. If the callback function
         * returns negative (error occurs), then we must return -1 instead of
         * the return value.
         *
         * If only "callback" is specified, then we return the value returned
         * by the callback.
         *
         * If only "return" is specified, then we return the value returned by
         * the callback.
         */
        case QUERY_UPDATE:
        case QUERY_INSERT:
        case QUERY_UPSERT:
        case QUERY_DELETE:
        case QUERY_SELECT_FIRST:
            mstream_cstr(ms, "next_step:" NL);
            mstream_cstr(ms, "    ret = sqlite3_step(ctx->");
            write_func_name(ms, g, q, data);
            mstream_cstr(ms, ");" NL);
            mstream_cstr(ms, "    switch (ret)" NL "    {" NL);
            mstream_cstr(ms, "        case SQLITE_BUSY: goto next_step;" NL);

            if (q->return_name.len || q->cb_args)
                mstream_cstr(ms, "        case SQLITE_ROW:" NL);

            if (q->return_name.len)
            {
                mstream_fmt(ms, "            %S = sqlite3_column_int(ctx->",
                    q->return_name, data);
                write_func_name(ms, g, q, data);
                mstream_cstr(ms, ", 0);" NL);
            }

            if (q->cb_args)
            {
                write_sqlite_exec_callback(ms, g, q, data);
                if (q->return_name.len)
                {
                    mstream_cstr(ms, "            if (ret < 0)" NL);
                    mstream_cstr(ms, "            {" NL);
                    mstream_cstr(ms, "                sqlite3_reset(ctx->");
                    write_func_name(ms, g, q, data);
                    mstream_cstr(ms, ");" NL);
                    mstream_cstr(ms, "                return ret;" NL);
                    mstream_cstr(ms, "            }" NL);
                }
                else
                {
                    mstream_cstr(ms, "            sqlite3_reset(ctx->");
                    write_func_name(ms, g, q, data);
                    mstream_cstr(ms, ");" NL);
                    mstream_cstr(ms, "            return ret;" NL);
                }
            }
            if (q->return_name.len || q->cb_args)
                mstream_cstr(ms, "        case SQLITE_DONE: goto done;" NL);
            else
            {
                mstream_cstr(ms, "        case SQLITE_DONE:" NL);
                mstream_cstr(ms, "            sqlite3_reset(ctx->");
                write_func_name(ms, g, q, data);
                mstream_cstr(ms, ");" NL);
                mstream_cstr(ms, "            return 0;" NL);
            }

            mstream_cstr(ms, "    }" NL NL);
            mstream_fmt(ms, "    %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(ctx->db));" NL,
                        LOG_SQL_ERR(root->log_sql_err, data));
            if (q->return_name.len || q->cb_args)
                mstream_cstr(ms, "done:" NL);
            mstream_cstr(ms, "    sqlite3_reset(ctx->");
            write_func_name(ms, g, q, data);
            mstream_cstr(ms, ");" NL);
            if (q->return_name.len)
                mstream_fmt(ms, "    return %S;" NL, q->return_name, data);
            else
                mstream_cstr(ms, "    return -1;" NL);
            break;

        /*
         * These query types return multiple values.
         */
        case QUERY_SELECT_ALL:
            mstream_cstr(ms, "next_step:" NL);
            mstream_cstr(ms, "    ret = sqlite3_step(ctx->");
            write_func_name(ms, g, q, data);
            mstream_cstr(ms, ");" NL);
            mstream_cstr(ms, "    switch (ret)" NL "    {" NL);
            mstream_cstr(ms, "        case SQLITE_ROW:" NL);

            if (q->return_name.len)
            {
                mstream_fmt(ms, "            %S = sqlite3_column_int(ctx->",
                    q->return_name, data);
                write_func_name(ms, g, q, data);
                mstream_cstr(ms, ", 0);" NL);
            }

            if (q->cb_args)
                write_sqlite_exec_callback(ms, g, q, data);

            if (q->cb_args)
            {
                if (q->type == QUERY_SELECT_ALL)
                    mstream_cstr(ms, "            if (ret == 0) goto next_step;" NL);
            }

            if (q->cb_args)
            {
                mstream_cstr(ms, "            sqlite3_reset(ctx->");
                write_func_name(ms, g, q, data);
                mstream_cstr(ms, ");" NL);
                if (q->return_name.len)
                {
                    mstream_cstr(ms, "            if (ret < 0) return -1;" NL);
                    mstream_fmt(ms, "            return %S;" NL, q->return_name, data);
                }
                else
                    mstream_cstr(ms, "            return ret;" NL);
            }

            mstream_cstr(ms, "        case SQLITE_BUSY: goto next_step;" NL);
            mstream_cstr(ms, "        case SQLITE_DONE:" NL);
            mstream_cstr(ms, "            sqlite3_reset(ctx->");
            write_func_name(ms, g, q, data);
            mstream_cstr(ms, ");" NL);
            if (q->return_name.len)
                mstream_fmt(ms, "            return %S;" NL, q->return_name, data);
            else
                mstream_cstr(ms, "            return 0;" NL);

            mstream_cstr(ms, "    }" NL NL);
            mstream_fmt(ms, "    %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(ctx->db));" NL,
                        LOG_SQL_ERR(root->log_sql_err, data));
            mstream_cstr(ms, "    sqlite3_reset(ctx->");
            write_func_name(ms, g, q, data);
            mstream_cstr(ms, ");" NL);
            mstream_cstr(ms, "    return -1;" NL);
            break;
    }
}

static void
write_migration_sql_stmts(struct mstream* ms, const struct root* root, const struct migration* m, const char* data, const char* type)
{
    while (m)
    {
        int p;
        mstream_fmt(ms, "static const char* %S_%s%d =" NL, PREFIX(root->prefix, data), type, m->version);
        mstream_cstr(ms, "    \"");
        for (p = 0; p != m->sql.len; ++p)
        {
            if (data[m->sql.off + p] == '\n')
            {
                if (p + 1 < m->sql.len)
                    mstream_cstr(ms, "\\n\"" NL "    \"");
            }
            else if (data[m->sql.off + p] != '\r')
            {
                char c = data[m->sql.off + p];
                if (c == '"')
                    mstream_putc(ms, '\\');
                else if (c == '\\')
                    mstream_putc(ms, '\\');
                mstream_putc(ms, c);
            }
        }
        mstream_cstr(ms, "\";" NL NL);

        m = m->next;
    }
}

static void
write_run_sql_stmts_func(struct mstream* ms, const struct root* root, const char* data)
{
    mstream_cstr(ms, "static int run_sqlite3_sql(sqlite3* db, const char* sql)" NL "{" NL);
    mstream_cstr(ms, "    int ret;" NL);
    mstream_cstr(ms, "    int sql_len;" NL);
    mstream_cstr(ms, "    const char* sql_next;" NL);
    mstream_cstr(ms, "    sqlite3_stmt* stmt;" NL NL);

    mstream_cstr(ms, "    sql_len = (int)strlen(sql);" NL NL);

    mstream_cstr(ms, "next_step:" NL);
    mstream_cstr(ms, "    ret = sqlite3_prepare_v2(db, sql, sql_len, &stmt, &sql_next);" NL);
    mstream_cstr(ms, "    if (ret != SQLITE_OK)" NL "    {" NL);
    mstream_fmt (ms, "        %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(db));" NL, LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "        goto prepare_failed;" NL);
    mstream_cstr(ms, "    }" NL NL);

    mstream_cstr(ms, "retry_step:" NL);
    mstream_cstr(ms, "    switch (ret = sqlite3_step(stmt))" NL "    {" NL);
    mstream_cstr(ms, "        case SQLITE_BUSY: goto retry_step;" NL);
    mstream_cstr(ms, "        case SQLITE_ROW:" NL);
    mstream_cstr(ms, "        case SQLITE_DONE:" NL);
    mstream_cstr(ms, "            sql_len -= (int)(sql_next - sql);" NL);
    mstream_cstr(ms, "            sql = sql_next;" NL);
    mstream_cstr(ms, "            for (; sql_len && isspace(*sql); ++sql, --sql_len) {}" NL);
    mstream_cstr(ms, "            if (sql_len <= 0)" NL);
    mstream_cstr(ms, "                break;" NL);
    mstream_cstr(ms, "            sqlite3_finalize(stmt);" NL);
    mstream_cstr(ms, "            goto next_step;" NL);
    mstream_cstr(ms, "        default:" NL);
    mstream_fmt (ms, "            %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(db));" NL, LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "            goto exec_failed;" NL);
    mstream_cstr(ms, "    }" NL NL);

    mstream_cstr(ms, "    sqlite3_finalize(stmt);" NL);
    mstream_cstr(ms, "    return 0;" NL NL);

    mstream_cstr(ms, "exec_failed    : sqlite3_finalize(stmt);" NL);
    mstream_cstr(ms, "prepare_failed : return -1;" NL);
    mstream_cstr(ms, "}" NL NL);
}

static void
write_version_func(struct mstream* ms, const struct root* root, const char* data)
{
    mstream_fmt (ms, "static int %S_version(struct %S* ctx)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
    mstream_cstr(ms, "    int ret, version = 0;" NL);
    mstream_cstr(ms, "    sqlite3_stmt* stmt;" NL NL);

    mstream_cstr(ms, "    ret = sqlite3_prepare_v2(ctx->db, \"PRAGMA user_version;\", -1, &stmt, NULL);" NL);
    mstream_cstr(ms, "    if (ret != SQLITE_OK)" NL "    {" NL);
    mstream_fmt (ms, "        %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(ctx->db));" NL, LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "        return -1;" NL);
    mstream_cstr(ms, "    }" NL NL);

    mstream_cstr(ms, "    ret = sqlite3_step(stmt);" NL);
    mstream_cstr(ms, "    if (ret != SQLITE_ROW)" NL "    {" NL);
    mstream_fmt (ms, "        %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(ctx->db));" NL, LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "        sqlite3_finalize(stmt);" NL);
    mstream_cstr(ms, "        return -1;" NL);
    mstream_cstr(ms, "    }" NL NL);

    mstream_cstr(ms, "    version = sqlite3_column_int(stmt, 0);" NL);
    mstream_cstr(ms, "    sqlite3_finalize(stmt);" NL);
    mstream_cstr(ms, "    return version;" NL);
    mstream_cstr(ms, "}" NL NL);
}

static void
write_downgrade_forward_compat_func(struct mstream* ms, const struct root* root, const char* data)
{
    mstream_fmt(ms, "static int %S_downgrade_forward_compat(sqlite3* db)" NL "{" NL,
        PREFIX(root->prefix, data));
    mstream_cstr(ms, "    int ret, i;" NL);
    mstream_cstr(ms, "    sqlite3_stmt* stmt;" NL);
    mstream_cstr(ms, "    const char* str;" NL);
    mstream_cstr(ms, "    void* tmp;" NL);
    mstream_cstr(ms, "    char** sql = NULL;" NL);
    mstream_cstr(ms, "    int sql_num = 0;" NL);
    mstream_cstr(ms, "    int success = -1;" NL);
    mstream_fmt (ms, "    ret = sqlite3_prepare_v2(db, \"SELECT sql FROM %S_downgrades WHERE version >= %d ORDER BY version DESC;\", -1, &stmt, NULL);" NL,
        PREFIX(root->prefix, data), root->downgrade ? root->downgrade->version + 1 : 0);
    mstream_cstr(ms, "    if (ret != SQLITE_OK)" NL "    {" NL);
    mstream_fmt (ms, "        %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(db));" NL,
        LOG_SQL_ERR(root->log_sql_err, data));
    mstream_fmt (ms, "        return -1;" NL);
    mstream_fmt (ms, "    }" NL NL);

    mstream_cstr(ms, "next_step:" NL);
    mstream_cstr(ms, "    ret = sqlite3_step(stmt);" NL);
    mstream_cstr(ms, "    switch (ret)" NL "    {" NL);
    mstream_cstr(ms, "        case SQLITE_ROW:" NL);
    mstream_cstr(ms, "            tmp = realloc(sql, sizeof(char*) * (sql_num + 1));" NL);
    mstream_cstr(ms, "            if (tmp == NULL) goto done;" NL);
    mstream_cstr(ms, "            sql = tmp;" NL NL);
    mstream_cstr(ms, "            str = (const char*)sqlite3_column_text(stmt, 0);" NL);
    mstream_cstr(ms, "            sql[sql_num] = malloc(strlen(str) + 1);" NL);
    mstream_cstr(ms, "            if (sql[sql_num] == NULL) goto done;" NL);
    mstream_cstr(ms, "            strcpy(sql[sql_num++], str);" NL NL);
    mstream_cstr(ms, "            goto next_step;" NL);
    mstream_cstr(ms, "        case SQLITE_BUSY: goto next_step;" NL);
    mstream_cstr(ms, "        case SQLITE_DONE:" NL);
    mstream_cstr(ms, "            for (i = 0; i != sql_num; ++i)" NL);
    mstream_cstr(ms, "            {" NL);
    mstream_cstr(ms, "                if (run_sqlite3_sql(db, sql[i]) != 0)" NL);
    mstream_cstr(ms, "                    goto done;" NL);
    mstream_cstr(ms, "            }" NL);
    mstream_cstr(ms, "            success = 0;" NL);
    mstream_cstr(ms, "            goto done;" NL);
    mstream_cstr(ms, "    }" NL NL);

    mstream_fmt (ms, "    %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(db));" NL,
        LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "done:" NL);
    mstream_cstr(ms, "    for (i = 0; i != sql_num; ++i)" NL);
    mstream_cstr(ms, "        if (sql[i])" NL);
    mstream_cstr(ms, "            free(sql[i]);" NL);
    mstream_cstr(ms, "    if (sql)" NL);
    mstream_cstr(ms, "        free(sql);" NL);
    mstream_cstr(ms, "    sqlite3_reset(stmt);" NL);
    mstream_cstr(ms, "    return success;" NL);
    mstream_cstr(ms, "}" NL NL);
}

static void
write_migration_body(struct mstream* ms, const struct root* root, const char* data, char reinit_db, char no_forwards_compat)
{
    int max_version = 0;
    struct migration* m;

    m = root->upgrade;
    while (m)
    {
        max_version = m->version;
        m = m->next;
    }

    mstream_cstr(ms, "    int ret;" NL);
    mstream_cstr(ms, "    int version;" NL);
    mstream_cstr(ms, "    char* error;" NL);
    if (!reinit_db)
        mstream_cstr(ms, "    char buf[sizeof(\"PRAGMA user_version=+2147483648;\")];" NL NL);

    /* Get current db version */
    mstream_fmt (ms, "    version = %S_version(ctx);" NL, PREFIX(root->prefix, data));
    mstream_cstr(ms, "    if (version < 0)" NL);
    mstream_cstr(ms, "        return -1;" NL NL);

    /* Begin transaction */
    mstream_cstr(ms, "    ret = sqlite3_exec(ctx->db, \"BEGIN TRANSACTION;\", NULL, NULL, &error);" NL);
    mstream_cstr(ms, "    if (ret != SQLITE_OK)" NL "    {" NL);
    mstream_fmt (ms, "        %S(ret, error, sqlite3_errmsg(ctx->db));" NL, LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "        sqlite3_free(error);" NL);
    mstream_cstr(ms, "        return -1;" NL);
    mstream_cstr(ms, "    }" NL NL);

    /* Downgrade code */
    mstream_cstr(ms, "    switch (version)" NL "    {" NL);
    mstream_cstr(ms, "        default:" NL);
    if (no_forwards_compat)
    {
        mstream_fmt(ms, "            %S(\"Database was created by a newer version of the software! "
            "Can't downgrade, because forwards compatibility was disabled in sqlgen.\");" NL,
            LOG_ERR(root->log_err, data));
        mstream_cstr(ms, "            goto migration_failed;" NL);
    }
    else
    {
        mstream_fmt (ms, "            if (%S_downgrade_forward_compat(ctx->db) != 0)" NL,
            PREFIX(root->prefix, data));
        mstream_fmt (ms, "                goto migration_failed;" NL);
        mstream_fmt (ms, "            version = %d;" NL,
            root->downgrade ? root->downgrade->version + 1 : 0);
    }

    m = root->downgrade;
    while (m)
    {
        mstream_fmt (ms, "        case %d:" NL, m->version + 1);
        if (!reinit_db)
        {
            mstream_cstr(ms, "            if (version == target_version)" NL);
            mstream_cstr(ms, "                break;" NL);
        }
        mstream_fmt (ms, "            if (run_sqlite3_sql(ctx->db, %S_downgrade%d) != 0)" NL,
            PREFIX(root->prefix, data), m->version);
        mstream_cstr(ms, "                goto migration_failed;" NL);

        if (!no_forwards_compat && m->version == 0)
        {
            mstream_fmt (ms, "            if (run_sqlite3_sql(ctx->db, \"DROP TABLE IF EXISTS %S_downgrades;\") != 0)" NL,
                PREFIX(root->prefix, data));
            mstream_cstr(ms, "                goto migration_failed;" NL);
        }

        mstream_fmt (ms, "            version = %d;" NL, m->version);
        m = m->next;
    }
    mstream_cstr(ms, "        case 0:" NL);
    mstream_cstr(ms, "            break;" NL);
    mstream_cstr(ms, "    }" NL NL);

    /* Upgrade code */
    mstream_cstr(ms, "    switch (version)" NL "    {" NL);
    m = root->upgrade;
    while (m)
    {
        mstream_fmt(ms, "        case %d:" NL, m->version - 1);
        if (!reinit_db)
        {
            mstream_cstr(ms, "            if (version == target_version)" NL);
            mstream_cstr(ms, "                break;" NL);
        }

        /* The first migration also includes forward-compat code */
        if (!no_forwards_compat && m->version == 1)
        {
            struct migration* m2;
            mstream_cstr(ms, "            if (run_sqlite3_sql(ctx->db," NL);
            mstream_fmt (ms, "                \"CREATE TABLE IF NOT EXISTS %S_downgrades (\\n\"" NL,
                PREFIX(root->prefix, data));
            mstream_cstr(ms, "                \"    version INTEGER PRIMARY KEY NOT NULL,\\n\"" NL);
            mstream_cstr(ms, "                \"    sql TEXT NOT NULL);\\n\"");

            m2 = root->downgrade;
            while (m2)
            {
                int p;

                mstream_fmt(ms, NL "                "
                    "\"INSERT OR IGNORE INTO %S_downgrades (version, sql) "
                    "VALUES (%d, '",
                    PREFIX(root->prefix, data), m2->version);

                p = 0;
                while (p != m2->sql.len)
                {
                    while (isspace(data[m2->sql.off + p]))
                        p++;

                    mstream_cstr(ms, "\"" NL "                    \"");
                    while (p != m2->sql.len)
                    {
                        char c = data[m2->sql.off + p++];
                        /* Escape SQL for insertion into table */
                        if (c == '\'')
                            mstream_putc(ms, '\'');
                        /* Escape SQL for C string */
                        else if (c == '"')
                            mstream_putc(ms, '\\');
                        else if (c == '\\')
                            mstream_putc(ms, '\\');
                        else if (c == '\r')
                            continue;
                        else if (c == '\n')
                        {
                            mstream_cstr(ms, "\\\\n");
                            break;
                        }
                        mstream_putc(ms, c);
                    }
                }

                mstream_cstr(ms, "');\\n\"");

                m2 = m2->next;
            }
            //mstream_cstr(ms, NL "                    \"');\"");
            mstream_cstr(ms, ") != 0)" NL);
            mstream_cstr(ms, "                goto migration_failed;" NL);
        }
        mstream_fmt(ms, "            if (run_sqlite3_sql(ctx->db, %S_upgrade%d) != 0)" NL, PREFIX(root->prefix, data), m->version);
        mstream_cstr(ms, "                goto migration_failed;" NL);
        mstream_fmt (ms, "            version = %d;" NL, m->version);
        m = m->next;
    }
    mstream_fmt (ms, "        case %d: break;" NL, max_version);
    mstream_cstr(ms, "        default:" NL);
    mstream_fmt(ms, "            %S(\"Failed to upgrade db: Unknown version %%d\\n\", version);" NL, LOG_ERR(root->log_err, data));
    mstream_cstr(ms, "            goto migration_failed;" NL);
    mstream_cstr(ms, "    }" NL NL);

    /* Write new version to db */
    if (reinit_db)
        mstream_fmt(ms, "    ret = sqlite3_exec(ctx->db, \"PRAGMA user_version=%d;\", NULL, NULL, &error);" NL, max_version);
    else
    {
        mstream_cstr(ms, "    sprintf(buf, \"PRAGMA user_version=%d;\", target_version);" NL);
        mstream_cstr(ms, "    ret = sqlite3_exec(ctx->db, buf, NULL, NULL, &error);" NL);
    }
    mstream_cstr(ms, "    if (ret != SQLITE_OK)" NL "    {" NL);
    mstream_fmt (ms, "        %S(ret, error, sqlite3_errmsg(ctx->db));" NL, LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "        sqlite3_free(error);" NL);
    mstream_cstr(ms, "        goto migration_failed;" NL);
    mstream_cstr(ms, "    }" NL NL);

    /* Commit transaction */
    mstream_cstr(ms, "    ret = sqlite3_exec(ctx->db, \"COMMIT TRANSACTION;\", NULL, NULL, &error);" NL);
    mstream_cstr(ms, "    if (ret != SQLITE_OK)" NL "    {" NL);
    mstream_fmt (ms, "        %S(ret, error, sqlite3_errmsg(ctx->db));" NL, LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "        sqlite3_free(error);" NL);
    mstream_cstr(ms, "        goto migration_failed;" NL);
    mstream_cstr(ms, "    }" NL NL);

    mstream_cstr(ms, "    return 0;" NL NL);

    /* Abort transaction */
    mstream_cstr(ms, "migration_failed:" NL);
    mstream_cstr(ms, "    ret = sqlite3_exec(ctx->db, \"ROLLBACK TRANSACTION;\", NULL, NULL, &error);" NL);
    mstream_cstr(ms, "    if (ret != SQLITE_OK)" NL "    {" NL);
    mstream_fmt (ms, "        %S(ret, error, sqlite3_errmsg(ctx->db));" NL, LOG_SQL_ERR(root->log_sql_err, data));
    mstream_cstr(ms, "        sqlite3_free(error);" NL);
    mstream_cstr(ms, "    }" NL);
    mstream_cstr(ms, "    return -1;" NL);
}

static void
write_migration_to_func(struct mstream* ms, const struct root* root, const char* data, char no_forwards_compat)
{
    mstream_fmt(ms, "static int %S_migrate_to(struct %S* ctx, int target_version)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
    write_migration_body(ms, root, data, 0, no_forwards_compat);
    mstream_cstr(ms, "}" NL NL);
}

static void
write_upgrade_func(struct mstream* ms, const struct root* root, const char* data)
{
    int max_version = 0;
    struct migration* m = root->upgrade;
    while (m)
    {
        max_version = m->version;
        m = m->next;
    }
    mstream_fmt(ms, "static int %S_upgrade(struct %S* ctx)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
    mstream_fmt(ms, "    return %S_migrate_to(ctx, %d);" NL, PREFIX(root->prefix, data), max_version);
    mstream_cstr(ms, "}" NL NL);
}

static void
write_reinit_func(struct mstream* ms, const struct root* root, const char* data, char no_forwards_compat)
{
    mstream_fmt(ms, "static int %S_reinit(struct %S* ctx)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
    write_migration_body(ms, root, data, 1, no_forwards_compat);
    mstream_cstr(ms, "}" NL NL);
}

static void
write_debug_wrapper(struct mstream* ms, const struct root* root, const struct query_group* g, const struct query* q, const char* data)
{
    struct arg* a;
    if (q->cb_args)
    {
        mstream_cstr(ms, "static int" NL "dbg_");
        write_func_name(ms, g, q, data);
        mstream_cstr(ms, "_on_row(");

        a = q->cb_args;
        while (a)
        {
            if (a != q->cb_args) mstream_cstr(ms, ", ");
            mstream_fmt(ms, "%S %S", a->type, data, a->name, data);
            a = a->next;
        }
        mstream_cstr(ms, ", void* user_data)" NL "{" NL);

        mstream_cstr(ms, "    void** dbg = user_data;" NL);

        mstream_fmt(ms, "    %S(\"  ", LOG_DBG(root->log_dbg, data));
        a = q->cb_args;
        while (a)
        {
            if (a != q->cb_args) mstream_cstr(ms, " | ");
            if (str_eq_cstr("const char*", a->type, data))
                mstream_cstr(ms, "\\\"%s\\\"");
            else
                mstream_cstr(ms, "%d");
            a = a->next;
        }
        mstream_cstr(ms, "\\n\", ");
        a = q->cb_args;
        while (a)
        {
            if (a != q->cb_args) mstream_cstr(ms, ", ");
            if (str_eq_cstr("const char*", a->type, data)) {}
            else
                mstream_cstr(ms, "(int)");
            mstream_str(ms, a->name, data);
            a = a->next;
        }
        mstream_cstr(ms, ");" NL);

        mstream_cstr(ms, "    return (*(int(*)(");
        a = q->cb_args;
        while (a)
        {
            if (a != q->cb_args) mstream_cstr(ms, ", ");
            mstream_str(ms, a->type, data);
            a = a->next;
        }
        mstream_cstr(ms, ",void*))dbg[0])(");
        a = q->cb_args;
        while (a)
        {
            if (a != q->cb_args) mstream_cstr(ms, ", ");
            mstream_str(ms, a->name, data);
            a = a->next;
        }
        mstream_cstr(ms, ", dbg[1]);" NL);
        mstream_cstr(ms, "}" NL);
    }

    write_dbg_func_decl(ms, root, g, q, data);
    mstream_cstr(ms, NL "{" NL);

    mstream_cstr(ms, "    int result;" NL);
    mstream_cstr(ms, "    char* sql;" NL);
    if (q->cb_args)
        mstream_cstr(ms, "    void* dbg[2] = { (void*)on_row, user_data };" NL);

    mstream_fmt(ms, "    %S(\"db_sqlite3.", LOG_DBG(root->log_dbg, data));
    if (g)
        mstream_fmt(ms, "%S.", g->name, data);
    mstream_str(ms, q->name, data);
    mstream_cstr(ms, "(");
    a = q->in_args;
    while (a)
    {
        if (a != q->in_args)
            mstream_cstr(ms, ", ");

        if (str_eq_cstr("const char*", a->type, data))
            mstream_cstr(ms, "\\\"%s\\\"");
        else if (str_eq_cstr("struct str_view", a->type, data))
            mstream_cstr(ms, "\\\"%.*s\\\"");
        else if (str_eq_cstr("int64_t", a->type, data))
            mstream_cstr(ms, "%\" PRIi64\"");
        else if (str_eq_cstr("uint64_t", a->type, data))
            mstream_cstr(ms, "%\" PRIu64\"");
        else
            mstream_cstr(ms, "%d");
        a = a->next;
    }
    mstream_cstr(ms, ")\\n\"");
    if (q->in_args)
        mstream_cstr(ms, ", ");
    a = q->in_args;
    while (a)
    {
        if (a != q->in_args) mstream_cstr(ms, ", ");
        if (str_eq_cstr("const char*", a->type, data))
            mstream_str(ms, a->name, data);
        else if (str_eq_cstr("struct str_view", a->type, data))
            mstream_fmt(ms, "%S.len, %S.data", a->name, data, a->name, data);
        else if (str_eq_cstr("int64_t", a->type, data))
            mstream_str(ms, a->name, data);
        else if (str_eq_cstr("uint64_t", a->type, data))
            mstream_str(ms, a->name, data);
        else
            mstream_fmt(ms, "(int)%S", a->name, data);
        a = a->next;
    }
    mstream_cstr(ms, ");" NL);

    if (q->cb_args)
        mstream_fmt(ms, "    %S(\"  ", LOG_DBG(root->log_dbg, data));
    a = q->cb_args;
    while (a)
    {
        if (a != q->cb_args) mstream_cstr(ms, " | ");
        mstream_str(ms, a->name, data);
        a = a->next;
    }
    if (q->cb_args)
        mstream_fmt(ms, "\\n\");" NL);

    mstream_cstr(ms, "    result = db_sqlite3.");
    if (g)
        mstream_fmt(ms, "%S.", g->name, data);
    mstream_str(ms, q->name, data);
    mstream_cstr(ms, "(ctx");
    a = q->in_args;
    while (a)
    {
        mstream_fmt(ms, ", %S", a->name, data);
        a = a->next;
    }
    if (q->cb_args)
    {
        mstream_cstr(ms, ", dbg_");
        write_func_name(ms, g, q, data);
        mstream_cstr(ms, "_on_row, dbg");
    }
    mstream_cstr(ms, ");" NL);

    mstream_cstr(ms, "    sql = sqlite3_expanded_sql(ctx->");
    write_func_name(ms, g, q, data);
    mstream_cstr(ms, ");" NL);
    mstream_fmt(ms, "    %S(\"retval=%%d\\n%%s\\n\\n\", result, sql);" NL,
                LOG_DBG(root->log_dbg, data));
    mstream_cstr(ms, "    sqlite3_free(sql);" NL);
    mstream_cstr(ms, "    return result;" NL);
    mstream_cstr(ms, "}" NL NL);
}

static int
determine_indent(struct mstream* ms, struct str_view str, const char* data)
{
    int p;
    int indent = 0;
    int min_indent = INT_MAX;

    p = 0;
    while (p != str.len)
    {
        while (p != str.len)
        {
            if (!isspace(data[str.off + p++]))
                break;
            indent++;
        }

        if (min_indent > indent)
            min_indent = indent;

        while (p != str.len)
            if (data[str.off + p++] == '\n')
            {
                indent = 0;
                break;
            }
    }

    return min_indent;
}

static void
write_block_reindented(struct mstream* ms, int indent, struct str_view str, const char* data)
{
    int p, i;
    int unindent = determine_indent(ms, str, data);

    p = 0;
    while (p != str.len)
    {
        i = unindent;
        while (p != str.len && i--)
            p++;

        for (i = 0; i != indent; ++i)
            mstream_putc(ms, ' ');

        while (p != str.len)
        {
            char c = data[str.off + p++];
            mstream_putc(ms, c);
            if (c == '\n')
                break;
        }
    }

    mstream_cstr(ms, NL);
}

static void
write_block_reindented_cstr(struct mstream* ms, int indent, const char* data)
{
    struct str_view str = {
        0,
        strlen(data)
    };
    write_block_reindented(ms, indent, str, data);
}

static int
gen_header(const struct root* root, const char* data, const char* file_name,
    char custom_init, char custom_deinit, char custom_api)
{
    struct query* q;
    struct query_group* g;
    struct function* f;
    struct arg* a;
    struct mfile mf;
    struct mstream ms = mstream_init_writeable();

    if (root->header_preamble.len)
        mstream_fmt(&ms, NL "%S" NL, root->header_preamble, data);

    mstream_fmt(&ms, "struct %S;" NL, PREFIX(root->prefix, data));
    mstream_fmt(&ms, "struct %S_interface" NL "{" NL, PREFIX(root->prefix, data));

    /* Hard-coded functions */
    write_block_reindented_cstr(&ms, 4, "/*!" NL
        " * \\brief Open a database connection. Must be closed again after use." NL
        " * \\param[in] uri A file path to a database file." NL
        " * \\return If successful, the database connection is returned, which can be used" NL
        " * for all future queries." NL
        " */");
    mstream_fmt(&ms, "    struct %S* (*open)(const char* uri);" NL,
        PREFIX(root->prefix, data));
    write_block_reindented_cstr(&ms, 4, "/*!" NL
        " * \\brief Closes the database connection." NL
        " * \\param[in] ctx Connection returned from the call to open()." NL
        " */");
    mstream_fmt(&ms, "    void (*close)(struct %S* ctx);" NL,
        PREFIX(root->prefix, data));
    write_block_reindented_cstr(&ms, 4, "/*!" NL
        " * \\brief Gets the current version of the database." NL
        " * A new, empty database will always have a version of 0. Calling upgrade()" NL
        " * may change the version if a migration occurs." NL
        " */");
    mstream_fmt(&ms, "    int (*version)(struct %S* ctx);" NL,
        PREFIX(root->prefix, data));
    write_block_reindented_cstr(&ms, 4, "/*!" NL
        " * \\brief Migrates the database to the newest version." NL
        " * \\return 0 on success, negative on error. If an error occurs, the database " NL
        " * is rolled back to the state prior to calling this function." NL
        " */");
    mstream_fmt(&ms, "    int (*upgrade)(struct %S* ctx);" NL,
        PREFIX(root->prefix, data));
    write_block_reindented_cstr(&ms, 4, "/*!" NL
        " * \\brief Fully downgrades the database, and then upgrades it again." NL
        " * This function is often useful during development." NL
        " * \\warning This will wipe all data in the database!" NL
        " */");
    mstream_fmt(&ms, "    int (*reinit)(struct %S* ctx);" NL,
        PREFIX(root->prefix, data));
    write_block_reindented_cstr(&ms, 4, "/*!" NL
        " * \\brief Migrates the database to a specific version." NL
        " * The version can older or newer than the current state of the database." NL
        " */");
    mstream_fmt(&ms, "    int (*migrate_to)(struct %S* ctx, int target_version);" NL,
        PREFIX(root->prefix, data));

    /* Global queries */
    q = root->queries;
    while (q)
    {
        mstream_cstr(&ms, "    ");
        write_func_ptr_decl(&ms, root, NULL, q, data);
        mstream_cstr(&ms, ";" NL);
        q = q->next;
    }
    mstream_cstr(&ms, NL);

    /* Global functions */
    f = root->functions;
    while (f)
    {
        mstream_fmt(&ms, "    int (*%S)(struct %S* ctx",
                f->name, data,
                PREFIX(root->prefix, data));
        a = f->args;
        while (a)
        {
            mstream_cstr(&ms, ", ");
            mstream_fmt(&ms, "%S %S", a->type, data, a->name, data);
            a = a->next;
        }
        mstream_cstr(&ms, ");" NL);

        f = f->next;
    }

    /* Grouped queries */
    g = root->query_groups;
    while (g)
    {
        mstream_cstr(&ms, "    struct {" NL);

        /* Queries */
        q = g->queries;
        while (q)
        {
            if (q->doxygen.len)
                write_block_reindented(&ms, 8, q->doxygen, data);
            mstream_cstr(&ms, "        ");
            write_func_ptr_decl(&ms, root, NULL, q, data);
            mstream_cstr(&ms, ";" NL);
            q = q->next;
        }

        /* Functions */
        f = g->functions;
        while (f)
        {
            mstream_fmt(&ms, "        int (*%S)(struct %S* ctx",
                    f->name, data,
                    PREFIX(root->prefix, data));
            a = f->args;
            while (a)
            {
                mstream_cstr(&ms, ", ");
                mstream_fmt(&ms, "%S %S", a->type, data, a->name, data);
                a = a->next;
            }
            mstream_cstr(&ms, ");" NL);
            f = f->next;
        }

        mstream_fmt(&ms, "    } %S;" NL NL, g->name, data);
        g = g->next;
    }
    mstream_cstr(&ms, "};" NL NL);

    /* API */
    if (!custom_init)
        mstream_fmt(&ms, "int %S_init(void);" NL, PREFIX(root->prefix, data));
    if (!custom_deinit)
        mstream_fmt(&ms, "void %S_deinit(void);" NL, PREFIX(root->prefix, data));
    if (!custom_api)
        mstream_fmt(&ms, "struct %S_interface* %S(const char* backend);" NL,
                PREFIX(root->prefix, data), PREFIX(root->prefix, data));

    if (root->header_postamble.len)
        mstream_fmt(&ms, NL "%S" NL, root->header_postamble, data);

    /* Don't write header if it is identical to the existing one -- causes less
     * rebuilds */
    if (mfile_map_read(&mf, file_name) == 0)
    {
        if (mf.size == ms.idx && memcmp(mf.address, ms.address, mf.size) == 0)
            return 0;
        mfile_unmap(&mf);
    }

    if (mfile_map_write(&mf, file_name, ms.idx) != 0)
        return -1;
    memcpy(mf.address, ms.address, ms.idx);
    mfile_unmap(&mf);

    return 0;
}

static int
gen_source(const struct root* root, const char* data, const char* file_name,
    char debug_layer, char custom_init, char custom_deinit, char custom_api, char no_forwards_compat)
{
    struct query* q;
    struct query_group* g;
    struct function* f;
    struct arg* a;
    struct mfile mf;
    struct mstream ms = mstream_init_writeable();

    if (root->source_includes.len)
        mstream_fmt(&ms, NL "%S" NL NL, root->source_includes, data);

    mstream_cstr(&ms, "#include <ctype.h>" NL);
    mstream_cstr(&ms, "#include <stdlib.h>" NL);
    mstream_cstr(&ms, "#include <string.h>" NL);
    mstream_cstr(&ms, "#include <stdio.h>" NL);

    /* ------------------------------------------------------------------------
     * Context structure declaration
     * --------------------------------------------------------------------- */

    mstream_fmt(&ms, "struct %S" NL "{" NL,
            PREFIX(root->prefix, data));
    mstream_fmt(&ms, "    sqlite3* db;" NL);
    /* Global queries */
    q = root->queries;
    while (q)
    {
        mstream_fmt(&ms, "    sqlite3_stmt* %S;" NL, q->name, data);
        q = q->next;
    }
    /* Grouped queries */
    g = root->query_groups;
    while (g)
    {
        q = g->queries;
        while (q)
        {
            mstream_fmt(&ms, "    sqlite3_stmt* %S_%S;" NL, g->name, data, q->name, data);
            q = q->next;
        }
        g = g->next;
    }
    mstream_cstr(&ms, "};" NL);

    /* Error function */
    if (root->log_sql_err.len == 0)
    {
        mstream_cstr(&ms, "static void" NL "sqlgen_error(int error_code, const char* error_code_str, const char* error_msg)" NL "{" NL);
        mstream_cstr(&ms, "    printf(\"SQL Error: %s (%d): %s\\n\", error_code_str, error_code, error_msg);" NL);
        mstream_cstr(&ms, "}" NL NL);
    }

    if (root->source_preamble.len)
        mstream_fmt(&ms, NL "%S" NL NL, root->source_preamble, data);

    /* ------------------------------------------------------------------------
     * Query implementations
     * --------------------------------------------------------------------- */

    q = root->queries;
    while (q)
    {
        write_func_decl(&ms, root, NULL, q, data);
        mstream_cstr(&ms, NL "{" NL);

        /* Local variables */
        mstream_cstr(&ms, "    int ret");
        if (q->return_name.len)
            mstream_fmt(&ms, ", %S = -1", q->return_name, data);
        mstream_cstr(&ms, ";" NL);

        write_sqlite_prepare_stmt(&ms, root, NULL, q, data);
        write_sqlite_bind_args(&ms, root, NULL, q, data);
        write_sqlite_exec(&ms, root, NULL, q, data);

        mstream_cstr(&ms, "}" NL NL);

        q = q->next;
    }

    g = root->query_groups;
    while (g)
    {
        q = g->queries;
        while (q)
        {
            write_func_decl(&ms, root, g, q, data);
            mstream_cstr(&ms, NL "{" NL);

            /* Local variables */
            mstream_cstr(&ms, "    int ret");
            if (q->return_name.len)
                mstream_fmt(&ms, ", %S = -1", q->return_name, data);
            mstream_cstr(&ms, ";" NL);

            write_sqlite_prepare_stmt(&ms, root, g, q, data);
            write_sqlite_bind_args(&ms, root, g, q, data);
            write_sqlite_exec(&ms, root, g, q, data);

            mstream_cstr(&ms, "}" NL NL);

            q = q->next;
        }
        g = g->next;
    }

    /* ------------------------------------------------------------------------
     * Functions
     * --------------------------------------------------------------------- */

    f = root->functions;
    while (f)
    {
        mstream_fmt(&ms, "static int" NL "%S(struct %S* ctx", f->name, data,
                PREFIX(root->prefix, data));
        a = f->args;
        while (a)
        {
            mstream_fmt(&ms, ", %S %S", a->type, data, a->name, data);
            a = a->next;
        }
        mstream_cstr(&ms, ")" NL "{" NL);
        mstream_fmt(&ms, NL "%S" NL, f->body, data);
        mstream_cstr(&ms, NL "}" NL NL);

        f = f->next;
    }

    g = root->query_groups;
    while (g)
    {
        f = g->functions;
        while (f)
        {
            mstream_fmt(&ms, "static int" NL "%S_%S(struct %S* ctx",
                    g->name, data,
                    f->name, data,
                    PREFIX(root->prefix, data));
            a = f->args;
            while (a)
            {
                mstream_fmt(&ms, ", %S %S", a->type, data, a->name, data);
                a = a->next;
            }
            mstream_cstr(&ms, ")" NL "{" NL);
            mstream_fmt(&ms, NL "%S" NL, f->body, data);
            mstream_cstr(&ms, NL "}" NL NL);

            f = f->next;
        }
        g = g->next;
    }

    /* ------------------------------------------------------------------------
     * Open and close
     * --------------------------------------------------------------------- */

    mstream_fmt(&ms, "static struct %S*" NL "%S_open(const char* uri)" NL "{" NL,
            PREFIX(root->prefix, data),
            PREFIX(root->prefix, data));
    mstream_fmt(&ms, "    int ret;" NL);
    mstream_fmt(&ms, "    struct %S* ctx = %S(sizeof *ctx);" NL,
            PREFIX(root->prefix, data),
            MALLOC(root->malloc, data));
    mstream_cstr(&ms, "    if (ctx == NULL)" NL);
    mstream_cstr(&ms, "        return NULL;" NL);
    mstream_cstr(&ms, "    memset(ctx, 0, sizeof *ctx);" NL NL);
    mstream_cstr(&ms, "    ret = sqlite3_open_v2(uri, &ctx->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);" NL);
    mstream_cstr(&ms, "    if (ret == SQLITE_OK)" NL);
    mstream_cstr(&ms, "        return ctx;" NL NL);
    mstream_fmt(&ms, "    %S(ret, sqlite3_errstr(ret), sqlite3_errmsg(ctx->db));" NL,
                LOG_SQL_ERR(root->log_sql_err, data));
    mstream_fmt(&ms, "    %S(ctx);" NL, FREE(root->free, data));
    mstream_cstr(&ms, "    return NULL;" NL);
    mstream_cstr(&ms, "}" NL NL);

    mstream_fmt(&ms, "static void" NL "%S_close(struct %S* ctx)" NL "{" NL,
            PREFIX(root->prefix, data),
            PREFIX(root->prefix, data));
    /* Global queries */
    q = root->queries;
    while (q)
    {
        mstream_fmt(&ms, "    sqlite3_finalize(ctx->%S);" NL, q->name, data);
        q = q->next;
    }
    /* Grouped queries */
    g = root->query_groups;
    while (g)
    {
        q = g->queries;
        while (q)
        {
            mstream_fmt(&ms, "    sqlite3_finalize(ctx->%S_%S);" NL, g->name, data, q->name, data);
            q = q->next;
        }
        g = g->next;
    }
    mstream_cstr(&ms, "    sqlite3_close(ctx->db);" NL);
    mstream_fmt(&ms, "    %S(ctx);" NL, FREE(root->free, data));
    mstream_cstr(&ms, "}" NL NL);

    /* ------------------------------------------------------------------------
     * Migration
     * --------------------------------------------------------------------- */

    write_migration_sql_stmts(&ms, root, root->upgrade, data, "upgrade");
    write_migration_sql_stmts(&ms, root, root->downgrade, data, "downgrade");
    write_run_sql_stmts_func(&ms, root, data);
    write_version_func(&ms, root, data);
    if (!no_forwards_compat)
        write_downgrade_forward_compat_func(&ms, root, data);
    write_migration_to_func(&ms, root, data, no_forwards_compat);
    write_upgrade_func(&ms, root, data);
    write_reinit_func(&ms, root, data, no_forwards_compat);

    /* ------------------------------------------------------------------------
     * Interface
     * --------------------------------------------------------------------- */

    mstream_fmt(&ms, "static struct %S_interface db_sqlite3 = {" NL, PREFIX(root->prefix, data));
    mstream_fmt(&ms, "    %S_open," NL "    %S_close," NL,
            PREFIX(root->prefix, data),
            PREFIX(root->prefix, data));
    mstream_fmt(&ms, "    %S_version," NL, PREFIX(root->prefix, data));
    mstream_fmt(&ms, "    %S_upgrade," NL, PREFIX(root->prefix, data));
    mstream_fmt(&ms, "    %S_reinit," NL, PREFIX(root->prefix, data));
    mstream_fmt(&ms, "    %S_migrate_to," NL, PREFIX(root->prefix, data));

    /* Global queries */
    q = root->queries;
    while (q)
    {
        mstream_fmt(&ms, "    %S," NL, q->name, data);
        q = q->next;
    }

    /* Global functions */
    f = root->functions;
    while (f)
    {
        mstream_fmt(&ms, "    %S," NL, f->name, data);
        f = f->next;
    }

    /* Grouped queries */
    g = root->query_groups;
    while (g)
    {
        mstream_cstr(&ms, "    {" NL);

        /* Queries */
        q = g->queries;
        while (q)
        {
            mstream_fmt(&ms, "        %S_%S," NL, g->name, data, q->name, data);
            q = q->next;
        }

        /* Functions */
        f = g->functions;
        while (f)
        {
            mstream_fmt(&ms, "        %S_%S," NL, g->name, data, f->name, data);
            f = f->next;
        }

        mstream_cstr(&ms, "    }," NL);
        g = g->next;
    }

    mstream_cstr(&ms, "};" NL NL);

    /* ------------------------------------------------------------------------
     * Debug layer
     * --------------------------------------------------------------------- */

    if (debug_layer)
    {
        q = root->queries;
        while (q)
        {
            write_debug_wrapper(&ms, root, NULL, q, data);
            q = q->next;
        }
        g = root->query_groups;
        while (g)
        {
            q = g->queries;
            while (q)
            {
                write_debug_wrapper(&ms, root, g, q, data);
                q = q->next;
            }
            g = g->next;
        }

        /* Open and close wrappers */
        mstream_fmt (&ms, "static struct %S* dbg_%S_open(const char* uri)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
        mstream_fmt (&ms, "    struct %S* ctx;" NL, PREFIX(root->prefix, data));
        mstream_fmt (&ms, "    %S(\"Opening database \\\"%%s\\\"\\n\", uri);" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    ctx = db_sqlite3.open(uri);" NL);
        mstream_fmt (&ms, "    %S(\"retval=%%p\\n\", ctx);" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    return ctx;" NL);
        mstream_cstr(&ms, "}" NL NL);

        mstream_fmt (&ms, "static void dbg_%S_close(struct %S* ctx)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
        mstream_fmt (&ms, "    %S(\"Closing database\\n\");" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    db_sqlite3.close(ctx);" NL);
        mstream_cstr(&ms, "}" NL NL);

        mstream_fmt (&ms, "static int dbg_%S_version(struct %S* ctx)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
        mstream_cstr(&ms, "    int version;" NL);
        mstream_fmt (&ms, "    %S(\"Getting version...\\n\");" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    version = db_sqlite3.version(ctx);" NL);
        mstream_fmt (&ms, "    %S(\"retval=%%d\\n\", version);" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    return version;" NL);
        mstream_cstr(&ms, "}" NL NL);

        mstream_fmt (&ms, "static int dbg_%S_upgrade(struct %S* ctx)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
        mstream_cstr(&ms, "    int ret;" NL);
        mstream_fmt (&ms, "    %S(\"Upgrading db...\\n\");" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    ret = db_sqlite3.upgrade(ctx);" NL);
        mstream_fmt (&ms, "    %S(\"retval=%%d\\n\", ret);" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    return ret;" NL);
        mstream_cstr(&ms, "}" NL NL);

        mstream_fmt (&ms, "static int dbg_%S_reinit(struct %S* ctx)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
        mstream_cstr(&ms, "    int ret;" NL);
        mstream_fmt (&ms, "    %S(\"Re-initializing db...\\n\");" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    ret = db_sqlite3.reinit(ctx);" NL);
        mstream_fmt (&ms, "    %S(\"retval=%%d\\n\", ret);" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    return ret;" NL);
        mstream_cstr(&ms, "}" NL NL);

        mstream_fmt (&ms, "static int dbg_%S_migrate_to(struct %S* ctx, int target_version)" NL "{" NL, PREFIX(root->prefix, data), PREFIX(root->prefix, data));
        mstream_cstr(&ms, "    int ret;" NL);
        mstream_fmt (&ms, "    %S(\"Migrating db to version: %%d...\\n\", target_version);" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    ret = db_sqlite3.migrate_to(ctx, target_version);" NL);
        mstream_fmt (&ms, "    %S(\"retval=%%d\\n\", ret);" NL, LOG_DBG(root->log_dbg, data));
        mstream_cstr(&ms, "    return ret;" NL);
        mstream_cstr(&ms, "}" NL NL);

        mstream_fmt(&ms, "static struct %S_interface dbg_db_sqlite3 = {" NL, PREFIX(root->prefix, data));
        mstream_fmt(&ms,
            "    dbg_%S_open," NL
            "    dbg_%S_close," NL
            "    dbg_%S_version," NL
            "    dbg_%S_upgrade," NL
            "    dbg_%S_reinit," NL
            "    dbg_%S_migrate_to," NL,
                PREFIX(root->prefix, data),
                PREFIX(root->prefix, data),
                PREFIX(root->prefix, data),
                PREFIX(root->prefix, data),
                PREFIX(root->prefix, data),
                PREFIX(root->prefix, data));
        /* Functions */
        f = root->functions;
        while (f)
        {
            mstream_fmt(&ms, "    %S," NL, f->name, data);
            f = f->next;
        }
        /* Global queries */
        q = root->queries;
        while (q)
        {
            mstream_fmt(&ms, "    dbg_%S," NL, q->name, data);
            q = q->next;
        }
        /* Grouped queries */
        g = root->query_groups;
        while (g)
        {
            mstream_cstr(&ms, "    {" NL);
            q = g->queries;
            while (q)
            {
                mstream_fmt(&ms, "        dbg_%S_%S," NL, g->name, data, q->name, data);
                q = q->next;
            }
            mstream_cstr(&ms, "    }," NL);
            g = g->next;
        }
        mstream_cstr(&ms, "};" NL NL);
    }

    /* ------------------------------------------------------------------------
     * API
     * --------------------------------------------------------------------- */

    if (!custom_init)
    {
        mstream_fmt(&ms, "int" NL "%S_init(void)" NL "{" NL, PREFIX(root->prefix, data));
        mstream_cstr(&ms, "    if (sqlite3_initialize() != SQLITE_OK)" NL);
        mstream_cstr(&ms, "        return -1;" NL);
        mstream_cstr(&ms, "    return 0;" NL);
        mstream_cstr(&ms, "}" NL NL);
    }

    if (!custom_deinit)
    {
        mstream_fmt(&ms, "void" NL "%S_deinit(void)" NL "{" NL, PREFIX(root->prefix, data));
        mstream_cstr(&ms, "    sqlite3_shutdown();" NL);
        mstream_cstr(&ms, "}" NL NL);
    }

    if (!custom_api)
    {
        mstream_fmt(&ms, "struct %S_interface* %S(const char* backend)" NL "{" NL,
            PREFIX(root->prefix, data),
            PREFIX(root->prefix, data));
        mstream_cstr(&ms, "    if (strcmp(\"sqlite3\", backend) == 0)" NL);
        mstream_fmt(&ms, "        return &%sdb_sqlite3;" NL, debug_layer ? "dbg_" : "");
        mstream_fmt(&ms, "    %S(\"%S(): Unknown backend \\\"%%s\\\"\", backend);" NL,
            LOG_ERR(root->log_err, data), PREFIX(root->prefix, data));
        mstream_cstr(&ms, "    return NULL;" NL);
        mstream_cstr(&ms, "}" NL);
    }

    if (root->source_postamble.len)
        mstream_fmt(&ms, NL "%S" NL, root->source_postamble, data);

    /* Don't write source if it is identical to the existing one -- causes less
     * rebuilds */
    if (mfile_map_read(&mf, file_name) == 0)
    {
        if (mf.size == ms.idx && memcmp(mf.address, ms.address, mf.size) == 0)
            return 0;
        mfile_unmap(&mf);
    }

    if (mfile_map_write(&mf, file_name, ms.idx) != 0)
        return -1;
    memcpy(mf.address, ms.address, ms.idx);
    mfile_unmap(&mf);

    return 0;
}

int main(int argc, char** argv)
{
    struct parser parser;
    struct mfile mf;
    struct root root;
    struct cfg cfg = {0};
    if (parse_cmdline(argc, argv, &cfg) != 0)
        return -1;

    if (mfile_map_read(&mf, cfg.input_file) < 0)
        return -1;

    root_init(&root);
    parser_init(&parser, &mf);
    if (parse(&parser, &root, &cfg) != 0)
        return -1;

    if (post_parse(&root, mf.address) != 0)
        return -1;

    if (gen_header(&root, mf.address, cfg.output_header,
            cfg.custom_init_decl, cfg.custom_deinit_decl, cfg.custom_api_decl) < 0)
        return -1;
    if (gen_source(&root, mf.address, cfg.output_source,
            cfg.debug_layer, cfg.custom_init, cfg.custom_deinit, cfg.custom_api, cfg.no_forwards_compat) < 0)
        return -1;

    return 0;
}
