#pragma once
#include <QWidget>
#include <functional>
namespace compositor::ui {
void installNumericScrubbing(QWidget* owner,std::function<void(bool)> transaction);
}
