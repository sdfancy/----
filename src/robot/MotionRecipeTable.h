#pragma once

#include "robot/MotionTypes.h"

#include <QList>
#include <QString>

namespace spray::robot {

class MotionRecipeTable {
public:
    static MotionRecipeTable load(const QString& path, QString* errorMessage = nullptr);

    void addRecipe(MotionRecipe recipe);
    const MotionRecipe* recipeForArm(int armId) const;
    QList<MotionRecipe> recipes() const;
    bool validate(QString* errorMessage = nullptr) const;

private:
    QList<MotionRecipe> recipes_;
};

} // namespace spray::robot
