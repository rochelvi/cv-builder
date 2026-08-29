#include "editor_widgets.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

#include "layout.h"  // parseColor
#include "text.h"
#include "theme.h"

namespace cvb {
namespace qtui {
namespace {

constexpr int kGap = 6;
constexpr int kToolButton = 26;

QString trimmed(const QString& text) { return text.trimmed(); }

// The small square buttons that move and delete a row. Arrows rather than words:
// the row is already as wide as the pane.
QPushButton* toolButton(const QString& glyph, const QString& tip) {
    auto* button = new QPushButton(glyph);
    button->setToolTip(tip);
    button->setFixedSize(kToolButton, kToolButton);
    button->setFocusPolicy(Qt::TabFocus);
    return button;
}

QLineEdit* field(const QString& placeholder) {
    auto* edit = new QLineEdit;
    edit->setPlaceholderText(placeholder);
    return edit;
}

}  // namespace

QString fromUtf8(const std::string& text) { return QString::fromUtf8(text.c_str(), static_cast<qsizetype>(text.size())); }

std::string toUtf8(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

// ------------------------------------------------------------------- Swatch

Swatch::Swatch(QWidget* parent) : QPushButton(parent) {
    setFixedHeight(26);
    setMinimumWidth(84);
}

void Swatch::setColour(const std::string& hex) {
    hex_ = hex;
    update();
}

void Swatch::paintEvent(QPaintEvent*) {
    const RGB rgb = parseColor(hex_, RGB{0, 0, 0});
    const QColor colour(rgb.r, rgb.g, rgb.b);

    QPainter painter(this);
    painter.fillRect(rect(), colour);
    painter.setPen(toQt(hasFocus() ? currentPalette().accent : currentPalette().cardEdge));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    // Whichever of black or white stays readable on the colour being shown.
    const int luma = (rgb.r * 299 + rgb.g * 587 + rgb.b * 114) / 1000;
    painter.setPen(luma > 140 ? QColor(Qt::black) : QColor(Qt::white));
    painter.drawText(rect(), Qt::AlignCenter, fromUtf8(hex_));
}

// --------------------------------------------------------------- ListEditor

ListEditor::ListEditor(QString placeholder, QWidget* parent)
    : QWidget(parent), placeholder_(std::move(placeholder)) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(4);

    rows_ = new QVBoxLayout;
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setSpacing(4);
    outer->addLayout(rows_);

    auto* add = new QPushButton(QStringLiteral("+ Добавить"));
    add->setFixedWidth(120);
    connect(add, &QPushButton::clicked, this, [this] {
        addRow(std::string());
        emit changed();
    });
    outer->addWidget(add, 0, Qt::AlignLeft);
}

void ListEditor::addRow(const std::string& text) {
    auto row = std::make_unique<Row>();
    Row* raw = row.get();

    raw->frame = new QWidget;
    auto* line = new QHBoxLayout(raw->frame);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(4);

    raw->edit = field(placeholder_);
    raw->edit->setText(fromUtf8(text));
    connect(raw->edit, &QLineEdit::textEdited, this, [this] { emit changed(); });
    line->addWidget(raw->edit, 1);

    // The handlers find their row by pointer rather than by index: a row that has
    // been moved would otherwise act on whatever ended up in its old place.
    auto* up = toolButton(QStringLiteral("↑"), tr("Выше"));
    auto* down = toolButton(QStringLiteral("↓"), tr("Ниже"));
    auto* del = toolButton(QStringLiteral("✕"), tr("Удалить"));
    connect(up, &QPushButton::clicked, this, [this, raw] { moveRow(raw, -1); });
    connect(down, &QPushButton::clicked, this, [this, raw] { moveRow(raw, 1); });
    connect(del, &QPushButton::clicked, this, [this, raw] { removeRow(raw); });
    line->addWidget(up);
    line->addWidget(down);
    line->addWidget(del);

    rows_->addWidget(raw->frame);
    list_.push_back(std::move(row));
}

void ListEditor::removeRow(const Row* row) {
    for (size_t i = 0; i < list_.size(); ++i) {
        if (list_[i].get() != row) continue;
        list_[i]->frame->deleteLater();
        list_[i]->frame->setParent(nullptr);
        list_.erase(list_.begin() + static_cast<long>(i));
        emit changed();
        return;
    }
}

void ListEditor::moveRow(const Row* row, int delta) {
    for (size_t i = 0; i < list_.size(); ++i) {
        if (list_[i].get() != row) continue;
        const size_t target = static_cast<size_t>(static_cast<long>(i) + delta);
        if (target >= list_.size()) return;
        std::swap(list_[i], list_[target]);
        relayout();
        emit changed();
        return;
    }
}

void ListEditor::relayout() {
    // Taking every row out and putting them back in the new order is what keeps
    // the widget order and the vector order the same thing; with a handful of rows
    // it costs nothing.
    for (const auto& row : list_) rows_->removeWidget(row->frame);
    for (const auto& row : list_) rows_->addWidget(row->frame);
}

void ListEditor::setValues(const std::vector<std::string>& values) {
    for (const auto& row : list_) {
        row->frame->setParent(nullptr);
        row->frame->deleteLater();
    }
    list_.clear();
    for (const std::string& value : values) addRow(value);
}

std::vector<std::string> ListEditor::values() const {
    std::vector<std::string> out;
    for (const auto& row : list_) {
        const QString text = trimmed(row->edit->text());
        if (!text.isEmpty()) out.push_back(toUtf8(text));
    }
    return out;
}

// -------------------------------------------------------- SectionListEditor

SectionListEditor::SectionListEditor(QWidget* parent) : QWidget(parent) {
    rows_ = new QVBoxLayout(this);
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setSpacing(4);
}

void SectionListEditor::addRow(const SectionRef& ref) {
    auto row = std::make_unique<Row>();
    Row* raw = row.get();
    raw->id = ref.id;

    raw->frame = new QWidget;
    auto* line = new QHBoxLayout(raw->frame);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(4);

    raw->enabled = new QCheckBox(fromUtf8(uicommon::sectionName(ref.id)));
    raw->enabled->setChecked(ref.enabled);
    raw->enabled->setMinimumWidth(190);
    connect(raw->enabled, &QCheckBox::toggled, this, [this] { emit changed(); });
    line->addWidget(raw->enabled);

    raw->label = field(tr("Заголовок на странице"));
    raw->label->setText(fromUtf8(ref.label));
    connect(raw->label, &QLineEdit::textEdited, this, [this] { emit changed(); });
    line->addWidget(raw->label, 1);

    auto* up = toolButton(QStringLiteral("↑"), tr("Выше"));
    auto* down = toolButton(QStringLiteral("↓"), tr("Ниже"));
    connect(up, &QPushButton::clicked, this, [this, raw] { move(raw, -1); });
    connect(down, &QPushButton::clicked, this, [this, raw] { move(raw, 1); });
    line->addWidget(up);
    line->addWidget(down);

    rows_->addWidget(raw->frame);
    list_.push_back(std::move(row));
}

void SectionListEditor::move(const Row* row, int delta) {
    for (size_t i = 0; i < list_.size(); ++i) {
        if (list_[i].get() != row) continue;
        const size_t target = static_cast<size_t>(static_cast<long>(i) + delta);
        if (target >= list_.size()) return;
        std::swap(list_[i], list_[target]);
        relayout();
        emit changed();
        return;
    }
}

void SectionListEditor::relayout() {
    for (const auto& row : list_) rows_->removeWidget(row->frame);
    for (const auto& row : list_) rows_->addWidget(row->frame);
}

void SectionListEditor::setSections(const std::vector<SectionRef>& sections) {
    for (const auto& row : list_) {
        row->frame->setParent(nullptr);
        row->frame->deleteLater();
    }
    list_.clear();
    for (const SectionRef& ref : sections) addRow(ref);
}

std::vector<SectionRef> SectionListEditor::values() const {
    std::vector<SectionRef> out;
    for (size_t i = 0; i < list_.size(); ++i) {
        SectionRef ref;
        ref.id = list_[i]->id;
        ref.label = toUtf8(trimmed(list_[i]->label->text()));
        if (ref.label.empty()) ref.label = defaultSectionLabel(ref.id);
        ref.enabled = list_[i]->enabled->isChecked();
        // Written out from the row's position, so the list on screen is the order
        // in the file.
        ref.order = static_cast<int>(i);
        out.push_back(std::move(ref));
    }
    return out;
}

// --------------------------------------------------------------------- Card

Card::Card(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("card"));
    setFrameShape(QFrame::NoFrame);
}

void Card::buildFrame() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(kGap);

