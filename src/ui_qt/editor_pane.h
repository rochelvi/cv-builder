// The editing pane: every control the user types into.
//
// Same structure as the Win32 form, section for section, because the two are
// meant to be the same application: colours, the running order of the document,
// the header, then one block per section of the page.
//
// Unlike the Win32 version this reads the widgets directly when a CV is asked
// for. There it was a round trip through GetWindowText per control, which is why
// that version kept shadow copies; here a QLineEdit's text is a QString in
// memory, so collect() is a walk over a few hundred strings and costs less than
// the layout pass it feeds.
#pragma once

#include <QScrollArea>
#include <QString>
#include <memory>
#include <vector>

#include "model.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

namespace cvb {
namespace qtui {

class CardList;
class ListEditor;
class SectionListEditor;
class Swatch;

class EditorPane : public QScrollArea {
    Q_OBJECT

public:
    explicit EditorPane(QWidget* parent = nullptr);
    ~EditorPane() override;

    void setCV(const CV& cv);
    CV collect() const;

    void applyTheme();

signals:
    // Something the document depends on changed. Emitted for a keystroke as well
    // as for a card being added, so the owner can debounce it.
    void changed();

private:
    void buildColours(QVBoxLayout* layout);
    void buildSections(QVBoxLayout* layout);
    void buildHeader(QVBoxLayout* layout);
    void buildBlocks(QVBoxLayout* layout);
    void applyPreset(int index);
    void syncPresetSelection();
    void pickColour(int role);

    // While a whole CV is being loaded, the editors fire their signals as they are
    // filled; announcing each one would relayout the document a few hundred times.
    bool loading_ = false;

    QComboBox* preset_ = nullptr;
    Swatch* swatch_[TR_Count] = {};
    std::string colours_[TR_Count];

    QLineEdit* name_ = nullptr;
    QLineEdit* role_ = nullptr;
    QLineEdit* email_ = nullptr;
    QLineEdit* location_ = nullptr;
    QLineEdit* website_ = nullptr;
    QPlainTextEdit* summary_ = nullptr;

    SectionListEditor* sections_ = nullptr;
    ListEditor* softSkills_ = nullptr;
    ListEditor* labBullets_ = nullptr;
    CardList* jobs_ = nullptr;
    CardList* volunteering_ = nullptr;
    CardList* skillGroups_ = nullptr;
    CardList* studies_ = nullptr;
    CardList* education_ = nullptr;
};

}  // namespace qtui
}  // namespace cvb
