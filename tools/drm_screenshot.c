/**
 * drm_screenshot - save what the Force is showing, as a PPM image (root only, read-only).
 *
 *   drm_screenshot [-r] [out.ppm] [/dev/dri/card1]
 *
 * Reads the framebuffer scanned out by the largest enabled plane, which is MPC's interface.
 * The panel is portrait (800x1280) while MPC draws a landscape interface rotated by 90 degrees,
 * so by default the image is rotated back to landscape (1280x800); -r keeps the panel's own
 * orientation. The hardware cursor is a separate plane and does not appear in the image.
 */
#include <drm_fourcc.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main(int argc, char** argv)
{
    int raw = 0, argi = 1;
    if (argi < argc && !strcmp(argv[argi], "-r")) {
        raw = 1;
        argi++;
    }
    const char* out = argi < argc ? argv[argi++] : "/tmp/screen.ppm";
    const char* card = argi < argc ? argv[argi] : "/dev/dri/card1";

    int fd = open(card, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(card);
        return 1;
    }
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

    drmModePlaneRes* planes = drmModeGetPlaneResources(fd);
    uint32_t best_fb = 0;
    uint64_t best_area = 0;
    for (uint32_t i = 0; planes && i < planes->count_planes; i++) {
        drmModePlane* p = drmModeGetPlane(fd, planes->planes[i]);
        if (p && p->fb_id) {
            drmModeFB2* fb = drmModeGetFB2(fd, p->fb_id);
            if (fb && (uint64_t)fb->width * fb->height > best_area) {
                best_area = (uint64_t)fb->width * fb->height;
                best_fb = p->fb_id;
            }
            drmModeFreeFB2(fb);
        }
        drmModeFreePlane(p);
    }
    if (!best_fb) {
        fprintf(stderr, "no enabled plane with a framebuffer\n");
        return 1;
    }

    drmModeFB2* fb = drmModeGetFB2(fd, best_fb);
    if (!fb || !fb->handles[0]) {
        fprintf(stderr, "cannot get the framebuffer handle (run as root)\n");
        return 1;
    }
    if (fb->pixel_format != DRM_FORMAT_XRGB8888 && fb->pixel_format != DRM_FORMAT_ARGB8888) {
        fprintf(stderr, "unsupported pixel format 0x%08x\n", fb->pixel_format);
        return 1;
    }

    struct drm_mode_map_dumb map;
    memset(&map, 0, sizeof map);
    map.handle = fb->handles[0];
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        perror("MAP_DUMB");
        return 1;
    }
    size_t size = (size_t)fb->pitches[0] * fb->height;
    const uint8_t* src = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, map.offset);
    if (src == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    unsigned w = raw ? fb->width : fb->height;
    unsigned h = raw ? fb->height : fb->width;
    FILE* f = fopen(out, "wb");
    if (!f) {
        perror(out);
        return 1;
    }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    uint8_t* row = malloc(w * 3);
    for (unsigned v = 0; v < h; v++) {
        for (unsigned u = 0; u < w; u++) {
            // landscape (u right, v down) from the panel (x_p, y_p): x_p = v, y_p = height - 1 - u
            unsigned xp = raw ? u : v;
            unsigned yp = raw ? v : fb->height - 1 - u;
            const uint8_t* px = src + (size_t)yp * fb->pitches[0] + (size_t)xp * 4; // B G R X
            row[u * 3 + 0] = px[2];
            row[u * 3 + 1] = px[1];
            row[u * 3 + 2] = px[0];
        }
        fwrite(row, 1, w * 3, f);
    }
    fclose(f);
    printf("%s: fb %u %ux%u %s, %ux%u written\n", out, best_fb, fb->width, fb->height, raw ? "raw" : "rotated", w, h);
    return 0;
}
