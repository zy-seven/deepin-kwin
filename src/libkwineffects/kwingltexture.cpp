/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2006-2007 Rivo Laks <rivolaks@hot.ee>
    SPDX-FileCopyrightText: 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2012 Philipp Knechtges <philipp-dev@knechtges.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwingltexture_p.h"
#include "libkwineffects/kwinconfig.h" // KWIN_HAVE_OPENGL
#include "libkwineffects/kwineffects.h"
#include "libkwineffects/kwinglplatform.h"
#include "libkwineffects/kwinglutils.h"
#include "libkwineffects/kwinglutils_funcs.h"
#include "logging_p.h"

#include <QImage>
#include <QPixmap>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

namespace KWin
{

//****************************************
// GLTexture
//****************************************

bool GLTexturePrivate::s_supportsFramebufferObjects = false;
bool GLTexturePrivate::s_supportsARGB32 = false;
bool GLTexturePrivate::s_supportsUnpack = false;
bool GLTexturePrivate::s_supportsTextureStorage = false;
bool GLTexturePrivate::s_supportsTextureSwizzle = false;
bool GLTexturePrivate::s_supportsTextureFormatRG = false;
bool GLTexturePrivate::s_supportsTexture16Bit = false;
uint GLTexturePrivate::s_fbo = 0;

// Table of GL formats/types associated with different values of QImage::Format.
// Zero values indicate a direct upload is not feasible.
//
// Note: Blending is set up to expect premultiplied data, so the non-premultiplied
// Format_ARGB32 must be converted to Format_ARGB32_Premultiplied ahead of time.
struct
{
    GLenum internalFormat;
    GLenum format;
    GLenum type;
} static const formatTable[] = {
    {0, 0, 0}, // QImage::Format_Invalid
    {0, 0, 0}, // QImage::Format_Mono
    {0, 0, 0}, // QImage::Format_MonoLSB
    {0, 0, 0}, // QImage::Format_Indexed8
    {GL_RGB8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV}, // QImage::Format_RGB32
    {0, 0, 0}, // QImage::Format_ARGB32
    {GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV}, // QImage::Format_ARGB32_Premultiplied
    {GL_RGB8, GL_BGR, GL_UNSIGNED_SHORT_5_6_5_REV}, // QImage::Format_RGB16
    {0, 0, 0}, // QImage::Format_ARGB8565_Premultiplied
    {0, 0, 0}, // QImage::Format_RGB666
    {0, 0, 0}, // QImage::Format_ARGB6666_Premultiplied
    {GL_RGB5, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV}, // QImage::Format_RGB555
    {0, 0, 0}, // QImage::Format_ARGB8555_Premultiplied
    {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE}, // QImage::Format_RGB888
    {GL_RGB4, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV}, // QImage::Format_RGB444
    {GL_RGBA4, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV}, // QImage::Format_ARGB4444_Premultiplied
    {GL_RGB8, GL_RGBA, GL_UNSIGNED_BYTE}, // QImage::Format_RGBX8888
    {0, 0, 0}, // QImage::Format_RGBA8888
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE}, // QImage::Format_RGBA8888_Premultiplied
    {GL_RGB10, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV}, // QImage::Format_BGR30
    {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV}, // QImage::Format_A2BGR30_Premultiplied
    {GL_RGB10, GL_BGRA, GL_UNSIGNED_INT_2_10_10_10_REV}, // QImage::Format_RGB30
    {GL_RGB10_A2, GL_BGRA, GL_UNSIGNED_INT_2_10_10_10_REV}, // QImage::Format_A2RGB30_Premultiplied
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE}, // QImage::Format_Alpha8
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE}, // QImage::Format_Grayscale8
    {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT}, // QImage::Format_RGBX64
    {0, 0, 0}, // QImage::Format_RGBA64
    {GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT}, // QImage::Format_RGBA64_Premultiplied
    {GL_R16, GL_RED, GL_UNSIGNED_SHORT}, // QImage::Format_Grayscale16
    {0, 0, 0}, // QImage::Format_BGR888
};

GLTexture::GLTexture(GLenum target)
    : d(std::make_unique<GLTexturePrivate>())
{
    d->m_target = target;
}

