/* $OpenBSD: kopenbsd.h,v 1.2 2007/12/23 14:28:10 matthieu Exp $ */
/*
 * Copyright (c) 2007 Matthieu Herrb <matthieu.herrb@laas.fr>
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

#ifndef _KOpenBSD_H
#define _KOpenBSD_H

Bool OpenBSDFindPci(CARD16, CARD16, CARD32 , KdCardAttr *);
unsigned char *OpenBSDGetPciCfg(KdCardAttr *);

#endif /* _KOpenBSD_H */