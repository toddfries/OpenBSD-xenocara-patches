/*
 * Quartz-specific support for the XRandR extension
 *
 * Copyright (c) 2001-2004 Greg Parker and Torrey T. Lyons,
 *               2010      Jan Hauffa.
 *               2010-2011 Apple Inc.
 *                 All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the sale,
 * use or other dealings in this Software without prior written authorization.
 */

#include "sanitizedCarbon.h"

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "quartzCommon.h"
#include "quartzRandR.h"
#include "quartz.h"
#include "darwin.h"

#include "X11Application.h"

#include <AvailabilityMacros.h>

#include <X11/extensions/randr.h>
#include <randrstr.h>
#include <IOKit/graphics/IOGraphicsTypes.h>

/* TODO: UGLY, find a better way!
 * We want to ignore kXquartzDisplayChanged which are generated by us
 */
static Bool ignore_next_fake_mode_update = FALSE;

#define FAKE_REFRESH_ROOTLESS 1
#define FAKE_REFRESH_FULLSCREEN 2

#define DEFAULT_REFRESH  60
#define kDisplayModeUsableFlags  (kDisplayModeValidFlag | kDisplayModeSafeFlag)

#define CALLBACK_SUCCESS 0
#define CALLBACK_CONTINUE 1
#define CALLBACK_ERROR -1

typedef int (*QuartzModeCallback)
    (ScreenPtr, QuartzModeInfoPtr, void *);

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1060

static long getDictLong (CFDictionaryRef dictRef, CFStringRef key) {
    long value;

    CFNumberRef numRef = (CFNumberRef) CFDictionaryGetValue(dictRef, key);
    if (!numRef)
        return 0;

    if (!CFNumberGetValue(numRef, kCFNumberLongType, &value))
        return 0;
    return value;
}

static double getDictDouble (CFDictionaryRef dictRef, CFStringRef key) {
    double value;

    CFNumberRef numRef = (CFNumberRef) CFDictionaryGetValue(dictRef, key);
    if (!numRef)
        return 0.0;

    if (!CFNumberGetValue(numRef, kCFNumberDoubleType, &value))
        return 0.0;
    return value;
}

static void QuartzRandRGetModeInfo (CFDictionaryRef modeRef,
                                    QuartzModeInfoPtr pMode) {
    pMode->width = (size_t) getDictLong(modeRef, kCGDisplayWidth);
    pMode->height = (size_t) getDictLong(modeRef, kCGDisplayHeight);
    pMode->refresh = (int)(getDictDouble(modeRef, kCGDisplayRefreshRate) + 0.5);
    if (pMode->refresh == 0)
        pMode->refresh = DEFAULT_REFRESH;
    pMode->ref = NULL;
    pMode->pSize = NULL;
}

static Bool QuartzRandRCopyCurrentModeInfo (CGDirectDisplayID screenId,
                                           QuartzModeInfoPtr pMode) {
    CFDictionaryRef curModeRef = CGDisplayCurrentMode(screenId);
    if (!curModeRef)
        return FALSE;

    QuartzRandRGetModeInfo(curModeRef, pMode);
    pMode->ref = (void *)curModeRef;
    CFRetain(pMode->ref);
    return TRUE;
}

static Bool QuartzRandRSetCGMode (CGDirectDisplayID screenId,
                                QuartzModeInfoPtr pMode) {
    CFDictionaryRef modeRef = (CFDictionaryRef) pMode->ref;
    return (CGDisplaySwitchToMode(screenId, modeRef) == kCGErrorSuccess);
}

