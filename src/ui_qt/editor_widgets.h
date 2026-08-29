// The pieces the editing pane is built from: a colour swatch, a list of text
// fields with reordering, the running-order list, and cards.
//
// They know nothing about a CV beyond the small struct each one edits, which is
// what lets the pane assemble them without any of them knowing about the others.
#pragma once

#include <QFrame>
#include <QPushButton>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

#include "model.h"

class QCheckBox;
class QLineEdit;
class QVBoxLayout;

namespace cvb {
namespace qtui {

// UTF-8 in the model, QString in the widgets, in exactly two functions.
QString fromUtf8(const std::string& text);
std::string toUtf8(const QString& text);

// A button showing a colour, with its hex value written on it in whichever of
// black or white stays readable.
class Swatch : public QPushButton {
    Q_OBJECT

public:
    explicit Swatch(QWidget* parent = nullptr);

    void setColour(const std::string& hex);
    const std::string& colour() const { return hex_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::string hex_;
};

// A vertical list of text fields with add / move / remove controls.
class ListEditor : public QWidget {
    Q_OBJECT

public:
    ListEditor(QString placeholder, QWidget* parent = nullptr);

    void setValues(const std::vector<std::string>& values);
    std::vector<std::string> values() const;  // blank entries are dropped

signals:
    void changed();

private:
    struct Row;
    void addRow(const std::string& text);
    void removeRow(const Row* row);
    void moveRow(const Row* row, int delta);
    void relayout();

    struct Row {
        QWidget* frame = nullptr;
        QLineEdit* edit = nullptr;
    };

    QString placeholder_;
    QVBoxLayout* rows_ = nullptr;
    std::vector<std::unique_ptr<Row>> list_;
};

// The running order of the document: one row per section, with a checkbox that
// keeps it off the page without discarding what it holds, an editable heading, and
// arrows that move it.
class SectionListEditor : public QWidget {
    Q_OBJECT

public:
    explicit SectionListEditor(QWidget* parent = nullptr);

    void setSections(const std::vector<SectionRef>& sections);
    std::vector<SectionRef> values() const;

signals:
    void changed();

private:
    struct Row {
        std::string id;
        QWidget* frame = nullptr;
        QCheckBox* enabled = nullptr;
        QLineEdit* label = nullptr;
    };

    void addRow(const SectionRef& ref);
    void move(const Row* row, int delta);
    void relayout();

    QVBoxLayout* rows_ = nullptr;
    std::vector<std::unique_ptr<Row>> list_;
};

// One card in a CardList. Subclasses own their controls and know how to read
// themselves back into the model.
class Card : public QFrame {
    Q_OBJECT

public:
    explicit Card(QWidget* parent = nullptr);

signals:
    void changed();

protected:
    // Where a subclass puts its own controls; the tools row above it belongs to
    // the card.
    QVBoxLayout* body() const { return body_; }
    void buildFrame();

private:
    QVBoxLayout* body_ = nullptr;
};

class JobCard : public Card {
    Q_OBJECT

public:
    struct Hints {
        QString title = QStringLiteral("Должность");
        QString period = QStringLiteral("2022 – 2025 · 3 года");
        QString company = QStringLiteral("Компания");
        QString location = QStringLiteral("Город");
        QString bullet = QStringLiteral("Достижение или обязанность");
    };

    // Two overloads rather than a defaulted Hints argument: a default argument
    // that constructs a nested type needs that type complete, and it is not until
    // this class is.
    explicit JobCard(const Job& job, QWidget* parent = nullptr);
    JobCard(const Job& job, const Hints& hints, QWidget* parent = nullptr);

    Job read() const;

private:
    void build(const Job& job, const Hints& hints);

private:
    QLineEdit* title_ = nullptr;
    QLineEdit* period_ = nullptr;
    QLineEdit* company_ = nullptr;
    QLineEdit* location_ = nullptr;
    ListEditor* bullets_ = nullptr;
};

// Studies and jobs share a shape but not a vocabulary, so the words are passed in
// rather than hard-coded.
JobCard::Hints studyHints();

class SkillGroupCard : public Card {
    Q_OBJECT

public:
    explicit SkillGroupCard(const SkillGroup& group, QWidget* parent = nullptr);

    SkillGroup read() const;

private:
    struct Row {
        QWidget* frame = nullptr;
        QLineEdit* name = nullptr;
        QCheckBox* highlight = nullptr;
    };

    void addRow(const Skill& skill);
    void removeRow(const Row* row);

    QLineEdit* title_ = nullptr;
    QVBoxLayout* rows_ = nullptr;
    std::vector<std::unique_ptr<Row>> list_;
};

class EducationCard : public Card {
    Q_OBJECT

public:
    explicit EducationCard(const Education& item, QWidget* parent = nullptr);

    Education read() const;

private:
    QLineEdit* title_ = nullptr;
    QLineEdit* subtitle_ = nullptr;
    QCheckBox* highlight_ = nullptr;
};

// A column of cards with an add button underneath.
class CardList : public QWidget {
    Q_OBJECT

public:
    using Factory = std::function<Card*()>;

    CardList(QString addLabel, Factory factory, QWidget* parent = nullptr);

    void clear();
    void append(Card* card);
    const std::vector<Card*>& cards() const { return cards_; }

signals:
    void changed();

private:
    void adopt(Card* card);
    void move(Card* card, int delta);
    void remove(Card* card);
    void relayout();

    Factory factory_;
    QVBoxLayout* column_ = nullptr;
    std::vector<Card*> cards_;
};

}  // namespace qtui
}  // namespace cvb
