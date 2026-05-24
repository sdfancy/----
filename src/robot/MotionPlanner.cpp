#include "robot/MotionPlanner.h"

#include "domain/Payloads.h"

#include <cmath>

namespace spray::robot {

namespace {

QString invalidPayload(const QString& detail)
{
    return QStringLiteral("invalid arm payload: %1").arg(detail);
}

QString invalidRecipe(const QString& detail)
{
    return QStringLiteral("invalid motion recipe: %1").arg(detail);
}

MotionSegment makeMotionSegment(MotionSegmentType type,
                                const Pose6d& pose,
                                const MotionRecipe& recipe,
                                double velocity)
{
    MotionSegment segment;
    segment.type = type;
    segment.pose = pose;
    segment.qNear = recipe.qNear;
    segment.velocity = velocity;
    segment.acceleration = recipe.acceleration;
    segment.radius = recipe.radius;
    segment.tool = recipe.tool;
    segment.wobj = recipe.wobj;
    return segment;
}

MotionSegment makeIoSegment(const MotionRecipe& recipe, bool value)
{
    MotionSegment segment;
    segment.type = recipe.sprayIoType;
    segment.ioChannel = recipe.sprayIoChannel;
    segment.ioValue = value;
    return segment;
}

bool isDigitalOutSegment(MotionSegmentType type)
{
    return type == MotionSegmentType::ToolDigitalOut
        || type == MotionSegmentType::StandardDigitalOut;
}

bool readPose(const ArmPayload& payload, const MotionRecipe& recipe, Pose6d* pose, QString* error)
{
    if (recipe.poseValueIndices.size() != 6) {
        if (error) {
            *error = invalidRecipe(QStringLiteral("pose mapping must contain 6 indices"));
        }
        return false;
    }

    QList<double> values;
    values.reserve(6);
    for (const int index : recipe.poseValueIndices) {
        if (index < 0 || index >= payload.values.size()) {
            if (error) {
                *error = invalidPayload(QStringLiteral("not enough pose values"));
            }
            return false;
        }
        const double value = payload.values.at(index);
        if (!std::isfinite(value)) {
            if (error) {
                *error = invalidPayload(QStringLiteral("pose value is not finite"));
            }
            return false;
        }
        values.append(value);
    }

    *pose = {values.at(0), values.at(1), values.at(2), values.at(3), values.at(4), values.at(5)};
    return true;
}

} // namespace

MotionPlanResult MotionPlanner::parseArmPayload(const QByteArray& payload)
{
    MotionPlanResult result;
    const QByteArray trimmed = payload.trimmed();
    if (!trimmed.startsWith('(') || !trimmed.endsWith(")E")) {
        result.message = invalidPayload(QStringLiteral("expected (...)E frame"));
        return result;
    }

    const QByteArray body = trimmed.mid(1, trimmed.size() - 3);
    const auto parts = body.split(',');
    if (parts.size() < 2) {
        result.message = invalidPayload(QStringLiteral("expected flag and count"));
        return result;
    }

    bool flagOk = false;
    const int flag = parts.at(0).trimmed().toInt(&flagOk);
    if (!flagOk) {
        result.message = invalidPayload(QStringLiteral("flag is not an integer"));
        return result;
    }

    bool countOk = false;
    const int count = parts.at(1).trimmed().toInt(&countOk);
    if (!countOk || count < 0 || count > 65535) {
        result.message = invalidPayload(QStringLiteral("count is out of range"));
        return result;
    }

    ArmPayload parsed;
    parsed.flag = flag;
    parsed.armId = flag / 1000;
    parsed.count = static_cast<quint16>(count);
    parsed.defaultNoop = domain::isDefaultPayload(trimmed);

    if (parsed.armId != 1 && parsed.armId != 2) {
        result.message = invalidPayload(QStringLiteral("flag does not map to arm 1 or 2"));
        return result;
    }

    for (qsizetype i = 2; i < parts.size(); ++i) {
        bool valueOk = false;
        const double value = parts.at(i).trimmed().toDouble(&valueOk);
        if (!valueOk) {
            result.message = invalidPayload(QStringLiteral("motion value is not numeric"));
            return result;
        }
        parsed.values.append(value);
    }

    result.ok = true;
    result.payload = parsed;
    result.planned.task.armId = parsed.armId;
    result.planned.task.count = parsed.count;
    result.planned.task.payload = trimmed;
    result.planned.task.defaultNoop = parsed.defaultNoop;
    result.message = QStringLiteral("payload parsed");
    return result;
}

MotionPlanResult MotionPlanner::plan(const core::RobotTask& task, const MotionRecipe& recipe)
{
    auto result = parseArmPayload(task.payload);
    if (!result.ok) {
        return result;
    }

    result.planned.task = task;
    result.planned.segments.clear();
    result.payload.defaultNoop = task.defaultNoop || result.payload.defaultNoop;
    result.planned.task.defaultNoop = result.payload.defaultNoop;

    if (result.payload.armId != task.armId) {
        result.ok = false;
        result.message = invalidPayload(QStringLiteral("payload arm does not match task arm"));
        return result;
    }
    if (result.payload.count != task.count) {
        result.ok = false;
        result.message = invalidPayload(QStringLiteral("payload count does not match task count"));
        return result;
    }

    if (result.payload.defaultNoop) {
        result.message = QStringLiteral("default noop task planned");
        return result;
    }

    if (!recipe.enabled) {
        result.ok = false;
        result.message = invalidRecipe(QStringLiteral("recipe is disabled"));
        return result;
    }
    if (recipe.armId != task.armId) {
        result.ok = false;
        result.message = invalidRecipe(QStringLiteral("recipe arm does not match task arm"));
        return result;
    }
    if (recipe.approachSpeed <= 0.0 || recipe.lineSpeed <= 0.0 || recipe.acceleration <= 0.0) {
        result.ok = false;
        result.message = invalidRecipe(QStringLiteral("speed and acceleration must be > 0"));
        return result;
    }
    if (!isDigitalOutSegment(recipe.sprayIoType) || recipe.sprayIoChannel <= 0) {
        result.ok = false;
        result.message = invalidRecipe(QStringLiteral("spray IO is invalid"));
        return result;
    }

    Pose6d pose;
    if (!readPose(result.payload, recipe, &pose, &result.message)) {
        result.ok = false;
        return result;
    }

    result.planned.segments.append(
        makeMotionSegment(MotionSegmentType::MoveJPose2, pose, recipe, recipe.approachSpeed));
    result.planned.segments.append(makeIoSegment(recipe, true));
    result.planned.segments.append(
        makeMotionSegment(MotionSegmentType::MoveL, pose, recipe, recipe.lineSpeed));
    result.planned.segments.append(makeIoSegment(recipe, false));
    result.message = QStringLiteral("motion planned");
    return result;
}

} // namespace spray::robot