    body_ = new QVBoxLayout;
    body_->setContentsMargins(0, 0, 0, 0);
    body_->setSpacing(kGap);
    outer->addLayout(body_);
}

// ------------------------------------------------------------------ JobCard

JobCard::Hints studyHints() {
    JobCard::Hints hints;
    hints.title = QStringLiteral("Программа, специальность или степень");
    hints.period = QStringLiteral("2022 – 2026");
    hints.company = QStringLiteral("Университет или школа");
    hints.location = QStringLiteral("Город");
    hints.bullet = QStringLiteral("Курс, проект, олимпиада, активность");
    return hints;
}

JobCard::JobCard(const Job& job, QWidget* parent) : Card(parent) { build(job, Hints()); }

JobCard::JobCard(const Job& job, const Hints& hints, QWidget* parent) : Card(parent) {
    build(job, hints);
}

void JobCard::build(const Job& job, const Hints& hints) {
    buildFrame();

    title_ = field(hints.title);
    period_ = field(hints.period);
    company_ = field(hints.company);
    location_ = field(hints.location);
    title_->setText(fromUtf8(job.title));
    period_->setText(fromUtf8(job.period));
    company_->setText(fromUtf8(job.company));
    location_->setText(fromUtf8(job.location));

    for (QLineEdit* edit : {title_, period_, company_, location_})
        connect(edit, &QLineEdit::textEdited, this, [this] { emit changed(); });

    // Title takes two thirds and the period the rest, as on the page itself.
    auto* first = new QHBoxLayout;
    first->setSpacing(kGap);
    first->addWidget(title_, 2);
    first->addWidget(period_, 1);
    body()->addLayout(first);

    auto* second = new QHBoxLayout;
    second->setSpacing(kGap);
    second->addWidget(company_, 2);
    second->addWidget(location_, 1);
    body()->addLayout(second);

    body()->addWidget(new QLabel(tr("Пункты")));
    bullets_ = new ListEditor(hints.bullet);
    bullets_->setValues(job.bullets);
    connect(bullets_, &ListEditor::changed, this, [this] { emit changed(); });
    body()->addWidget(bullets_);
}