GLTexture::GLTexture(GLenum target, GLuint textureId, GLenum internalFormat, const QSize &size, int levels, bool owning, TextureTransforms transform)
    : GLTexture(target)
{
    d->m_owning = owning;
    d->m_texture = textureId;
    d->m_scale.setWidth(1.0 / size.width());
    d->m_scale.setHeight(1.0 / size.height());
    d->m_size = size;
    d->m_canUseMipmaps = levels > 1;
    d->m_mipLevels = levels;
    d->m_filter = levels > 1 ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST;
    d->m_internalFormat = internalFormat;
    d->m_textureToBufferTransform = transform;

    d->updateMatrix();
}

GLTexture::~GLTexture()
{
}

bool GLTexture::create()
{
    if (!isNull()) {
        return true;
    }
    glGenTextures(1, &d->m_texture);
    return d->m_texture != GL_NONE;
}

GLTexturePrivate::GLTexturePrivate()
    : m_texture(0)
    , m_target(0)
    , m_internalFormat(0)
    , m_filter(GL_NEAREST)
    , m_wrapMode(GL_REPEAT)
    , m_canUseMipmaps(false)
    , m_markedDirty(false)
    , m_filterChanged(true)
    , m_wrapModeChanged(false)
    , m_owning(true)
    , m_mipLevels(1)
    , m_unnormalizeActive(0)
    , m_normalizeActive(0)
{
}

GLTexturePrivate::~GLTexturePrivate()
{
    if (m_texture != 0 && m_owning) {
        glDeleteTextures(1, &m_texture);
    }
}

void GLTexturePrivate::initStatic()
{
    if (!GLPlatform::instance()->isGLES()) {
        s_supportsFramebufferObjects = hasGLVersion(3, 0) || hasGLExtension("GL_ARB_framebuffer_object") || hasGLExtension(QByteArrayLiteral("GL_EXT_framebuffer_object"));
        s_supportsTextureStorage = hasGLVersion(4, 2) || hasGLExtension(QByteArrayLiteral("GL_ARB_texture_storage"));
        s_supportsTextureSwizzle = hasGLVersion(3, 3) || hasGLExtension(QByteArrayLiteral("GL_ARB_texture_swizzle"));
        // see https://www.opengl.org/registry/specs/ARB/texture_rg.txt
        s_supportsTextureFormatRG = hasGLVersion(3, 0) || hasGLExtension(QByteArrayLiteral("GL_ARB_texture_rg"));
        s_supportsTexture16Bit = true;
        s_supportsARGB32 = true;
        s_supportsUnpack = true;
    } else {
        s_supportsFramebufferObjects = true;
        s_supportsTextureStorage = hasGLVersion(3, 0) || hasGLExtension(QByteArrayLiteral("GL_EXT_texture_storage"));
        s_supportsTextureSwizzle = hasGLVersion(3, 0);
        // see https://www.khronos.org/registry/gles/extensions/EXT/EXT_texture_rg.txt
        s_supportsTextureFormatRG = hasGLVersion(3, 0) || hasGLExtension(QByteArrayLiteral("GL_EXT_texture_rg"));
        s_supportsTexture16Bit = hasGLExtension(QByteArrayLiteral("GL_EXT_texture_norm16"));

        // QImage::Format_ARGB32_Premultiplied is a packed-pixel format, so it's only
        // equivalent to GL_BGRA/GL_UNSIGNED_BYTE on little-endian systems.
        s_supportsARGB32 = QSysInfo::ByteOrder == QSysInfo::LittleEndian && hasGLExtension(QByteArrayLiteral("GL_EXT_texture_format_BGRA8888"));

        s_supportsUnpack = hasGLExtension(QByteArrayLiteral("GL_EXT_unpack_subimage"));
    }
}

void GLTexturePrivate::cleanup()
{
    s_supportsFramebufferObjects = false;
    s_supportsARGB32 = false;
    if (s_fbo) {
        glDeleteFramebuffers(1, &s_fbo);
        s_fbo = 0;
    }
}

bool GLTexture::isNull() const
{
    return GL_NONE == d->m_texture;
}

QSize GLTexture::size() const
{
    return d->m_size;
}

void GLTexture::setSize(const QSize &size)
{
    if (!isNull()) {
        return;
    }
    d->m_size = size;
    d->updateMatrix();
}

