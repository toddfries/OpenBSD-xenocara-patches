#	$OpenBSD: Makefile,v 1.9 2013/09/05 15:11:57 jsg Exp $

LIB=	i915_dri.so

DRIVER_SOURCES = \
	i830_context.c \
	i830_state.c \
	i830_texblend.c \
	i830_texstate.c \
	i830_vtbl.c \
	intel_render.c \
	intel_regions.c \
	intel_buffer_objects.c \
	intel_batchbuffer.c \
	intel_clear.c \
	intel_extensions.c \
	intel_mipmap_tree.c \
	intel_tex_layout.c \
	intel_tex_image.c \
	intel_tex_subimage.c \
	intel_tex_copy.c \
	intel_tex_validate.c \
	intel_tex.c \
	intel_pixel.c \
	intel_pixel_bitmap.c \
	intel_pixel_copy.c \
	intel_pixel_draw.c \
	intel_pixel_read.c \
	intel_buffers.c \
	intel_blit.c \
	i915_tex_layout.c \
	i915_texstate.c \
	i915_context.c \
	i915_debug_fp.c \
	i915_fragprog.c \
	i915_program.c \
	i915_state.c \
	i915_vtbl.c \
	intel_context.c \
	intel_screen.c \
	intel_state.c \
	intel_syncobj.c \
	intel_tris.c \
	intel_fbo.c

SRCS= \
	$(COMMON_SOURCES) \
	$(DRIVER_SOURCES)

DRIVER_DEFINES= \
	-I$(MESA)/drivers/dri/i915 \
	-DI915 \
	-DFEATURE_ES1=1 -DFEATURE_ES2=1

.include <bsd.xorg.mk>

LDADD+=	-ldrm_intel

.PATH: ${MESA}/drivers/dri/i915