Job JobCard::read() const {
    Job job;
    job.title = toUtf8(title_->text());
    job.period = toUtf8(period_->text());
    job.company = toUtf8(company_->text());
    job.location = toUtf8(location_->text());
    job.bullets = bullets_->values();
    return job;
}

// ----------------------------------------------------------- SkillGroupCard

SkillGroupCard::SkillGroupCard(const SkillGroup& group, QWidget* parent) : Card(parent) {
    buildFrame();

    title_ = field(tr("Название группы, например «Сети»"));
    title_->setText(fromUtf8(group.title));
    connect(title_, &QLineEdit::textEdited, this, [this] { emit changed(); });
    body()->addWidget(title_);

    rows_ = new QVBoxLayout;
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setSpacing(4);
    body()->addLayout(rows_);

    auto* add = new QPushButton(QStringLiteral("+ Навык"));
    add->setFixedWidth(110);
    connect(add, &QPushButton::clicked, this, [this] {
        addRow(Skill());
        emit changed();
    });
    body()->addWidget(add, 0, Qt::AlignLeft);

    for (const Skill& skill : group.skills) addRow(skill);
}

void SkillGroupCard::addRow(const Skill& skill) {
    auto row = std::make_unique<Row>();
    Row* raw = row.get();

    raw->frame = new QWidget;
    auto* line = new QHBoxLayout(raw->frame);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(4);

    raw->name = field(tr("Навык"));
    raw->name->setText(fromUtf8(skill.name));
    connect(raw->name, &QLineEdit::textEdited, this, [this] { emit changed(); });
    line->addWidget(raw->name, 1);

    raw->highlight = new QCheckBox(tr("выделить"));
    raw->highlight->setChecked(skill.highlight);
    connect(raw->highlight, &QCheckBox::toggled, this, [this] { emit changed(); });
    line->addWidget(raw->highlight);

    auto* del = toolButton(QStringLiteral("✕"), tr("Удалить"));
    connect(del, &QPushButton::clicked, this, [this, raw] { removeRow(raw); });
    line->addWidget(del);

    rows_->addWidget(raw->frame);
    list_.push_back(std::move(row));
}

void SkillGroupCard::removeRow(const Row* row) {
    for (size_t i = 0; i < list_.size(); ++i) {
        if (list_[i].get() != row) continue;
        list_[i]->frame->setParent(nullptr);
        list_[i]->frame->deleteLater();
        list_.erase(list_.begin() + static_cast<long>(i));
        emit changed();
        return;
    }
}

