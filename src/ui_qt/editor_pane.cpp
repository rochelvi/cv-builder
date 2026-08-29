#include "editor_pane.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "editor_widgets.h"
#include "text.h"
#include "theme.h"

namespace cvb {
namespace qtui {
namespace {

constexpr int kMargin = 14;
constexpr int kSectionGap = 14;

QLabel* heading(const QString& text) {
    auto* label = new QLabel(text);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

QLabel* hint(const QString& text) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, toQt(currentPalette().subtext));
    label->setPalette(p);
    return label;
}

std::string hexOf(const QColor& colour) {
    return toUtf8(colour.name(QColor::HexRgb));  // "#rrggbb", lower case
}

}  // namespace

EditorPane::EditorPane(QWidget* parent) : QScrollArea(parent) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("formPane"));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    layout->setSpacing(6);

    buildColours(layout);
    buildSections(layout);
    buildHeader(layout);
    buildBlocks(layout);
    layout->addStretch(1);

    setWidget(content);
    applyTheme();
}

EditorPane::~EditorPane() = default;

void EditorPane::buildColours(QVBoxLayout* layout) {
    layout->addWidget(heading(tr("Цвета")));

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Пресет")));
    preset_ = new QComboBox;
    for (const Preset& item : presets()) preset_->addItem(fromUtf8(item.name));
    preset_->setCurrentIndex(-1);
    connect(preset_, &QComboBox::activated, this, [this](int index) { applyPreset(index); });
    row->addWidget(preset_, 1);
    layout->addLayout(row);

    // Two columns of four, the same arrangement as the Win32 form.
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    const Theme defaults;
    for (int i = 0; i < TR_Count; ++i) {
        colours_[i] = defaults.c[i];
        swatch_[i] = new Swatch;
        swatch_[i]->setColour(colours_[i]);
        connect(swatch_[i], &Swatch::clicked, this, [this, i] { pickColour(i); });

        grid->addWidget(swatch_[i], i / 2, (i % 2) * 2);
        grid->addWidget(new QLabel(fromUtf8(uicommon::kThemeRoles[i])), i / 2, (i % 2) * 2 + 1);
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    layout->addLayout(grid);
    layout->addSpacing(kSectionGap);
}

void EditorPane::buildSections(QVBoxLayout* layout) {
    layout->addWidget(heading(tr("Разделы")));
    layout->addWidget(
        hint(tr("Снимите галочку, чтобы убрать раздел со страницы; ↑ ↓ меняют порядок.")));

    sections_ = new SectionListEditor;
    sections_->setSections(defaultSections());
    connect(sections_, &SectionListEditor::changed, this, [this] {
        if (!loading_) emit changed();
    });
    layout->addWidget(sections_);
    layout->addSpacing(kSectionGap);
}

void EditorPane::buildHeader(QVBoxLayout* layout) {
    layout->addWidget(heading(tr("Шапка")));

    name_ = new QLineEdit;
    name_->setPlaceholderText(tr("Полное имя"));
    role_ = new QLineEdit;
    role_->setPlaceholderText(tr("Должность, например SYSTEM ADMINISTRATOR"));
    layout->addWidget(name_);
    layout->addWidget(role_);

    email_ = new QLineEdit;
    email_->setPlaceholderText(tr("Email"));
    location_ = new QLineEdit;
    location_->setPlaceholderText(tr("Город, страна"));
    website_ = new QLineEdit;
    website_->setPlaceholderText(tr("Сайт / GitHub"));

    auto* contacts = new QHBoxLayout;
    contacts->setSpacing(6);
    contacts->addWidget(email_);
    contacts->addWidget(location_);
    contacts->addWidget(website_);
    layout->addLayout(contacts);
    layout->addSpacing(kSectionGap);

    for (QLineEdit* edit : {name_, role_, email_, location_, website_})
        connect(edit, &QLineEdit::textEdited, this, [this] {
            if (!loading_) emit changed();
        });

    layout->addWidget(heading(tr("О себе")));
    summary_ = new QPlainTextEdit;
    summary_->setMinimumHeight(96);
    summary_->setTabChangesFocus(true);
    connect(summary_, &QPlainTextEdit::textChanged, this, [this] {
        if (!loading_) emit changed();
    });
    layout->addWidget(summary_);
    layout->addSpacing(kSectionGap);
}

void EditorPane::buildBlocks(QVBoxLayout* layout) {
    auto add = [&](const QString& title, QWidget* editor, const QString& note = QString()) {
        layout->addWidget(heading(title));
        if (!note.isEmpty()) layout->addWidget(hint(note));
        layout->addWidget(editor);
        layout->addSpacing(kSectionGap);
    };

    auto listChanged = [this](QWidget* w) {
        if (auto* list = qobject_cast<CardList*>(w))
            connect(list, &CardList::changed, this, [this] {
                if (!loading_) emit changed();
            });
        if (auto* list = qobject_cast<ListEditor*>(w))
            connect(list, &ListEditor::changed, this, [this] {
                if (!loading_) emit changed();
            });
        return w;
    };

    jobs_ = new CardList(tr("+ Добавить работу"), [] {
        return new JobCard(Job{"Должность", "2024 – 2025", "Компания", "", {}});
    });
    add(tr("Опыт работы"), listChanged(jobs_));

    volunteering_ = new CardList(tr("+ Добавить волонтёрство"), [] {
        return new JobCard(Job{"Роль", "2024", "Организация", "", {}});
    });
    add(tr("Волонтёрство"), listChanged(volunteering_));

    skillGroups_ = new CardList(tr("+ Добавить группу навыков"), [] {
        SkillGroup group;
        group.title = "Новая группа";
        group.skills.push_back(Skill{"Навык", true});
        return new SkillGroupCard(group);
    });
    add(tr("Технические навыки"), listChanged(skillGroups_),
        tr("Группы заполняют две колонки слева направо."));

    softSkills_ = new ListEditor(tr("Soft skill"));
    add(tr("Soft skills"), listChanged(softSkills_));

    studies_ = new CardList(tr("+ Добавить учёбу"), [] {
        return new JobCard(Job{"Программа обучения", "2022 – 2026", "Университет", "", {}},
                           studyHints());
    });
    add(tr("Учёба"), listChanged(studies_),
        tr("Развёрнутая учёба: программа, вуз, годы и чем занимались — как в опыте работы."));

    education_ = new CardList(tr("+ Добавить запись"), [] {
        return new EducationCard(Education{"Сертификация", "Запланировано", false});
    });
    add(tr("Сертификаты и курсы"), listChanged(education_));

    labBullets_ = new ListEditor(tr("Проект или пункт лаборатории"));
    add(tr("Личная лаборатория"), listChanged(labBullets_));
}

void EditorPane::applyTheme() {
    const uicommon::Palette& colours = currentPalette();
    auto hex = [](const RGB& c) {
        return QColor(c.r, c.g, c.b).name(QColor::HexRgb);
    };
    // A card is a painted rectangle rather than a window of its own, exactly as in
    // the Win32 form; here the style sheet does the painting.
    setStyleSheet(QStringLiteral("QWidget#formPane { background: %1; }"
                                 "QFrame#card { background: %2; border: 1px solid %3;"
                                 " border-radius: 8px; }")
                      .arg(hex(colours.pane), hex(colours.card), hex(colours.cardEdge)));
    update();
}

void EditorPane::applyPreset(int index) {
    const std::vector<Preset>& list = presets();
    if (index < 0 || static_cast<size_t>(index) >= list.size()) return;
    for (int i = 0; i < TR_Count; ++i) {
        colours_[i] = list[static_cast<size_t>(index)].theme.c[i];
        swatch_[i]->setColour(colours_[i]);
    }
    if (!loading_) emit changed();
}

void EditorPane::syncPresetSelection() {
    Theme current;
    for (int i = 0; i < TR_Count; ++i) current.c[i] = colours_[i];
    const std::vector<Preset>& list = presets();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].theme == current) {
            preset_->setCurrentIndex(static_cast<int>(i));
            return;
        }
    }
    // A hand-picked palette matches no preset, and saying so is more honest than
    // leaving the last preset's name under a set of colours it no longer describes.
    preset_->setCurrentIndex(-1);
}

