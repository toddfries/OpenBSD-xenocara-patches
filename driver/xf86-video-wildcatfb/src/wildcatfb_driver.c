/*	$OpenBSD: wildcatfb_driver.c,v 1.3 2009/09/22 22:00:54 matthieu Exp $	*/

/*
 * Copyright (c) 2009 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Heavily based on the xf86-video-wsfb driver:
 *
 * Copyright (c) 2001 Matthieu Herrb
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Based on fbdev.c written by:
 *
 * Authors:  Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *	     Michel Dänzer, <michdaen@iiic.ethz.ch>
 */

/*
 * WildcatFBShadowUpdate() is derived from shadowUpdatePacked():
 *
 * Copyright © 2000 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <dev/wscons/wsconsio.h>

/* All drivers need this. */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "mipointer.h"
#include "mibstore.h"
#include "micmap.h"
#include "colormapst.h"
#include "xf86cmap.h"
#include "shadow.h"

#include "fb.h"

#include "xf86Resources.h"
#include "xf86RAC.h"

#ifdef XvExtension
#include "xf86xv.h"
#endif

/* extra includes for shadow */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include    <X11/X.h>
#include    "scrnintstr.h"
#include    "windowstr.h"
#include    <X11/fonts/font.h>
#include    "dixfontstr.h"
#include    <X11/fonts/fontstruct.h>
#include    "mi.h"
#include    "regionstr.h"
#include    "globals.h"
#include    "gcstruct.h"

#ifdef X_PRIVSEP
extern int priv_open_device(const char *);
#else
#define priv_open_device(n)    open(n,O_RDWR|O_NONBLOCK|O_EXCL)
#endif

#define WILDCATFB_DEFAULT_DEV "/dev/ttyC0"

#define DEBUG 0

#if DEBUG
# define TRACE_ENTER(str)       ErrorF("wildcatfb: " str " %d\n",pScrn->scrnIndex)
# define TRACE_EXIT(str)        ErrorF("wildcatfb: " str " done\n")
# define TRACE(str)             ErrorF("wildcatfb trace: " str "\n")
#else
# define TRACE_ENTER(str)
# define TRACE_EXIT(str)
# define TRACE(str)
#endif

/* Prototypes */
#ifdef XFree86LOADER
static pointer WildcatFBSetup(pointer, pointer, int *, int *);
#endif
static Bool WildcatFBGetRec(ScrnInfoPtr);
static void WildcatFBFreeRec(ScrnInfoPtr);
static const OptionInfoRec * WildcatFBAvailableOptions(int, int);
static void WildcatFBIdentify(int);
static Bool WildcatFBProbe(DriverPtr, int);
static Bool WildcatFBPreInit(ScrnInfoPtr, int);
static Bool WildcatFBScreenInit(int, ScreenPtr, int, char **);
static Bool WildcatFBCloseScreen(int, ScreenPtr);
static void *WildcatFBWindowLinear(ScreenPtr, CARD32, CARD32, int, CARD32 *,
			      void *);
static Bool WildcatFBEnterVT(int, int);
static void WildcatFBLeaveVT(int, int);
static Bool WildcatFBSwitchMode(int, DisplayModePtr, int);
static int WildcatFBValidMode(int, DisplayModePtr, Bool, int);
static void WildcatFBLoadPalette(ScrnInfoPtr, int, int *, LOCO *, VisualPtr);
static Bool WildcatFBSaveScreen(ScreenPtr, int);
static void WildcatFBSave(ScrnInfoPtr);
static void WildcatFBRestore(ScrnInfoPtr);
static void WildcatFBShadowUpdate(ScreenPtr, shadowBufPtr);

static Bool WildcatFBDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
				pointer ptr);

/* Helper functions */
static int wildcatfb_open(char *);
static pointer wildcatfb_mmap(size_t, off_t, int);

#define WILDCATFB_VERSION 	0000
#define WILDCATFB_NAME		"wildcatfb"
#define WILDCATFB_DRIVER_NAME	"wildcatfb"

_X_EXPORT DriverRec WILDCATFB = {
	WILDCATFB_VERSION,
	WILDCATFB_DRIVER_NAME,
	WildcatFBIdentify,
	WildcatFBProbe,
	WildcatFBAvailableOptions,
	NULL,
	0,
	WildcatFBDriverFunc
};

