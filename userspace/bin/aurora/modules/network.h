#ifndef AURORA_NETWORK_H
#define AURORA_NETWORK_H

#include "aurora_types.h"
#include "draw.h"

static inline void network_refresh_info(struct aurora_network_info *info) {
    char dev_buf[512];
    char route_buf[512];
    memset(info, 0, sizeof(*info));
    strcpy(info->ifname, "eth0");
    strcpy(info->ip_addr, "10.0.2.15");
    strcpy(info->gateway, "10.0.2.2");
    strcpy(info->netmask, "255.255.255.0");
    info->signal_percent = 100;

    if (read_text_file("/proc/net/dev", dev_buf, sizeof(dev_buf))) {
        if (strstr(dev_buf, "eth0:")) {
            info->available = 1;
            strcpy(info->ifname, "eth0");
        } else if (strstr(dev_buf, "wlan0:")) {
            info->available = 1;
            strcpy(info->ifname, "wlan0");
        }
    }

    if (info->available && read_text_file("/proc/net/route", route_buf, sizeof(route_buf))) {
        if (strstr(route_buf, info->ifname)) {
            info->connected = 1;
        }
    }
}

static inline void render_network_popup(uint32_t *pixels, uint32_t width, uint32_t height,
                                        const struct aurora_network_info *net) {
    /* Network status flyout window */
    fill_rounded_rect(pixels, width, height, 0, 0, (int)width, (int)height, 12,
                      g_light_theme ? UINT32_C(0xf6f8fb) : UINT32_C(0x151e2b),
                      g_light_theme ? UINT32_C(0xbcc8d6) : UINT32_C(0x4c5e77));

    /* Header */
    draw_ui_text(pixels, width, height, 16, 12, 1, "Network Connectivity",
                 g_light_theme ? UINT32_C(0x19273a) : UINT32_C(0xf2f6fb), (int)width - 60);

    /* Connection status badge */
    int conn = net ? net->connected : 0;
    fill_rounded_rect(pixels, width, height, (int)width - 32, 14, 10, 10, 5,
                      conn ? UINT32_C(0x22c55e) : UINT32_C(0xef4444),
                      conn ? UINT32_C(0x16a34a) : UINT32_C(0xdc2626));

    /* Separator */
    fill_rect(pixels, width, height, 14, 34, (int)width - 28, 1,
              g_light_theme ? UINT32_C(0xd0d8e2) : UINT32_C(0x2d3d52));

    /* Interface info */
    char buf[64];
    snprintf(buf, sizeof(buf), "Interface: %s", net && net->ifname[0] ? net->ifname : "eth0");
    draw_ui_text(pixels, width, height, 16, 44, 1, buf,
                 g_light_theme ? UINT32_C(0x2d3d52) : UINT32_C(0xe2e8f0), (int)width - 32);

    snprintf(buf, sizeof(buf), "State: %s", conn ? "Connected (Hardware Link UP)" : "Disconnected");
    draw_ui_text(pixels, width, height, 16, 64, 1, buf,
                 conn ? UINT32_C(0x4ade80) : UINT32_C(0xf87171), (int)width - 32);

    snprintf(buf, sizeof(buf), "IP Address: %s", conn && net ? net->ip_addr : "None");
    draw_ui_text(pixels, width, height, 16, 84, 1, buf,
                 g_light_theme ? UINT32_C(0x475569) : UINT32_C(0x94a3b8), (int)width - 32);

    snprintf(buf, sizeof(buf), "Gateway: %s", conn && net ? net->gateway : "None");
    draw_ui_text(pixels, width, height, 16, 104, 1, buf,
                 g_light_theme ? UINT32_C(0x475569) : UINT32_C(0x94a3b8), (int)width - 32);

    snprintf(buf, sizeof(buf), "Netmask: %s", conn && net ? net->netmask : "None");
    draw_ui_text(pixels, width, height, 16, 124, 1, buf,
                 g_light_theme ? UINT32_C(0x475569) : UINT32_C(0x94a3b8), (int)width - 32);

    /* Quick Action: Settings Shortcut */
    fill_rounded_rect(pixels, width, height, 14, (int)height - 34, (int)width - 28, 24, 6,
                      g_light_theme ? UINT32_C(0xe2e8f0) : UINT32_C(0x202c3e),
                      g_light_theme ? UINT32_C(0xcad4e0) : UINT32_C(0x3a4b64));
    draw_ui_text(pixels, width, height, 24, (int)height - 29, 1, "Configure Adapter (akaos-config)",
                 UINT32_C(0x60a5fa), (int)width - 48);
}

#endif /* AURORA_NETWORK_H */
