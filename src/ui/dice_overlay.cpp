#include "ui/dice_overlay.h"

#include "ui/dice_samples.h"
#include "ui/theme.h"
#ifdef FREYA_HAVE_AUDIO
#include "ui/dice_audio.h"
#endif

#include <QEvent>
#include <QApplication>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QResizeEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace combat::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Timeline, in seconds from the throw.
constexpr double kSettleAt = 0.75;    // the dice turn to their results
constexpr double kSettleTime = 0.2;
// The card is fully in this long after the last throw; then the animation
// stops and the dice wait for a click.
constexpr double kStillAfter = 1.4;
constexpr double kDimIn = 0.15;
constexpr double kStagePause = 0.35;  // between a stage settling and the next throw
// The camera looks down at the table, tilted toward the bottom of the page.
constexpr float kTilt = 0.34f;  // radians from straight down
constexpr float kGravity = 3000.0f;
// The dice move in fixed steps, so a throw can be run ahead to see how it
// lands, and then played out exactly the same.
constexpr float kStep = 1.0f / 120.0f;

// A die's solid: corners at distance 1 from its centre, faces wound
// anticlockwise seen from outside, each face's outward normal, and its value.
// A d4 is read at its top corner, so its corners carry the values.
struct Shape {
    std::vector<QVector3D> corners;
    std::vector<std::vector<int>> faces;
    std::vector<QVector3D> normals;
    std::vector<int> faceValues;
    std::vector<std::size_t> opposite;  // each face's opposite face
    std::vector<int> cornerValues;  // d4 only
    float inradius = 0.5f;           // centre to a face
    float size = 1.0f;               // drawn size against the others
    float fontScale = 0.5f;
};

QVector3D faceCentre(const Shape& shape, const std::vector<int>& face)
{
    QVector3D sum;
    for (const int index : face) {
        sum += shape.corners[static_cast<std::size_t>(index)];
    }
    return sum / static_cast<float>(face.size());
}

// The corners on each of these face directions, in order around it.
void facesFromNormals(Shape& shape, const std::vector<QVector3D>& directions)
{
    for (const QVector3D& raw : directions) {
        const QVector3D normal = raw.normalized();
        float best = -2.0f;
        for (const QVector3D& corner : shape.corners) {
            best = std::max(best, QVector3D::dotProduct(corner, normal));
        }
        std::vector<int> face;
        for (std::size_t i = 0; i < shape.corners.size(); ++i) {
            if (QVector3D::dotProduct(shape.corners[i], normal) >= best - 1e-3f) {
                face.push_back(static_cast<int>(i));
            }
        }
        QVector3D centre;
        for (const int index : face) {
            centre += shape.corners[static_cast<std::size_t>(index)];
        }
        centre /= static_cast<float>(face.size());
        const QVector3D u = (shape.corners[static_cast<std::size_t>(face.front())] - centre).normalized();
        const QVector3D w = QVector3D::crossProduct(normal, u);
        std::sort(face.begin(), face.end(), [&](int a, int b) {
            const QVector3D pa = shape.corners[static_cast<std::size_t>(a)] - centre;
            const QVector3D pb = shape.corners[static_cast<std::size_t>(b)] - centre;
            return std::atan2(QVector3D::dotProduct(pa, w), QVector3D::dotProduct(pa, u)) <
                   std::atan2(QVector3D::dotProduct(pb, w), QVector3D::dotProduct(pb, u));
        });
        shape.faces.push_back(face);
        shape.normals.push_back(normal);
    }
}

// Opposite faces add up to sides + 1, as on real dice.
void numberOpposites(Shape& shape)
{
    const std::size_t count = shape.faces.size();
    shape.faceValues.assign(count, 0);
    int next = 1;
    for (std::size_t i = 0; i < count; ++i) {
        if (shape.faceValues[i] != 0) {
            continue;
        }
        std::size_t opposite = i;
        float most = 2.0f;
        for (std::size_t j = 0; j < count; ++j) {
            const float dot = QVector3D::dotProduct(shape.normals[i], shape.normals[j]);
            if (j != i && shape.faceValues[j] == 0 && dot < most) {
                most = dot;
                opposite = j;
            }
        }
        shape.faceValues[i] = next;
        if (opposite != i) {
            shape.faceValues[opposite] = static_cast<int>(count) + 1 - next;
        }
        ++next;
    }
    shape.opposite.assign(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t far = i;
        float most = 2.0f;
        for (std::size_t j = 0; j < count; ++j) {
            const float dot = QVector3D::dotProduct(shape.normals[i], shape.normals[j]);
            if (j != i && dot < most) {
                most = dot;
                far = j;
            }
        }
        shape.opposite[i] = far;
    }
}

void normalizeCorners(Shape& shape)
{
    for (QVector3D& corner : shape.corners) {
        corner.normalize();
    }
}

float inradiusOf(const Shape& shape)
{
    float least = 1.0f;
    for (std::size_t i = 0; i < shape.faces.size(); ++i) {
        least = std::min(least, QVector3D::dotProduct(faceCentre(shape, shape.faces[i]), shape.normals[i]));
    }
    return least;
}

