# $OpenBSD: Makefile,v 1.2 2009/05/22 15:11:02 matthieu Exp $

HEADERS_SUBDIR=	X11/extensions/
HEADERS=	render.h renderproto.h
PKGCONFIG=	renderproto.pc

afterinstall: renderproto.txt
	${INSTALL_DATA} ${.CURDIR}/renderproto.txt \
		${DESTDIR}${X11BASE}/share/doc/renderproto

.include <bsd.xorg.mk>
