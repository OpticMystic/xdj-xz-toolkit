// SPDX-License-Identifier: MPL-2.0
// Volatile DirectFB/Fusion overlay probe for Pioneer XDJ-XZ firmware 1.26.

#include <directfb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int check(DFBResult result, const char *operation) {
    if (result != DFB_OK) {
        fprintf(stderr, "%s failed: %d (%s)\n", operation, result, DirectFBErrorString(result));
        return 0;
    }
    printf("%s ok\n", operation);
    return 1;
}

int main(int argc, char **argv) {
    IDirectFB *dfb = NULL;
    IDirectFBDisplayLayer *layer = NULL;
    IDirectFBWindow *window = NULL;
    IDirectFBSurface *surface = NULL;
    DFBDisplayLayerDescription layer_desc;
    DFBWindowDescription desc;
    int seconds = argc > 1 ? atoi(argv[1]) : 20;

    if (!check(DirectFBInit(&argc, &argv), "DirectFBInit")) return 2;
    if (!check(DirectFBCreate(&dfb), "DirectFBCreate")) return 2;
    if (!check(dfb->GetDisplayLayer(dfb, DLID_PRIMARY, &layer), "GetDisplayLayer")) goto cleanup;
    memset(&layer_desc, 0, sizeof(layer_desc));
    if (check(layer->GetDescription(layer, &layer_desc), "GetDescription")) {
        printf("layer name=%s caps=0x%x level=%d regions=%d\n",
               layer_desc.name, layer_desc.caps, layer_desc.level, layer_desc.regions);
    }
    if (!check(layer->SetCooperativeLevel(layer, DLSCL_SHARED), "SetCooperativeLevel(SHARED)")) goto cleanup;

    memset(&desc, 0, sizeof(desc));
    desc.flags = DWDESC_CAPS | DWDESC_WIDTH | DWDESC_HEIGHT |
                 DWDESC_POSX | DWDESC_POSY | DWDESC_PIXELFORMAT |
                 DWDESC_OPTIONS | DWDESC_STACKING;
    desc.caps = DWCAPS_ALPHACHANNEL | DWCAPS_NODECORATION | DWCAPS_NOFOCUS;
    desc.width = 800;
    desc.height = 120;
    desc.posx = 0;
    desc.posy = 180;
    desc.pixelformat = DSPF_ARGB;
    desc.options = DWOP_ALPHACHANNEL;
    desc.stacking = DWSC_UPPER;

    if (!check(layer->CreateWindow(layer, &desc, &window), "CreateWindow")) goto cleanup;
    if (!check(window->GetSurface(window, &surface), "GetSurface")) goto cleanup;
    check(surface->Clear(surface, 0, 0, 0, 0), "Clear(transparent)");
    check(surface->SetColor(surface, 255, 170, 0, 255), "SetColor(amber)");
    check(surface->FillRectangle(surface, 0, 0, 800, 5), "Fill(top)");
    check(surface->FillRectangle(surface, 0, 115, 800, 5), "Fill(bottom)");
    check(surface->FillRectangle(surface, 0, 0, 5, 120), "Fill(left)");
    check(surface->FillRectangle(surface, 795, 0, 5, 120), "Fill(right)");
    check(surface->FillRectangle(surface, 60, 48, 680, 24), "Fill(center bar)");
    check(surface->Flip(surface, NULL, DSFLIP_WAITFORSYNC), "Flip");
    check(window->SetOpacity(window, 0xff), "SetOpacity");
    check(window->RaiseToTop(window), "RaiseToTop");
    printf("DIRECTFB_PROBE_VISIBLE seconds=%d\n", seconds);
    fflush(stdout);
    sleep(seconds);

cleanup:
    if (surface) surface->Release(surface);
    if (window) window->Release(window);
    if (layer) layer->Release(layer);
    if (dfb) dfb->Release(dfb);
    return window && surface ? 0 : 1;
}
