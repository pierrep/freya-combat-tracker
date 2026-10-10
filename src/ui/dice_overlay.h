#pragma once

#include <QElapsedTimer>
#include <QQuaternion>
#include <QString>
#include <QTimer>
#include <QVector3D>
#include <QWidget>

#include <vector>

namespace combat::ui {

// One die the app rolled: its kind (4, 6, 8, 10, 12, 20, or 100 for a
// percentile roll) and the face it came up.
struct ThrownDie {
    int sides = 20;
    int face = 1;
};

// One throw in a sequence: an attack's d20s, then (on a hit) its damage.
struct DiceStage {
    std::vector<ThrownDie> dice;
    QString caption;
    // The card's second line: a calculation ("1d6+2 slashing"). Empty: none
    // (the dice themselves are on the table).
    QString detail{};
    // No second line at all (the dice are on the table to see).
    bool noDetail = false;
    // Extra seconds after the stage before has settled, before this one is
    // thrown (a hit's card is read before its damage comes).
    double extraPause = 0.0;
    // No card at all (initiative).
    bool noCard = false;
};

// 3D dice thrown across the page they cover, over a dimmed page: they tumble,
// bounce off each other and the edges, and settle with the rolled number on
// top, then a card shows what the roll was for. They stay until dismissed: a
// click anywhere in the window (or Escape, Space, or Enter) takes them away at
// once, whatever they are doing, and goes no further. Drawn with QPainter (no
// 3D library).
class DiceOverlay : public QWidget {
    Q_OBJECT

public:
    // Covers the parent and follows its size.
    explicit DiceOverlay(QWidget* parent);

    // Throws these dice (up to kMaxDice of them), replacing any still on the
    // page. caption says what they were for ("Goblin's Scimitar hits Aria").
    void throwDice(const std::vector<ThrownDie>& dice, const QString& caption);
    // Throws these in turn: each once the one before has settled (and a
    // short pause), onto the table beside the dice already there. The card
    // before leaves as the next throw starts, and the next card comes up
    // when that throw settles.
    void throwStages(const std::vector<DiceStage>& stages);
    // True from the throw until it is dismissed.
    bool active() const { return isVisible() && !m_dismissed; }
    // Takes the dice, the card, and the dimming away now.
    void dismiss();
    // How many dice are on the page, and the number each one shows.
    std::vector<int> shownFaces() const;
    // The kind of each die on the page (4 to 20; a percentile roll is two d10s).
    std::vector<int> shownSides() const;
    // The kind of each die thrown so far (an attack's damage waits for its
    // d20 to settle), and the caption on the card now (empty while none shows).
    std::vector<int> thrownSides() const;
    QString shownCaption() const;
    // The second line on the card now: a calculation, or empty.
    QString shownDetail() const;
    // For checking a throw: the number on each die's face (a d4's corner)
    // nearest the viewer, and the largest turn, in degrees, any die made to
    // settle.
    std::vector<int> facesTowardViewer() const;
    float largestSettleTurn() const;

    static constexpr int kMaxDice = 16;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // How a die's faces are labelled.
    enum class Labels { Plain, Tens, Ones };
    struct Body {
        int sides = 20;
        Labels labels = Labels::Plain;
        int result = 1;        // the face value that ends on top
        int shown = 1;         // what it reads (00-90 for tens, 0-9 for ones)
        QVector3D position;    // x, y on the table (pixels), z height
        QVector3D velocity;
        QQuaternion orientation;
        QVector3D spin;        // angular velocity, radians a second
        float radius = 30.0f;
        QQuaternion settleFrom;
        QQuaternion settleTo;
        bool settling = false;
        int settleOn = 0;    // the face (a d4's corner) it settles showing
        int plannedOn = -1;  // the one the run-ahead chose and numbered
        int stage = 0;          // which throw of the sequence it is in
        double thrownAt = 0.0;  // when that throw starts, seconds after the first
        // Its numbers: arranged per throw so the face it settles on shows
        // the result (opposite faces still add up as on a real die).
        std::vector<int> faceValues;
        std::vector<int> cornerValues;
    };

    void tick();
    // One fixed step of the dice, at this time since the throw.
    static void step(std::vector<Body>& bodies, double time, float dt, float width, float height);
    static void startSettling(Body& body);
    static int nearestToCamera(const Body& body);
    static void numberForResult(Body& body, int settleOn);

    // The stage whose card shows now, or -1.
    int cardStage() const;

    std::vector<Body> m_bodies;
    std::vector<QString> m_captions;  // each stage's
    std::vector<QString> m_details;   // each stage's second line; empty lists its dice
    std::vector<bool> m_noDetail;
    std::vector<bool> m_noCard;
    std::vector<double> m_stageAt;    // when each stage is thrown
    double m_lastThrowAt = 0.0;
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_age = 0.0;  // seconds since the throw, in whole steps
    // The dimming fades in once, when the overlay comes up (not again when a
    // later throw replaces the dice on it).
    QElapsedTimer m_dimClock;
    // A click took the dice away: its release is swallowed too.
    bool m_dismissed = false;
    bool m_swallowRelease = false;
};

}  // namespace combat::ui
