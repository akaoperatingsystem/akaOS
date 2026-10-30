#include <cstdio>

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickView>
#include <QtQuick/QSGRendererInterface>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlError>

extern "C" int main(int argc, char **argv) {
    std::fputs("Qt Quick smoke: entered main\n", stderr);
    std::fflush(stderr);
    qputenv("XDG_RUNTIME_DIR", QByteArrayLiteral("/run/user/0"));
    qputenv("WAYLAND_DISPLAY", QByteArrayLiteral("wayland-0"));
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland"));
    qputenv("QT_PLUGIN_PATH", QByteArrayLiteral("/usr/local/plugins"));
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",
            QByteArrayLiteral("/usr/local/plugins/platforms"));
    qputenv("QML_IMPORT_PATH", QByteArrayLiteral("/usr/local/qml"));
    qputenv("QML2_IMPORT_PATH", QByteArrayLiteral("/usr/local/qml"));
    qputenv("QT_DEBUG_PLUGINS", QByteArrayLiteral("1"));

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    std::fputs("Qt Quick smoke: creating QGuiApplication\n", stderr);
    std::fflush(stderr);
    QGuiApplication application(argc, argv);
    std::puts("Qt Quick smoke: QGuiApplication initialized");
    std::fflush(stdout);
    QQuickView window;
    const QUrl source_url(QStringLiteral("file:///qtquick-wayland-smoke.qml"));
    QQmlComponent component(window.engine());
    component.setData(QByteArrayLiteral(
        "import QtQuick\n"
        "Rectangle { width: 520; height: 240; color: \"#0cb44b\" }\n"),
        source_url);
    QObject *root_object = component.create();
    if (!root_object) {
        for (const QQmlError &error : component.errors())
            std::fprintf(stderr, "Qt Quick smoke: %s\n",
                         qPrintable(error.toString()));
        return 2;
    }
    if (!qobject_cast<QQuickItem *>(root_object)) {
        std::fputs("Qt Quick smoke: QML root is not a QQuickItem\n", stderr);
        delete root_object;
        return 3;
    }

    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.setContent(source_url, &component, root_object);
    window.resize(520, 240);
    auto *root_item = qobject_cast<QQuickItem *>(root_object);
    window.setTitle(QStringLiteral("akaOS Qt Quick smoke test"));

    bool frame_swapped = false;
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window,
                     [&frame_swapped] { frame_swapped = true; });
    window.show();

    int result = 1;
    int attempts = 0;
    QTimer frame_probe;
    frame_probe.setInterval(100);
    QObject::connect(&frame_probe, &QTimer::timeout, &window, [&] {
        ++attempts;
        if (window.isExposed() && frame_swapped) {
            const QColor item_color =
                root_item->property("color").value<QColor>();
            if (window.isSceneGraphInitialized() &&
                root_item->width() >= 519.5 && root_item->height() >= 239.5 &&
                item_color == QColor(12, 180, 75)) {
                std::puts("Qt Quick Wayland QPA rendered and exposed a native window");
                result = 0;
                QCoreApplication::quit();
                return;
            }
        }
        if (attempts >= 50) {
            const QColor item_color =
                root_item->property("color").value<QColor>();
            std::fprintf(stderr,
                         "Qt Quick Wayland render/exposure failed "
                         "(window=%dx%d, visible=%d, exposed=%d, "
                         "scenegraph=%d, frame=%d, item=%.0fx%.0f, color=%s)\n",
                         window.width(), window.height(), window.isVisible(),
                         window.isExposed(), window.isSceneGraphInitialized(),
                         frame_swapped, root_item->width(), root_item->height(),
                         qPrintable(item_color.name()));
            QCoreApplication::quit();
        }
    });
    frame_probe.start();

    application.exec();
    return result;
}
