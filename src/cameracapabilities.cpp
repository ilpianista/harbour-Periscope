#include "cameracapabilities.h"

#include <QCamera>
#include <QCameraImageCapture>

CameraCapabilities::CameraCapabilities(QObject *parent)
    : QObject(parent)
{}

void CameraCapabilities::setCamera(QObject *camera)
{
    if (m_qmlCamera == camera) {
        return;
    }

    if (m_cameraConnection) {
        disconnect(m_cameraConnection);
        m_cameraConnection = QMetaObject::Connection();
    }

    m_qmlCamera = camera;
    m_camera = camera ? camera->property("mediaObject").value<QCamera *>() : nullptr;

    if (m_camera) {
        m_cameraConnection = connect(m_camera,
                                     &QCamera::statusChanged,
                                     this,
                                     &CameraCapabilities::handleStatus);
    }

    m_ready = false;
    m_imageResolutions.clear();
    m_viewfinderResolutions.clear();

    emit cameraChanged();
    emit imageResolutionsChanged();
    emit viewfinderResolutionsChanged();
    emit readyChanged();

    handleStatus();
}

void CameraCapabilities::handleStatus()
{
    if (!m_camera || !m_qmlCamera || m_ready) {
        return;
    }

    const QCamera::Status status = m_camera->status();
    if (status != QCamera::LoadedStatus && status != QCamera::StartingStatus
        && status != QCamera::ActiveStatus) {
        return;
    }

    QObject *qmlCapture = m_qmlCamera->property("imageCapture").value<QObject *>();
    if (!qmlCapture) {
        return;
    }

    QCameraImageCapture *capture = qmlCapture->findChild<QCameraImageCapture *>();
    if (!capture && m_camera) {
        capture = m_camera->findChild<QCameraImageCapture *>();
    }
    if (!capture) {
        return;
    }

    m_imageResolutions.clear();
    const QList<QSize> imageResolutions = capture->supportedResolutions();
    for (const QSize &resolution : imageResolutions) {
        m_imageResolutions.append(resolution);
    }

    m_viewfinderResolutions.clear();
    const QList<QSize> viewfinderResolutions = m_camera->supportedViewfinderResolutions();
    for (const QSize &resolution : viewfinderResolutions) {
        m_viewfinderResolutions.append(resolution);
    }

    m_ready = true;
    emit imageResolutionsChanged();
    emit viewfinderResolutionsChanged();
    emit readyChanged();
}