/* Supported "chipsets" */
static SymTabRec WildcatFBChipsets[] = {
	{ 0, "wildcatfb" },
	{ -1, NULL }
};

/* Supported options */
static const OptionInfoRec WildcatFBOptions[] = {
	{ -1, NULL, OPTV_NONE, {0}, FALSE}
};

/* Symbols needed from other modules. */
static const char *fbSymbols[] = {
	"fbPictureInit",
	"fbScreenInit",
	NULL
};
static const char *shadowSymbols[] = {
	"shadowAdd",
	"shadowSetup",
	NULL
};

#ifdef XFree86LOADER
static XF86ModuleVersionInfo WildcatFBVersRec = {
	"wildcatfb",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, 
	PACKAGE_VERSION_MINOR, 
	PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	NULL,
	{0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData wildcatfbModuleData = { &WildcatFBVersRec, WildcatFBSetup, NULL };

static pointer
WildcatFBSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;
	const char *osname;

	/* Check that we're being loaded on a OpenBSD system. */
	LoaderGetOS(&osname, NULL, NULL, NULL);
	if (!osname || strcmp(osname, "openbsd") != 0) {
		if (errmaj)
			*errmaj = LDR_BADOS;
		if (errmin)
			*errmin = 0;
		return NULL;
	}
	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&WILDCATFB, module, HaveDriverFuncs);
		LoaderRefSymLists(fbSymbols, shadowSymbols, NULL);
		return (pointer)1;
	} else {
		if (errmaj != NULL)
			*errmaj = LDR_ONCEONLY;
		return NULL;
	}
}
#endif /* XFree86LOADER */

/* Private data */
typedef struct {
	int			fd; /* File descriptor of open device. */
	struct wsdisplay_fbinfo info; /* Frame buffer characteristics. */
	int			linebytes; /* Number of bytes per row. */
	unsigned char*		ov0;
	unsigned char*		ov1;
	size_t			ov_len;
	void *			shadow;
	CloseScreenProcPtr	CloseScreen;
	CreateScreenResourcesProcPtr CreateScreenResources;
	EntityInfoPtr		pEnt;
	struct wsdisplay_cmap	saved_cmap;

	OptionInfoPtr		Options;
} WildcatFBRec, *WildcatFBPtr;

#define WILDCATFBPTR(p) ((WildcatFBPtr)((p)->driverPrivate))

static Bool
WildcatFBGetRec(ScrnInfoPtr pScrn)
{

	if (pScrn->driverPrivate != NULL)
		return TRUE;

	pScrn->driverPrivate = xnfcalloc(sizeof(WildcatFBRec), 1);
	return TRUE;
}