void GLTexture::update(const QImage &image, const QPoint &offset, const QRect &src)
{
    if (image.isNull() || isNull()) {
        return;
    }

    Q_ASSERT(d->m_owning);

    GLenum glFormat;
    GLenum type;
    QImage::Format uploadFormat;
    if (!GLPlatform::instance()->isGLES()) {
        const QImage::Format index = image.format();

        if (index < sizeof(formatTable) / sizeof(formatTable[0]) && formatTable[index].internalFormat
            && !(formatTable[index].type == GL_UNSIGNED_SHORT && !d->s_supportsTexture16Bit)) {
            glFormat = formatTable[index].format;
            type = formatTable[index].type;
            uploadFormat = index;
        } else {
            glFormat = GL_BGRA;
            type = GL_UNSIGNED_INT_8_8_8_8_REV;
            uploadFormat = QImage::Format_ARGB32_Premultiplied;
        }
    } else {
        if (d->s_supportsARGB32) {
            glFormat = GL_BGRA_EXT;
            type = GL_UNSIGNED_BYTE;
            uploadFormat = QImage::Format_ARGB32_Premultiplied;
        } else {
            glFormat = GL_RGBA;
            type = GL_UNSIGNED_BYTE;
            uploadFormat = QImage::Format_RGBA8888_Premultiplied;
        }
    }
    bool useUnpack = d->s_supportsUnpack && image.format() == uploadFormat && !src.isNull();

    QImage im;
    if (useUnpack) {
        im = image;
        Q_ASSERT(im.depth() % 8 == 0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, im.bytesPerLine() / (im.depth() / 8));
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, src.x());
        glPixelStorei(GL_UNPACK_SKIP_ROWS, src.y());
    } else {
        if (src.isNull()) {
            im = image;
        } else {
            im = image.copy(src);
        }
        if (im.format() != uploadFormat) {
            im.convertTo(uploadFormat);
        }
    }

    int width = image.width();
    int height = image.height();
    if (!src.isNull()) {
        width = src.width();
        height = src.height();
    }

    bind();

    glTexSubImage2D(d->m_target, 0, offset.x(), offset.y(), width, height, glFormat, type, im.constBits());

    unbind();

    if (useUnpack) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    }
}

