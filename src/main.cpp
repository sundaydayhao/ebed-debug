#include "MainWindow.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("ebed-debug-pro");
  app.setOrganizationName("ebed");

  qRegisterMetaType<ChannelKey>("ChannelKey");
  qRegisterMetaType<PayloadFormat>("PayloadFormat");
  qRegisterMetaType<Direction>("Direction");

  MainWindow window;
  window.show();
  return app.exec();
}