static Bool QuartzRandREnumerateModes (ScreenPtr pScreen,
                                       QuartzModeCallback callback,
                                       void *data) {
    Bool retval = FALSE;
    QuartzScreenPtr pQuartzScreen = QUARTZ_PRIV(pScreen);

    /* Just an 800x600 fallback if we have no attached heads */
    if(pQuartzScreen->displayIDs) {
        CFDictionaryRef curModeRef, modeRef;
        long curBpp;
        CFArrayRef modes;
        QuartzModeInfo modeInfo;
        int i;
        CGDirectDisplayID screenId = pQuartzScreen->displayIDs[0];

        curModeRef = CGDisplayCurrentMode(screenId);
        if (!curModeRef)
            return FALSE;
        curBpp = getDictLong(curModeRef, kCGDisplayBitsPerPixel);

        modes = CGDisplayAvailableModes(screenId);
        if (!modes)
            return FALSE;
        for (i = 0; i < CFArrayGetCount(modes); i++) {
            int cb;
            modeRef = (CFDictionaryRef) CFArrayGetValueAtIndex(modes, i);

            /* Skip modes that are not usable on the current display or have a
               different pixel encoding than the current mode. */
            if (((unsigned long) getDictLong(modeRef, kCGDisplayIOFlags) &
                 kDisplayModeUsableFlags) != kDisplayModeUsableFlags)
                continue;
            if (getDictLong(modeRef, kCGDisplayBitsPerPixel) != curBpp)
                continue;

            QuartzRandRGetModeInfo(modeRef, &modeInfo);
            modeInfo.ref = (void *)modeRef;
            cb = callback(pScreen, &modeInfo, data);
            if (cb == CALLBACK_CONTINUE)
                retval = TRUE;
            else if (cb == CALLBACK_SUCCESS)
                return TRUE;
            else if (cb == CALLBACK_ERROR)
                return FALSE;
        }
    }

    switch(callback(pScreen, &pQuartzScreen->rootlessMode, data)) {
        case CALLBACK_SUCCESS:
            return TRUE;
        case CALLBACK_ERROR:
            return FALSE;
        case CALLBACK_CONTINUE:
            retval = TRUE;
        default:
            break;
    }

    switch(callback(pScreen, &pQuartzScreen->fullscreenMode, data)) {
        case CALLBACK_SUCCESS:
            return TRUE;
        case CALLBACK_ERROR:
            return FALSE;
        case CALLBACK_CONTINUE:
            retval = TRUE;
        default:
            break;
    }

    return retval;
}

#else /* we have the new CG APIs from Snow Leopard */

static void QuartzRandRGetModeInfo (CGDisplayModeRef modeRef,
                                    QuartzModeInfoPtr pMode) {
    pMode->width = CGDisplayModeGetWidth(modeRef);
    pMode->height = CGDisplayModeGetHeight(modeRef);
    pMode->refresh = (int) (CGDisplayModeGetRefreshRate(modeRef) + 0.5);
    if (pMode->refresh == 0)
        pMode->refresh = DEFAULT_REFRESH;
    pMode->ref = NULL;
    pMode->pSize = NULL;
}

static Bool QuartzRandRCopyCurrentModeInfo (CGDirectDisplayID screenId,
                                            QuartzModeInfoPtr pMode) {
    CGDisplayModeRef curModeRef = CGDisplayCopyDisplayMode(screenId);
    if (!curModeRef)
        return FALSE;

    QuartzRandRGetModeInfo(curModeRef, pMode);
    pMode->ref = curModeRef;
    return TRUE;
}

static Bool QuartzRandRSetCGMode (CGDirectDisplayID screenId,
                                QuartzModeInfoPtr pMode) {
    CGDisplayModeRef modeRef = (CGDisplayModeRef) pMode->ref;
    if (!modeRef)
        return FALSE;

    return (CGDisplaySetDisplayMode(screenId, modeRef, NULL) == kCGErrorSuccess);
}

static Bool QuartzRandREnumerateModes (ScreenPtr pScreen,
                                       QuartzModeCallback callback,
                                       void *data) {
    Bool retval = FALSE;
    QuartzScreenPtr pQuartzScreen = QUARTZ_PRIV(pScreen);

    /* Just an 800x600 fallback if we have no attached heads */
    if(pQuartzScreen->displayIDs) {
        CGDisplayModeRef curModeRef, modeRef;
        CFStringRef curPixelEnc, pixelEnc;
        CFComparisonResult pixelEncEqual;
        CFArrayRef modes;
        QuartzModeInfo modeInfo;
        int i;
        CGDirectDisplayID screenId = pQuartzScreen->displayIDs[0];

        curModeRef = CGDisplayCopyDisplayMode(screenId);
        if (!curModeRef)
            return FALSE;
        curPixelEnc = CGDisplayModeCopyPixelEncoding(curModeRef);
        CGDisplayModeRelease(curModeRef);

        modes = CGDisplayCopyAllDisplayModes(screenId, NULL);
        if (!modes) {
            CFRelease(curPixelEnc);
            return FALSE;
        }
        for (i = 0; i < CFArrayGetCount(modes); i++) {
            int cb;
            modeRef = (CGDisplayModeRef) CFArrayGetValueAtIndex(modes, i);

            /* Skip modes that are not usable on the current display or have a
               different pixel encoding than the current mode. */
            if ((CGDisplayModeGetIOFlags(modeRef) & kDisplayModeUsableFlags) !=
                kDisplayModeUsableFlags)
                continue;
            pixelEnc = CGDisplayModeCopyPixelEncoding(modeRef);
            pixelEncEqual = CFStringCompare(pixelEnc, curPixelEnc, 0);
            CFRelease(pixelEnc);
            if (pixelEncEqual != kCFCompareEqualTo)
                continue;

            QuartzRandRGetModeInfo(modeRef, &modeInfo);
            modeInfo.ref = modeRef;
            cb = callback(pScreen, &modeInfo, data);
            if (cb == CALLBACK_CONTINUE) {
                retval = TRUE;
            } else if (cb == CALLBACK_SUCCESS) {
                CFRelease(modes);
                CFRelease(curPixelEnc);
                return TRUE;
            } else if (cb == CALLBACK_ERROR) {
                CFRelease(modes);
                CFRelease(curPixelEnc);
                return FALSE;
            }
        }

        CFRelease(modes);
        CFRelease(curPixelEnc);
    }

    switch(callback(pScreen, &pQuartzScreen->rootlessMode, data)) {
        case CALLBACK_SUCCESS:
            return TRUE;
        case CALLBACK_ERROR:
            return FALSE;
        case CALLBACK_CONTINUE:
            retval = TRUE;
        default:
            break;
    }

    switch(callback(pScreen, &pQuartzScreen->fullscreenMode, data)) {
        case CALLBACK_SUCCESS:
            return TRUE;
        case CALLBACK_ERROR:
            return FALSE;
        case CALLBACK_CONTINUE:
            retval = TRUE;
        default:
            break;
    }

    return retval;
}