void EditorPane::pickColour(int role) {
    const RGB rgb = parseColor(colours_[role], RGB{0, 0, 0});
    const QColor chosen =
        QColorDialog::getColor(QColor(rgb.r, rgb.g, rgb.b), this, tr("Цвет"));
    if (!chosen.isValid()) return;

    colours_[role] = hexOf(chosen);
    swatch_[role]->setColour(colours_[role]);
    syncPresetSelection();
    if (!loading_) emit changed();
}

void EditorPane::setCV(const CV& cv) {
    // The editors announce every change as it is made; while a whole document is
    // being loaded that would relayout the page a few hundred times.
    loading_ = true;

    name_->setText(fromUtf8(cv.name));
    role_->setText(fromUtf8(cv.role));
    email_->setText(fromUtf8(cv.email));
    location_->setText(fromUtf8(cv.location));
    website_->setText(fromUtf8(cv.website));
    summary_->setPlainText(fromUtf8(cv.summary));
    sections_->setSections(cv.sections);

    for (int i = 0; i < TR_Count; ++i) {
        colours_[i] = cv.theme.c[i];
        swatch_[i]->setColour(colours_[i]);
    }
    syncPresetSelection();

    jobs_->clear();
    for (const Job& job : cv.jobs) jobs_->append(new JobCard(job));

    volunteering_->clear();
    for (const Job& job : cv.volunteering) volunteering_->append(new JobCard(job));

    skillGroups_->clear();
    for (const SkillGroup& group : cv.skillGroups)
        skillGroups_->append(new SkillGroupCard(group));

    studies_->clear();
    for (const Job& item : cv.studies) studies_->append(new JobCard(item, studyHints()));

    education_->clear();
    for (const Education& item : cv.education) education_->append(new EducationCard(item));

    softSkills_->setValues(cv.softSkills);
    labBullets_->setValues(cv.labBullets);

    loading_ = false;
}

CV EditorPane::collect() const {
    CV cv;
    cv.name = toUtf8(name_->text());
    cv.role = toUtf8(role_->text());
    cv.email = toUtf8(email_->text());
    cv.location = toUtf8(location_->text());
    cv.website = toUtf8(website_->text());
    // toPlainText gives plain LF line ends, which is what the model stores.
    cv.summary = toUtf8(summary_->toPlainText());
    cv.sections = sections_->values();

    for (Card* card : jobs_->cards())
        cv.jobs.push_back(static_cast<JobCard*>(card)->read());
    for (Card* card : volunteering_->cards())
        cv.volunteering.push_back(static_cast<JobCard*>(card)->read());
    for (Card* card : skillGroups_->cards())
        cv.skillGroups.push_back(static_cast<SkillGroupCard*>(card)->read());
    for (Card* card : studies_->cards())
        cv.studies.push_back(static_cast<JobCard*>(card)->read());
    for (Card* card : education_->cards())
        cv.education.push_back(static_cast<EducationCard*>(card)->read());

    cv.softSkills = softSkills_->values();
    cv.labBullets = labBullets_->values();
    for (int i = 0; i < TR_Count; ++i) cv.theme.c[i] = colours_[i];
    return cv;
}

}  // namespace qtui
}  // namespace cvb