static void
WildcatFBFreeRec(ScrnInfoPtr pScrn)
{

	if (pScrn->driverPrivate == NULL)
		return;
	xfree(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

static const OptionInfoRec *
WildcatFBAvailableOptions(int chipid, int busid)
{
	return WildcatFBOptions;
}

static void
WildcatFBIdentify(int flags)
{
	xf86PrintChipsets(WILDCATFB_NAME, "driver for ``Wildcat'' wsdisplay framebuffer",
			  WildcatFBChipsets);
}

/* Open the framebuffer device. */
static int
wildcatfb_open(char *dev)
{
	int fd = -1;

	/* Try argument from xorg.conf first. */
	if (dev == NULL || ((fd = priv_open_device(dev)) == -1)) {
		/* Second: environment variable. */
		dev = getenv("XDEVICE");
		if (dev == NULL || ((fd = priv_open_device(dev)) == -1)) {
			/* Last try: default device. */
			dev = WILDCATFB_DEFAULT_DEV;
			if ((fd = priv_open_device(dev)) == -1) {
				return -1;
			}
		}
	}
	return fd;
}

/* Map the framebuffer's memory. */
static pointer
wildcatfb_mmap(size_t len, off_t off, int fd)
{
	int pagemask, mapsize;
	caddr_t addr;
	pointer mapaddr;

	pagemask = getpagesize() - 1;
	mapsize = ((int) len + pagemask) & ~pagemask;
	addr = 0;

	/*
	 * Try and make it private first, that way once we get it, an
	 * interloper, e.g. another server, can't get this frame buffer,
	 * and if another server already has it, this one won't.
	 */
	mapaddr = (pointer) mmap(addr, mapsize,
				 PROT_READ | PROT_WRITE, MAP_SHARED,
				 fd, off);
	if (mapaddr == (pointer) -1) {
		mapaddr = NULL;
	}
#if DEBUG
	ErrorF("mmap returns: addr %p len 0x%x\n", mapaddr, mapsize);
#endif
	return mapaddr;
}

static Bool
WildcatFBProbe(DriverPtr drv, int flags)
{
	int i, fd, entity;
       	GDevPtr *devSections;
	int numDevSections;
	char *dev;
	Bool foundScreen = FALSE;

	TRACE("probe start");

	/* For now, just bail out for PROBE_DETECT. */
	if (flags & PROBE_DETECT)
		return FALSE;

	if ((numDevSections = xf86MatchDevice(WILDCATFB_DRIVER_NAME,
					      &devSections)) <= 0)
		return FALSE;

	for (i = 0; i < numDevSections; i++) {
		ScrnInfoPtr pScrn = NULL;

		dev = xf86FindOptionValue(devSections[i]->options, "device");
		if ((fd = wildcatfb_open(dev)) >= 0) {
			entity = xf86ClaimFbSlot(drv, 0, devSections[i], TRUE);
			pScrn = xf86ConfigFbEntity(NULL,0,entity,
						   NULL,NULL,NULL,NULL);
			if (pScrn != NULL) {
				foundScreen = TRUE;
				pScrn->driverVersion = WILDCATFB_VERSION;
				pScrn->driverName = WILDCATFB_DRIVER_NAME;
				pScrn->name = WILDCATFB_NAME;
				pScrn->Probe = WildcatFBProbe;
				pScrn->PreInit = WildcatFBPreInit;
				pScrn->ScreenInit = WildcatFBScreenInit;
				pScrn->SwitchMode = WildcatFBSwitchMode;
				pScrn->AdjustFrame = NULL;
				pScrn->EnterVT = WildcatFBEnterVT;
				pScrn->LeaveVT = WildcatFBLeaveVT;
				pScrn->ValidMode = WildcatFBValidMode;

				xf86DrvMsg(pScrn->scrnIndex, X_INFO,
				    "using %s\n", dev != NULL ? dev :
				    "default device");
			}
		}
	}
	xfree(devSections);
	TRACE("probe done");
	return foundScreen;
}

static Bool
WildcatFBPreInit(ScrnInfoPtr pScrn, int flags)
{
	WildcatFBPtr fPtr;
	int defaultDepth, depths, wstype;
	char *dev, *s;
	Gamma zeros = {0.0, 0.0, 0.0};
	DisplayModePtr mode;

	if (flags & PROBE_DETECT) return FALSE;

	TRACE_ENTER("PreInit");

	if (pScrn->numEntities != 1) return FALSE;

	pScrn->monitor = pScrn->confScreen->monitor;

	WildcatFBGetRec(pScrn);
	fPtr = WILDCATFBPTR(pScrn);

	fPtr->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

	pScrn->racMemFlags = RAC_FB | RAC_COLORMAP | RAC_CURSOR | RAC_VIEWPORT;
	pScrn->racIoFlags = pScrn->racMemFlags;

	dev = xf86FindOptionValue(fPtr->pEnt->device->options, "device");
	fPtr->fd = wildcatfb_open(dev);
	if (fPtr->fd == -1) {
		return FALSE;
	}
	if (ioctl(fPtr->fd, WSDISPLAYIO_GTYPE, &wstype) == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "ioctl WSDISPLAY_GTYPE: %s\n",
			   strerror(errno));
		return FALSE;
	}
	if (wstype != WSDISPLAY_TYPE_IFB) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "not an Wildcat device (WSDISPLAY_GTYPE: %d)\n",
			   wstype);
		return FALSE;
	}

	/*
	 * Depth
	 */
	defaultDepth = 7;	/* high bit is too magic for us */

	if (!xf86SetDepthBpp(pScrn, defaultDepth, 0, 0, 0))
		return FALSE;

	if (ioctl(fPtr->fd, WSDISPLAYIO_GINFO, &fPtr->info) == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		    "ioctl WSDISPLAY_GINFO: %s\n",
		    strerror(errno));
		return FALSE;
	}
	if (ioctl(fPtr->fd, WSDISPLAYIO_LINEBYTES, &fPtr->linebytes) == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "ioctl WSDISPLAYIO_LINEBYTES: %s\n",
			   strerror(errno));
		return FALSE;
	}
	/*
	 * Allocate room for saving the colormap.
	 */
	if (fPtr->info.cmsize != 0) {
		fPtr->saved_cmap.red =
		    (unsigned char *)xalloc(fPtr->info.cmsize);
		if (fPtr->saved_cmap.red == NULL) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			    "Cannot malloc %d bytes\n", fPtr->info.cmsize);
			return FALSE;
		}
		fPtr->saved_cmap.green =
		    (unsigned char *)xalloc(fPtr->info.cmsize);
		if (fPtr->saved_cmap.green == NULL) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			    "Cannot malloc %d bytes\n", fPtr->info.cmsize);
			xfree(fPtr->saved_cmap.red);
			return FALSE;
		}
		fPtr->saved_cmap.blue =
		    (unsigned char *)xalloc(fPtr->info.cmsize);
		if (fPtr->saved_cmap.blue == NULL) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			    "Cannot malloc %d bytes\n", fPtr->info.cmsize);
			xfree(fPtr->saved_cmap.red);
			xfree(fPtr->saved_cmap.green);
			return FALSE;
		}
	}


	/* Check consistency. */
	if (pScrn->bitsPerPixel != fPtr->info.depth) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		    "specified depth (%d) or bpp (%d) doesn't match "
		    "framebuffer depth (%d)\n", pScrn->depth, 
		    pScrn->bitsPerPixel, fPtr->info.depth);
		return FALSE;
	}
	xf86PrintDepthBpp(pScrn);

	/* Visual init */
	if (!xf86SetDefaultVisual(pScrn, -1))
		return FALSE;

	xf86SetGamma(pScrn,zeros);

	pScrn->progClock = TRUE;
	pScrn->rgbBits = 6;
	pScrn->chipset   = "wildcatfb";
	pScrn->videoRam  = fPtr->linebytes * fPtr->info.height;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Vidmem: %dk\n",
		   pScrn->videoRam/1024);

	/* Handle options. */
	xf86CollectOptions(pScrn, NULL);
	if (!(fPtr->Options = xalloc(sizeof(WildcatFBOptions))))
		return FALSE;
	memcpy(fPtr->Options, WildcatFBOptions, sizeof(WildcatFBOptions));
	xf86ProcessOptions(pScrn->scrnIndex, fPtr->pEnt->device->options,
			   fPtr->Options);

	/* Fake video mode struct. */
	mode = (DisplayModePtr)xalloc(sizeof(DisplayModeRec));
	mode->prev = mode;
	mode->next = mode;
	mode->name = "wildcatfb current mode";
	mode->status = MODE_OK;
	mode->type = M_T_BUILTIN;
	mode->Clock = 0;
	mode->HDisplay = fPtr->info.width;
	mode->HSyncStart = 0;
	mode->HSyncEnd = 0;
	mode->HTotal = 0;
	mode->HSkew = 0;
	mode->VDisplay = fPtr->info.height;
	mode->VSyncStart = 0;
	mode->VSyncEnd = 0;
	mode->VTotal = 0;
	mode->VScan = 0;
	mode->Flags = 0;

	pScrn->currentMode = pScrn->modes = mode;
	pScrn->virtualX = fPtr->info.width;
	pScrn->virtualY = fPtr->info.height;
	pScrn->displayWidth = pScrn->virtualX;

	/* Set the display resolution. */
	xf86SetDpi(pScrn, 0, 0);

	/* Load shadow. */
	xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
		   "Using \"Shadow Framebuffer\"\n");
	if (xf86LoadSubModule(pScrn, "shadow") == NULL) {
		WildcatFBFreeRec(pScrn);
		return FALSE;
	}
	xf86LoaderReqSymLists(shadowSymbols, NULL);

	if (xf86LoadSubModule(pScrn, "fb") == NULL) {
		WildcatFBFreeRec(pScrn);
		return FALSE;
	}
	xf86LoaderReqSymLists(fbSymbols, NULL);

	TRACE_EXIT("PreInit");
	return TRUE;
}