void GLTexture::bind()
{
    Q_ASSERT(d->m_texture);

    glBindTexture(d->m_target, d->m_texture);

    if (d->m_markedDirty) {
        onDamage();
    }
    if (d->m_filterChanged) {
        GLenum minFilter = GL_NEAREST;
        GLenum magFilter = GL_NEAREST;

        switch (d->m_filter) {
        case GL_NEAREST:
            minFilter = magFilter = GL_NEAREST;
            break;

        case GL_LINEAR:
            minFilter = magFilter = GL_LINEAR;
            break;

        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
            magFilter = GL_NEAREST;
            minFilter = d->m_canUseMipmaps ? d->m_filter : GL_NEAREST;
            break;

        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
            magFilter = GL_LINEAR;
            minFilter = d->m_canUseMipmaps ? d->m_filter : GL_LINEAR;
            break;
        }

        glTexParameteri(d->m_target, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(d->m_target, GL_TEXTURE_MAG_FILTER, magFilter);

        d->m_filterChanged = false;
    }
    if (d->m_wrapModeChanged) {
        glTexParameteri(d->m_target, GL_TEXTURE_WRAP_S, d->m_wrapMode);
        glTexParameteri(d->m_target, GL_TEXTURE_WRAP_T, d->m_wrapMode);
        d->m_wrapModeChanged = false;
    }
}

void GLTexture::generateMipmaps()
{
    if (d->m_canUseMipmaps && d->s_supportsFramebufferObjects) {
        glGenerateMipmap(d->m_target);
    }
}

void GLTexture::unbind()
{
    glBindTexture(d->m_target, 0);
}

void GLTexture::render(const QSizeF &size, qreal scale)
{
    render(infiniteRegion(), size, scale, false);
}

void GLTexture::render(const QRegion &region, const QSizeF &targetSize, double scale, bool hardwareClipping)
{
    const auto rotatedSize = d->m_textureToBufferMatrix.mapRect(QRect(QPoint(), size())).size();
    render(QRectF(QPoint(), rotatedSize), region, targetSize, scale, hardwareClipping);
}

void GLTexture::render(const QRectF &source, const QRegion &region, const QSizeF &targetSize, double scale, bool hardwareClipping)
{
    if (targetSize.isEmpty()) {
        return; // nothing to paint and m_vbo is likely nullptr and d->m_cachedSize empty as well, #337090
    }

    QSizeF destinationSize = (targetSize * scale).toSize();
    if (destinationSize != d->m_cachedSize || d->m_cachedSource != source) {
        d->m_cachedSize = destinationSize;
        d->m_cachedSource = source;
        if (!d->m_vbo) {
            d->m_vbo = std::make_unique<GLVertexBuffer>(KWin::GLVertexBuffer::Static);
        }

        const float verts[4 * 2] = {
            0.0f, 0.0f,
            0.0f, float(destinationSize.height()),
            float(destinationSize.width()), 0.0f,
            float(destinationSize.width()), float(destinationSize.height())};

        const float texWidth = (target() == GL_TEXTURE_RECTANGLE_ARB) ? width() : 1.0f;
        const float texHeight = (target() == GL_TEXTURE_RECTANGLE_ARB) ? height() : 1.0f;

        const QSize rotatedSize = d->m_textureToBufferMatrix.mapRect(QRect(QPoint(), size())).size();

        QMatrix4x4 textureMat;
        textureMat.translate(texWidth / 2, texHeight / 2);
        textureMat *= d->m_textureToBufferMatrix;
        // our Y axis is flipped vs OpenGL
        textureMat.scale(1, -1);
        textureMat.translate(-texWidth / 2, -texHeight / 2);
        textureMat.scale(texWidth / rotatedSize.width(), texHeight / rotatedSize.height());

        const QPointF p1 = textureMat.map(QPointF(source.x(), source.y()));
        const QPointF p2 = textureMat.map(QPointF(source.x(), source.y() + source.height()));
        const QPointF p3 = textureMat.map(QPointF(source.x() + source.width(), source.y()));
        const QPointF p4 = textureMat.map(QPointF(source.x() + source.width(), source.y() + source.height()));

        const float texcoords[4 * 2] = {
            float(p1.x()), float(p1.y()),
            float(p2.x()), float(p2.y()),
            float(p3.x()), float(p3.y()),
            float(p4.x()), float(p4.y())};

        d->m_vbo->setData(4, 2, verts, texcoords);
    }
    bind();
    d->m_vbo->render(region, GL_TRIANGLE_STRIP, hardwareClipping);
    unbind();
}

GLuint GLTexture::texture() const
{
    return d->m_texture;
}

GLenum GLTexture::target() const
{
    return d->m_target;
}

GLenum GLTexture::filter() const
{
    return d->m_filter;
}

GLenum GLTexture::internalFormat() const
{
    return d->m_internalFormat;
}

void GLTexture::clear()
{
    Q_ASSERT(d->m_owning);
    if (!GLTexturePrivate::s_fbo && GLFramebuffer::supported() && GLPlatform::instance()->driver() != Driver_Catalyst) { // fail. -> bug #323065
        glGenFramebuffers(1, &GLTexturePrivate::s_fbo);
    }

    if (GLTexturePrivate::s_fbo) {
        // Clear the texture
        GLuint previousFramebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, reinterpret_cast<GLint *>(&previousFramebuffer));
        if (GLTexturePrivate::s_fbo != previousFramebuffer) {
            glBindFramebuffer(GL_FRAMEBUFFER, GLTexturePrivate::s_fbo);
        }
        glClearColor(0, 0, 0, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, d->m_texture, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (GLTexturePrivate::s_fbo != previousFramebuffer) {
            glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
        }
    } else {
        if (const int size = width() * height()) {
            std::vector<uint32_t> buffer(size, 0);
            bind();
            if (!GLPlatform::instance()->isGLES()) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width(), height(),
                                GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, buffer.data());
            } else {
                const GLenum format = d->s_supportsARGB32 ? GL_BGRA_EXT : GL_RGBA;
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width(), height(),
                                format, GL_UNSIGNED_BYTE, buffer.data());
            }
            unbind();
        }
    }
}

bool GLTexture::isDirty() const
{
    return d->m_markedDirty;
}

