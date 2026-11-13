#ifndef CAMERACAPABILITIES_H
#define CAMERACAPABILITIES_H

#include <QObject>
#include <QPointer>
#include <QVariantList>

class QCamera;

class CameraCapabilities : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *camera READ camera WRITE setCamera NOTIFY cameraChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QVariantList imageResolutions READ imageResolutions NOTIFY imageResolutionsChanged)
    Q_PROPERTY(QVariantList viewfinderResolutions READ viewfinderResolutions NOTIFY
                   viewfinderResolutionsChanged)

public:
    explicit CameraCapabilities(QObject *parent = nullptr);

    QObject *camera() const { return m_qmlCamera; }
    void setCamera(QObject *camera);

    bool ready() const { return m_ready; }
    QVariantList imageResolutions() const { return m_imageResolutions; }
    QVariantList viewfinderResolutions() const { return m_viewfinderResolutions; }

signals:
    void cameraChanged();
    void readyChanged();
    void imageResolutionsChanged();
    void viewfinderResolutionsChanged();

private:
    void handleStatus();

    QObject *m_qmlCamera = nullptr;
    QPointer<QCamera> m_camera;
    QMetaObject::Connection m_cameraConnection;
    bool m_ready = false;
    QVariantList m_imageResolutions;
    QVariantList m_viewfinderResolutions;
};

#endif // CAMERACAPABILITIES_H
