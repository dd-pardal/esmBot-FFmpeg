#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "safepath.h"

static const char *strip_dot_slash(const char *path) {
    return (path[0] == '.' && path[1] == '/') ? path + 2 : path;
}

bool ff_safepath_is_safe(const char *path) {
    path = strip_dot_slash(path);
    return av_strstart(path, "assets/", NULL) != 0;
}

bool ff_safepath_is_safe_font(const char *path) {
    path = strip_dot_slash(path);
    return av_strstart(path, "assets/fonts/", NULL) != 0;
}

void ff_safepath_log_error(void *avcl, char *path) {
    av_log(avcl, AV_LOG_ERROR, "The file '%s' is not available for use with esmBot.\n", path);
}