void GLTexture::setFilter(GLenum filter)
{
    if (filter != d->m_filter) {
        d->m_filter = filter;
        d->m_filterChanged = true;
    }
}

void GLTexture::setWrapMode(GLenum mode)
{
    if (mode != d->m_wrapMode) {
        d->m_wrapMode = mode;
        d->m_wrapModeChanged = true;
    }
}

void GLTexture::setDirty()
{
    d->m_markedDirty = true;
}

void GLTexture::onDamage()
{
}

void GLTexturePrivate::updateMatrix()
{
    m_textureToBufferMatrix.setToIdentity();
    if (m_textureToBufferTransform & TextureTransform::MirrorX) {
        m_textureToBufferMatrix.scale(-1, 1);
    }
    if (m_textureToBufferTransform & TextureTransform::MirrorY) {
        m_textureToBufferMatrix.scale(1, -1);
    }
    if (m_textureToBufferTransform & TextureTransform::Rotate90) {
        m_textureToBufferMatrix.rotate(90, 0, 0, 1);
    } else if (m_textureToBufferTransform & TextureTransform::Rotate180) {
        m_textureToBufferMatrix.rotate(180, 0, 0, 1);
    } else if (m_textureToBufferTransform & TextureTransform::Rotate270) {
        m_textureToBufferMatrix.rotate(270, 0, 0, 1);
    }

    m_matrix[NormalizedCoordinates].setToIdentity();
    m_matrix[UnnormalizedCoordinates].setToIdentity();

    if (m_target == GL_TEXTURE_RECTANGLE_ARB) {
        m_matrix[NormalizedCoordinates].scale(m_size.width(), m_size.height());
    } else {
        m_matrix[UnnormalizedCoordinates].scale(1.0 / m_size.width(), 1.0 / m_size.height());
    }

    m_matrix[NormalizedCoordinates].translate(0.5, 0.5);
    m_matrix[NormalizedCoordinates] *= m_textureToBufferMatrix;
    // our Y axis is flipped vs OpenGL
    m_matrix[NormalizedCoordinates].scale(1, -1);
    m_matrix[NormalizedCoordinates].translate(-0.5, -0.5);

    m_matrix[UnnormalizedCoordinates].translate(m_size.width() / 2, m_size.height() / 2);
    m_matrix[UnnormalizedCoordinates] *= m_textureToBufferMatrix;
    m_matrix[UnnormalizedCoordinates].scale(1, -1);
    m_matrix[UnnormalizedCoordinates].translate(-m_size.width() / 2, -m_size.height() / 2);
}

void GLTexture::setContentTransform(TextureTransforms transform)
{
    if (d->m_textureToBufferTransform != transform) {
        d->m_textureToBufferTransform = transform;
        d->updateMatrix();
    }
}

TextureTransforms GLTexture::contentTransforms() const
{
    return d->m_textureToBufferTransform;
}

QMatrix4x4 GLTexture::contentTransformMatrix() const
{
    return d->m_textureToBufferMatrix;
}

void GLTexture::setSwizzle(GLenum red, GLenum green, GLenum blue, GLenum alpha)
{
    if (!GLPlatform::instance()->isGLES()) {
        const GLuint swizzle[] = {red, green, blue, alpha};
        glTexParameteriv(d->m_target, GL_TEXTURE_SWIZZLE_RGBA, (const GLint *)swizzle);
    } else {
        glTexParameteri(d->m_target, GL_TEXTURE_SWIZZLE_R, red);
        glTexParameteri(d->m_target, GL_TEXTURE_SWIZZLE_G, green);
        glTexParameteri(d->m_target, GL_TEXTURE_SWIZZLE_B, blue);
        glTexParameteri(d->m_target, GL_TEXTURE_SWIZZLE_A, alpha);
    }
}

int GLTexture::width() const
{
    return d->m_size.width();
}

int GLTexture::height() const
{
    return d->m_size.height();
}

QMatrix4x4 GLTexture::matrix(TextureCoordinateType type) const
{
    return d->m_matrix[type];
}

bool GLTexture::framebufferObjectSupported()
{
    return GLTexturePrivate::s_supportsFramebufferObjects;
}

bool GLTexture::supportsSwizzle()
{
    return GLTexturePrivate::s_supportsTextureSwizzle;
}