Shape makeShape(int sides)
{
    Shape shape;
    const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<QVector3D> directions;
    switch (sides) {
    case 4:
        shape.corners = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
        normalizeCorners(shape);
        for (const QVector3D& corner : shape.corners) {
            directions.push_back(-corner);
        }
        facesFromNormals(shape, directions);
        shape.cornerValues = {1, 2, 3, 4};
        shape.faceValues = {0, 0, 0, 0};
        shape.size = 1.25f;
        shape.fontScale = 0.44f;
        break;
    case 6:
        for (const int x : {-1, 1}) {
            for (const int y : {-1, 1}) {
                for (const int z : {-1, 1}) {
                    shape.corners.emplace_back(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                }
            }
        }
        normalizeCorners(shape);
        directions = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        facesFromNormals(shape, directions);
        numberOpposites(shape);
        shape.size = 0.92f;
        shape.fontScale = 0.62f;
        break;
    case 8:
        shape.corners = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const int x : {-1, 1}) {
            for (const int y : {-1, 1}) {
                for (const int z : {-1, 1}) {
                    directions.emplace_back(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                }
            }
        }
        facesFromNormals(shape, directions);
        numberOpposites(shape);
        shape.size = 1.05f;
        shape.fontScale = 0.46f;
        break;
    case 12:
    case 20: {
        // The icosahedron's corners are the dodecahedron's face directions,
        // and the other way round.
        std::vector<QVector3D> ico;
        for (const float a : {-1.0f, 1.0f}) {
            for (const float b : {-phi, phi}) {
                ico.emplace_back(0.0f, a, b);
                ico.emplace_back(a, b, 0.0f);
                ico.emplace_back(b, 0.0f, a);
            }
        }
        std::vector<QVector3D> dodeca;
        for (const int x : {-1, 1}) {
            for (const int y : {-1, 1}) {
                for (const int z : {-1, 1}) {
                    dodeca.emplace_back(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                }
            }
        }
        // (The other cyclic order from the icosahedron's, so each of these
        // points at the middle of one of its faces.)
        for (const float a : {-1.0f / phi, 1.0f / phi}) {
            for (const float b : {-phi, phi}) {
                dodeca.emplace_back(a, 0.0f, b);
                dodeca.emplace_back(0.0f, b, a);
                dodeca.emplace_back(b, a, 0.0f);
            }
        }
        shape.corners = sides == 20 ? ico : dodeca;
        normalizeCorners(shape);
        facesFromNormals(shape, sides == 20 ? dodeca : ico);
        numberOpposites(shape);
        shape.size = sides == 20 ? 1.08f : 1.04f;
        shape.fontScale = sides == 20 ? 0.34f : 0.42f;
        break;
    }
    case 10:
    default: {
        // A pentagonal trapezohedron: two apexes and a zigzag ring, ten kites.
        constexpr float ring = 0.10557281f;  // keeps each kite flat
        shape.corners.emplace_back(0.0f, 0.0f, 1.0f);
        shape.corners.emplace_back(0.0f, 0.0f, -1.0f);
        for (int k = 0; k < 10; ++k) {
            const double angle = kPi * 36.0 * k / 180.0;
            shape.corners.emplace_back(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)),
                                       k % 2 == 0 ? ring : -ring);
        }
        const auto at = [](int k) { return 2 + ((k % 10) + 10) % 10; };
        for (int i = 0; i < 5; ++i) {
            shape.faces.push_back({0, at(2 * i), at(2 * i + 1), at(2 * i + 2)});
            shape.faces.push_back({1, at(2 * i + 1), at(2 * i + 2), at(2 * i + 3)});
        }
        // Each kite's normal by Newell's method, facing outward.
        for (std::vector<int>& face : shape.faces) {
            QVector3D normal;
            for (std::size_t i = 0; i < face.size(); ++i) {
                const QVector3D& a = shape.corners[static_cast<std::size_t>(face[i])];
                const QVector3D& b = shape.corners[static_cast<std::size_t>(face[(i + 1) % face.size()])];
                normal += QVector3D((a.y() - b.y()) * (a.z() + b.z()), (a.z() - b.z()) * (a.x() + b.x()),
                                    (a.x() - b.x()) * (a.y() + b.y()));
            }
            normal.normalize();
            if (QVector3D::dotProduct(normal, faceCentre(shape, face)) < 0.0f) {
                std::reverse(face.begin(), face.end());
                normal = -normal;
            }
            shape.normals.push_back(normal);
        }
        numberOpposites(shape);
        shape.size = 1.0f;
        shape.fontScale = 0.4f;
        break;
    }
    }
    shape.inradius = inradiusOf(shape);
    return shape;
}

const Shape& shapeFor(int sides)
{
    static std::map<int, Shape> shapes;
    auto found = shapes.find(sides);
    if (found == shapes.end()) {
        found = shapes.emplace(sides, makeShape(sides)).first;
    }
    return found->second;
}

int dieKind(int sides)
{
    switch (sides) {
    case 4:
    case 6:
    case 8:
    case 10:
    case 12:
    case 20:
        return sides;
    default:
        return 20;  // any other die is shown as a d20 with its number
    }
}

float random(float low, float high)
{
    return low + static_cast<float>(QRandomGenerator::global()->generateDouble()) * (high - low);
}

double ease(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The colours of a die: Freya's accent, the tens die of a percentile roll a
// shade deeper.
QColor dieColour(int sides, bool tens)
{
    if (tens) {
        return palette::accent.darker(135);
    }
    return sides == 20 ? palette::accent : palette::accent.lighter(112);
}

}  // namespace

