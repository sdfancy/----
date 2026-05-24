#pragma once

#include <QCoreApplication>
#include <memory>

namespace spray::app {
class Application;
}

namespace spray::ui {

class ApplicationHmiRunner {
public:
    static int run(QCoreApplication& qtApp, std::shared_ptr<spray::app::Application> coreApp, bool headless);
};

} // namespace spray::ui