bool GLTexture::supportsFormatRG()
{
    return GLTexturePrivate::s_supportsTextureFormatRG;
}

QImage GLTexture::toImage() const
{
    if (target() != GL_TEXTURE_2D) {
        return QImage();
    }
    QImage ret(size(), QImage::Format_RGBA8888_Premultiplied);

    GLint currentTextureBinding;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &currentTextureBinding);

    if (GLuint(currentTextureBinding) != texture()) {
        glBindTexture(GL_TEXTURE_2D, texture());
    }
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, ret.bits());
    if (GLuint(currentTextureBinding) != texture()) {
        glBindTexture(GL_TEXTURE_2D, currentTextureBinding);
    }
    return ret;
}

std::unique_ptr<GLTexture> GLTexture::createNonOwningWrapper(GLuint textureId, GLenum internalFormat, const QSize &size)
{
    return std::unique_ptr<GLTexture>(new GLTexture(GL_TEXTURE_2D, textureId, internalFormat, size, 1, false, TextureTransforms{}));
}

std::unique_ptr<GLTexture> GLTexture::allocate(GLenum internalFormat, const QSize &size, int levels)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        qCWarning(LIBKWINGLUTILS, "generating OpenGL texture handle failed");
        return nullptr;
    }
    glBindTexture(GL_TEXTURE_2D, texture);

    if (!GLPlatform::instance()->isGLES()) {
        if (GLTexturePrivate::s_supportsTextureStorage) {
            glTexStorage2D(GL_TEXTURE_2D, levels, internalFormat, size.width(), size.height());
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size.width(), size.height(), 0,
                         GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, nullptr);
        }
    } else {
        // The format parameter in glTexSubImage() must match the internal format
        // of the texture, so it's important that we allocate the texture with
        // the format that will be used in update() and clear().
        const GLenum format = GLTexturePrivate::s_supportsARGB32 ? GL_BGRA_EXT : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, format, size.width(), size.height(), 0,
                     format, GL_UNSIGNED_BYTE, nullptr);

        // The internalFormat is technically not correct, but it means that code that calls
        // internalFormat() won't need to be specialized for GLES2.
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return std::unique_ptr<GLTexture>(new GLTexture(GL_TEXTURE_2D, texture, internalFormat, size, levels, true, TextureTransforms{}));
}

std::unique_ptr<GLTexture> GLTexture::upload(const QImage &image)
{
    if (image.isNull()) {
        return nullptr;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        qCWarning(LIBKWINGLUTILS, "generating OpenGL texture handle failed");
        return nullptr;
    }
    glBindTexture(GL_TEXTURE_2D, texture);

    GLenum internalFormat;
    if (!GLPlatform::instance()->isGLES()) {
        QImage im;
        GLenum format;
        GLenum type;

        const QImage::Format index = image.format();

        if (index < sizeof(formatTable) / sizeof(formatTable[0]) && formatTable[index].internalFormat
            && !(formatTable[index].type == GL_UNSIGNED_SHORT && !GLTexturePrivate::s_supportsTexture16Bit)) {
            internalFormat = formatTable[index].internalFormat;
            format = formatTable[index].format;
            type = formatTable[index].type;
            im = image;
        } else {
            im = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            internalFormat = GL_RGBA8;
            format = GL_BGRA;
            type = GL_UNSIGNED_INT_8_8_8_8_REV;
        }

        if (GLTexturePrivate::s_supportsTextureStorage) {
            glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, im.width(), im.height());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, im.width(), im.height(),
                            format, type, im.constBits());
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, im.width(), im.height(), 0,
                         format, type, im.constBits());
        }
    } else {
        internalFormat = GL_RGBA8;

        if (GLTexturePrivate::s_supportsARGB32) {
            const QImage im = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, im.width(), im.height(),
                         0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, im.constBits());
        } else {
            const QImage im = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.width(), im.height(),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, im.constBits());
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return std::unique_ptr<GLTexture>(new GLTexture(GL_TEXTURE_2D, texture, internalFormat, image.size(), 1, true, TextureTransform::MirrorY));
}

std::unique_ptr<GLTexture> GLTexture::upload(const QPixmap &pixmap)
{
    return upload(pixmap.toImage());
}

} // namespace KWin
