#include <stdbool.h>
#include <stdint.h>
#include <dev/char/serial.h>
#include <lib/libc.h>
#include <lib/lock.h>
#include <lib/print.h>

static const char *base_digits_lowercase = "0123456789abcdef";
static const char *base_digits_uppercase = "0123456789ABCDEF";

static inline void print_char(char *buffer, size_t size, size_t *offset, char ch) {
    if (*offset < size - 1) {
        buffer[(*offset)++] = ch;
    }
    buffer[*offset] = 0;
}

static inline void print_nstr(char *buffer, size_t size, size_t *offset, const char *str, size_t len) {
    while (*offset < size - 1 && len > 0) {
        buffer[(*offset)++] = *str++;
        len--;
    }
    buffer[*offset] = 0;
}

#define MAX_INT_WIDTH 64

static void print_uint(char *buffer, size_t size, size_t *offset, uint64_t value,
                       int base, const char *prefix, size_t field_len, char pad,
                       bool left_justify, const char *digits_set) {
    size_t prefix_len = strlen(prefix);

    char temp_buffer[MAX_INT_WIDTH];

    size_t start = MAX_INT_WIDTH;
    if (!value) {
        temp_buffer[--start] = '0';
    }
    while (value > 0) {
        temp_buffer[--start] = digits_set[value % base];
        value = value / base;
    }

    size_t len = MAX_INT_WIDTH - start + prefix_len;
    size_t to_pad = len < field_len ? field_len - len : 0;

    if (pad == '0') {
        print_nstr(buffer, size, offset, prefix, prefix_len);
    }

    for (size_t i = 0; pad != '\0' && !left_justify && i < to_pad; ++i) {
        print_char(buffer, size, offset, pad);
    }

    if (pad != '0') {
        print_nstr(buffer, size, offset, prefix, prefix_len);
    }

    print_nstr(buffer, size, offset, temp_buffer + start, MAX_INT_WIDTH - start);

    for (size_t i = 0; pad != '\0' && left_justify && i < to_pad; ++i) {
        print_char(buffer, size, offset, pad);
    }
}

static void print_int(char *buffer, size_t size, size_t *offset, int64_t value,
                      int base, const char *negative_prefix,
                      const char *positive_prefix, size_t field_len, char pad,
                      bool left_justify, const char *digits_set) {
    uint64_t modulus;
    const char *prefix;

    if (value == INT64_MIN) {
        prefix = negative_prefix;
        modulus = (uint64_t)INT64_MAX + 1;
    } else if (value < 0) {
        prefix = negative_prefix;
        modulus = -value;
    } else {
        prefix = positive_prefix;
        modulus = value;
    }

    print_uint(buffer, size, offset, modulus, base, prefix, field_len, pad,
               left_justify, digits_set);
}

static void print_nstr_pad(char *buffer, size_t size, size_t *offset,
                           const char *str, size_t len, size_t field_len,
                           char pad, bool left_justify) {
    unsigned int to_pad = len < field_len ? field_len - len : 0;
    for (size_t i = 0; !left_justify && i < to_pad; ++i) {
        print_char(buffer, size, offset, pad);
    }

    print_nstr(buffer, size, offset, str, len);

    for (size_t i = 0; left_justify && i < to_pad; ++i) {
        print_char(buffer, size, offset, pad);
    }
}

#define NULL_REPR "(null)"
#define NULL_REPR_LEN 6

