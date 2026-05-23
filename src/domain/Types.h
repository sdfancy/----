#pragma once

namespace spray::domain {

enum class FeedbackStage {
    Normal,
    Done,
};

enum class QueueItemStatus {
    Pending,
    Cached,
    Accepted,
    Done,
};

enum class PayloadSource {
    Pending,
    Camera,
    Default,
};

} // namespace spray::domain