static Bool
WildcatFBCreateScreenResources(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);
	PixmapPtr pPixmap;
	Bool ret;

	pScreen->CreateScreenResources = fPtr->CreateScreenResources;
	ret = pScreen->CreateScreenResources(pScreen);
	pScreen->CreateScreenResources = WildcatFBCreateScreenResources;

	if (!ret)
		return FALSE;

	pPixmap = pScreen->GetScreenPixmap(pScreen);

	if (!shadowAdd(pScreen, pPixmap, WildcatFBShadowUpdate,
		WildcatFBWindowLinear, 0, NULL)) {
		return FALSE;
	}
	return TRUE;
}

static Bool
WildcatFBShadowInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);

	if (!shadowSetup(pScreen))
		return FALSE;
	fPtr->CreateScreenResources = pScreen->CreateScreenResources;
	pScreen->CreateScreenResources = WildcatFBCreateScreenResources;

	return TRUE;
}

static Bool
WildcatFBScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);
	VisualPtr visual;
	int ret, flags, ncolors;
	int wsmode = WSDISPLAYIO_MODE_MAPPED;
	size_t len;

	TRACE_ENTER("WildcatFBScreenInit");

	len = fPtr->linebytes*fPtr->info.height;

	/* Switch to graphics mode - required before mmap. */
	if (ioctl(fPtr->fd, WSDISPLAYIO_SMODE, &wsmode) == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "ioctl WSDISPLAYIO_SMODE: %s\n",
			   strerror(errno));
		return FALSE;
	}

	/* Try to map the two overlay areas */
	fPtr->ov0 = wildcatfb_mmap(len, 0x00000000, fPtr->fd);
	if (fPtr->ov0 == NULL) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "wildcatfb_mmap: %s\n", strerror(errno));
		return FALSE;
	}
	fPtr->ov1 = wildcatfb_mmap(len, 0x01000000, fPtr->fd);
	if (fPtr->ov1 == NULL) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "wildcatfb_mmap: %s\n", strerror(errno));
		if (munmap(fPtr->ov0, len) == -1) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "munmap: %s\n", strerror(errno));
		}
		return FALSE;
	}
	fPtr->ov_len = len;

	WildcatFBSave(pScrn);
	pScrn->vtSema = TRUE;

	/* MI layer */
	miClearVisualTypes();
	if (!miSetVisualTypes(pScrn->depth,
			      miGetDefaultVisualMask(pScrn->depth),
			      pScrn->rgbBits, pScrn->defaultVisual))
		return FALSE;

	if (!miSetPixmapDepths())
		return FALSE;

	fPtr->shadow = xcalloc(1, pScrn->virtualX * pScrn->virtualY *
	    pScrn->bitsPerPixel);
		
	if (!fPtr->shadow) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		    "Failed to allocate shadow framebuffer\n");
		return FALSE;
	}

	ret = fbScreenInit(pScreen,
	    fPtr->shadow,
	    pScrn->virtualX, pScrn->virtualY,
	    pScrn->xDpi, pScrn->yDpi,
	    pScrn->displayWidth, pScrn->bitsPerPixel);

	if (!ret) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		    "fbScreenInit error");
		return FALSE;
	}

	if (!fbPictureInit(pScreen, NULL, 0))
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "RENDER extension initialisation failed.");

	if (!WildcatFBShadowInit(pScreen)) {
		xf86DrvMsg(scrnIndex, X_ERROR,
		    "shadow framebuffer initialization failed\n");
		return FALSE;
	}

	xf86SetBlackWhitePixels(pScreen);
	miInitializeBackingStore(pScreen);
	xf86SetBackingStore(pScreen);

	/* Software cursor. */
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

	/* Colormap */
	if (!miCreateDefColormap(pScreen))
		return FALSE;
	flags = CMAP_RELOAD_ON_MODE_SWITCH;
	ncolors = fPtr->info.cmsize;
	if(!xf86HandleColormaps(pScreen, ncolors, 8, WildcatFBLoadPalette,
				NULL, flags))
		return FALSE;

	pScreen->SaveScreen = WildcatFBSaveScreen;

