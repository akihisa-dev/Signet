// SPDX-License-Identifier: AGPL-3.0-or-later
#include "signet_version.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("Signet"));
  QCoreApplication::setApplicationVersion(QStringLiteral(SIGNET_VERSION_STRING));
  QCoreApplication::setOrganizationName(QStringLiteral("Signet contributors"));

  QTranslator translator;
  if (translator.load(QLocale::system(), QStringLiteral("Signet"), QStringLiteral("_"),
                      QStringLiteral(":/i18n"))) {
    application.installTranslator(&translator);
  }

  signet::ui::MainWindow window;
  window.show();
  return application.exec();
}
