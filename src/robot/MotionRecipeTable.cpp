#include "robot/MotionRecipeTable.h"

#include <QFile>
#include <QSet>
#include <QTextStream>

#include <utility>

namespace spray::robot {
namespace {

QString stripComment(QString line)
{
    const auto hashIndex = line.indexOf('#');
    if (hashIndex >= 0) {
        line.truncate(hashIndex);
    }
    return line.trimmed();
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.mid(1, value.size() - 2);
    }
    return value;
}

bool parseBool(const QString& value, bool fallback)
{
    const auto lowered = value.trimmed().toLower();
    if (lowered == QStringLiteral("true")) {
        return true;
    }
    if (lowered == QStringLiteral("false")) {
        return false;
    }
    return fallback;
}

int parseInt(const QString& value, int fallback)
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

double parseDouble(const QString& value, double fallback)
{
    bool ok = false;
    const double parsed = value.trimmed().toDouble(&ok);
    return ok ? parsed : fallback;
}

QStringList arrayItems(QString value)
{
    value = value.trimmed();
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        return {};
    }
    value = value.mid(1, value.size() - 2).trimmed();
    if (value.isEmpty()) {
        return {};
    }
    return value.split(',', Qt::SkipEmptyParts);
}

bool parseDoubleArray(const QString& value, QList<double>* out)
{
    const auto items = arrayItems(value);
    if (items.isEmpty()) {
        return false;
    }

    QList<double> parsed;
    parsed.reserve(items.size());
    for (const auto& item : items) {
        bool ok = false;
        const double number = item.trimmed().toDouble(&ok);
        if (!ok) {
            return false;
        }
        parsed.append(number);
    }
    *out = parsed;
    return true;
}

bool parseIntArray(const QString& value, QList<int>* out)
{
    const auto items = arrayItems(value);
    if (items.isEmpty()) {
        return false;
    }

    QList<int> parsed;
    parsed.reserve(items.size());
    for (const auto& item : items) {
        bool ok = false;
        const int number = item.trimmed().toInt(&ok);
        if (!ok) {
            return false;
        }
        parsed.append(number);
    }
    *out = parsed;
    return true;
}

bool parseSprayIoType(const QString& value, MotionSegmentType* type)
{
    const QString lowered = unquote(value).trimmed().toLower();
    if (lowered == QStringLiteral("tool")) {
        *type = MotionSegmentType::ToolDigitalOut;
        return true;
    }
    if (lowered == QStringLiteral("standard")) {
        *type = MotionSegmentType::StandardDigitalOut;
        return true;
    }
    return false;
}

void applyQNear(MotionRecipe* recipe, const QList<double>& values)
{
    if (values.size() != 6) {
        return;
    }
    recipe->qNear = {values.at(0), values.at(1), values.at(2), values.at(3), values.at(4), values.at(5)};
}

bool recipeHasContent(const MotionRecipe& recipe)
{
    return recipe.armId != 0
        || recipe.enabled
        || recipe.tool != QStringLiteral("default")
        || recipe.wobj != QStringLiteral("default")
        || recipe.approachSpeed != 1.0
        || recipe.lineSpeed != 1.0
        || recipe.acceleration != 1.0
        || recipe.radius != 0.0
        || recipe.sprayIoChannel != 1;
}

} // namespace

