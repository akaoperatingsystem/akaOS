#include <cstdio>
#include <cerrno>
#include <cstring>

#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QTemporaryFile>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static bool check_qfile_memfd_resize() {
    int fd = memfd_create("qt-wayland-smoke", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        std::fprintf(stderr, "Qt Wayland smoke: memfd_create failed: %s\n",
                     std::strerror(errno));
        return false;
    }
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_SEAL) != 0) {
        std::fprintf(stderr, "Qt Wayland smoke: sealing memfd failed: %s\n",
                     std::strerror(errno));
        close(fd);
        return false;
    }

    QFile file;
    if (!file.open(fd, QIODevice::ReadWrite | QIODevice::Unbuffered,
                   QFile::AutoCloseHandle)) {
        std::fprintf(stderr, "Qt Wayland smoke: QFile::open(memfd) failed: %s\n",
                     qPrintable(file.errorString()));
        close(fd);
        return false;
    }
    constexpr qint64 pool_size = 552960;
    if (!file.resize(pool_size)) {
        std::fprintf(stderr, "Qt Wayland smoke: QFile::resize(memfd) failed: %s\n",
                     qPrintable(file.errorString()));
        return false;
    }

    std::puts("Qt Wayland smoke: sealed QFile memfd resize passed");
    return true;
}

static bool check_qtemporaryfile_runtime_path() {
    QTemporaryFile file(QStringLiteral("/run/user/0/wayland-shm-XXXXXX"));
    if (!file.open()) {
        std::fprintf(stderr, "Qt Wayland smoke: QTemporaryFile open failed: %s\n",
                     qPrintable(file.errorString()));
        return false;
    }
    if (!file.resize(4096)) {
        std::fprintf(stderr, "Qt Wayland smoke: QTemporaryFile resize failed: %s\n",
                     qPrintable(file.errorString()));
        return false;
    }
    std::puts("Qt Wayland smoke: runtime QTemporaryFile resize passed");
    return true;
}

extern "C" int main(int argc, char **argv) {
    qputenv("XDG_RUNTIME_DIR", QByteArrayLiteral("/run/user/0"));
    qputenv("WAYLAND_DISPLAY", QByteArrayLiteral("wayland-0"));
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland"));
    qputenv("QT_PLUGIN_PATH", QByteArrayLiteral("/usr/local/plugins"));
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",
            QByteArrayLiteral("/usr/local/plugins/platforms"));
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", QByteArrayLiteral("xdg-shell"));
    qputenv("QT_DEBUG_PLUGINS", QByteArrayLiteral("1"));

    if (!check_qfile_memfd_resize() || !check_qtemporaryfile_runtime_path())
        return 3;

    std::puts("Qt Wayland smoke: entering QApplication");
    std::fflush(stdout);
    QApplication application(argc, argv);
    std::puts("Qt Wayland smoke: Qt Widgets QPA initialized");
    std::fflush(stdout);
    if (QGuiApplication::platformName() != QStringLiteral("wayland")) {
        std::fputs("Qt selected a non-Wayland platform plugin\n", stderr);
        return 2;
    }

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("akaOS Qt Wayland smoke test"));
    auto *label = new QLabel(
        QStringLiteral("Qt Widgets is running on akaOS Wayland"));
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(
        QStringLiteral("background-color: rgb(12, 180, 75); color: white"));
    window.setCentralWidget(label);
    window.resize(520, 240);
    window.show();

    int result = 1;
    QTimer::singleShot(750, &application, [&] {
        QWindow *native_window = window.windowHandle();
        application.processEvents();
        const QPixmap capture = window.grab();
        const QImage rendered = capture.toImage();
        const QColor center = rendered.isNull() ? QColor() :
            rendered.pixelColor(10, 10);
        if (native_window && native_window->isExposed() &&
            rendered.width() > 10 && rendered.height() > 10 &&
            center == QColor(12, 180, 75)) {
            std::puts("Qt Widgets Wayland QPA painted and exposed a native window");
            result = 0;
        } else {
            std::fprintf(stderr,
                         "Qt Wayland QPA failed paint/exposure check (size=%dx%d, center=%s)\n",
                         rendered.width(), rendered.height(),
                         qPrintable(center.name()));
        }
        application.quit();
    });

    application.exec();
    return result;
}