size_t vsnprint(char *buffer, size_t size, const char *fmt, va_list args) {
    size_t offset = 0;

    while (offset < size - 1) {
        while (*fmt && *fmt != '%') {
            print_char(buffer, size, &offset, *fmt++);
        }

        if (!*fmt++ || offset == size - 1) {
            goto out;
        }

        bool long_arg = false;
        size_t field_len = 0;
        bool dash_flag = false;
        bool zero_flag = false;
        bool plus_flag = false;
        bool space_flag = false;
        bool hash_flag = false;

parse_flags:
        char ch = *fmt++;
        char pad = (zero_flag && !dash_flag) ? '0' : ' ';
        bool left_justify = dash_flag;

        switch (ch) {
            case '-':
                dash_flag = true;
                goto parse_flags;
            case '0':
                zero_flag = true;
                goto parse_flags;
            case ' ':
                space_flag = true;
                goto parse_flags;
            case '+':
                plus_flag = true;
                goto parse_flags;
            case '#':
                hash_flag = true;
                goto parse_flags;
            case '1'...'9': {
                field_len = ch - '0';
                while ('0' <= *fmt && *fmt <= '9') {
                    ch = *fmt++;
                    field_len = field_len * 10 + ch - '0';
                }
                goto parse_flags;
            }
            case 'l':
                long_arg = true;
                goto parse_flags;
            case 's':
            case 'S': {
                char *str = va_arg(args, char *);
                size_t len;
                if (str == NULL) {
                    str = NULL_REPR;
                    len = NULL_REPR_LEN;
                } if (ch == 's') {
                    len = strlen(str);
                } else {
                    len = va_arg(args, size_t);
                }
                if (len == 0 && space_flag) {
                    str = " ";
                    len = 1;
                }
                print_nstr_pad(buffer, size, &offset, str, len, field_len, ' ',
                               left_justify);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                print_char(buffer, size, &offset, c);
                break;
            }
            case '%':
                print_char(buffer, size, &offset, '%');
                break;
            case 'i':
            case 'd': {
                int64_t value;
                if (long_arg) {
                    value = va_arg(args, int64_t);
                } else {
                    value = va_arg(args, int32_t);
                }
                const char *pos_prefix = "";
                if (plus_flag) {
                    pos_prefix = "+";
                } else if (space_flag) {
                    pos_prefix = " ";
                }
                print_int(buffer, size, &offset, value, 10, "-", pos_prefix, field_len,
                          pad, left_justify, base_digits_lowercase);
                break;
            }
            case 'u': {
                uint64_t value;
                if (long_arg) {
                    value = va_arg(args, uint64_t);
                } else {
                    value = va_arg(args, uint32_t);
                }
                print_uint(buffer, size, &offset, value, 10, "", field_len, pad,
                           left_justify, base_digits_lowercase);
                break;
            }
            case 'o': {
                uint64_t value;
                if (long_arg) {
                    value = va_arg(args, uint64_t);
                } else {
                    value = va_arg(args, uint32_t);
                }
                const char *prefix = "";
                if (hash_flag && value != 0) {
                    prefix = "0";
                }
                print_uint(buffer, size, &offset, value, 8, prefix, field_len, pad,
                           left_justify, base_digits_lowercase);
                break;
            }
            case 'p':
            case 'x':
            case 'X': {
                uint64_t value;
                bool pointer = ch == 'p';
                if (pointer) {
                    value = (uint64_t)va_arg(args, void *);
                } else if (long_arg) {
                    value = va_arg(args, uint64_t);
                } else {
                    value = va_arg(args, uint32_t);
                }

                const char *digits_set = base_digits_lowercase;
                const char *prefix = "";
                if (ch == 'X') {
                    digits_set = base_digits_uppercase;
                    if (hash_flag) {
                        prefix = "0X";
                    }
                } else if (hash_flag) {
                    prefix = "0x";
                }

                if (pointer && value == 0) {
                    print_nstr_pad(buffer, size, &offset, NULL_REPR, NULL_REPR_LEN,
                                   field_len, pad, left_justify);
                } else {
                    print_uint(buffer, size, &offset, value, 16, prefix, field_len, pad,
                               left_justify, digits_set);
                }
                break;
            }
            default:
                print_char(buffer, size, &offset, '?');
                break;
        }
    }

out:
    return offset;
}

size_t snprint(char *buffer, size_t size, const char *fmt, ...) {
    va_list args;
    size_t ret;

    va_start(args, fmt);
    ret = vsnprint(buffer, size, fmt, args);
    va_end(args);

    return ret;
}

void vprint(const char *fmt, va_list args) {
    static spinlock_t lock = SPINLOCK_INIT;

    spinlock_acquire(&lock);

    char buffer[1024];
    vsnprint(buffer, sizeof(buffer), fmt, args);
    serial_outstr(buffer);

    spinlock_release(&lock);
}

void print(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vprint(fmt, args);
    va_end(args);
}
