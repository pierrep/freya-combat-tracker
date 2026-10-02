#include "ui/placeholder_page.h"

#include "ui/page_title.h"

#include <QLabel>
#include <QVBoxLayout>

namespace combat::ui {

PlaceholderPage::PlaceholderPage(const QString& title, const QString& message, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(16);
    layout->addWidget(makePageTitle(title));
    if (!message.isEmpty()) {
        auto* text = new QLabel(message);
        text->setWordWrap(true);
        layout->addWidget(text);
    }
    layout->addStretch(1);
}

}  // namespace combat::ui
