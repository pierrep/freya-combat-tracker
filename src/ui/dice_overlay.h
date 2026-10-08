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

// 3D dice thrown across the page they cover: they tumble, bounce off each
// other and the edges, and settle with the rolled number on top, then a card
// shows what the roll was for, and everything fades. Drawn with QPainter (no
// 3D library). Clicks pass through to the page underneath.
class DiceOverlay : public QWidget {
    Q_OBJECT

public:
    // Covers the parent and follows its size.
    explicit DiceOverlay(QWidget* parent);

    // Throws these dice (up to kMaxDice of them), replacing any still on the
    // page. caption says what they were for ("Goblin's Scimitar hits Aria").
    void throwDice(const std::vector<ThrownDie>& dice, const QString& caption);
    // True from the throw until the dice have faded.
    bool active() const { return m_timer.isActive(); }
    // How many dice are on the page, and the number each one shows.
    std::vector<int> shownFaces() const;
    // The kind of each die on the page (4 to 20; a percentile roll is two d10s).
    std::vector<int> shownSides() const;
    // For checking a throw: the number on each die's face (a d4's corner)
    // nearest the viewer, and the largest turn, in degrees, any die made to
    // settle.
    std::vector<int> facesTowardViewer() const;
    float largestSettleTurn() const;

    static constexpr int kMaxDice = 16;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

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

    std::vector<Body> m_bodies;
    QString m_caption;
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_age = 0.0;  // seconds since the throw, in whole steps
};

}  // namespace combat::ui
