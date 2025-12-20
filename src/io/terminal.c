/* terminal.c - Terminal I/O stub */
#include "basic/basic.h"
#include "basic/errors.h"

/* Error codes */
const char ERROR_CODES[][3] = {
    "NF", "SN", "RG", "OD", "FC", "OV", "OM", "UL",
    "BS", "DD", "/0", "ID", "TM", "OS", "LS", "ST",
    "CN", "UF", "MO"
};

/* Global error context */
error_context_t g_last_error = {0};

const char *error_code_string(basic_error_t err) {
    if (err >= ERR_COUNT) return "??";
    return ERROR_CODES[err];
}

void basic_error(basic_error_t code) {
    g_last_error.code = code;
}

void basic_error_at_line(basic_error_t code, uint16_t line) {
    g_last_error.code = code;
    g_last_error.line_number = line;
}

void basic_clear_error(void) {
    g_last_error.code = ERR_NONE;
}