#endif  /* Snow Leopard CoreGraphics APIs */


static Bool QuartzRandRModesEqual (QuartzModeInfoPtr pMode1,
                                   QuartzModeInfoPtr pMode2) {
    return (pMode1->width == pMode2->width) &&
           (pMode1->height == pMode2->height) &&
           (pMode1->refresh == pMode2->refresh);
}

static Bool QuartzRandRRegisterMode (ScreenPtr pScreen,
                                     QuartzModeInfoPtr pMode) {
    QuartzScreenPtr pQuartzScreen = QUARTZ_PRIV(pScreen);
    Bool isCurrentMode = QuartzRandRModesEqual(&pQuartzScreen->currentMode, pMode);

    /* TODO: DPI */
    pMode->pSize = RRRegisterSize(pScreen, pMode->width, pMode->height, pScreen->mmWidth, pScreen->mmHeight);
    if (pMode->pSize) {
        //DEBUG_LOG("registering: %d x %d @ %d %s\n", (int)pMode->width, (int)pMode->height, (int)pMode->refresh, isCurrentMode ? "*" : "");
        RRRegisterRate(pScreen, pMode->pSize, pMode->refresh);

        if (isCurrentMode)
            RRSetCurrentConfig(pScreen, RR_Rotate_0, pMode->refresh, pMode->pSize);

        return TRUE;
    }
    return FALSE;
}

static int QuartzRandRRegisterModeCallback (ScreenPtr pScreen,
                                        QuartzModeInfoPtr pMode,
                                        void *data __unused) {
    if(QuartzRandRRegisterMode(pScreen, pMode)) {
        return CALLBACK_CONTINUE;
    } else {
        return CALLBACK_ERROR;
    }
}

static Bool QuartzRandRSetMode(ScreenPtr pScreen, QuartzModeInfoPtr pMode, BOOL doRegister) {
    QuartzScreenPtr pQuartzScreen = QUARTZ_PRIV(pScreen);
    Bool captureDisplay = (pMode->refresh != FAKE_REFRESH_FULLSCREEN && pMode->refresh != FAKE_REFRESH_ROOTLESS);
    CGDirectDisplayID screenId;

    if(pQuartzScreen->displayIDs == NULL)
        return FALSE;

    screenId = pQuartzScreen->displayIDs[0];
    if(XQuartzShieldingWindowLevel == 0 && captureDisplay) {
        if(!X11ApplicationCanEnterRandR())
            return FALSE;
        CGCaptureAllDisplays();
        XQuartzShieldingWindowLevel = CGShieldingWindowLevel(); // 2147483630
        DEBUG_LOG("Display captured.  ShieldWindowID: %u, Shield level: %d\n",
                  CGShieldingWindowID(screenId), XQuartzShieldingWindowLevel);
    }

    if (pQuartzScreen->currentMode.ref && CFEqual(pMode->ref, pQuartzScreen->currentMode.ref)) {
        DEBUG_LOG("Requested RandR resolution matches current CG mode\n");
    } if (QuartzRandRSetCGMode(screenId, pMode)) {
        ignore_next_fake_mode_update = TRUE;
    } else {
        DEBUG_LOG("Error while requesting CG resolution change.\n");
        return FALSE;
    }

    /* If the client requested the fake rootless mode, switch to rootless.
     * Otherwise, force fullscreen mode.
     */
    QuartzSetRootless(pMode->refresh == FAKE_REFRESH_ROOTLESS);
    if (pMode->refresh != FAKE_REFRESH_ROOTLESS) {
        QuartzShowFullscreen(TRUE);
    }

    if(pQuartzScreen->currentMode.ref)
        CFRelease(pQuartzScreen->currentMode.ref);
    pQuartzScreen->currentMode = *pMode;
    if(pQuartzScreen->currentMode.ref)
        CFRetain(pQuartzScreen->currentMode.ref);
    
    if(XQuartzShieldingWindowLevel != 0 && !captureDisplay) {
        CGReleaseAllDisplays();
        XQuartzShieldingWindowLevel = 0;
    }

    return TRUE;
}

