/* No ARGB icon tables: a build without Python 3 (no icon at all),
 * or Windows, whose window icons come from the .exe's resource. */
#include "xeen/platform.h"
/** Icon images for a build without generated icons: none. */
const xeen_icon_image *xeen_platform_icon_images(int which, size_t *count)
{
    (void)which;
    if (count) {
        *count = 0;
    }
    return NULL;
}
