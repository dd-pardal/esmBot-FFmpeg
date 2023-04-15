#include <stdbool.h>

/**
 * @return true if the path is safe, false otherwise
 */
bool ff_safepath_is_safe(const char *path);

/**
 * @return true if the path is safe for font loading, false otherwise
 */
bool ff_safepath_is_safe_font(const char *path);

/**
 * Prints an error message.
 */
void ff_safepath_log_error(void *avcl, char *path);