static int QuartzRandRSetModeCallback (ScreenPtr pScreen,
                                       QuartzModeInfoPtr pMode,
                                       void *data) {
    QuartzModeInfoPtr pReqMode = (QuartzModeInfoPtr) data;
	
    if (!QuartzRandRModesEqual(pMode, pReqMode))
        return CALLBACK_CONTINUE;  /* continue enumeration */

    DEBUG_LOG("Found a match for requested RandR resolution (%dx%d@%d).\n", (int)pMode->width, (int)pMode->height, (int)pMode->refresh);

    if(QuartzRandRSetMode(pScreen, pMode, FALSE))
        return CALLBACK_SUCCESS;
    else
        return CALLBACK_ERROR;
}

static Bool QuartzRandRGetInfo (ScreenPtr pScreen, Rotation *rotations) {
    *rotations = RR_Rotate_0;  /* TODO: support rotation */

    return QuartzRandREnumerateModes(pScreen, QuartzRandRRegisterModeCallback, NULL);
}

static Bool QuartzRandRSetConfig (ScreenPtr           pScreen,
                                  Rotation            randr,
                                  int                 rate,
                                  RRScreenSizePtr     pSize) {
    QuartzScreenPtr pQuartzScreen = QUARTZ_PRIV(pScreen);
    QuartzModeInfo reqMode;

    reqMode.width = pSize->width;
    reqMode.height = pSize->height;
    reqMode.refresh = rate;

    /* Do not switch modes if requested mode is equal to current mode. */
    if (QuartzRandRModesEqual(&reqMode, &pQuartzScreen->currentMode))
        return TRUE;
        
    if (QuartzRandREnumerateModes(pScreen, QuartzRandRSetModeCallback, &reqMode)) {
        return TRUE;
    }
    
    DEBUG_LOG("Unable to find a matching config: %d x %d @ %d\n", (int)reqMode.width, (int)reqMode.height, (int)reqMode.refresh);
    return FALSE;
}

static Bool _QuartzRandRUpdateFakeModes (ScreenPtr pScreen) {
    QuartzScreenPtr pQuartzScreen = QUARTZ_PRIV(pScreen);
    QuartzModeInfo activeMode;

    if(pQuartzScreen->displayCount > 0) {
        if(!QuartzRandRCopyCurrentModeInfo(pQuartzScreen->displayIDs[0], &activeMode)) {
            ErrorF("Unable to determine current display mode.\n");
            return FALSE;
        }
    } else {
        memset(&activeMode, 0, sizeof(activeMode));
        activeMode.width = 800;
        activeMode.height = 600;
        activeMode.refresh = 60;
    }

    if(pQuartzScreen->fullscreenMode.ref)
        CFRelease(pQuartzScreen->fullscreenMode.ref);
    if(pQuartzScreen->currentMode.ref)
        CFRelease(pQuartzScreen->currentMode.ref);

    if(pQuartzScreen->displayCount > 1) {
        activeMode.width = pScreen->width;
        activeMode.height = pScreen->height;
        if(XQuartzIsRootless)
            activeMode.height += aquaMenuBarHeight;
    }

    pQuartzScreen->fullscreenMode = activeMode; 
    pQuartzScreen->fullscreenMode.refresh = FAKE_REFRESH_FULLSCREEN;

    pQuartzScreen->rootlessMode = activeMode;
    pQuartzScreen->rootlessMode.refresh = FAKE_REFRESH_ROOTLESS;
    pQuartzScreen->rootlessMode.height -= aquaMenuBarHeight;

    if(XQuartzIsRootless) {
        pQuartzScreen->currentMode = pQuartzScreen->rootlessMode;
    } else {
        pQuartzScreen->currentMode = pQuartzScreen->fullscreenMode;
    }

    /* This extra retain is for currentMode's copy.
     * fullscreen and rootless share a retain.
     */
    if(pQuartzScreen->currentMode.ref)
        CFRetain(pQuartzScreen->currentMode.ref);
    
    DEBUG_LOG("rootlessMode: %d x %d\n", (int)pQuartzScreen->rootlessMode.width, (int)pQuartzScreen->rootlessMode.height);
    DEBUG_LOG("fullscreenMode: %d x %d\n", (int)pQuartzScreen->fullscreenMode.width, (int)pQuartzScreen->fullscreenMode.height);
    DEBUG_LOG("currentMode: %d x %d\n", (int)pQuartzScreen->currentMode.width, (int)pQuartzScreen->currentMode.height);
    
    return TRUE;
}

