/**
 * drm_planes - list the CRTCs, connectors and planes of a DRM device with their type,
 * supported formats and current state. Read-only: it only issues GET ioctls, so it is
 * safe to run while MPC owns the display.
 *
 *   drm_planes [/dev/dri/card1]
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static const char* plane_type_name(uint64_t v)
{
    return v == 0 ? "overlay" : v == 1 ? "primary" : v == 2 ? "cursor" : "?";
}

static void print_fourcc(uint32_t f)
{
    printf("%c%c%c%c ", f & 0xff, (f >> 8) & 0xff, (f >> 16) & 0xff, (f >> 24) & 0xff);
}

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "/dev/dri/card1";
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(path);
        return 1;
    }
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

    drmModeRes* res = drmModeGetResources(fd);
    if (!res) {
        fprintf(stderr, "drmModeGetResources failed\n");
        return 1;
    }
    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtc* c = drmModeGetCrtc(fd, res->crtcs[i]);
        printf("crtc[%d] id=%u mode=%s %ux%u\n", i, res->crtcs[i], c && c->mode_valid ? c->mode.name : "(none)",
            c ? c->width : 0, c ? c->height : 0);
        drmModeFreeCrtc(c);
    }

    drmModePlaneRes* pr = drmModeGetPlaneResources(fd);
    for (uint32_t i = 0; pr && i < pr->count_planes; i++) {
        drmModePlane* p = drmModeGetPlane(fd, pr->planes[i]);
        if (!p)
            continue;
        printf("plane id=%u possible_crtcs=0x%x crtc_id=%u fb_id=%u\n  formats: ", p->plane_id, p->possible_crtcs,
            p->crtc_id, p->fb_id);
        for (uint32_t k = 0; k < p->count_formats; k++)
            print_fourcc(p->formats[k]);
        printf("\n  props:");
        drmModeObjectProperties* props = drmModeObjectGetProperties(fd, p->plane_id, DRM_MODE_OBJECT_PLANE);
        for (uint32_t k = 0; props && k < props->count_props; k++) {
            drmModePropertyRes* pr2 = drmModeGetProperty(fd, props->props[k]);
            if (!pr2)
                continue;
            if (!strcmp(pr2->name, "type"))
                printf(" type=%s(%llu)", plane_type_name(props->prop_values[k]), (unsigned long long)props->prop_values[k]);
            else
                printf(" %s=%llu", pr2->name, (unsigned long long)props->prop_values[k]);
            drmModeFreeProperty(pr2);
        }
        printf("\n");
        drmModeFreeObjectProperties(props);
        drmModeFreePlane(p);
    }
    return 0;
}