#ifdef XvExtension
	{
		XF86VideoAdaptorPtr *ptr;

		int n = xf86XVListGenericAdaptors(pScrn,&ptr);
		if (n) {
			xf86XVScreenInit(pScreen,ptr,n);
		}
	}
#endif

	/* Wrap the current CloseScreen function. */
	fPtr->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = WildcatFBCloseScreen;

	TRACE_EXIT("WildcatFBScreenInit");
	return TRUE;
}

static Bool
WildcatFBCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);
	PixmapPtr pPixmap = pScreen->GetScreenPixmap(pScreen);

	TRACE_ENTER("WildcatFBCloseScreen");

	shadowRemove(pScreen, pPixmap);

	if (pScrn->vtSema) {
		WildcatFBRestore(pScrn);
		if (munmap(fPtr->ov0, fPtr->ov_len) == -1) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "munmap: %s\n", strerror(errno));
		}
		fPtr->ov0 = NULL;
		if (munmap(fPtr->ov1, fPtr->ov_len) == -1) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "munmap: %s\n", strerror(errno));
		}
		fPtr->ov1 = NULL;
	}
	pScrn->vtSema = FALSE;

	/* Unwrap CloseScreen. */
	pScreen->CloseScreen = fPtr->CloseScreen;
	TRACE_EXIT("WildcatFBCloseScreen");
	return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}