Bool QuartzRandRUpdateFakeModes (BOOL force_update) {
    ScreenPtr pScreen = screenInfo.screens[0];
    
    if(ignore_next_fake_mode_update) {
        DEBUG_LOG("Ignoring update request caused by RandR resolution change.\n");
        ignore_next_fake_mode_update = FALSE;
        return TRUE;
    }
    
    if(!_QuartzRandRUpdateFakeModes(pScreen))
        return FALSE;
    
    if(force_update)
        RRGetInfo(pScreen, TRUE);

    return TRUE;
}

Bool QuartzRandRInit (ScreenPtr pScreen) {
    rrScrPrivPtr    pScrPriv;
    
    if (!RRScreenInit (pScreen)) return FALSE;
    if (!_QuartzRandRUpdateFakeModes (pScreen)) return FALSE;

    pScrPriv = rrGetScrPriv(pScreen);
    pScrPriv->rrGetInfo = QuartzRandRGetInfo;
    pScrPriv->rrSetConfig = QuartzRandRSetConfig;
    return TRUE;
}

void QuartzRandRSetFakeRootless (void) {
    int i;
    
    DEBUG_LOG("QuartzRandRSetFakeRootless called.\n");
    
    for (i=0; i < screenInfo.numScreens; i++) {
        ScreenPtr pScreen = screenInfo.screens[i];
        QuartzScreenPtr pQuartzScreen = QUARTZ_PRIV(pScreen);

        QuartzRandRSetMode(pScreen, &pQuartzScreen->rootlessMode, TRUE);
    }
}

void QuartzRandRSetFakeFullscreen (BOOL state) {
    int i;

    DEBUG_LOG("QuartzRandRSetFakeFullscreen called.\n");
    
    for (i=0; i < screenInfo.numScreens; i++) {
        ScreenPtr pScreen = screenInfo.screens[i];
        QuartzScreenPtr pQuartzScreen = QUARTZ_PRIV(pScreen);

        QuartzRandRSetMode(pScreen, &pQuartzScreen->fullscreenMode, TRUE);
    }
    
    QuartzShowFullscreen(state);
}

/* Toggle fullscreen mode.  If "fake" fullscreen is the current mode,
 * this will just show/hide the X11 windows.  If we are in a RandR fullscreen
 * mode, this will toggles us to the default fake mode and hide windows if
 * it is fullscreen
 */
void QuartzRandRToggleFullscreen (void) {
    ScreenPtr pScreen = screenInfo.screens[0];
    QuartzScreenPtr pQuartzScreen = QUARTZ_PRIV(pScreen);

    if (pQuartzScreen->currentMode.ref == NULL) {
        ErrorF("Ignoring QuartzRandRToggleFullscreen because don't have a current mode set.\n");
    } else if (pQuartzScreen->currentMode.refresh == FAKE_REFRESH_ROOTLESS) {
        ErrorF("Ignoring QuartzRandRToggleFullscreen because we are in rootless mode.\n");
    } else if (pQuartzScreen->currentMode.refresh == FAKE_REFRESH_FULLSCREEN) {
        /* Legacy fullscreen mode.  Hide/Show */
        QuartzShowFullscreen(!XQuartzFullscreenVisible);
    } else {
        /* RandR fullscreen mode.  Return to default mode and hide if it is fullscreen. */
        if(XQuartzRootlessDefault) {
            QuartzRandRSetFakeRootless();
        } else {
            QuartzRandRSetFakeFullscreen(FALSE);
        }
    }    
}
