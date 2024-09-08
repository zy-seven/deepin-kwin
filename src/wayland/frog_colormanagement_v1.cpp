/*
    SPDX-FileCopyrightText: 2023 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "frog_colormanagement_v1.h"
#include "display.h"
#include "surface.h"
#include "surface_p.h"

namespace KWin
{

static constexpr uint32_t s_version = 1;

FrogColorManagementV1::FrogColorManagementV1(Display *display, QObject *parent)
    : QObject(parent)
    , QtWaylandServer::frog_color_management_factory_v1(*display, s_version)
{
}

FrogColorManagementV1::~FrogColorManagementV1()
{
}

void FrogColorManagementV1::frog_color_management_factory_v1_get_color_managed_surface(Resource *resource, wl_resource *surface, uint32_t callback)
{
    SurfaceInterface *surf = SurfaceInterface::get(surface);
    SurfaceInterfacePrivate::get(surf)->frogColorManagement = new FrogColorManagementSurfaceV1(surf, resource->client(), callback);
}

void FrogColorManagementV1::frog_color_management_factory_v1_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

FrogColorManagementSurfaceV1::FrogColorManagementSurfaceV1(SurfaceInterface *surface, wl_client *client, uint32_t id)
    : QtWaylandServer::frog_color_managed_surface(client, id, s_version)
    , m_surface(surface)
{
}

FrogColorManagementSurfaceV1::~FrogColorManagementSurfaceV1()
{
    if (m_surface) {
        const auto priv = SurfaceInterfacePrivate::get(m_surface);
        priv->pending->colorDescription = ColorDescription::sRGB;
        priv->pending->colorDescriptionIsSet = true;
        priv->frogColorManagement = nullptr;
    }
}

static QtWaylandServer::frog_color_managed_surface::transfer_function kwinToFrogTransferFunction(TransferFunction tf)
{
    switch (tf.type) {
    case TransferFunction::sRGB:
        return QtWaylandServer::frog_color_managed_surface::transfer_function_srgb;
    case TransferFunction::gamma22:
        return QtWaylandServer::frog_color_managed_surface::transfer_function_gamma_22;
    case TransferFunction::PerceptualQuantizer:
        return QtWaylandServer::frog_color_managed_surface::transfer_function_st2084_pq;
    case TransferFunction::linear:
        return QtWaylandServer::frog_color_managed_surface::transfer_function_scrgb_linear;
    }
    return QtWaylandServer::frog_color_managed_surface::transfer_function_undefined;
}

uint16_t encodePrimary(float primary)
{
    return uint16_t(std::clamp<float>(std::round(primary / 0.00002), 0, 0xC350));
}

void FrogColorManagementSurfaceV1::setPreferredColorDescription(const ColorDescription &colorDescription)
{
    const auto color = colorDescription.masteringColorimetry().value_or(colorDescription.containerColorimetry());
    const QVector2D red = Colorimetry::xyzToXY(color.red());
    const QVector2D green = Colorimetry::xyzToXY(color.green());
    const QVector2D blue = Colorimetry::xyzToXY(color.blue());
    const QVector2D white = Colorimetry::xyzToXY(color.white());
    send_preferred_metadata(kwinToFrogTransferFunction(colorDescription.transferFunction()),
                            encodePrimary(red.x()), encodePrimary(red.y()),
                            encodePrimary(green.x()), encodePrimary(green.y()),
                            encodePrimary(blue.x()), encodePrimary(blue.y()),
                            encodePrimary(white.x()), encodePrimary(white.y()),
                            std::round(colorDescription.maxHdrLuminance().value_or(0)),
                            std::round(colorDescription.minLuminance() / 0.0001),
                            std::round(colorDescription.maxAverageLuminance().value_or(0)));
}

void FrogColorManagementSurfaceV1::frog_color_managed_surface_set_known_transfer_function(Resource *resource, uint32_t transfer_function)
{
    switch (transfer_function) {
    case transfer_function_undefined:
    case transfer_function_srgb:
    case transfer_function_gamma_22:
        m_transferFunction = TransferFunction(TransferFunction::gamma22);
        break;
    case transfer_function_st2084_pq:
        m_transferFunction = TransferFunction(TransferFunction::PerceptualQuantizer);
        break;
    case transfer_function_scrgb_linear:
        m_transferFunction = TransferFunction(TransferFunction::linear, 0.0, 80.0);
        break;
    }
    updateColorDescription();
}

void FrogColorManagementSurfaceV1::frog_color_managed_surface_set_known_container_color_volume(Resource *resource, uint32_t primaries)
{
    switch (primaries) {
    case primaries_undefined:
    case primaries_rec709:
        m_containerColorimetry = NamedColorimetry::BT709;
        break;
    case primaries_rec2020:
        m_containerColorimetry = NamedColorimetry::BT2020;
        break;
    }
    updateColorDescription();
}

void FrogColorManagementSurfaceV1::frog_color_managed_surface_set_render_intent(Resource *resource, uint32_t render_intent)
{
    // there's only perceptual right now, so this can be ignored
}

void FrogColorManagementSurfaceV1::frog_color_managed_surface_set_hdr_metadata(Resource *resource,
                                                                               uint32_t mastering_display_primary_red_x, uint32_t mastering_display_primary_red_y,
                                                                               uint32_t mastering_display_primary_green_x, uint32_t mastering_display_primary_green_y,
                                                                               uint32_t mastering_display_primary_blue_x, uint32_t mastering_display_primary_blue_y,
                                                                               uint32_t mastering_white_point_x, uint32_t mastering_white_point_y,
                                                                               uint32_t max_display_mastering_luminance, uint32_t min_display_mastering_luminance,
                                                                               uint32_t max_cll, uint32_t max_fall)
{
    // max_display_mastering_luminance and max_cll more or less have the same meaning in practice
    // it seems that max_cll, if set, is more accurate, so prefer it
    if (max_cll != 0) {
        m_maxPeakBrightness = max_cll;
    } else if (max_display_mastering_luminance != 0) {
        m_maxPeakBrightness = max_display_mastering_luminance;
    }
    if (max_fall != 0 && (!m_maxPeakBrightness || max_fall <= *m_maxPeakBrightness)) {
        m_maxAverageLuminance = max_fall;
    }
    const double minLuminance = min_display_mastering_luminance / 10'000.0;
    if ((!m_maxPeakBrightness && !m_maxAverageLuminance)
        || (m_maxPeakBrightness && minLuminance < *m_maxPeakBrightness)
        || (m_maxAverageLuminance && minLuminance < *m_maxAverageLuminance)) {
        m_minMasteringLuminance = minLuminance;
    }
    if (mastering_display_primary_red_x > 0 && mastering_display_primary_red_y > 0 && mastering_display_primary_green_x > 0 && mastering_display_primary_green_y > 0 && mastering_display_primary_blue_x > 0 && mastering_display_primary_blue_y > 0 && mastering_white_point_x > 0 && mastering_white_point_y > 0) {
        m_masteringColorimetry = Colorimetry{
            Colorimetry::xyToXYZ(QVector2D(mastering_display_primary_red_x / 10'000.0, mastering_display_primary_red_y / 10'000.0)),
            Colorimetry::xyToXYZ(QVector2D(mastering_display_primary_green_x / 10'000.0, mastering_display_primary_green_y / 10'000.0)),
            Colorimetry::xyToXYZ(QVector2D(mastering_display_primary_blue_x / 10'000.0, mastering_display_primary_blue_y / 10'000.0)),
            Colorimetry::xyToXYZ(QVector2D(mastering_white_point_x / 10'000.0, mastering_white_point_y / 10'000.0)),
        };
    }
    updateColorDescription();
}

void FrogColorManagementSurfaceV1::frog_color_managed_surface_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void FrogColorManagementSurfaceV1::frog_color_managed_surface_destroy_resource(Resource *resource)
{
    delete this;
}

void FrogColorManagementSurfaceV1::updateColorDescription()
{
    if (m_surface) {
        SurfaceInterfacePrivate *priv = SurfaceInterfacePrivate::get(m_surface);
        double referenceLuminance = m_transferFunction.maxLuminance;
        if (!m_transferFunction.isRelative()) {
            referenceLuminance = priv->preferredColorDescription.value_or(ColorDescription::sRGB).referenceLuminance();
        }
        priv->pending->colorDescription = ColorDescription(m_containerColorimetry, m_transferFunction, referenceLuminance, m_minMasteringLuminance.value_or(m_transferFunction.minLuminance), m_maxAverageLuminance, m_maxPeakBrightness, m_masteringColorimetry, Colorimetry::fromName(NamedColorimetry::BT709));
        priv->pending->colorDescriptionIsSet = true;
    }
}

}

#include "moc_frog_colormanagement_v1.cpp"