static void *
WildcatFBWindowLinear(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode,
		CARD32 *size, void *closure)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

	xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		   "%s called, but never should be!\n", __func__);

	return NULL;
}

static Bool
WildcatFBEnterVT(int scrnIndex, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
	WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);
	int wsmode = WSDISPLAYIO_MODE_DUMBFB;

	TRACE_ENTER("EnterVT");
	
	/* Switch to graphics mode. */
	if (ioctl(fPtr->fd, WSDISPLAYIO_SMODE, &wsmode) == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		    "ioctl WSDISPLAYIO_SMODE: %s\n",
		    strerror(errno));
		return FALSE;
	}

	pScrn->vtSema = TRUE;
	TRACE_EXIT("EnterVT");
	return TRUE;
}

static void
WildcatFBLeaveVT(int scrnIndex, int flags)
{
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];

	TRACE_ENTER("LeaveVT");

	WildcatFBRestore(pScrn);

	TRACE_EXIT("LeaveVT");
}

static Bool
WildcatFBSwitchMode(int scrnIndex, DisplayModePtr mode, int flags)
{
#if DEBUG
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
#endif

	TRACE_ENTER("SwitchMode");
	/* Nothing else to do. */
	return TRUE;
}

static int
WildcatFBValidMode(int scrnIndex, DisplayModePtr mode, Bool verbose, int flags)
{
#if DEBUG
	ScrnInfoPtr pScrn = xf86Screens[scrnIndex];
#endif

	TRACE_ENTER("ValidMode");
	return MODE_OK;
}

static void
WildcatFBLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
	       LOCO *colors, VisualPtr pVisual)
{
	WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);
	struct wsdisplay_cmap cmap;
	unsigned char red[256],green[256],blue[256];
	int i, indexMin=256, indexMax=0;

	TRACE_ENTER("LoadPalette");

	cmap.count   = 1;
	cmap.red   = red;
	cmap.green = green;
	cmap.blue  = blue;

	/*
	 * Change all colors in 2 ioctls
	 * and limit the data to be transfered.
	 */
	for (i = 0; i < numColors; i++) {
		if (indices[i] < indexMin)
			indexMin = indices[i];
		if (indices[i] > indexMax)
			indexMax = indices[i];
	}
	cmap.index = indexMin;
	cmap.count = indexMax - indexMin + 1;
	cmap.red = &red[indexMin];
	cmap.green = &green[indexMin];
	cmap.blue = &blue[indexMin];
	/* Get current map. */
	if (ioctl(fPtr->fd, WSDISPLAYIO_GETCMAP, &cmap) == -1)
		ErrorF("ioctl FBIOGETCMAP: %s\n", strerror(errno));
	/* Change the colors that require updating. */
	for (i = 0; i < numColors; i++) {
		red[indices[i]]   = colors[indices[i]].red;
		green[indices[i]] = colors[indices[i]].green;
		blue[indices[i]]  = colors[indices[i]].blue;
	}
	/* Write the colormap back. */
	if (ioctl(fPtr->fd,WSDISPLAYIO_PUTCMAP, &cmap) == -1)
		ErrorF("ioctl FBIOPUTCMAP: %s\n", strerror(errno));

	TRACE_EXIT("LoadPalette");
}

static Bool
WildcatFBSaveScreen(ScreenPtr pScreen, int mode)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);
	int state;

	TRACE_ENTER("SaveScreen");

	if (!pScrn->vtSema)
		return TRUE;

	if (mode != SCREEN_SAVER_FORCER) {
		state = xf86IsUnblank(mode)?WSDISPLAYIO_VIDEO_ON:
		                            WSDISPLAYIO_VIDEO_OFF;
		ioctl(fPtr->fd,
		      WSDISPLAYIO_SVIDEO, &state);
	}
	TRACE_EXIT("SaveScreen");
	return TRUE;
}