MotionRecipeTable MotionRecipeTable::load(const QString& path, QString* errorMessage)
{
    MotionRecipeTable table;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("cannot open motion recipe table: %1").arg(path);
        }
        return table;
    }

    MotionRecipe current;
    bool inRecipe = false;
    QString parseError;

    const auto finishRecipe = [&]() {
        if (inRecipe || recipeHasContent(current)) {
            table.addRecipe(current);
        }
        current = MotionRecipe{};
        inRecipe = false;
    };

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto line = stripComment(stream.readLine());
        if (line.isEmpty()) {
            continue;
        }
        if (line == QStringLiteral("[[recipes]]")) {
            finishRecipe();
            current = MotionRecipe{};
            inRecipe = true;
            continue;
        }

        const auto equalIndex = line.indexOf('=');
        if (equalIndex < 0 || !parseError.isEmpty()) {
            continue;
        }

        const auto key = line.left(equalIndex).trimmed();
        const auto value = line.mid(equalIndex + 1).trimmed();
        if (key == QStringLiteral("arm_id")) {
            current.armId = parseInt(value, current.armId);
        } else if (key == QStringLiteral("enabled")) {
            current.enabled = parseBool(value, current.enabled);
        } else if (key == QStringLiteral("tool")) {
            current.tool = unquote(value);
        } else if (key == QStringLiteral("wobj")) {
            current.wobj = unquote(value);
        } else if (key == QStringLiteral("q_near")) {
            QList<double> values;
            if (!parseDoubleArray(value, &values) || values.size() != 6) {
                parseError = QStringLiteral("q_near must contain 6 numeric values");
            } else {
                applyQNear(&current, values);
            }
        } else if (key == QStringLiteral("pose_indices")) {
            QList<int> values;
            if (!parseIntArray(value, &values)) {
                parseError = QStringLiteral("pose_indices must contain integers");
            } else {
                current.poseValueIndices = values;
            }
        } else if (key == QStringLiteral("approach_speed")) {
            current.approachSpeed = parseDouble(value, current.approachSpeed);
        } else if (key == QStringLiteral("line_speed")) {
            current.lineSpeed = parseDouble(value, current.lineSpeed);
        } else if (key == QStringLiteral("acceleration")) {
            current.acceleration = parseDouble(value, current.acceleration);
        } else if (key == QStringLiteral("radius")) {
            current.radius = parseDouble(value, current.radius);
        } else if (key == QStringLiteral("spray_io")) {
            if (!parseSprayIoType(value, &current.sprayIoType)) {
                parseError = QStringLiteral("spray_io must be tool or standard");
            }
        } else if (key == QStringLiteral("spray_io_channel")) {
            current.sprayIoChannel = parseInt(value, current.sprayIoChannel);
        }
    }
    finishRecipe();

    if (!parseError.isEmpty()) {
        if (errorMessage) {
            *errorMessage = parseError;
        }
        return {};
    }

    QString validationError;
    if (!table.validate(&validationError)) {
        if (errorMessage) {
            *errorMessage = validationError;
        }
        return {};
    }
    return table;
}

void MotionRecipeTable::addRecipe(MotionRecipe recipe)
{
    recipes_.append(std::move(recipe));
}

const MotionRecipe* MotionRecipeTable::recipeForArm(int armId) const
{
    for (const auto& recipe : recipes_) {
        if (recipe.armId == armId) {
            return &recipe;
        }
    }
    return nullptr;
}

QList<MotionRecipe> MotionRecipeTable::recipes() const
{
    return recipes_;
}

bool MotionRecipeTable::validate(QString* errorMessage) const
{
    QSet<int> seen;
    for (const auto& recipe : recipes_) {
        if (recipe.armId != 1 && recipe.armId != 2) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("motion recipe arm must be 1 or 2");
            }
            return false;
        }
        if (seen.contains(recipe.armId)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("duplicate motion recipe arm: %1").arg(recipe.armId);
            }
            return false;
        }
        seen.insert(recipe.armId);

        if (recipe.poseValueIndices.size() != 6) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("pose_indices must contain 6 values for arm %1").arg(recipe.armId);
            }
            return false;
        }
        for (const int index : recipe.poseValueIndices) {
            if (index < 0) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("pose_indices must be non-negative for arm %1").arg(recipe.armId);
                }
                return false;
            }
        }
        if (recipe.approachSpeed <= 0.0 || recipe.lineSpeed <= 0.0 || recipe.acceleration <= 0.0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("motion recipe speed is invalid for arm %1").arg(recipe.armId);
            }
            return false;
        }
        if ((recipe.sprayIoType != MotionSegmentType::ToolDigitalOut
             && recipe.sprayIoType != MotionSegmentType::StandardDigitalOut)
            || recipe.sprayIoChannel <= 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("spray IO is invalid for arm %1").arg(recipe.armId);
            }
            return false;
        }
    }

    if (!seen.contains(1) || !seen.contains(2)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("missing motion recipe for arm %1").arg(seen.contains(1) ? 2 : 1);
        }
        return false;
    }
    return true;
}

} // namespace spray::robot