DiceOverlay::DiceOverlay(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("diceOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &DiceOverlay::tick);
    if (parent != nullptr) {
        parent->installEventFilter(this);
        setGeometry(parent->rect());
    }
    hide();
}

bool DiceOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    // While shown, the overlay watches the whole application (installed in
    // throwStages): the click or key that dismisses it is not passed on.
    const QEvent::Type type = event->type();
    auto* widget = qobject_cast<QWidget*>(watched);
    const bool ours = widget != nullptr && widget->window() == window();
    if (ours && (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick)) {
        if (!m_dismissed && waitsForClick()) {
            proceed();
            m_swallowRelease = true;  // the release is not a click on the page
            return true;
        }
        if (!m_dismissed) {
            dismiss();
        }
        if (isVisible()) {
            m_swallowRelease = true;  // hidden on the release, which goes nowhere either
        }
        return true;
    }
    if (ours && type == QEvent::MouseButtonRelease && m_swallowRelease) {
        m_swallowRelease = false;
        if (m_dismissed) {
            hide();
        }
        return true;
    }
    if (ours && !m_dismissed && (type == QEvent::KeyPress || type == QEvent::ShortcutOverride)) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (type == QEvent::ShortcutOverride) {
                event->accept();  // no shortcut (Escape cancelling targets) takes it
            } else if (waitsForClick()) {
                proceed();
            } else {
                dismiss();
                hide();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DiceOverlay::setSound(bool on, int volume, TableSurface surface)
{
    m_soundOn = on;
    m_volume = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
    m_surface = surface;
#ifdef FREYA_HAVE_AUDIO
    if (on && m_audio == nullptr) {
        m_audio = new DiceAudio(this);
    }
#endif
#ifdef FREYA_HAVE_AUDIO
    if (!on && m_audio != nullptr) {
        m_audio->stop();
    }
#endif
}

void DiceOverlay::sound(const std::vector<DiceImpact>& impacts)
{
    m_lastImpacts = impacts;
    m_lastSound.clear();
    if (!m_soundOn || m_samples == nullptr || m_samples->empty()) {
        return;  // nothing to play
    }
    // Each die, with a little detuning of its own.
    std::vector<DieVoice> voices;
    for (const Body& body : m_bodies) {
        DieVoice voice;
        voice.sides = body.sides;
        voice.detune = random(0.97f, 1.03f);
        voices.push_back(voice);
    }
    DiceSoundSettings settings;
    settings.surface = m_surface;
    settings.volume = m_volume;
#ifdef FREYA_HAVE_AUDIO
    const bool output = m_audio != nullptr && m_audio->prepare();
    if (output) {
        settings.sampleRate = m_audio->sampleRate();
    }
#endif
    m_lastSoundRate = settings.sampleRate;
    m_lastSound = renderDiceSound(impacts, voices, settings, QRandomGenerator::global()->generate(), m_samples);
#ifdef FREYA_HAVE_AUDIO
    if (output) {
        m_audio->play(m_lastSound);
    }
#endif
}

void DiceOverlay::dismiss()
{
#ifdef FREYA_HAVE_AUDIO
    if (m_audio != nullptr) {
        m_audio->stop();
    }
#endif
    m_dismissed = true;
    m_timer.stop();
    m_bodies.clear();
    m_captions.clear();
    m_details.clear();
    m_noDetail.clear();
    m_noCard.clear();
    m_stageAt.clear();
    m_stageImpacts.clear();
    update();
    if (!m_swallowRelease) {
        // A click hides it on the release; anything else hides it now.
        QMetaObject::invokeMethod(
            this, [this] {
                if (m_dismissed && !m_swallowRelease) {
                    hide();
                }
            },
            Qt::QueuedConnection);
    }
}

void DiceOverlay::hideEvent(QHideEvent* event)
{
#ifdef FREYA_HAVE_AUDIO
    if (m_audio != nullptr) {
        m_audio->stop();
    }
#endif
    qApp->removeEventFilter(this);
    m_timer.stop();
    m_dismissed = true;
    m_swallowRelease = false;
    QWidget::hideEvent(event);
}

std::vector<int> DiceOverlay::shownFaces() const
{
    std::vector<int> faces;
    for (const Body& body : m_bodies) {
        faces.push_back(body.shown);
    }
    return faces;
}

std::vector<int> DiceOverlay::shownSides() const
{
    std::vector<int> sides;
    for (const Body& body : m_bodies) {
        sides.push_back(body.sides);
    }
    return sides;
}

std::vector<int> DiceOverlay::facesTowardViewer() const
{
    std::vector<int> faces;
    for (const Body& body : m_bodies) {
        const int nearest = nearestToCamera(body);
        faces.push_back(body.sides == 4 ? body.cornerValues[static_cast<std::size_t>(nearest)]
                                        : body.faceValues[static_cast<std::size_t>(nearest)]);
    }
    return faces;
}

float DiceOverlay::largestSettleTurn() const
{
    float largest = 0.0f;
    for (const Body& body : m_bodies) {
        const float dot = std::min(1.0f, std::abs(QQuaternion::dotProduct(body.settleFrom, body.settleTo)));
        largest = std::max(largest, 2.0f * std::acos(dot) * 180.0f / static_cast<float>(kPi));
    }
    return largest;
}

void DiceOverlay::throwDice(const std::vector<ThrownDie>& dice, const QString& caption)
{
    throwStages({DiceStage{dice, caption}});
}

std::vector<int> DiceOverlay::thrownSides() const
{
    std::vector<int> sides;
    for (const Body& body : m_bodies) {
        if (body.thrownAt <= m_age) {
            sides.push_back(body.sides);
        }
    }
    return sides;
}

int DiceOverlay::cardStage() const
{
    int shown = -1;
    for (std::size_t k = 0; k < m_stageAt.size(); ++k) {
        if (m_age >= m_stageAt[k] + kSettleAt + kSettleTime * 0.6) {
            shown = static_cast<int>(k);
        } else if (k > 0 && m_age >= m_stageAt[k]) {
            // The next throw is in the air. The card before it stays down
            // until this throw settles and its own card comes up.
            return -1;
        }
    }
    return shown;
}

QString DiceOverlay::shownCaption() const
{
    const int stage = cardStage();
    return active() && stage >= 0 && !m_noCard[static_cast<std::size_t>(stage)] ? m_captions[static_cast<std::size_t>(stage)]
                                                                               : QString();
}

bool DiceOverlay::waitsForClick() const
{
    const int stage = cardStage();
    if (stage < 0 || stage + 1 >= static_cast<int>(m_stageAt.size())) {
        return false;
    }
    return !m_noCard[static_cast<std::size_t>(stage)];
}

void DiceOverlay::playStage(int stage)
{
    if (stage < 0 || stage >= static_cast<int>(m_stageImpacts.size())) {
        return;
    }
    sound(m_stageImpacts[static_cast<std::size_t>(stage)]);
}

double DiceOverlay::clockNow() const
{
    return m_timeBase + static_cast<double>(m_clock.elapsed()) / 1000.0;
}

void DiceOverlay::proceed()
{
    if (m_openStages >= static_cast<int>(m_stageAt.size())) {
        return;
    }
    const double target = m_stageAt[static_cast<std::size_t>(m_openStages)];
    const float width = static_cast<float>(std::max(200, this->width()));
    const float height = static_cast<float>(std::max(200, this->height()));
    int guard = 0;
    while (m_age + static_cast<double>(kStep) < target && guard < 20000) {
        m_age += static_cast<double>(kStep);
        step(m_bodies, m_age, kStep, width, height);
        ++guard;
    }
    const int starting = m_openStages;
    ++m_openStages;
    playStage(starting);
    m_timeBase = m_age;
    m_clock.restart();
    if (!m_timer.isActive()) {
        m_timer.start();
    }
    update();
}

QString DiceOverlay::shownDetail() const
{
    const int stage = cardStage();
    if (!active() || stage < 0) {
        return QString();
    }
    const std::size_t at = static_cast<std::size_t>(stage);
    return m_noDetail[at] ? QString() : m_details[at];
}

void DiceOverlay::throwStages(const std::vector<DiceStage>& stages)
{
    m_bodies.clear();
    m_captions.clear();
    m_details.clear();
    m_noDetail.clear();
    m_noCard.clear();
    m_stageAt.clear();
    m_stageImpacts.clear();
    m_openStages = 1;
    m_timeBase = 0.0;
    const float width = static_cast<float>(std::max(200, this->width()));
    const float height = static_cast<float>(std::max(200, this->height()));
    const float base = std::clamp(std::min(width, height) / 15.0f, 22.0f, 40.0f);
    int stageIndex = 0;
    double stageAt = 0.0;
    const auto add = [&](int sides, Labels labels, int result, int shown) {
        if (static_cast<int>(m_bodies.size()) >= kMaxDice) {
            return;
        }
        Body body;
        body.sides = dieKind(sides);
        body.labels = labels;
        body.result = result;
        body.shown = shown;
        body.stage = stageIndex;
        body.thrownAt = stageAt;
        body.radius = base * shapeFor(body.sides).size;
        body.faceValues = shapeFor(body.sides).faceValues;
        body.cornerValues = shapeFor(body.sides).cornerValues;
        // From the bottom edge, up into the page, spinning.
        body.position = QVector3D(random(width * 0.3f, width * 0.7f), height - body.radius - 4.0f,
                                  body.radius * random(2.0f, 3.5f));
        body.velocity = QVector3D(random(-0.3f, 0.3f) * width, -random(0.75f, 1.05f) * height,
                                  random(250.0f, 520.0f));
        body.orientation = QQuaternion::fromEulerAngles(random(0, 360), random(0, 360), random(0, 360));
        body.spin = QVector3D(random(-1, 1), random(-1, 1), random(-1, 1)).normalized() * random(9.0f, 17.0f);
        m_bodies.push_back(body);
    };
    for (const DiceStage& stage : stages) {
        if (stageIndex > 0) {
            stageAt += std::max(0.0, stage.extraPause);
        }
        const std::size_t before = m_bodies.size();
        for (const ThrownDie& die : stage.dice) {
            if (die.sides == 100) {
                const int value = ((die.face % 100) + 100) % 100;
                add(10, Labels::Tens, value / 10, value / 10 * 10);
                add(10, Labels::Ones, value % 10, value % 10);
            } else {
                add(die.sides, Labels::Plain, die.face, die.face);
            }
        }
        if (m_bodies.size() == before) {
            continue;  // nothing thrown: no stage
        }
        m_captions.push_back(stage.caption);
        m_details.push_back(stage.detail);
        m_noDetail.push_back(stage.noDetail);
        m_noCard.push_back(stage.noCard);
        m_stageAt.push_back(stageAt);
        m_lastThrowAt = stageAt;
        ++stageIndex;
        stageAt += kSettleAt + kSettleTime + kStagePause;
    }
    if (m_bodies.empty()) {
        return;
    }
    // Run the throw ahead to see which face of each die is nearest the
    // viewer as it starts to settle, and number that die so the face shows
    // what was rolled. It then only tips onto that face, never flips over.
    // Every contact on the way is noted for the throw's sound.
    std::vector<DiceImpact> impacts;
    {
        std::vector<Body> rehearsal = m_bodies;
        double time = 0.0;
        while (std::any_of(rehearsal.begin(), rehearsal.end(), [](const Body& b) { return !b.settling; })) {
            time += static_cast<double>(kStep);
            step(rehearsal, time, kStep, width, height, m_soundOn ? &impacts : nullptr);
        }
        for (std::size_t i = 0; i < m_bodies.size(); ++i) {
            numberForResult(m_bodies[i], rehearsal[i].settleOn);
        }
        // On to the last die at rest: knocks while settling, and each one
        // tipping onto its face.
        if (m_soundOn) {
            const double end = m_lastThrowAt + kSettleAt + kSettleTime + 2.0 * static_cast<double>(kStep);
            while (time < end) {
                time += static_cast<double>(kStep);
                step(rehearsal, time, kStep, width, height, &impacts);
            }
        }
    }
    m_stageImpacts.assign(m_stageAt.size(), {});
    for (const DiceImpact& impact : impacts) {
        std::size_t stage = 0;
        for (std::size_t k = 0; k < m_stageAt.size(); ++k) {
            if (impact.time + 1e-9 >= m_stageAt[k]) {
                stage = k;
            }
        }
        DiceImpact shifted = impact;
        shifted.time -= m_stageAt[stage];
        m_stageImpacts[stage].push_back(shifted);
    }
    playStage(0);
    m_age = 0.0;
    m_timeBase = 0.0;
    m_clock.start();
    setGeometry(parentWidget() != nullptr ? parentWidget()->rect() : geometry());
    if (!isVisible() || m_dismissed) {
        m_dimClock.start();
    }
    m_dismissed = false;
    m_swallowRelease = false;
    show();
    raise();
    qApp->removeEventFilter(this);
    qApp->installEventFilter(this);
    m_timer.start();
    update();
}

namespace {

// Where the camera is, seen from the table: up, and toward the bottom of the page.
QVector3D towardCamera()
{
    return QVector3D(0.0f, std::sin(kTilt), std::cos(kTilt));
}

}  // namespace

int DiceOverlay::nearestToCamera(const Body& body)
{
    // The face (a d4's corner) that points most at the camera.
    const Shape& shape = shapeFor(body.sides);
    const QVector3D camera = towardCamera();
    const std::vector<QVector3D>& candidates = body.sides == 4 ? shape.corners : shape.normals;
    int best = 0;
    float most = -2.0f;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const float dot = QVector3D::dotProduct(body.orientation.rotatedVector(candidates[i]), camera);
        if (dot > most) {
            most = dot;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void DiceOverlay::numberForResult(Body& body, int settleOn)
{
    body.plannedOn = settleOn;
    const Shape& shape = shapeFor(body.sides);
    const auto index = static_cast<std::size_t>(settleOn);
    if (body.sides == 4) {
        // The rolled number goes on the corner that ends on top.
        const int wanted = std::clamp(body.result, 1, 4);
        const auto has = std::find(body.cornerValues.begin(), body.cornerValues.end(), wanted);
        if (has != body.cornerValues.end() && index < body.cornerValues.size()) {
            std::iter_swap(has, body.cornerValues.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return;
    }
    // A percentile die's 0 is the face marked 10.
    const int wanted = body.labels == Labels::Plain ? body.result : (body.result == 0 ? 10 : body.result);
    const auto has = std::find(body.faceValues.begin(), body.faceValues.end(), wanted);
    if (has == body.faceValues.end() || index >= body.faceValues.size()) {
        return;
    }
    const auto from = static_cast<std::size_t>(has - body.faceValues.begin());
    if (from == index) {
        return;
    }
    std::swap(body.faceValues[from], body.faceValues[index]);
    // Their opposite faces swap too, so opposite faces still add up as on a
    // real die (unless the two were each other's opposites).
    if (shape.opposite[from] != index) {
        std::swap(body.faceValues[shape.opposite[from]], body.faceValues[shape.opposite[index]]);
    }
}

void DiceOverlay::startSettling(Body& body)
{
    const Shape& shape = shapeFor(body.sides);
    // Tip the nearest face (or corner) the short way round to face the camera:
    // the one the run-ahead found and numbered (the same, unless the page
    // changed size mid-throw).
    body.settleOn = body.plannedOn >= 0 ? body.plannedOn : nearestToCamera(body);
    const QVector3D local = body.sides == 4 ? shape.corners[static_cast<std::size_t>(body.settleOn)]
                                            : shape.normals[static_cast<std::size_t>(body.settleOn)];
    const QVector3D now = body.orientation.rotatedVector(local);
    QQuaternion target = (QQuaternion::rotationTo(now, towardCamera()) * body.orientation).normalized();
    if (QQuaternion::dotProduct(target, body.orientation) < 0.0f) {
        target = -target;
    }
    body.settleFrom = body.orientation;
    body.settleTo = target;
    body.settling = true;
}

void DiceOverlay::step(std::vector<Body>& bodies, double time, float dt, float width, float height,
                       std::vector<DiceImpact>* impacts)
{
    // A die not thrown yet isn't on the table; one that has settled stays put.
    const auto local = [time](const Body& body) { return time - body.thrownAt; };
    const auto still = [&local](const Body& body) { return local(body) >= kSettleAt + kSettleTime; };
    const auto pan = [width](float x) { return std::clamp(x / std::max(1.0f, width) * 2.0f - 1.0f, -1.0f, 1.0f); };
    for (std::size_t index = 0; index < bodies.size(); ++index) {
        Body& body = bodies[index];
        const Shape& shape = shapeFor(body.sides);
        const double age = local(body);
        if (age < 0.0) {
            continue;
        }
        if (still(body)) {
            if (impacts != nullptr && age - static_cast<double>(dt) < kSettleAt + kSettleTime) {
                // It has tipped onto its face: a soft, flat tock.
                DiceImpact impact;
                impact.kind = DiceImpact::Kind::Settle;
                impact.time = time;
                impact.die = static_cast<int>(index);
                impact.speed = 4.0f;
                impact.contacts = 3;
                impact.pan = pan(body.position.x());
                impacts->push_back(impact);
            }
            body.orientation = body.settleTo;
            body.velocity = QVector3D();
            continue;
        }
        if (age >= kSettleAt && !body.settling) {
            startSettling(body);
        }
        if (body.settling) {
            const double t = ease((age - kSettleAt) / kSettleTime);
            body.orientation = QQuaternion::slerp(body.settleFrom, body.settleTo, static_cast<float>(t));
            const float rest = body.radius * shape.inradius;
            body.position.setZ(body.position.z() + (rest - body.position.z()) * std::min(1.0f, dt * 12.0f));
            const float slow = std::exp(-7.0f * dt);
            body.velocity.setX(body.velocity.x() * slow);
            body.velocity.setY(body.velocity.y() * slow);
            body.velocity.setZ(0.0f);
            body.position += QVector3D(body.velocity.x(), body.velocity.y(), 0.0f) * dt;
        } else {
            body.velocity.setZ(body.velocity.z() - kGravity * dt);
            body.position += body.velocity * dt;
            const float angle = body.spin.length() * dt;
            if (angle > 0.0f) {
                body.orientation = (QQuaternion::fromAxisAndAngle(body.spin.normalized(),
                                                                  angle * 180.0f / static_cast<float>(kPi)) *
                                    body.orientation)
                                       .normalized();
            }
            // The lowest corner meets the table.
            float lowest = 0.0f;
            for (const QVector3D& corner : shape.corners) {
                lowest = std::min(lowest, body.orientation.rotatedVector(corner).z());
            }
            const float floor = -lowest * body.radius;
            if (body.position.z() < floor) {
                body.position.setZ(floor);
                if (impacts != nullptr && body.velocity.z() < 0.0f) {
                    const float speed = -body.velocity.z() / body.radius;
                    if (speed >= kQuietestImpact) {
                        // How much of it meets the table: a corner, an edge, or a face.
                        int touching = 0;
                        for (const QVector3D& corner : shape.corners) {
                            if (body.orientation.rotatedVector(corner).z() - lowest < 0.06f) {
                                ++touching;
                            }
                        }
                        DiceImpact impact;
                        impact.time = time;
                        impact.die = static_cast<int>(index);
                        impact.speed = speed;
                        impact.contacts = std::max(1, touching);
                        impact.pan = pan(body.position.x());
                        impacts->push_back(impact);
                    }
                }
                if (body.velocity.z() < 0.0f) {
                    body.velocity.setZ(-body.velocity.z() * 0.42f);
                    body.velocity.setX(body.velocity.x() * 0.8f);
                    body.velocity.setY(body.velocity.y() * 0.8f);
                    body.spin *= 0.72f;
                    // The table kicks it round, rolling the way it travels.
                    body.spin += QVector3D(-body.velocity.y(), body.velocity.x(), 0.0f) * (0.6f / body.radius);
                }
            }
            if (impacts != nullptr) {
                // Tumbling: each corner that comes down onto the table knocks,
                // as hard as it swings into it.
                unsigned now = 0;
                float hardest = 0.0f;
                int touching = 0;
                if (body.position.z() <= floor + 0.5f) {
                    for (std::size_t c = 0; c < shape.corners.size() && c < 32; ++c) {
                        const QVector3D corner = body.orientation.rotatedVector(shape.corners[c]);
                        if (corner.z() - lowest < 0.06f) {
                            now |= 1u << c;
                            ++touching;
                            if ((body.touching & (1u << c)) == 0) {
                                const float down = -(body.velocity.z() / body.radius +
                                                     QVector3D::crossProduct(body.spin, corner).z());
                                hardest = std::max(hardest, down);
                            }
                        }
                    }
                }
                if (body.touching != 0 && hardest >= kQuietestImpact) {
                    DiceImpact impact;
                    impact.time = time;
                    impact.die = static_cast<int>(index);
                    impact.speed = hardest;
                    impact.contacts = std::max(1, touching);
                    impact.pan = pan(body.position.x());
                    impacts->push_back(impact);
                }
                body.touching = now;
            }
            if (body.position.z() <= floor + 1.0f) {
                const float rub = std::exp(-2.2f * dt);
                body.velocity.setX(body.velocity.x() * rub);
                body.velocity.setY(body.velocity.y() * rub);
            }
        }
        // The edges of the page.
        const float r = body.radius;
        if (body.position.x() < r) {
            body.position.setX(r);
            body.velocity.setX(std::abs(body.velocity.x()) * 0.6f);
        } else if (body.position.x() > width - r) {
            body.position.setX(width - r);
            body.velocity.setX(-std::abs(body.velocity.x()) * 0.6f);
        }
        if (body.position.y() < r * 1.6f) {
            body.position.setY(r * 1.6f);
            body.velocity.setY(std::abs(body.velocity.y()) * 0.6f);
        } else if (body.position.y() > height - r) {
            body.position.setY(height - r);
            body.velocity.setY(-std::abs(body.velocity.y()) * 0.6f);
        }
    }
    // Dice knock into each other.
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        for (std::size_t j = i + 1; j < bodies.size(); ++j) {
            Body& a = bodies[i];
            Body& b = bodies[j];
            if (local(a) < 0.0 || local(b) < 0.0 || (still(a) && still(b))) {
                continue;
            }
            QVector3D gap(b.position.x() - a.position.x(), b.position.y() - a.position.y(), 0.0f);
            const float reach = (a.radius + b.radius) * 0.92f;
            const float distance = gap.length();
            if (distance >= reach) {
                continue;
            }
            const QVector3D normal = distance > 0.01f ? gap / distance : QVector3D(1.0f, 0.0f, 0.0f);
            // A settled die doesn't move: the other bounces off it.
            const bool aStill = still(a);
            const bool bStill = still(b);
            const float overlap = reach - distance;
            if (aStill) {
                b.position += normal * overlap;
            } else if (bStill) {
                a.position -= normal * overlap;
            } else {
                a.position -= normal * (overlap / 2.0f);
                b.position += normal * (overlap / 2.0f);
            }
            const float closing = QVector3D::dotProduct(a.velocity - b.velocity, normal);
            if (impacts != nullptr && closing > 0.0f) {
                const float speed = closing / ((a.radius + b.radius) * 0.5f);
                if (speed >= kQuietestImpact) {
                    DiceImpact impact;
                    impact.kind = DiceImpact::Kind::Dice;
                    impact.time = time;
                    impact.die = static_cast<int>(i);
                    impact.other = static_cast<int>(j);
                    impact.speed = speed;
                    impact.pan = pan((a.position.x() + b.position.x()) * 0.5f);
                    impacts->push_back(impact);
                }
            }
            if (closing > 0.0f) {
                const QVector3D impulse = normal * (closing * 0.85f);
                if (aStill) {
                    b.velocity += impulse * 2.0f;
                } else if (bStill) {
                    a.velocity -= impulse * 2.0f;
                } else {
                    a.velocity -= impulse;
                    b.velocity += impulse;
                }
            }
        }
    }
}

void DiceOverlay::tick()
{
    // Catch up with the clock in fixed steps (the same ones the run-ahead took).
    // A later throw does not start until its card has been clicked on.
    const double now = clockNow();
    const float width = static_cast<float>(std::max(200, this->width()));  // as the throw had it
    const float height = static_cast<float>(std::max(200, this->height()));
    const bool gated = m_openStages < static_cast<int>(m_stageAt.size()) &&
                       !m_noCard[static_cast<std::size_t>(m_openStages - 1)];
    const double gate = gated ? m_stageAt[static_cast<std::size_t>(m_openStages)] : 1.0e9;
    int steps = 0;
    while (m_age + static_cast<double>(kStep) <= now && steps < 40) {
        if (m_age + static_cast<double>(kStep) >= gate) {
            m_timer.stop();  // the card stays until a click starts the next throw
            break;
        }
        m_age += static_cast<double>(kStep);
        step(m_bodies, m_age, kStep, width, height);
        ++steps;
    }
    if (m_age >= m_lastThrowAt + kStillAfter) {
        m_timer.stop();  // settled, with its card: it waits for a click
    }
    update();
}

void DiceOverlay::paintEvent(QPaintEvent* /*event*/)
{
    if (m_bodies.empty() || m_dismissed) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    // The page under the dice, dimmed: a deeper wash in dark mode, where the
    // page is already dark and a light one would not show.
    {
        const double dimIn = std::clamp(static_cast<double>(m_dimClock.elapsed()) / 1000.0 / kDimIn, 0.0, 1.0);
        const QColor wash = darkMode() ? QColor(0, 0, 0, static_cast<int>(130 * dimIn))
                                       : QColor(24, 22, 48, static_cast<int>(95 * dimIn));
        painter.fillRect(rect(), wash);
    }
    const double fade = 1.0;

    const float sinT = std::sin(kTilt);
    const float cosT = std::cos(kTilt);
    const QVector3D toCamera(0.0f, sinT, cosT);
    //const QVector3D light = QVector3D(-0.35f, -0.55f, 0.76f).normalized();
    const QVector3D light = QVector3D(-0.0f, -0.0f, 0.76f).normalized();

    // Nearer the bottom of the page, and higher, is nearer the camera.
    std::vector<const Body*> order;
    for (const Body& body : m_bodies) {
        if (body.thrownAt > m_age) {
            continue;  // still to be thrown
        }
        order.push_back(&body);
    }
    std::sort(order.begin(), order.end(), [&](const Body* a, const Body* b) {
        return a->position.y() * sinT + a->position.z() * cosT < b->position.y() * sinT + b->position.z() * cosT;
    });

    // Soft shadows first, on the table under each die: smaller and fainter
    // the higher it is.
    painter.setPen(Qt::NoPen);
    for (const Body* body : order) {
        const float lift = std::clamp(body->position.z() / (body->radius * 6.0f), 0.0f, 1.0f);
        const float spread = body->radius * (0.95f - 0.25f * lift);
        const QPointF at(body->position.x() + body->position.z() * 0.12f, body->position.y() + body->radius * 0.35f);
        QRadialGradient soft(at, spread);
        soft.setColorAt(0.0, QColor(30, 26, 70, static_cast<int>(70.0f * (1.0f - 0.6f * lift))));
        soft.setColorAt(1.0, QColor(30, 26, 70, 0));
        painter.setBrush(soft);
        painter.save();
        painter.translate(at);
        painter.scale(1.0, static_cast<double>(cosT) * 0.75);
        painter.translate(-at);
        painter.drawEllipse(at, spread, spread);
        painter.restore();
    }

    QRectF cluster;
    for (const Body* body : order) {
        const Shape& shape = shapeFor(body->sides);
        const float scale = body->radius * (1.0f + body->position.z() / 1400.0f);
        const QPointF centre(body->position.x(), body->position.y() - body->position.z() * sinT);
        const auto project = [&](const QVector3D& local) {
            const QVector3D p = body->orientation.rotatedVector(local) * scale;
            return QPointF(centre.x() + static_cast<double>(p.x()),
                           centre.y() + static_cast<double>(p.y() * cosT - p.z() * sinT));
        };
        cluster = cluster.united(QRectF(centre.x() - scale, centre.y() - scale, 2.0 * scale, 2.0 * scale));

        // Back faces are hidden; the near ones are drawn far to near.
        std::vector<std::size_t> faces;
        for (std::size_t i = 0; i < shape.faces.size(); ++i) {
            if (QVector3D::dotProduct(body->orientation.rotatedVector(shape.normals[i]), toCamera) > 0.0f) {
                faces.push_back(i);
            }
        }
        std::sort(faces.begin(), faces.end(), [&](std::size_t a, std::size_t b) {
            return QVector3D::dotProduct(body->orientation.rotatedVector(faceCentre(shape, shape.faces[a])), toCamera) <
                   QVector3D::dotProduct(body->orientation.rotatedVector(faceCentre(shape, shape.faces[b])), toCamera);
        });
        const QColor base = dieColour(body->sides, body->labels == Labels::Tens);
        for (const std::size_t i : faces) {
            const QVector3D normal = body->orientation.rotatedVector(shape.normals[i]);
            const float diffuse = std::max(0.0f, QVector3D::dotProduct(normal, light));
            const float level = 0.5f + 0.62f * diffuse;
            const float shine = std::pow(diffuse, 14.0f) * 0.35f;
            const auto channel = [&](int value) {
                const float lit = static_cast<float>(value) * level;
                return std::clamp(static_cast<int>(lit + (255.0f - lit) * shine), 0, 255);
            };
            QPolygonF polygon;
            for (const int corner : shape.faces[i]) {
                polygon << project(shape.corners[static_cast<std::size_t>(corner)]);
            }
            painter.setBrush(QColor(channel(base.red()), channel(base.green()), channel(base.blue())));
            painter.setPen(QPen(base.darker(170), 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPolygon(polygon);

            // The numbers, upright along each face.
            // (A d4 stands on a face with a corner up, so its faces lean away:
            // its numbers show at a shallower angle.)
            const float facing = QVector3D::dotProduct(normal, toCamera);
            const float readable = body->sides == 4 ? 0.12f : 0.3f;
            if (facing < readable) {
                continue;
            }
            QFont font = painter.font();
            font.setBold(true);
            font.setPixelSize(std::max(7, static_cast<int>(scale * shape.fontScale * (0.55f + 0.45f * facing))));
            painter.setFont(font);
            QColor ink(255, 255, 255, static_cast<int>(255.0f * std::min(1.0f, (facing - readable) * 3.0f)));
            painter.setPen(ink);
            const QVector3D middle = faceCentre(shape, shape.faces[i]);
            const auto label = [&](const QString& text, const QVector3D& at, const QVector3D& towards) {
                const QPointF from = project(at);
                const QPointF to = project(towards);
                const double angle = std::atan2(to.y() - from.y(), to.x() - from.x()) * 180.0 / kPi + 90.0;
                painter.save();
                painter.translate(from);
                painter.rotate(angle);
                const QFontMetricsF metrics(font);
                const QRectF box(-metrics.horizontalAdvance(text) / 2.0 - 2.0, -metrics.height() / 2.0,
                                 metrics.horizontalAdvance(text) + 4.0, metrics.height());
                painter.drawText(box, Qt::AlignCenter, text);
                painter.restore();
            };
            if (body->sides == 4) {
                // A number by each corner, read at the top one.
                for (const int corner : shape.faces[i]) {
                    const QVector3D tip = shape.corners[static_cast<std::size_t>(corner)];
                    const QVector3D spot = middle + (tip - middle) * 0.55f;
                    label(QString::number(body->cornerValues[static_cast<std::size_t>(corner)]), spot, tip);
                }
                continue;
            }
            const int value = body->faceValues[i];
            QString text;
            if (body->labels == Labels::Tens) {
                text = QStringLiteral("%1").arg((value % 10) * 10, 2, 10, QLatin1Char('0'));
            } else if (body->labels == Labels::Ones) {
                text = QString::number(value % 10);
            } else {
                text = QString::number(value);
            }
            if ((text == QLatin1String("6") || text == QLatin1String("9")) && body->sides > 6) {
                text += QLatin1Char('.');  // 6. and 9. tell which way up
            }
            const QVector3D first = shape.corners[static_cast<std::size_t>(shape.faces[i].front())];
            label(text, middle, middle + (first - middle) * 0.5f);
        }
    }

    // Once a throw settles: what it was for. A later throw takes that card
    // down while its dice are in the air, then shows its own.
    const int stage = cardStage();
    if (stage < 0 || m_noCard[static_cast<std::size_t>(stage)]) {
        return;
    }
    const QString caption = m_captions[static_cast<std::size_t>(stage)];
    const std::size_t at = static_cast<std::size_t>(stage);
    // The dice are on the table, so the card never lists them: only a
    // calculation (a damage roll's "1d6+2 slashing") when there is one.
    const QString detail = m_noDetail[at] ? QString() : m_details[at];
    QFont captionFont = font();
    captionFont.setBold(true);
    captionFont.setPixelSize(26);
    QFont detailFont = font();
    detailFont.setPixelSize(22);
    QFont hintFont = detailFont;
    hintFont.setPixelSize(14);  // 35% smaller than the detail line
    const double maxWidth = std::min(1040.0, width() - 64.0);
    const QFontMetricsF captionMetrics(captionFont);
    const QFontMetricsF detailMetrics(detailFont);
    const QFontMetricsF hintMetrics(hintFont);
    const QRectF captionBounds =
        captionMetrics.boundingRect(QRectF(0, 0, maxWidth - 56.0, 400.0), Qt::TextWordWrap, caption);
    const QRectF detailBounds =
        detail.isEmpty() ? QRectF()
                         : detailMetrics.boundingRect(QRectF(0, 0, maxWidth - 56.0, 400.0), Qt::TextWordWrap, detail);
    // On every card: a click continues, to the next throw or back to the page.
    const QString hint = tr("Click anywhere to continue");
    const QRectF hintBounds = hint.isEmpty() ? QRectF()
                                             : hintMetrics.boundingRect(QRectF(0, 0, maxWidth - 56.0, 400.0),
                                                                        Qt::TextWordWrap, hint);
    const double cardWidth = std::max({captionBounds.width(), detailBounds.width(), hintBounds.width()}) + 56.0;
    const double cardHeight = (caption.isEmpty() ? 0.0 : captionBounds.height() + 6.0) + detailBounds.height() +
                              (hint.isEmpty() ? 0.0 : hintBounds.height() + (detail.isEmpty() ? 0.0 : 8.0)) + 36.0;
    // Under the dice (the controls that threw them are usually above), or
    // over them when there is no room below.
    double x = cluster.center().x() - cardWidth / 2.0;
    double y = cluster.bottom() + 28.0;
    if (y + cardHeight > height() - 16.0) {
        y = cluster.top() - cardHeight - 28.0;
    }
    x = std::clamp(x, 16.0, std::max(16.0, width() - cardWidth - 16.0));
    y = std::clamp(y, 16.0, std::max(16.0, height() - cardHeight - 16.0));
    const QRectF card(x, y, cardWidth, cardHeight);
    const double appear =
        ease((m_age - (m_stageAt[static_cast<std::size_t>(stage)] + kSettleAt + kSettleTime * 0.6)) / 0.25);
    painter.setOpacity(std::clamp(fade, 0.0, 1.0) * appear);
    // Filled with the accent, white text: the Next turn button's look.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 18, 50, 50));
    painter.drawRoundedRect(card.translated(0, 6), 24, 24);
    painter.setBrush(palette::accent);
    painter.setPen(QPen(palette::accent.darker(120), 2.0));
    painter.drawRoundedRect(card, 24, 24);
    double textTop = card.top() + 18.0;
    if (!caption.isEmpty()) {
        painter.setFont(captionFont);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(card.left() + 28, textTop, card.width() - 48, captionBounds.height() + 4),
                         Qt::TextWordWrap, caption);
        textTop += captionBounds.height() + 6.0;
    }
    painter.setFont(detailFont);
    painter.setPen(QColor(255, 255, 255, 215));
    painter.drawText(QRectF(card.left() + 28, textTop, card.width() - 48, detailBounds.height() + 4), Qt::TextWordWrap,
                     detail);
    if (!hint.isEmpty()) {
        textTop += detail.isEmpty() ? 0.0 : detailBounds.height() + 8.0;
        painter.setFont(hintFont);
        painter.setPen(QColor(255, 255, 255, 150));
        painter.drawText(QRectF(card.left() + 28, textTop, card.width() - 48, hintBounds.height() + 4),
                         Qt::TextWordWrap, hint);
    }
}

}  // namespace combat::ui
