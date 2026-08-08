#include "utils/bounded_queue.hpp"

#include <gtest/gtest.h>

TEST(BoundedQueue, AcceptsUntilCapacity)
{
    utils::BoundedQueue<int> queue(2);

    EXPECT_EQ(queue.push_back(1), utils::QueuePushResult::Accepted);
    EXPECT_EQ(queue.push_back(2), utils::QueuePushResult::Accepted);
    EXPECT_EQ(queue.push_back(3), utils::QueuePushResult::DroppedFull);

    EXPECT_EQ(queue.size(), 2u);
    EXPECT_EQ(queue.front(), 1);
    queue.pop_front();
    EXPECT_EQ(queue.front(), 2);
}

TEST(BoundedQueue, PushAfterCloseIsRejected)
{
    utils::BoundedQueue<int> queue(2);
    EXPECT_EQ(queue.push_back(1), utils::QueuePushResult::Accepted);

    queue.close();

    EXPECT_EQ(queue.push_back(2), utils::QueuePushResult::Closed);
    EXPECT_EQ(queue.size(), 1u);
    EXPECT_TRUE(queue.closed());
}