SkillGroup SkillGroupCard::read() const {
    SkillGroup group;
    group.title = toUtf8(title_->text());
    for (const auto& row : list_) {
        const QString name = row->name->text().trimmed();
        if (name.isEmpty()) continue;
        Skill skill;
        skill.name = toUtf8(name);
        skill.highlight = row->highlight->isChecked();
        group.skills.push_back(std::move(skill));
    }
    return group;
}

// ------------------------------------------------------------ EducationCard

EducationCard::EducationCard(const Education& item, QWidget* parent) : Card(parent) {
    buildFrame();

    title_ = field(tr("Программа или сертификация"));
    subtitle_ = field(tr("Учреждение, год / статус"));
    title_->setText(fromUtf8(item.title));
    subtitle_->setText(fromUtf8(item.subtitle));
    for (QLineEdit* edit : {title_, subtitle_})
        connect(edit, &QLineEdit::textEdited, this, [this] { emit changed(); });

    highlight_ = new QCheckBox(tr("выделить"));
    highlight_->setChecked(item.highlight);
    connect(highlight_, &QCheckBox::toggled, this, [this] { emit changed(); });

    body()->addWidget(title_);
    body()->addWidget(subtitle_);
    body()->addWidget(highlight_);
}

Education EducationCard::read() const {
    Education item;
    item.title = toUtf8(title_->text());
    item.subtitle = toUtf8(subtitle_->text());
    item.highlight = highlight_->isChecked();
    return item;
}

// ----------------------------------------------------------------- CardList

CardList::CardList(QString addLabel, Factory factory, QWidget* parent)
    : QWidget(parent), factory_(std::move(factory)) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);

    column_ = new QVBoxLayout;
    column_->setContentsMargins(0, 0, 0, 0);
    column_->setSpacing(8);
    outer->addLayout(column_);

    auto* add = new QPushButton(addLabel);
    add->setMinimumWidth(170);
    connect(add, &QPushButton::clicked, this, [this] {
        append(factory_());
        emit changed();
    });
    outer->addWidget(add, 0, Qt::AlignLeft);
}

void CardList::append(Card* card) {
    adopt(card);
    cards_.push_back(card);
    column_->addWidget(card);
}

void CardList::adopt(Card* card) {
    connect(card, &Card::changed, this, [this] { emit changed(); });

    // The tools belong to the list, not to the card: moving and deleting are
    // operations on the collection.
    auto* tools = new QHBoxLayout;
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(4);
    tools->addStretch(1);

    auto* up = toolButton(QStringLiteral("↑"), tr("Выше"));
    auto* down = toolButton(QStringLiteral("↓"), tr("Ниже"));
    auto* del = toolButton(QStringLiteral("✕"), tr("Удалить"));
    connect(up, &QPushButton::clicked, this, [this, card] { move(card, -1); });
    connect(down, &QPushButton::clicked, this, [this, card] { move(card, 1); });
    connect(del, &QPushButton::clicked, this, [this, card] { remove(card); });
    tools->addWidget(up);
    tools->addWidget(down);
    tools->addWidget(del);

    // Above the card's own controls, which is where the Win32 version puts them.
    if (auto* layout = qobject_cast<QVBoxLayout*>(card->layout())) layout->insertLayout(0, tools);
}

void CardList::move(Card* card, int delta) {
    auto found = std::find(cards_.begin(), cards_.end(), card);
    if (found == cards_.end()) return;
    const size_t index = static_cast<size_t>(found - cards_.begin());
    const size_t target = static_cast<size_t>(static_cast<long>(index) + delta);
    if (target >= cards_.size()) return;
    std::swap(cards_[index], cards_[target]);
    relayout();
    emit changed();
}

void CardList::remove(Card* card) {
    auto found = std::find(cards_.begin(), cards_.end(), card);
    if (found == cards_.end()) return;
    cards_.erase(found);
    card->setParent(nullptr);
    card->deleteLater();
    emit changed();
}

void CardList::relayout() {
    for (Card* card : cards_) column_->removeWidget(card);
    for (Card* card : cards_) column_->addWidget(card);
}

void CardList::clear() {
    for (Card* card : cards_) {
        card->setParent(nullptr);
        card->deleteLater();
    }
    cards_.clear();
}

}  // namespace qtui
}  // namespace cvb