static void
WildcatFBSave(ScrnInfoPtr pScrn)
{
	WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);

	TRACE_ENTER("WildcatFBSave");

	if (fPtr->info.cmsize == 0)
		return;

	fPtr->saved_cmap.index = 0;
	fPtr->saved_cmap.count = fPtr->info.cmsize;
	if (ioctl(fPtr->fd, WSDISPLAYIO_GETCMAP,
		  &(fPtr->saved_cmap)) == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "error saving colormap %s\n", strerror(errno));
	}
	TRACE_EXIT("WildcatFBSave");
}

static void
WildcatFBRestore(ScrnInfoPtr pScrn)
{
	WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);
	int mode;

	TRACE_ENTER("WildcatFBRestore");

	if (fPtr->info.cmsize != 0) {
		/* reset colormap for text mode */
		if (ioctl(fPtr->fd, WSDISPLAYIO_PUTCMAP,
			  &(fPtr->saved_cmap)) == -1) {
			xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "error restoring colormap %s\n",
				   strerror(errno));
		}
	}

	/* Clear the screen. */
	memset(fPtr->ov0, 0, fPtr->ov_len);
	memset(fPtr->ov1, 0, fPtr->ov_len);

	/* Restore the text mode. */
	mode = WSDISPLAYIO_MODE_EMUL;
	if (ioctl(fPtr->fd, WSDISPLAYIO_SMODE, &mode) == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "error setting text mode %s\n", strerror(errno));
	}
	TRACE_EXIT("WildcatFBRestore");
}

static Bool
WildcatFBDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
	xorgHWFlags *flag;
	
	switch (op) {
	case GET_REQUIRED_HW_INTERFACES:
		flag = (CARD32*)ptr;
		*flag = 0;
		return TRUE;
	default:
		return FALSE;
	}
}

static void
WildcatFBShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    WildcatFBPtr fPtr = WILDCATFBPTR(pScrn);
    RegionPtr	damage = shadowDamage (pBuf);
    PixmapPtr	pShadow = pBuf->pPixmap;
    int		nbox = REGION_NUM_RECTS (damage);
    BoxPtr	pbox = REGION_RECTS (damage);
    FbBits	*shaBase, *shaLine, *sha;
    FbStride	shaStride;
    int		scrBase, scrLine, scr;
    int		shaBpp;
    int		shaXoff, shaYoff; /* XXX assumed to be zero */
    int		x, y, w, h, width;
    int         i;
    FbBits	*winBase0 = NULL, *winBase1 = NULL, *win0, *win1;
    int		winoffset;
    CARD32      winSize;

    fbGetDrawable (&pShadow->drawable, shaBase, shaStride, shaBpp, shaXoff, shaYoff);
    while (nbox--)
    {
	x = pbox->x1 * shaBpp;
	y = pbox->y1;
	w = (pbox->x2 - pbox->x1) * shaBpp;
	h = pbox->y2 - pbox->y1;

	scrLine = (x >> FB_SHIFT);
	shaLine = shaBase + y * shaStride + (x >> FB_SHIFT);
				   
	x &= FB_MASK;
	w = (w + x + FB_MASK) >> FB_SHIFT;
	
	while (h--)
	{
	    winSize = 0;
	    scrBase = 0;
	    width = w;
	    scr = scrLine;
	    sha = shaLine;
	    while (width) {
		/* how much remains in this window */
		i = scrBase + winSize - scr;
		if (i <= 0 || scr < scrBase)
		{
		    winSize = fPtr->linebytes;
		    winoffset = y * winSize + scr * sizeof (FbBits);
		    winBase0 = (FbBits *)((CARD8 *)fPtr->ov0 + winoffset);
		    winBase1 = (FbBits *)((CARD8 *)fPtr->ov1 + winoffset);

		    scrBase = scr;
		    winSize /= sizeof (FbBits);
		    i = winSize;
		}
		win0 = winBase0 + (scr - scrBase);
		win1 = winBase1 + (scr - scrBase);
		if (i > width)
		    i = width;
		width -= i;
		scr += i;
		while (i--) {
		    *win0++ = *sha;
		    *win1++ = *sha++;
		}
	    }
	    shaLine += shaStride;
	    y++;
	}
	pbox++;
    }
}
